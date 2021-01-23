#pragma once

#include "platform.h"

namespace pd2hook
{
	namespace tweaker
	{
		char* tweak_pd2_xml(char* text, int text_length);
		void free_tweaked_pd2_xml(char* text);

		void ignore_file(blt::idfile file);

		extern bool tweaker_enabled;
	}; // namespace tweaker
}; // namespace pd2hook
