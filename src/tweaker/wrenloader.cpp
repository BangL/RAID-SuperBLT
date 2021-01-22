#include <fstream>
#include <vector>

#include "db_hooks.h"
#include "global.h"
#include "plugins/plugins.h"
#include "util/util.h"
#include "wrenxml.h"
#include "xmltweaker_internal.h"

#include <wren.hpp>

#include "wren_generated_src.h"

using namespace pd2hook;
using namespace pd2hook::tweaker;
using namespace std;

static WrenVM* globalVM = nullptr;

static void err([[maybe_unused]] WrenVM* vm, [[maybe_unused]] WrenErrorType type, const char* module, int line,
                const char* message)
{
	if (module == nullptr)
		module = "<unknown>";
	PD2HOOK_LOG_LOG(string("[WREN ERR] ") + string(module) + ":" + to_string(line) + " ] " + message);
}

static void log(WrenVM* vm)
{
	const char* text = wrenGetSlotString(vm, 1);
	PD2HOOK_LOG_LOG(string("[WREN] ") + text);
}

static string file_to_string(ifstream& in)
{
	return string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

void io_listDirectory(WrenVM* vm)
{
	string filename = wrenGetSlotString(vm, 1);
	bool dir = wrenGetSlotBool(vm, 2);
	vector<string> files = Util::GetDirectoryContents(filename, dir);

	wrenSetSlotNewList(vm, 0);

	for (string const& file : files)
	{
		if (file == "." || file == "..")
			continue;

		wrenSetSlotString(vm, 1, file.c_str());
		wrenInsertInList(vm, 0, -1, 1);
	}
}

void io_info(WrenVM* vm)
{
	const char* path = wrenGetSlotString(vm, 1);

	Util::FileType type = Util::GetFileType(path);

	if (type == Util::FileType_None)
	{
		wrenSetSlotString(vm, 0, "none");
	}
	else if (type == Util::FileType_Directory)
	{
		wrenSetSlotString(vm, 0, "dir");
	}
	else
	{
		wrenSetSlotString(vm, 0, "file");
	}
}

void io_read(WrenVM* vm)
{
	string file = wrenGetSlotString(vm, 1);

	ifstream handle(file);
	if (!handle.good())
	{
		PD2HOOK_LOG_ERROR("Wren IO.read: Could not load file " + file);
		exit(1);
	}

	string contents = file_to_string(handle);
	wrenSetSlotString(vm, 0, contents.c_str());
}

void io_idstring_hash(WrenVM* vm)
{
	blt::idstring hash = idstring_hash(wrenGetSlotString(vm, 1));

	char hex[17]; // 16-chars long +1 for the null
	snprintf(hex, sizeof(hex), "%016llx", hash);
	wrenSetSlotString(vm, 0, hex);
}

static void io_load_plugin(WrenVM* vm)
{
	const char* plugin_filename = wrenGetSlotString(vm, 1);
	try
	{
		blt::plugins::LoadPlugin(plugin_filename);
	}
	catch (const string& err)
	{
		string msg = string("LoadPlugin: ") + string(plugin_filename) + string(" : ") + err;
		wrenSetSlotString(vm, 0, msg.c_str());
		wrenAbortFiber(vm, 0);
	}
}

static WrenForeignClassMethods bindForeignClass(WrenVM* vm, const char* module, const char* class_name)
{
	WrenForeignClassMethods methods = wrenxml::get_XML_class_def(vm, module, class_name);
	if (methods.allocate || methods.finalize)
		return methods;

	methods = dbhook::bind_dbhook_class(vm, module, class_name);

	return methods;
}

static WrenForeignMethodFn bindForeignMethod(WrenVM* vm, const char* module, const char* className, bool isStatic,
                                             const char* signature)
{
	WrenForeignMethodFn wxml_method = wrenxml::bind_wxml_method(vm, module, className, isStatic, signature);
	if (wxml_method)
		return wxml_method;

	WrenForeignMethodFn dbhook_method = dbhook::bind_dbhook_method(vm, module, className, isStatic, signature);
	if (dbhook_method)
		return dbhook_method;

	if (strcmp(module, "base/native") == 0)
	{
		if (strcmp(className, "Logger") == 0)
		{
			if (isStatic && strcmp(signature, "log(_)") == 0)
			{
				return &log; // C function for Math.add(_,_).
			}
			// Other foreign methods on Math...
		}
		else if (strcmp(className, "IO") == 0)
		{
			if (isStatic && strcmp(signature, "listDirectory(_,_)") == 0)
			{
				return &io_listDirectory;
			}
			if (isStatic && strcmp(signature, "info(_)") == 0)
			{
				return &io_info;
			}
			if (isStatic && strcmp(signature, "read(_)") == 0)
			{
				return &io_read;
			}
			if (isStatic && strcmp(signature, "idstring_hash(_)") == 0)
			{
				return &io_idstring_hash;
			}
			if (isStatic && strcmp(signature, "load_plugin(_)") == 0)
			{
				return &io_load_plugin;
			}
		}
		// Other classes in main...
	}
	// Other modules...

	return nullptr;
}

static char* getModulePath([[maybe_unused]] WrenVM* vm, const char* name_c)
{
	// First see if this is a module that's embedded within SuperBLT
	const char* builtin_string = nullptr;
	lookup_builtin_wren_src(name_c, &builtin_string);
	if (builtin_string)
	{
		size_t length = strlen(builtin_string) + 1;
		char* output = (char*)malloc(length); // +1 for the null
		portable_strncpy(output, builtin_string, length);

		return output; // free()d by Wren
	}

	// Otherwise it's a normal wren file, load it from the appropriate mod
	string name = name_c;
	string mod = name.substr(0, name.find_first_of('/'));
	string file = name.substr(name.find_first_of('/') + 1);

	ifstream handle("mods/" + mod + "/wren/" + file + ".wren");
	if (!handle.good())
	{
		return nullptr;
	}

	string str = file_to_string(handle);

	size_t length = str.length() + 1;
	char* output = (char*)malloc(length); // +1 for the null
	portable_strncpy(output, str.c_str(), length);

	return output; // free()d by Wren
}

static bool available = true;

const char* tweaker::transform_file(const char* text)
{
	// We've renamed the global variable (the old name, 'vm' was a bad idea for a global)
	// It's still more convenient to use it locally though.
	WrenVM*& vm = globalVM;

	if (vm == nullptr)
	{
		if (available)
		{
			// If the main file doesn't exist, do nothing
			Util::FileType ftyp = Util::GetFileType("mods/base/wren/base.wren");
			if (ftyp == Util::FileType_None)
				available = false;
		}

		if (!available)
			return text;

		WrenConfiguration config;
		wrenInitConfiguration(&config);
		config.errorFn = &err;
		config.bindForeignMethodFn = &bindForeignMethod;
		config.bindForeignClassFn = &bindForeignClass;
		config.loadModuleFn = &getModulePath;
		vm = wrenNewVM(&config);

		WrenInterpretResult result = wrenInterpret(vm, "__root", R"!( import "base/base" )!");
		if (result == WREN_RESULT_COMPILE_ERROR || result == WREN_RESULT_RUNTIME_ERROR)
		{
			PD2HOOK_LOG_ERROR("Wren init failed: compile or runtime error!");

#ifdef _WIN32
			MessageBox(nullptr, "Failed to initialise the Wren system - see the log for details", "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}
	}

	wrenEnsureSlots(vm, 4);

	wrenGetVariable(vm, "base/base", "BaseTweaker", 0);
	WrenHandle* tweakerClass = wrenGetSlotHandle(vm, 0);
	WrenHandle* sig = wrenMakeCallHandle(vm, "tweak(_,_,_)");

	char hex[17]; // 16-chars long +1 for the null

	wrenSetSlotHandle(vm, 0, tweakerClass);

	snprintf(hex, sizeof(hex), "%016llx", *blt::platform::last_loaded_name);
	wrenSetSlotString(vm, 1, hex);

	snprintf(hex, sizeof(hex), "%016llx", *blt::platform::last_loaded_ext);
	wrenSetSlotString(vm, 2, hex);

	wrenSetSlotString(vm, 3, text);

	// TODO give a reasonable amount of information on what happened.
	WrenInterpretResult result2 = wrenCall(vm, sig);
	if (result2 == WREN_RESULT_COMPILE_ERROR)
	{
		PD2HOOK_LOG_ERROR("Wren tweak file failed: compile error!");
		return text;
	}
	else if (result2 == WREN_RESULT_RUNTIME_ERROR)
	{
		PD2HOOK_LOG_ERROR("Wren tweak file failed: runtime error!");
		return text;
	}

	wrenReleaseHandle(vm, tweakerClass);
	wrenReleaseHandle(vm, sig);

	const char* new_text = wrenGetSlotString(vm, 0);

	return new_text;
}
