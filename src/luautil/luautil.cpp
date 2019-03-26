#include "luautil.h"

// Copied from 5.1's and 5.2's lbaselib.c

static int load_aux (lua_State *L, int status)
{
	if (!status)
	{
		return 1; // success
	}
	else
	{
		lua_pushnil(L);
		lua_insert(L, -2);  // put before error message
		return 2;  // return nil plus error message
	}
}

static int vmlua_load(lua_State *L)
{
	int status;
	size_t l;
	const char *s = lua_tolstring(L, 1, &l);
	if (s != NULL)    /* loading a string? */
	{
		const char *chunkname = luaL_optstring(L, 2, s);
		status = luaL_loadbufferx(L, s, l, chunkname, "t");
	}
	else    /* loading from a reader function */
	{
		// const char *chunkname = luaL_optstring(L, 2, "=(load)");
		// luaL_checktype(L, 1, LUA_TFUNCTION);
		// lua_settop(L, 3);  /* create reserved slot */
		// status = lua_load(L, generic_reader, NULL, chunkname, "t");
		luaL_error(L, "passing functions to blt.vm.load is not yet supported");

		// suppress GCC's warning about status being unset (control will never reach here)
		return 0;
	}

	return load_aux(L, status);
}

static int vmlua_loadfile (lua_State *L)
{
	const char *fname = luaL_checklstring(L, 1, nullptr);
	return load_aux(L, luaL_loadfile(L, fname));
}

static int vmlua_dofile (lua_State *L)
{
	const char *fname = luaL_checklstring(L, 1, nullptr);
	int n = lua_gettop(L);
	if (luaL_loadfile(L, fname) != 0) lua_error(L);
	lua_call(L, 0, LUA_MULTRET);
	return lua_gettop(L) - n;
}

void load_lua_utils(lua_State *L)
{
	luaL_Reg vmLib[] =
	{
		{ "load", vmlua_load },
		{ "loadfile", vmlua_loadfile },
		{ "dofile", vmlua_dofile },

		{ nullptr, nullptr }
	};

	lua_newtable(L);
	luaL_openlib(L, nullptr, vmLib, 0);
	lua_setfield(L, -2, "vm");
}
