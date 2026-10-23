#ifndef __HTTP_HEADER__
#define __HTTP_HEADER__

#include <string>
#include <memory>
#include <map>
#include <cstdint>
#include <vector>

namespace raidhook
{

	void download_blt();
	struct HTTPItem;

	typedef void(*HTTPCallback)(HTTPItem* httpItem);
	typedef void(*HTTPProgress)(void* data, long progress, long total);

	struct HTTPItem
	{
		HTTPCallback call = nullptr;
		HTTPProgress progress = nullptr;
		std::string url;

		// What the request looks like. Without a method it is a GET, or a POST if there is a body.
		std::string method;
		std::vector<std::string> requestHeaders; // "Name: value"
		bool hasBody = false;
		std::string body;
		std::string cookies;   // sent with this request only
		std::string cookieJar; // name of a cookie jar shared by all requests that use this name, empty for none

		std::string httpContents;
		std::map<std::string, std::string> responseHeaders;
		std::vector<std::string> setCookies; // every Set-Cookie header of the response, as sent
		int errorCode;
		long httpStatusCode;
		void* data = nullptr;
		uint64_t id = 0;

		long byteprogress = 0;
		long bytetotal = 0;
	};

	// Forgets all cookies of the named jar
	void ClearHttpCookies(const std::string& jar);

	class HTTPManager
	{
	private:
		HTTPManager();

	public:
		~HTTPManager();

		static HTTPManager* GetSingleton();

		void LaunchHTTPRequest(std::unique_ptr<HTTPItem> callback);
	};
}

#endif // __HTTP_HEADER__