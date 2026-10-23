#pragma once

#include <safetyhook.hpp>
#include <util/util.h>

namespace blt
{
	namespace hooks
	{
		// Installs an inline hook, logs on failure. Call the original via hook.call<Ret>(args...).
		template <typename T, typename U>
		inline bool install(safetyhook::InlineHook& hook, T target, U destination, const char* name)
		{
			auto result = safetyhook::InlineHook::create(reinterpret_cast<void*>(target), reinterpret_cast<void*>(destination));
			if (!result)
			{
				RAIDHOOK_LOG_ERROR("Failed to hook " << name << " (error type " << static_cast<int>(result.error().type) << ")");
				return false;
			}
			hook = std::move(*result);
			return true;
		}
	}
}

// Hooks are never destroyed. Destroying SafetyHook hooks while the process is ending (static destructors, or in
// DllMain on DLL_PROCESS_DETACH) hangs the game: a log written during such a shutdown stopped inside the destructor, with
// only one thread left in the process. So the objects are leaked on purpose.
#define SBLT_DECLARE_HOOK(name) static safetyhook::InlineHook& name = *new safetyhook::InlineHook()

extern safetyhook::InlineHook& luaCallDetour;
