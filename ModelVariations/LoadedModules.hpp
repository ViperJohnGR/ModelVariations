#pragma once

#include <string>
#include <vector>

#include <Windows.h>
#include <psapi.h>

enum loadedModNames
{
	MOD_FLA,
	MOD_OLA,
	MOD_WLE
};


class LoadedModules
{
public:
	static std::pair<std::string, MODULEINFO> GetModuleAtAddress(std::uintptr_t address);
	static std::pair<std::string, MODULEINFO> GetModule(std::string_view name, bool exactMatch = true);

	static bool IsModLoaded(loadedModNames mod);

	static void Log();

	static void Refresh();
};
