//
// Created by ZNix on 16/08/2020.
//

#include <vector>
#define INCLUDE_TRY_OPEN_FUNCTIONS

#include "assets.h"
#include "platform.h"
#include "hook_util.h"

#include <stdio.h>

#include <map>
#include <string>
#include <tweaker/db_hooks.h>
#include <utility>

using raidhook::tweaker::dbhook::hook_asset_load;

#include "assets/assets_game.cpp"

// Three hooks for the other try_open functions: property_match_resolver, language_resolver and english_resolver
DECLARE_PASSTHROUGH_ARRAY(0)
DECLARE_PASSTHROUGH_ARRAY(1)
DECLARE_PASSTHROUGH_ARRAY(2)
DECLARE_PASSTHROUGH(try_open_property_match_resolver)

void blt::win32::InitAssets()
{
#define SETUP_PASSTHROUGH(func) blt::hooks::install(hook_##func, func, stub_##func, #func)
#define SETUP_PASSTHROUGH_ARRAY(id) blt::hooks::install(hook_##id, try_open_functions.at(id), stub_##id, "try_open_" #id)

	if (!try_open_functions.empty())
		SETUP_PASSTHROUGH_ARRAY(0);
	if (try_open_functions.size() > 1)
		SETUP_PASSTHROUGH_ARRAY(1);
	if (try_open_functions.size() > 2)
		SETUP_PASSTHROUGH_ARRAY(2);

	SETUP_PASSTHROUGH(try_open_property_match_resolver);

	setup_extra_asset_hooks();
}
