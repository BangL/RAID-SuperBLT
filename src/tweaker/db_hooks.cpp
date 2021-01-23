//
// Created by ZNix on 23/11/2020.
//

#include "db_hooks.h"
#include "wrenloader.h"
#include "xmltweaker_internal.h"

#include <dbutil/DB.h>
#include <platform.h>
#include <util/util.h>

#include <assert.h>
#include <string.h>

#include <map>
#include <memory>
#include <optional>
#include <string>

using pd2hook::tweaker::idstring_hash;

using blt::db::DieselDB;
using blt::db::DslFile;

static const char* MODULE = "base/native/DB_001";

static void wrenRegisterAssetHook(WrenVM* vm);

class DBTargetFile
{
  public:
	blt::idfile id;

	/** If true, this asset will only be loaded if the default asset with this name/ext does not exist. */
	bool fallback = false;

	/** The path to the file to load and use for this asset */
	std::optional<std::string> plain_file;

	/** The ID of the in-bundle asset to use */
	blt::idfile direct_bundle = blt::idfile();

	/** The handle to a Wren object to run the loading callback on */
	WrenHandle* wren_loader_obj = nullptr;

	explicit DBTargetFile(blt::idfile id) : id(id)
	{
	}

	void clear_sources()
	{
		plain_file.reset();
		direct_bundle = blt::idfile();

		if (wren_loader_obj)
		{
			wrenReleaseHandle(pd2hook::wren::get_wren_vm(), wren_loader_obj);
			wren_loader_obj = nullptr;
		}
	}
};

class DBForeignFile
{
  public:
	// Magic cookie to make sure this is the correct class
	uint64_t magic;
	static const uint64_t MAGIC_COOKIE = 0xb4cb844461d94c07; // random value

	// Note: use unique_ptrs here for strings since our destructor won't be called, so otherwise they could leak
	std::unique_ptr<std::string> filename;
	blt::idfile asset;
	std::unique_ptr<std::string> stringLiteral;

	static void ofFile(WrenVM* vm);
	static void ofAsset(WrenVM* vm);
	static void fromString(WrenVM* vm);

	static void finalise(void* this_data);

  private:
	static DBForeignFile* create(WrenVM* vm);
};

class DBAssetHook
{
  public:
	// Magic cookie to make sure this is the correct class
	uint64_t magic;
	static const uint64_t MAGIC_COOKIE = 0xee3f682656ceebdd; // random value

	static void getFallback(WrenVM* vm);
	static void setFallback(WrenVM* vm);
	static void getMode(WrenVM* vm);
	static void isEnabled(WrenVM* vm);
	static void disable(WrenVM* vm);
	static void getPlainFile(WrenVM* vm);
	static void setPlainFile(WrenVM* vm);
	static void getDirectBundle(WrenVM* vm);
	static void setDirectBundle(WrenVM* vm);
	static void getWrenLoader(WrenVM* vm);
	static void setWrenLoader(WrenVM* vm);

	std::shared_ptr<DBTargetFile> file;

	static void finalise(void* this_data);
};

static std::map<blt::idfile, std::shared_ptr<DBTargetFile>> overriddenFiles;

WrenForeignMethodFn pd2hook::tweaker::dbhook::bind_dbhook_method(WrenVM* vm, const char* module,
                                                                 const char* class_name_s, bool is_static,
                                                                 const char* signature_c)
{
	if (std::string(module) != MODULE)
		return nullptr;

	std::string class_name = class_name_s;
	std::string signature = signature_c;

	if (class_name == "DBManager" && is_static)
	{
		if (signature == "register_asset_hook(_,_)")
		{
			return wrenRegisterAssetHook;
		}
	}
	else if (class_name == "DBAssetHook" && !is_static)
	{
		if (signature == "fallback")
			return &DBAssetHook::getFallback;
		else if (signature == "fallback=(_)")
			return &DBAssetHook::setFallback;
		else if (signature == "mode")
			return &DBAssetHook::getMode;
		else if (signature == "enabled")
			return &DBAssetHook::isEnabled;
		else if (signature == "disable()")
			return &DBAssetHook::disable;
		else if (signature == "plain_file")
			return &DBAssetHook::getPlainFile;
		else if (signature == "plain_file=(_)")
			return &DBAssetHook::setPlainFile;
		else if (signature == "direct_bundle")
			return &DBAssetHook::getDirectBundle;
		else if (signature == "set_direct_bundle(_,_)")
			return &DBAssetHook::setDirectBundle;
		else if (signature == "wren_loader")
			return &DBAssetHook::getWrenLoader;
		else if (signature == "wren_loader=(_)")
			return &DBAssetHook::setWrenLoader;
	}
	else if (class_name == "DBForeignFile" && is_static)
	{
		if (signature == "of_file(_)")
			return &DBForeignFile::ofFile;
		else if (signature == "of_asset(_,_)")
			return &DBForeignFile::ofAsset;
		else if (signature == "from_string(_)")
			return &DBForeignFile::fromString;
	}

	return nullptr;
}

WrenForeignClassMethods pd2hook::tweaker::dbhook::bind_dbhook_class([[maybe_unused]] WrenVM* vm, const char* module,
                                                                    const char* class_name)
{
	// Since we're using our own factory methods, this should never be called
	WrenForeignMethodFn fakeAllocate = [](WrenVM* vm) { abort(); };

	if (!strcmp(module, "base/native"))
	{
		if (!strcmp(class_name, "DBAssetHook"))
		{
			WrenForeignClassMethods def;
			def.allocate = fakeAllocate;
			def.finalize = &DBAssetHook::finalise;
			return def;
		}
		else if (!strcmp(class_name, "DBForeignFile"))
		{
			WrenForeignClassMethods def;
			def.allocate = fakeAllocate;
			def.finalize = &DBForeignFile::finalise;
			return def;
		}
	}
	return {nullptr, nullptr};
}

static blt::idstring parseHash(std::string value)
{
	if (value.size() == 17 && value.at(0) == '@')
	{
		char* endPtr = nullptr;
		blt::idstring id = strtoll(value.c_str() + 1, &endPtr, 16);
		if (endPtr != value.c_str() + 17)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1, "[wren] Invalid hash value '%s': Not a valid ID literal", value.c_str());
			PD2HOOK_LOG_ERROR(buff);
			abort();
		}
		return id;
	}

	return idstring_hash(value);
}

static void wrenRegisterAssetHook(WrenVM* vm)
{
	blt::idstring name = parseHash(wrenGetSlotString(vm, 1));
	blt::idstring ext = parseHash(wrenGetSlotString(vm, 2));

	blt::idfile file(name, ext);

	if (overriddenFiles.count(file))
	{
		const char* name_str = wrenGetSlotString(vm, 1);
		const char* ext_str = wrenGetSlotString(vm, 2);

		char buff[1024];
		memset(buff, 0, sizeof(buff));
		snprintf(buff, sizeof(buff) - 1, "[wren] Duplicate asset registration for %s.%s", name_str, ext_str);
		PD2HOOK_LOG_ERROR(buff);
		abort();
	}

	auto entry = std::make_shared<DBTargetFile>(file);
	overriddenFiles[file] = entry;

	wrenGetVariable(vm, MODULE, "DBAssetHook", 0);
	auto* hook = (DBAssetHook*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(DBAssetHook));
	hook->file = entry;
	hook->magic = DBAssetHook::MAGIC_COOKIE;
}

bool pd2hook::tweaker::dbhook::hook_asset_load(const blt::idfile& asset_file, BLTAbstractDataStore** out_datastore,
                                               int64_t* out_pos, int64_t* out_len, std::string& out_name,
                                               bool fallback_mode)
{
	// First zero everything
	*out_datastore = nullptr;
	*out_pos = 0;
	*out_len = 0;

	auto filePtr = overriddenFiles.find(asset_file);

	// If the file isn't defined, we're not overriding anything
	if (filePtr == overriddenFiles.end())
		return false;

	DBTargetFile& target = *filePtr->second;

	// If this target is in fallback mode (it'll only load if the base game doesn't provide such a file), and
	// we haven't yet tried loading the base game's version of the file, then stop here.
	if (target.fallback && !fallback_mode)
	{
		return false;
	}

	// TODO write to out_name somewhere

	// Define these loading functions here, as we can use them either directly or after calling Wren
	auto load_file = [&](const std::string& filename) {
		BLTFileDataStore* ds = BLTFileDataStore::Open(filename);

		if (!ds)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1, "Failed to open hooked file '%s' while loading %16llx.%016llx",
			         filename.c_str(), asset_file.name, asset_file.ext);
			PD2HOOK_LOG_ERROR(buff);

#ifdef _WIN32
			MessageBox(nullptr, "Failed to load hooked file - file not found. See log for more information.",
			           "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}

		*out_datastore = ds;
		*out_len = ds->size();
	};

	auto load_bundle_item = [&](blt::idfile bundle_item) {
		DslFile* file = DieselDB::Instance()->Find(bundle_item.name, bundle_item.ext);

		// Abort if the file isn't found - most likely this would lead to a crash anyway since the PD2 version
		// of this asset probably isn't loaded (otherwise why would you hook it?), this just makes it obvious
		// what happened.
		if (!file)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1,
			         "Failed to open hooked asset file %016llx.%016llx while loading %16llx.%016llx", bundle_item.name,
			         bundle_item.ext, asset_file.name, asset_file.ext);
			PD2HOOK_LOG_ERROR(buff);

#ifdef _WIN32
			MessageBox(nullptr, "Failed to load hooked asset - file not found. See log for more information.",
			           "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}

		*out_datastore = DieselDB::Instance()->Open(file->bundle);
		*out_pos = file->offset;
		*out_len = file->length;
	};

	if (target.plain_file)
	{
		load_file(target.plain_file.value());
	}
	else if (!target.direct_bundle.is_empty())
	{
		load_bundle_item(target.direct_bundle);
	}
	else if (target.wren_loader_obj)
	{
		auto lock = pd2hook::wren::lock_wren_vm();
		WrenVM* vm = pd2hook::wren::get_wren_vm();

		// Probably not ideal to have it as a static, but hey it works fine and we only ever make one Wren context
		static WrenHandle* callHandle = wrenMakeCallHandle(vm, "load_file(_,_)");

		char hex[17]; // 16-chars long +1 for the null
		memset(hex, 0, sizeof(hex));

		wrenEnsureSlots(vm, 3);
		wrenSetSlotHandle(vm, 0, target.wren_loader_obj);

		// Set the name
		snprintf(hex, sizeof(hex), "%016llx", asset_file.name);
		wrenSetSlotString(vm, 1, hex);

		// Set the extension
		snprintf(hex, sizeof(hex), "%016llx", asset_file.ext);
		wrenSetSlotString(vm, 2, hex);

		// Invoke it - if it fails the game is very likely going to crash anyway, so make it descriptive now
		WrenInterpretResult result = wrenCall(vm, callHandle);
		if (result == WREN_RESULT_COMPILE_ERROR || result == WREN_RESULT_RUNTIME_ERROR)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1, "Wren asset load failed for %016llx.%016llx: compile or runtime error!",
			         asset_file.name, asset_file.ext);
			PD2HOOK_LOG_ERROR(buff);

#ifdef _WIN32
			MessageBox(nullptr, "Failed to load Wren-based asset - see the log for details", "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}

		// Get the wrapper, and make sure it's valid
		auto* ff = (DBForeignFile*)wrenGetSlotForeign(vm, 0);
		if (!ff || ff->magic != DBForeignFile::MAGIC_COOKIE)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1,
			         "Wren load_file function return invalid class or null - for asset %016llx.%016llx ptr %p",
			         asset_file.name, asset_file.ext, ff);
			PD2HOOK_LOG_ERROR(buff);
#ifdef _WIN32
			MessageBox(nullptr, "Failed to load Wren-based asset - see the log for details", "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}

		// Now load it's value
		if (ff->filename)
		{
			load_file(*ff->filename);
		}
		else if (!ff->asset.is_empty())
		{
			load_bundle_item(ff->asset);
		}
		else if (ff->stringLiteral)
		{
			auto* ds = new BLTStringDataStore(*ff->stringLiteral);
			*out_datastore = ds;
			*out_len = ds->size();
		}
		else
		{
			// Should never happen
			PD2HOOK_LOG_ERROR("No output contents set for DBForeignFile");
#ifdef _WIN32
			MessageBox(nullptr, "Failed to load Wren-based asset - see the log for details", "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}
	}
	else
	{
		// File is disabled, use the regular version of the asset
		return false;
	}

	return true;
}

//////////////////////////////////////
//// Foreign file implementation /////
//////////////////////////////////////

void DBForeignFile::ofFile(WrenVM* vm)
{
	std::unique_ptr<std::string> filename = std::make_unique<std::string>(wrenGetSlotString(vm, 1));
	create(vm)->filename = std::move(filename);
}

void DBForeignFile::ofAsset(WrenVM* vm)
{
	blt::idstring name = parseHash(wrenGetSlotString(vm, 1));
	blt::idstring ext = parseHash(wrenGetSlotString(vm, 2));
	create(vm)->asset = blt::idfile(name, ext);
}

void DBForeignFile::fromString(WrenVM* vm)
{
	std::unique_ptr<std::string> contents = std::make_unique<std::string>(wrenGetSlotString(vm, 1));
	create(vm)->stringLiteral = std::move(contents);
}

DBForeignFile* DBForeignFile::create(WrenVM* vm)
{
	wrenGetVariable(vm, MODULE, "DBForeignFile", 0);
	auto* obj = (DBForeignFile*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(DBForeignFile));
	obj->magic = DBForeignFile::MAGIC_COOKIE;
	return obj;
}

void DBForeignFile::finalise(void* this_data)
{
	auto* file = (DBForeignFile*)this_data;
	file->filename.reset();
	file->stringLiteral.reset();
}

//////////////////////////////////////
///// Asset hook implementation //////
//////////////////////////////////////

static DBTargetFile* get_this(WrenVM* vm)
{
	auto* it = (DBAssetHook*)wrenGetSlotForeign(vm, 0);
	return it->file.get();
}

void DBAssetHook::getFallback(WrenVM* vm)
{
	auto* it = get_this(vm);
	wrenEnsureSlots(vm, 1);
	wrenSetSlotBool(vm, 0, it->fallback);
}

void DBAssetHook::setFallback(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->fallback = wrenGetSlotBool(vm, 1);
}

void DBAssetHook::getMode(WrenVM* vm)
{
	auto* it = get_this(vm);
	const char* str;

	// See the documentation on the Wren version of this method for the definitive list of strings
	if (it->plain_file)
		str = "plain_file";
	else if (!it->direct_bundle.is_empty())
		str = "direct_bundle";
	else if (it->wren_loader_obj)
		str = "wren_loader";
	else
		str = "disabled";

	wrenSetSlotString(vm, 0, str);
}

void DBAssetHook::isEnabled(WrenVM* vm)
{
	auto* it = get_this(vm);
	wrenSetSlotBool(vm, 0, it->plain_file->empty() && it->direct_bundle.is_empty() && !it->wren_loader_obj);
}

void DBAssetHook::disable(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->clear_sources();
}

void DBAssetHook::getPlainFile(WrenVM* vm)
{
	auto* it = get_this(vm);
	if (it->plain_file)
		wrenSetSlotString(vm, 0, it->plain_file->c_str());
	else
		wrenSetSlotNull(vm, 0);
}

void DBAssetHook::setPlainFile(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->clear_sources();
	it->plain_file = std::string(wrenGetSlotString(vm, 1));
}

void DBAssetHook::getDirectBundle(WrenVM* vm)
{
	auto* it = get_this(vm);
	if (it->direct_bundle.is_empty())
	{
		wrenSetSlotNull(vm, 0);
		return;
	}

	char buff[128];
	memset(buff, 0, sizeof(buff));
	snprintf(buff, sizeof(buff) - 1, "@%016llx.@%016llx", it->direct_bundle.name, it->direct_bundle.ext);
	wrenSetSlotString(vm, 0, buff);
}

void DBAssetHook::setDirectBundle(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->clear_sources();

	blt::idstring name = parseHash(wrenGetSlotString(vm, 1));
	blt::idstring ext = parseHash(wrenGetSlotString(vm, 2));

	it->direct_bundle = blt::idfile(name, ext);
}

void DBAssetHook::getWrenLoader(WrenVM* vm)
{
	auto* it = get_this(vm);
	if (it->wren_loader_obj == nullptr)
	{
		wrenSetSlotNull(vm, 0);
		return;
	}

	wrenSetSlotHandle(vm, 0, it->wren_loader_obj);
}

void DBAssetHook::setWrenLoader(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->clear_sources();

	it->wren_loader_obj = wrenGetSlotHandle(vm, 1);
}

void DBAssetHook::finalise(void* this_data)
{
	auto* this_ptr = (DBAssetHook*)this_data;
	this_ptr->file.reset();
}
