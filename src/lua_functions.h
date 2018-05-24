#pragma once

#include "lua.h"

namespace blt
{
	namespace lua_functions
	{
		void initiate_lua(lua_State *L);
		void update(lua_State *L);
		void close(lua_State *L);

		void perform_lua_pcall(lua_State* L, int args, int returns);
	};
};
