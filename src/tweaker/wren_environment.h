//
// Created by znix on 08/04/2021.
//

#pragma once

#include <wren.hpp>

namespace raidhook::tweaker::wren_env
{
	WrenForeignMethodFn bind_wren_env_method(WrenVM* vm, const char* module, const char* className, bool isStatic,
	                                         const char* signature);
} // namespace raidhook::tweaker::wren_env
