#include "global.h"
#include "xmltweaker_internal.h"
#include <stdio.h>
#include <fstream>
#include <unordered_set>
#include <set>
#include <mutex>
#include <string.h>
#include "util/util.h"

#include <wren.hpp>

using namespace std;
using namespace raidhook;
using namespace tweaker;
using blt::idstring;
using blt::idfile;

bool raidhook::tweaker::tweaker_enabled = true;

// The hooked parser may be entered from several engine threads
static std::recursive_mutex tweaker_mutex;
static unordered_set<char*> buffers;
static set<idfile> ignored_files;

// The file we last parsed. If we try to parse the same file more than
// once, nothing should happen as a file from the filesystem is being loaded.
idfile last_parsed;

// This is the old way of tweaking XML files, which a current basemod switches off (Internal.tweaker_enabled) in favour of
// asset hooks that get the exact name and extension. It finds out which file is parsed from the engine globals of the last
// file opened, which is wrong for about 8 % of the parses (measured over 6,367): other threads open files in between, and
// the files of a package are parsed in batches without a fresh open. The file that this thread opened last, or the archive
// found on the stack of the parser, are no better (the archive is the package's, not the file's). Nothing in reach of the
// hook names the file, so this stays as it is for old basemods.
char* tweaker::tweak_raid_xml(char* text, int text_length)
{
	std::lock_guard<std::recursive_mutex> lock(tweaker_mutex);

	if (!tweaker_enabled)
	{
		return text;
	}

	idfile file = idfile(*blt::platform::last_loaded_name, *blt::platform::last_loaded_ext);

	// Don't parse the same file more than once, as it's not actually the same file.
	if (last_parsed == file)
	{
		return text;
	}

	last_parsed = file;

	// Don't bother with .model or .texture files
	if (
	    file.ext == 0xaf612bbc207e00bd ||	// idstring("model")
	    file.ext == 0x5368e150b05a5b8c		// idstring("texture")
	)
	{
		return text;
	}

	// Check the exclusion list
	if (ignored_files.count(file))
	{
		return text;
	}

	const char* new_text = transform_file(text);

	// If the text is not to be altered, we can return it as is.
	if (new_text == text) return text;

	// Otherwise, copy it so it's not invalidated by another Wren call

	size_t length = strlen(new_text) + 1; // +1 for the null

	char* buffer = (char*)malloc(length);
	buffers.insert(buffer);

	portable_strncpy(buffer, new_text, length);

	//if (!strncmp(new_text, "<network>", 9)) {
	//	std::ofstream out("output.txt");
	//	out << new_text;
	//	out.close();
	//}

	/*static int counter = 0;
	if (counter++ == 1) {
		printf("%s\n", buffer);

		const int kMaxCallers = 62;
		void* callers[kMaxCallers];
		int count = CaptureStackBackTrace(0, kMaxCallers, callers, NULL);
		for (int i = 0; i < count; i++)
			printf("*** %d called from .text:%08X\n", i, callers[i]);
		Sleep(20000);
	}*/

	return (char*)buffer;
}

void tweaker::free_tweaked_raid_xml(char* text)
{
	std::lock_guard<std::recursive_mutex> lock(tweaker_mutex);
	if (buffers.erase(text))
	{
		free(text);
	}
}

void raidhook::tweaker::ignore_file(idfile file)
{
	std::lock_guard<std::recursive_mutex> lock(tweaker_mutex);
	ignored_files.insert(file);
}
