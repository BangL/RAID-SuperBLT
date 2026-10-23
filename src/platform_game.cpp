#include "lua_functions.h"
#include "hook_util.h"

#include "tweaker/xmltweaker.h"

#include <thread>
#include <util/util.h>

static std::thread::id main_thread_id;
SBLT_DECLARE_HOOK(applicationUpdateDetour);
SBLT_DECLARE_HOOK(newStateDetour);
SBLT_DECLARE_HOOK(luaCloseDetour);
SBLT_DECLARE_HOOK(node_from_xmlDetour);

static void init_idstring_pointers()
{
	char *tmp;

	tmp = (char*)try_open_property_match_resolver;
	tmp += 0x3A;
	tmp += *(unsigned int*)tmp + 4; // 64-Bit RIP Offset MOV

	blt::platform::last_loaded_name = (blt::idstring*)tmp;

	tmp = (char*)try_open_property_match_resolver;
	tmp += 0x33;
	tmp += *(unsigned int*)tmp + 4; // 64-Bit RIP Offset MOV

	blt::platform::last_loaded_ext = (blt::idstring*)tmp;
}

static void* ctor_lua_Alloc_new(void* ud, void* ptr, size_t osize, size_t nsize)
{
	void* ret = newStateDetour.call<void*>(ud, ptr, osize, nsize);

	lua_State* L = (lua_State*)*((void**)ud);
	size_t pVal = reinterpret_cast<size_t>(ptr);
	if (ret && L && pVal == 1 && osize == 1)
	{
		printf("Lua State: %p\n", (void*)L);
		blt::lua_functions::initiate_lua(L);
	}

	return ret;
}

static void* application_update_new(void* _unk0, int* _unk1, int* _unk2)
{
	if (std::this_thread::get_id() == main_thread_id)
	{
		blt::lua_functions::update();
	}

	return applicationUpdateDetour.call<void*>(_unk0, _unk1, _unk2);
}

static void lua_close_new(lua_State* L)
{
	blt::lua_functions::close(L);
	luaCloseDetour.call<void>(L);
}

// The length is a 64-bit value in the engine (it reads it as a qword to find the end of the text). With a 4-byte int
// the upper half is whatever lies next to it on the stack, which a GCC build does not keep zero.
static void node_from_xml_new(void* node, char* data, size_t* len)
{
	char* modded = raidhook::tweaker::tweak_raid_xml(data, static_cast<int>(*len));
	size_t modLen = *len;

	if (modded != data)
	{
		modLen = strlen(modded);
	}

	node_from_xmlDetour.call<void>(node, modded, &modLen);

	raidhook::tweaker::free_tweaked_raid_xml(modded);
}

static void setup_platform_game()
{
	main_thread_id = std::this_thread::get_id();

	blt::hooks::install(applicationUpdateDetour, application_update, application_update_new, "application_update");
	blt::hooks::install(newStateDetour, ctor_lua_Alloc, ctor_lua_Alloc_new, "ctor_lua_Alloc");
	blt::hooks::install(luaCloseDetour, lua_close_exe, lua_close_new, "lua_close");
	blt::hooks::install(node_from_xmlDetour, node_from_xml, node_from_xml_new, "node_from_xml");

	init_idstring_pointers();
}