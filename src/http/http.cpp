#include <curl/curl.h>
#include "http/http.h"
#include "threading/queue.h"
#include "util/util.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace raidhook
{
	namespace
	{
		struct InitHttpManager
		{
			InitHttpManager()
			{
				HTTPManager::GetSingleton();
			}
		};

		struct HTTPProgressNotification
		{
			uint64_t itemId;
			long byteProgress;
			long byteTotal;
		};

		// Live requests by id. Progress events are queued separately from the finished-event and
		// may run after the item was freed, so they must look the item up instead of holding a pointer.
		std::mutex liveItemsMutex;
		std::map<uint64_t, HTTPItem*> liveItems;
		uint64_t nextItemId = 1;

		// What requests may use. Redirects must not lead to plain FTP, so a https URL can't be turned into an
		// unencrypted download.
		constexpr const char* ALLOWED_PROTOCOLS = "http,https,ftp,ftps";
		constexpr const char* ALLOWED_REDIRECT_PROTOCOLS = "http,https,ftps";

		// Cookie jars. Every request has its own curl handle, so the cookies of a jar live in a share object that all
		// requests of that name use. Jars only exist in memory, and the requests of a mod only see the jars it asks for.
		struct CookieJar
		{
			CURLSH* share = nullptr;
			std::recursive_mutex mutex;
		};

		std::mutex cookieJarsMutex;
		std::map<std::string, CookieJar*> cookieJars; // never freed, a few at most

		void share_lock(CURL*, curl_lock_data, curl_lock_access, void* userp)
		{
			static_cast<CookieJar*>(userp)->mutex.lock();
		}

		void share_unlock(CURL*, curl_lock_data, void* userp)
		{
			static_cast<CookieJar*>(userp)->mutex.unlock();
		}

		CookieJar* GetCookieJar(const std::string& name, bool create)
		{
			std::lock_guard<std::mutex> lock(cookieJarsMutex);
			auto it = cookieJars.find(name);
			if (it != cookieJars.end())
				return it->second;
			if (!create)
				return nullptr;

			auto* jar = new CookieJar();
			jar->share = curl_share_init();
			curl_share_setopt(jar->share, CURLSHOPT_LOCKFUNC, share_lock);
			curl_share_setopt(jar->share, CURLSHOPT_UNLOCKFUNC, share_unlock);
			curl_share_setopt(jar->share, CURLSHOPT_USERDATA, jar);
			curl_share_setopt(jar->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
			cookieJars[name] = jar;
			return jar;
		}

		// Applies what the Lua side asked for (see parse_http_options). Returns the header list, which has to be freed
		// with curl_slist_free_all once the transfer is done.
		curl_slist* ApplyRequestOptions(CURL* curl, const HTTPItem& item)
		{
			curl_slist* headers = nullptr;
			for (const std::string& header : item.requestHeaders)
				headers = curl_slist_append(headers, header.c_str());
			if (headers)
				curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

			const std::string& method = item.method;
			if (method == "HEAD")
				curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
			else if (item.hasBody || method == "POST")
			{
				// Setting the fields makes it a POST, unless there is a custom method
				curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(item.body.size()));
				curl_easy_setopt(curl, CURLOPT_POSTFIELDS, item.body.data());
			}
			if (!method.empty() && method != "GET" && method != "HEAD" && method != "POST")
				curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());

			if (!item.cookies.empty())
				curl_easy_setopt(curl, CURLOPT_COOKIE, item.cookies.c_str());
			if (!item.cookieJar.empty())
			{
				// The empty file name turns the cookie engine on without reading anything
				curl_easy_setopt(curl, CURLOPT_SHARE, GetCookieJar(item.cookieJar, true)->share);
				curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
			}

			return headers;
		}

		// HSTS: once a host has said it only wants https, http:// URLs for it are upgraded.
		// Every request has its own curl handle (and with that its own HSTS cache), so the cache is kept here and handed to
		// the handles through curl's read and write callbacks. That keeps concurrent requests from overwriting each
		// other's entries in a file, and the file is only written from here.
		constexpr const char* HSTS_FILE = "mods/saves/sblt_hsts.txt";

		struct HstsEntry
		{
			std::string name;
			bool includeSubDomains;
			std::string expire; // "YYYYMMDD HH:MM:SS" (UTC), which sorts like the time itself
		};

		std::mutex hstsMutex;
		std::vector<HstsEntry> hstsEntries;
		bool hstsDirty = false;

		std::string HstsNow()
		{
			return std::format("{:%Y%m%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
		}

		// Caller holds hstsMutex
		void HstsMerge(std::string name, bool includeSubDomains, const std::string& expire)
		{
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
			if (name.empty() || name.size() > 255 || expire.size() != 17 || expire <= HstsNow())
				return;

			for (HstsEntry& entry : hstsEntries)
			{
				if (entry.name != name)
					continue;

				if ((includeSubDomains && !entry.includeSubDomains) || expire > entry.expire)
				{
					entry.includeSubDomains = entry.includeSubDomains || includeSubDomains;
					entry.expire = std::max(entry.expire, expire);
					hstsDirty = true;
				}
				return;
			}

			hstsEntries.push_back({std::move(name), includeSubDomains, expire});
			hstsDirty = true;
		}

		void HstsLoad()
		{
			std::lock_guard<std::mutex> lock(hstsMutex);

			// Lines of "name <tab> 0|1 for includeSubDomains <tab> expiry"
			std::ifstream in(HSTS_FILE);
			std::string line;
			while (std::getline(in, line))
			{
				size_t first = line.find('\t');
				size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
				if (second == std::string::npos || second != first + 2)
					continue;
				HstsMerge(line.substr(0, first), line[first + 1] == '1', line.substr(second + 1));
			}

			// What came from the file is already saved
			hstsDirty = false;

			// Known to be https only. Without this, the first http:// URL for it would go out unprotected.
			HstsMerge("modworkshop.net", true, "20371231 23:59:59");
		}

		void HstsSaveIfChanged()
		{
			std::lock_guard<std::mutex> lock(hstsMutex);
			if (!hstsDirty)
				return;

			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(HSTS_FILE).parent_path(), ec);

			// Write next to the file and swap it in, so a crash can't leave half a file
			const std::string temporary = std::string(HSTS_FILE) + ".tmp";
			{
				std::ofstream out(temporary, std::ios::trunc);
				for (const HstsEntry& entry : hstsEntries)
					out << entry.name << '\t' << (entry.includeSubDomains ? '1' : '0') << '\t' << entry.expire << '\n';
				if (!out)
					return;
			}
			std::filesystem::rename(temporary, HSTS_FILE, ec);
			if (!ec)
				hstsDirty = false;
		}

		struct HstsSnapshot
		{
			std::vector<HstsEntry> entries;
			size_t next = 0;
		};

		// Gives curl the next known entry
		CURLSTScode hsts_read(CURL*, struct curl_hstsentry* e, void* userp)
		{
			auto* snapshot = static_cast<HstsSnapshot*>(userp);
			while (snapshot->next < snapshot->entries.size())
			{
				const HstsEntry& entry = snapshot->entries[snapshot->next++];
				if (entry.name.size() + 1 > e->namelen || entry.expire.size() + 1 > sizeof(e->expire))
					continue;

				std::memcpy(e->name, entry.name.c_str(), entry.name.size() + 1);
				std::memcpy(e->expire, entry.expire.c_str(), entry.expire.size() + 1);
				e->includeSubDomains = entry.includeSubDomains;
				return CURLSTS_OK;
			}
			return CURLSTS_DONE;
		}

		// Called for every entry when the handle is cleaned up, including the ones we gave it
		CURLSTScode hsts_write(CURL*, struct curl_hstsentry* e, struct curl_index*, void*)
		{
			std::lock_guard<std::mutex> lock(hstsMutex);
			HstsMerge(e->name, e->includeSubDomains, e->expire);
			return CURLSTS_OK;
		}
	}

	using HTTPProgressNotificationPtr = std::unique_ptr<HTTPProgressNotification>;
	using HTTPItemPtr = std::unique_ptr<HTTPItem>;
	RAIDHOOK_REGISTER_EVENTQUEUE(HTTPProgressNotificationPtr, HTTPProgressNotification)
	RAIDHOOK_REGISTER_EVENTQUEUE(HTTPItemPtr, HTTPItem)

	HTTPManager::HTTPManager()
	{
		// Curl Init
		curl_global_init(CURL_GLOBAL_ALL);
		HstsLoad();
		RAIDHOOK_LOG_LOG("CURL_INITD");
	}

	HTTPManager::~HTTPManager()
	{
		// Never waits for the request threads: at process exit Windows has already killed them,
		// so they would never report back. The singleton is leaked on purpose (see GetSingleton).
		RAIDHOOK_LOG_LOG("CURL CLOSED");
		curl_global_cleanup();
	}

	HTTPManager* HTTPManager::GetSingleton()
	{
		// Intentionally never destroyed: running request threads still use the manager and curl,
		// and static destructors run after the process' other threads were terminated.
		static HTTPManager* httpSingleton = new HTTPManager();
		return httpSingleton;
	}

	void ClearHttpCookies(const std::string& jarName)
	{
		CookieJar* jar = GetCookieJar(jarName, false);
		if (!jar)
			return;

		// ALL goes through the share, so it empties the jar for everyone using it
		CURL* curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_SHARE, jar->share);
		curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
		curl_easy_setopt(curl, CURLOPT_COOKIELIST, "ALL");
		curl_easy_cleanup(curl);
	}

	size_t write_http_header(char* ptr, size_t size, size_t nmemb, void* data)
	{
		HTTPItem* mainItem = (HTTPItem*)data;
		size_t headerLineSize = size*nmemb;
		std::string headerLine = std::string(ptr, headerLineSize);
		size_t delimiterPosition = headerLine.find(": ");
		if(delimiterPosition != std::string::npos)
		{
			std::string headerKey = headerLine.substr(0, delimiterPosition);
			std::transform(headerKey.begin(), headerKey.end(), headerKey.begin(), [](unsigned char c) { return std::tolower(c); });
			std::string headerValue = headerLine.substr(delimiterPosition + 2);
			size_t valLen = headerValue.length();
			if(valLen > 2 && headerValue.at(valLen - 1) == '\n' && headerValue.at(valLen - 2) == '\r')
			{
				headerValue=headerValue.substr(0, valLen-2);
			}
			if (headerKey == "set-cookie")
				mainItem->setCookies.push_back(headerValue);
			mainItem->responseHeaders.insert({headerKey, headerValue});
		}
		return headerLineSize;
	}

	size_t write_http_data(char* ptr, size_t size, size_t nmemb, void* data)
	{
		std::string newData = std::string(ptr, size*nmemb);
		HTTPItem* mainItem = (HTTPItem*)data;
		mainItem->httpContents += newData;
		return size*nmemb;
	}

	void run_http_progress_event(std::unique_ptr<HTTPProgressNotification> ourNotify)
	{
		RAIDHOOK_TRACE_FUNC;
		HTTPItem* ourItem = nullptr;
		{
			std::lock_guard<std::mutex> lock(liveItemsMutex);
			auto it = liveItems.find(ourNotify->itemId);
			if (it == liveItems.end()) return;
			ourItem = it->second;
		}
		ourItem->progress(ourItem->data, ourNotify->byteProgress, ourNotify->byteTotal);
	}

	int http_progress_call(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
	{
		RAIDHOOK_TRACE_FUNC;
		HTTPItem* ourItem = (HTTPItem*)clientp;
		if (!ourItem->progress) return 0;
		if (dltotal == 0 || dlnow == 0) return 0;
		if (dltotal == dlnow) return 0;
		if (ourItem->byteprogress >= dlnow) return 0;
		ourItem->byteprogress = static_cast<long>(dlnow);
		ourItem->bytetotal = static_cast<long>(dltotal);

		HTTPProgressNotificationPtr notify(new HTTPProgressNotification());
		notify->itemId = ourItem->id;
		notify->byteProgress = static_cast<long>(dlnow);
		notify->byteTotal = static_cast<long>(dltotal);

		GetHTTPProgressNotificationQueue().AddToQueue(run_http_progress_event, std::move(notify));
		return 0;
	}

	void run_http_event(std::unique_ptr<HTTPItem> ourItem)
	{
		RAIDHOOK_TRACE_FUNC;
		{
			std::lock_guard<std::mutex> lock(liveItemsMutex);
			liveItems.erase(ourItem->id);
		}
		ourItem->call(ourItem.get());
	}

	void launch_thread_http(HTTPItem *raw_item)
	{
		RAIDHOOK_TRACE_FUNC;
		std::unique_ptr<HTTPItem> item(raw_item);
		CURL *curl = curl_easy_init();
		if (!curl)
		{
			// Out of memory. The callback still gets its answer, with an error.
			item->errorCode = CURLE_FAILED_INIT;
			item->httpStatusCode = 0;
			GetHTTPItemQueue().AddToQueue(run_http_event, std::move(item));
			return;
		}
		curl_easy_setopt(curl, CURLOPT_URL, item->url.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		// Verify certificates against the Windows store; skip revocation checks, they commonly fail under Wine
		curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);

		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);

		curl_easy_setopt(curl, CURLOPT_USERAGENT, "SuperBLT");

		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, ALLOWED_PROTOCOLS);
		curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, ALLOWED_REDIRECT_PROTOCOLS);

		// Accept every encoding curl was built with (gzip, deflate) and decode transparently
		curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

		HstsSnapshot hsts;
		{
			std::lock_guard<std::mutex> lock(hstsMutex);
			hsts.entries = hstsEntries;
		}
		curl_easy_setopt(curl, CURLOPT_HSTS_CTRL, static_cast<long>(CURLHSTS_ENABLE));
		curl_easy_setopt(curl, CURLOPT_HSTSREADFUNCTION, hsts_read);
		curl_easy_setopt(curl, CURLOPT_HSTSREADDATA, &hsts);
		curl_easy_setopt(curl, CURLOPT_HSTSWRITEFUNCTION, hsts_write);

		if (item->progress)
		{
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, http_progress_call);
			curl_easy_setopt(curl, CURLOPT_XFERINFODATA, item.get());
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
		}

		curl_slist* requestHeaders = ApplyRequestOptions(curl, *item);

		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_http_header);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, item.get());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_http_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, item.get());

		item->errorCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &(item->httpStatusCode));
		curl_easy_cleanup(curl); // hands the HSTS entries back through hsts_write
		curl_slist_free_all(requestHeaders);
		HstsSaveIfChanged();

		GetHTTPItemQueue().AddToQueue(run_http_event, std::move(item));
	}

	void HTTPManager::LaunchHTTPRequest(std::unique_ptr<HTTPItem> callback)
	{
		RAIDHOOK_TRACE_FUNC;
		// Request threads are detached, so finished ones don't pile up
		HTTPItem* raw_item = callback.release();
		{
			std::lock_guard<std::mutex> lock(liveItemsMutex);
			raw_item->id = nextItemId++;
			liveItems[raw_item->id] = raw_item;
		}
		try
		{
			std::thread(launch_thread_http, raw_item).detach();
		}
		catch (const std::system_error&)
		{
			// Couldn't start a thread, don't leak the item
			{
				std::lock_guard<std::mutex> lock(liveItemsMutex);
				liveItems.erase(raw_item->id);
			}
			delete raw_item;
			RAIDHOOK_LOG_ERROR("Failed to start HTTP request thread");
		}
	}
}
