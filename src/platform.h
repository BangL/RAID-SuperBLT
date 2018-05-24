#pragma once
#include "lua.h"

namespace blt
{
#define idstring_none 0
	typedef unsigned long long idstring;

	class idfile
	{
	public:
		idfile() : name(idstring_none), ext(idstring_none) {}
		idfile(idstring name, idstring ext) : name(name), ext(ext) {}
		idstring name;
		idstring ext;

		inline bool operator ==(const idfile &other) const
		{
			return other.name == name && other.ext == ext;
		}

		// Required for std::less to function on Windows
		inline bool operator < (const idfile &other) const
		{
			return (name != other.name) ? name < other.name : ext < other.ext;
		}
	};

	namespace platform
	{
		extern idstring *last_loaded_name, *last_loaded_ext;

		void InitPlatform();
		void ClosePlatform();

		void GetPlatformInformation(lua_State *L);

		namespace lua
		{
			bool GetForcePCalls();
			void SetForcePCalls(bool);
		};

#ifdef _WIN32
		namespace win32
		{
			void OpenConsole();
			void* get_lua_func(const char* name);
		};
#endif
	};
};
