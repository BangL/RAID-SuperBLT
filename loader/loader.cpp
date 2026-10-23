#define WIN32_LEAN_AND_MEAN 1
#include "InitState.h"
#include "util/util.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <windows.h>

#include "loader_asm.gen.h"

using namespace raidhook;

// Used by the generated assembly file, this contains pointers to every function we have to proxy
extern "C"
{
ProxyFunctionTable SBLT_PROXY_STRUCT_PTR = CreateInitialProxyFunctionTable();
void SBLT_PROXY_LOADER_FN_CXX(uint64_t functionAndDllId);
}

static std::mutex dllLoadingMutex;
static bool isWsockLoaded = false;
static bool isIphlpapiLoaded = false;

// Whether this DLL set up SuperBLT, as opposed to just proxying the networking functions
static bool isActive = false;

// Both builds are the same code, only the file name differs. WSOCK32.dll is the one that steps aside if the other
// build is installed as well (as it always did), since the game imports both DLLs and SuperBLT must only run once.
static bool IsThisWsock32(HINSTANCE hInst)
{
	std::string name = std::filesystem::path(Util::GetModuleFileNameCxx(hInst)).filename().string();
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
	return name == "wsock32.dll";
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hInst);

		// Make sure there aren't two copies of SBLT installed
		if (IsThisWsock32(hInst) && Util::GetFileType("IPHLPAPI.dll") == Util::FileType_File)
		{
			MessageBoxA(nullptr,
			            "You have both SuperBLT DLLs installed - IPHLPAPI.dll and \nWSOCK32.dll. "
			            "Please delete one (preferrably, delete IPHLPAPI.dll). Currently, the WSOCK32.dll loader will\n"
			            "be deactivated until IPHLPAPI.dll is removed.",
			            "Both SuperBLT DLLs installed!", MB_OK);
			return TRUE;
		}

		isActive = true;
		InitiateStates();
	}
	if (reason == DLL_PROCESS_DETACH)
	{
		if (isActive)
			DestroyStates();

		// It would in principal be nice to unload the networking library, but MSDN forbids FreeLibrary from DllMain.
		// (and there's no real reason to free it anyway)
	}

	return TRUE;
}

void SBLT_PROXY_LOADER_FN_CXX(uint64_t functionAndDllId)
{
	[[maybe_unused]] uint32_t functionId = functionAndDllId & 0xFFFFFFFF;
	uint32_t dllId = functionAndDllId >> 32;

	// Make sure that if multiple threads call the function concurrently, we still only load the DLL once
	std::lock_guard guard(dllLoadingMutex);

	char bufd[200];
	GetSystemDirectory(bufd, 200);
	std::string dllName = dllId == DLL_ID_WSOCK32 ? "WSOCK32.dll" : "IPHLPAPI.dll";
	std::string dllPath = std::string(bufd) + "\\" + dllName;

	HMODULE hL = LoadLibrary(dllPath.c_str());
	if (!hL)
	{
		std::string message = "Failed to load system " + dllName;
		MessageBoxA(nullptr, message.c_str(), "SuperBLT Loader", MB_OK);
		ExitProcess(1);
	}

	// Load the addresses for all the functions
	if (dllId == DLL_ID_WSOCK32)
	{
		LoadDllFunctionsWSOCK32(&SBLT_PROXY_STRUCT_PTR, hL);
		isWsockLoaded = true;
	}
	else
	{
		LoadDllFunctionsIPHLPAPI(&SBLT_PROXY_STRUCT_PTR, hL);
		isIphlpapiLoaded = true;
	}

	// std::string message = "A SuperBLT proxied function was called without initialisation. Please report this.\n";
	// message += "Function: " + std::string(FUNCTION_NAMES[functionId]);
	// MessageBoxA(nullptr, message.c_str(), "Bad Proxy Function", MB_OK);
	// abort();
}
