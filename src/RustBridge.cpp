#include "RustBridge.hpp"

#include <windows.h>

#include <iostream>

namespace {
constexpr const char *kRustLibraryName = "deepcool_rust_bridge.dll";
constexpr const char *kRustSymbolName = "deepcool_get_hid_data";

typedef const char *(__cdecl *GetHidDataFunc)();
}

void PrintRustHidData()
{
	HMODULE rustLibrary = LoadLibraryA(kRustLibraryName);
	if (rustLibrary == nullptr)
	{
		std::cout << "[Rust HID] Unable to load " << kRustLibraryName
				  << ". Build the Rust bridge DLL to enable HID output." << std::endl;
		return;
	}

	auto getHidData = reinterpret_cast<GetHidDataFunc>(
		GetProcAddress(rustLibrary, kRustSymbolName));
	if (getHidData == nullptr)
	{
		std::cout << "[Rust HID] Unable to locate symbol " << kRustSymbolName << "."
				  << std::endl;
		FreeLibrary(rustLibrary);
		return;
	}

	const char *hidData = getHidData();
	if (hidData != nullptr && hidData[0] != '\0')
	{
		std::cout << "[Rust HID] " << hidData << std::endl;
	}
	else
	{
		std::cout << "[Rust HID] (no data returned)" << std::endl;
	}

	FreeLibrary(rustLibrary);
}
