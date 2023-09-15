#include "LoadedModules.hpp"

#include "FuncUtil.hpp"
#include "Log.hpp"

#include <map>

std::map<loadedModNames, bool> loadedMods;
std::vector<std::pair<std::string, MODULEINFO>> loadedModules;


std::pair<std::string, MODULEINFO> LoadedModules::GetModuleAtAddress(std::uintptr_t address)
{
    if (address)
        for (auto& it : loadedModules)
        {
            uint32_t base = reinterpret_cast<uint32_t>(it.second.lpBaseOfDll);
            if (address >= base && address < base + it.second.SizeOfImage)
                return it;
        }

    return {};
}

std::pair<std::string, MODULEINFO> LoadedModules::GetModule(std::string_view name, bool exactMatch)
{
    for (auto& i : loadedModules)
        if (exactMatch)
        {
            if (_stricmp(i.first.c_str(), name.data()))
                return i;
        }
        else if (strcasestr(i.first, name.data()))
            return i;

    return {};
}

bool LoadedModules::IsModLoaded(loadedModNames mod)
{
    return loadedMods[mod];
}

void LoadedModules::Log()
{
    for (auto& i : loadedModules)
        Log::Write("0x%08X %s\n", i.second.lpBaseOfDll, i.first.c_str());
}

void LoadedModules::Refresh()
{
    loadedModules.clear();

    HMODULE modules[500] = {};
    HANDLE hProcess = GetCurrentProcess();
    DWORD cbNeeded = 0;

    if (EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded))
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
        {
            char szModName[MAX_PATH] = {};
            if (GetModuleFileNameEx(hProcess, modules[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
            {
                if (strcasestr(szModName, "III.VC.SA.LimitAdjuster"))
                    loadedMods[MOD_OLA] = true;
                else if (strcasestr(szModName, "fastman92limitAdjuster"))
                    loadedMods[MOD_FLA] = true;
                else if (strcasestr(szModName, "WantedLevelEditor"))
                    loadedMods[MOD_WLE] = true;

#ifdef _DEBUG
                assert(!_stricmp("ModelVariations.asi", getFilenameFromPath(szModName).c_str()));
#endif
                MODULEINFO mInfo;
                GetModuleInformation(hProcess, modules[i], &mInfo, sizeof(MODULEINFO));
                loadedModules.push_back({ szModName, mInfo });
                std::sort(loadedModules.begin(), loadedModules.end(), [](std::pair<std::string, MODULEINFO> a, std::pair<std::string, MODULEINFO> b)
                {
                    return a.second.lpBaseOfDll < b.second.lpBaseOfDll;
                });
            }
        }
}

