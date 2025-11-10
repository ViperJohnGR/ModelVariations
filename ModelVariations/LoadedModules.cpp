#include "LoadedModules.hpp"

#include "FuncUtil.hpp"
#include "Log.hpp"

#include <map>

std::map<loadedModNames, bool> loadedMods;
std::vector<std::pair<std::string, MODULEINFO>> loadedModules;
std::string modDirectory;


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

std::pair<std::string, MODULEINFO> LoadedModules::GetModule(const std::string &name, bool exactMatch)
{
    for (auto& i : loadedModules)
        if (exactMatch)
        {
            auto filename = getFilenameFromPath(i.first);
            if (_stricmp(filename.c_str(), name.c_str()) == 0)
                return i;
        }
        else if (strcasestr(i.first, name))
            return i;

    return {};
}

std::string LoadedModules::GetSelfDirectory()
{
    if (loadedModules.empty())
        LoadedModules::Refresh();

    if (modDirectory.empty())
    {
        std::string fullPath = LoadedModules::GetModule(MOD_NAME).first;

        size_t pos = fullPath.find_last_of("/\\");
        modDirectory = (pos != std::string::npos) ? fullPath.substr(0, pos) : "";
    }

    return modDirectory;
}

bool LoadedModules::IsModLoaded(loadedModNames mod)
{
    return loadedMods[mod];
}

void LoadedModules::Log()
{
    for (auto& i : loadedModules)
        Log::Write("0x%08X 0x%08X %s\n", i.second.lpBaseOfDll, i.second.SizeOfImage, i.first.c_str());
}

void LoadedModules::Refresh()
{
    loadedModules.clear();

    HMODULE modules[500] = {};
    HANDLE hProcess = GetCurrentProcess();
    DWORD cbNeeded = 0;

    if (EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded) && cbNeeded <= sizeof(modules))
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
                assert(_stricmp("ModelVariations.asi", getFilenameFromPath(szModName).c_str()) != 0);
#endif
                MODULEINFO mInfo;
                if (modules[i])
                {
                    GetModuleInformation(hProcess, modules[i], &mInfo, sizeof(MODULEINFO));
                    loadedModules.push_back({ szModName, mInfo });
                }
            }
        }

    std::sort(loadedModules.begin(), loadedModules.end(), [](std::pair<std::string, MODULEINFO> a, std::pair<std::string, MODULEINFO> b)
    {
        return a.second.lpBaseOfDll < b.second.lpBaseOfDll;
    });
}

