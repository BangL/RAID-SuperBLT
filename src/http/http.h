#ifndef __HTTP_HEADER__
#define __HTTP_HEADER__

#include <string>
#include <mutex>
#include <thread>
#include <list>
#include <memory>
#include <map>

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
		std::string httpContents;
		std::map<std::string, std::string> responseHeaders;
		int errorCode;
		long httpStatusCode;
		void* data = nullptr;

		long byteprogress = 0;
		long bytetotal = 0;
	};

	class HTTPManager
	{
	  private:
		HTTPManager();

	  public:
		~HTTPManager();

		static HTTPManager* GetSingleton();

		void LaunchHTTPRequest(std::unique_ptr<HTTPItem> callback);
		void CleanupFinishedThreads();

	  private:
		std::list<std::unique_ptr<std::thread>> threadList;
		std::mutex threadListMutex;
	};
} // namespace raidhook

#endif // __HTTP_HEADER__