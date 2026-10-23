#include "global.h"
#include "util/util.h"

#include <ctime>
#include <fstream>
#include <iostream>

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace raidhook
{
	namespace Logging
	{
		namespace
		{
			std::string GetDateString()
			{
				std::time_t currentTime = time(0);
				std::tm now;
				localtime_r(&currentTime, &now);

				char datestring[100];
				std::strftime(datestring, sizeof(datestring), "%Y_%m_%d", &now);
				return datestring;
			}

			std::ostream& LogTime(std::ostream& os)
			{
				std::time_t currentTime = time(0);
				std::tm now;
				localtime_r(&currentTime, &now);

				char datestring[100];
				std::strftime(datestring, sizeof(datestring), "%I:%M:%S %p", &now);

				os << datestring << ' ';
				return os;
			}

			std::ostream& operator<<(std::ostream& os, LogType msgType)
			{
				switch (msgType)
				{
				case LogType::LOGGING_FUNC:
					break;
				case LogType::LOGGING_LOG:
					os << "Log: ";
					break;
				case LogType::LOGGING_LUA:
					os << "Lua: ";
					break;
				case LogType::LOGGING_WARN:
					os << "WARNING: ";
					break;
				case LogType::LOGGING_ERROR:
					os << "FATAL ERROR: ";
					break;
				default:
					os << "Message: ";
					break;
				}
				return os;
			}

			std::ostream& quick_endl(std::ostream& os)
			{
				os << '\n';
				return os;
			}

			typedef decltype(&quick_endl) LineTerminator_t;

			// Timed, so the exit handler can give up instead of waiting for a thread that was already killed
			std::timed_mutex& GetLoggerMutex()
			{
				static std::timed_mutex loggerMutex;
				return loggerMutex;
			}

			class LoggerImpl : public Logger
			{
			public:
				LoggerImpl(std::string&& file);

				void setForceFlush(bool forceFlush);
				void flush();

				bool isOpen() const
				{
					return mIsOpen;
				}

				void openFile(std::string&& file);
				void close();

				void log(const Message_t& msg);

				// Writes buffered lines to disk if there are any
				void flushIfDirty();
				void flushAtExit();

			private:
				bool mIsOpen = false;
				bool mDirty = false;

				LineTerminator_t mEndl = quick_endl;
				std::string mFilename;
				std::ofstream mOut;
			};

			void LoggerImpl::setForceFlush(bool forceFlush)
			{
				std::ostream& (*forceFlusher)(std::ostream&) = &std::endl;
				mEndl = forceFlush ? forceFlusher : quick_endl;
			}

			void LoggerImpl::flush()
			{
				std::lock_guard<std::timed_mutex> lock(GetLoggerMutex());
				mOut.flush();
				mDirty = false;
			}

			void LoggerImpl::flushIfDirty()
			{
				std::lock_guard<std::timed_mutex> lock(GetLoggerMutex());
				if (mDirty)
				{
					mOut.flush();
					mDirty = false;
				}
			}

			void LoggerImpl::flushAtExit()
			{
				// Other threads are probably gone by now, don't hang if one of them died holding the lock
				std::unique_lock<std::timed_mutex> lock(GetLoggerMutex(), std::defer_lock);
				if (lock.try_lock_for(std::chrono::milliseconds(200)) && mIsOpen)
				{
					mOut.flush();
					mDirty = false;
				}
			}

			LoggerImpl::LoggerImpl(std::string&& file)
			{
				Util::EnsurePathWritable(file);
				openFile(std::forward<std::string>(file));
			}

			void LoggerImpl::openFile(std::string&& file)
			{
				if (mFilename == file)
				{
					return;
				}

				std::lock_guard<std::timed_mutex> lock(GetLoggerMutex());
				mOut.close();
				mOut = std::ofstream(file.c_str(), std::ios::app);
				mFilename = std::move(file);
				mIsOpen = !!mOut;
			}

			void LoggerImpl::close()
			{
				std::lock_guard<std::timed_mutex> lock(GetLoggerMutex());
				if (!mIsOpen)
				{
					return;
				}

				mOut.close();
				mFilename.clear();
				mIsOpen = false;
			}

			void LoggerImpl::log(const Message_t& msg)
			{
				std::lock_guard<std::timed_mutex> lock(GetLoggerMutex());

				std::cout << msg << mEndl;

				if (!mIsOpen)
				{
					return;
				}

				mOut << msg << mEndl;

				// With quick_endl nothing reaches the disk by itself, the flush thread takes care of that
				mDirty = true;
			}
		}

		Logger& Logger::Instance()
		{
			// Never destroyed: the flush thread keeps using it, and static destructors run at a point where
			// other threads may already be gone. The exit handler below takes over the final flush.
			static LoggerImpl* logger = []() {
				auto* impl = new LoggerImpl("mods/logs/" + GetDateString() + "_log.txt");

				// Lines are only flushed on warnings and errors, so without this the end of the log would be
				// lost on a crash. A short delay costs next to nothing, unlike flushing every line.
				std::thread([impl]() {
					while (true)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(250));
						impl->flushIfDirty();
					}
				}).detach();

				std::atexit([]() { static_cast<LoggerImpl&>(Logger::Instance()).flushAtExit(); });
				return impl;
			}();
			return *logger;
		}

		void Logger::Close()
		{
			static_cast<LoggerImpl&>(Instance()).close();
		}

		void Logger::setForceFlush(bool forceFlush)
		{
			static_cast<LoggerImpl *>(this)->setForceFlush(forceFlush);
		}

		void Logger::flush()
		{
			static_cast<LoggerImpl *>(this)->flush();
		}

		void Logger::log(const Message_t& msg)
		{
			static_cast<LoggerImpl *>(this)->log(msg);
		}

		LogWriter::LogWriter(LogType msgType)
		{
			*this << LogTime << msgType << ' ';
		}

		LogWriter::LogWriter(const char *file, int line, LogType msgType)
		{
			if (line && line > 0)
			{
				*this << LogTime << msgType << " (" << file << ':' << line << ") ";
			}
			else if (file)
			{
				*this << LogTime << msgType << " (" << file << ") ";
			}
			else
			{
				*this << LogTime << msgType;
			}

			// Always flush high-priority messages, in case the game crashes we don't want
			// the end of the log to get lost.
			if (msgType == LogType::LOGGING_WARN || msgType == LogType::LOGGING_ERROR)
			{
				needsFlush = true;
			}
		}
	}
}
