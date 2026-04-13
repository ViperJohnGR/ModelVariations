#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "SA.hpp"

#include "Peds.hpp"
#include "PedWeapons.hpp"
#include "Vehicles.hpp"

#include <plugin.h>
#include <CEntryExit.h>
#include <CEntryExitManager.h>
#include <CLoadedCarGroup.h>
#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CStreaming.h>
#include <CTheZones.h>
#include <CVector.h>

#include <chrono>
#include <map>
#include <set>
#include <thread>

#include <urlmon.h>

#pragma comment (lib, "bcrypt.lib")
#pragma comment (lib, "urlmon.lib")


#define MOD_VERSION "10.6"
//Using Plugin-SDK: 34ba198

struct jumpInfo {
    std::uintptr_t address;
    std::uintptr_t destination;
    unsigned char type;
};


std::unordered_map<std::string, std::vector<std::string>> areas;
std::unordered_map<std::string, std::vector<CZone*>> presetAllZones;


std::set<std::uintptr_t> callChecks;
std::set<unsigned short> referenceCountModels;
std::set<unsigned short> addedIDsInGroups;

std::string exePath(256, 0);

std::unordered_map<unsigned short, std::string> addedIDs;
std::unordered_map<unsigned short, std::string> modelNames;
int maxPedID = 0;

static const char* dataFileName = "ModelVariations.ini";
DataReader iniSettings(dataFileName);

std::chrono::steady_clock::time_point lastTime;
std::chrono::steady_clock::time_point loadTime;
std::chrono::milliseconds totalTimeSinceLoad(0);
std::chrono::milliseconds gameplayTimeSinceLoad(0);

int secondsSinceLastModCheck = -1;

int drawDebugText = false;

char currentZone[9] = {};
unsigned int currentWanted = 0;

bool transitioning = false;

bool jumpsLogged = false;

bool keyDown = false;
bool queuedReload = false;

std::atomic<bool> checkingForUpdates(false);
std::atomic<bool> newVersionFound(false);

int flaMaxID = -1;

//INI Options
bool enableLog = false;
bool logJumps = false;
bool enablePeds = false;
bool enableSpecialPeds = false;
bool enableVehicles = false;
bool enablePedWeapons = false;
bool forceEnableGlobal = false;
bool loadSettingsImmediately = false;
bool enableStreamingFix = false;
int loadStage = 1;
int trackReferenceCounts = -1;
int disableKey = 0;
int reloadKey = 0;
int debugKey = 0;

std::set<std::uintptr_t> forceEnable;

bool modInitialized = false;

std::jthread updatesThread;

void checkForUpdate()
{
    checkingForUpdates = true;

    struct Guard {
        std::atomic<bool>& flag;
        ~Guard() { flag.store(false); }
    } guard{ checkingForUpdates };

    IStream* stream;

    if (URLOpenBlockingStream(0, "http://api.github.com/repos/ViperJohnGR/ModelVariations/tags", &stream, 0, 0) != S_OK)
    {
        Log::Write("Check for updates failed. Cannot open connection.\n");
        return;
    }

    std::string str(51, 0);
    if (stream->Read(&str[0], 50, NULL) != S_OK)
    {
        Log::Write("Check for updates failed.\n");
        stream->Release();
        return;
    }

    stream->Release();

    if (auto start = str.find("\"v"); start != std::string::npos)
        if (auto end = str.find_first_of('"', start+1); end != std::string::npos && end > start + 2)
        {
            auto newV = splitString(str.substr(start+2, end - start - 2), '.');
            auto oldV = splitString(MOD_VERSION, '.');

            // Make both the same length by padding with "0"
            if (newV.size() < oldV.size()) newV.resize(oldV.size(), "0");
            else if (oldV.size() < newV.size()) oldV.resize(newV.size(), "0");

            for (size_t i = 0; i < newV.size(); i++)
            {
                int n1 = fast_atoi(newV[i].c_str());
                int n2 = fast_atoi(oldV[i].c_str());

                if (n1 == INT_MAX || n2 == INT_MAX) 
                    return;
                if (n1 > n2)
                {
                    newVersionFound = true;
                    return;
                }
                else if (n1 < n2) 
                    return;
            }

            return; // equal
        }

    Log::Write("Check for updates failed. Invalid version string.\n");
    return;
}

std::string getDatetime(bool printDate, bool printTime, bool printMs)
{
    SYSTEMTIME systime;
    GetSystemTime(&systime);
    char str[255] = {};
    int i = 0;

    if (printDate)
    {
        i += snprintf(str, 254, "%d/%d/%d", systime.wDay, systime.wMonth, systime.wYear);
        if (printTime)
            i += snprintf(str + i, 254U - i, " ");
    }

    if (printTime)
    {
        i += snprintf(str + i, 254U - i, "%02d:%02d:%02d", systime.wHour, systime.wMinute, systime.wSecond);

        if (printMs)
            snprintf(str + i, 254U - i, ".%03d", systime.wMilliseconds);
    }

    return str;
}

bool loadPESection(const char* filePath, int section, std::vector<unsigned char>& buffer, unsigned int* size)
{
    HANDLE hFile;
    HANDLE hFileMapping;
    LPVOID mapView;
    PIMAGE_DOS_HEADER dosHeader;
    PIMAGE_NT_HEADERS ntHeaders;
    PIMAGE_SECTION_HEADER sectionHeader;

    auto functionError = [&](const char* msg, int errorType)
    {
        Log::Write("Error logging jumps. %s.\n", msg);
        switch (errorType)
        {
            case 3:
                UnmapViewOfFile(mapView);
                [[fallthrough]];
            case 2:
                CloseHandle(hFileMapping);
                [[fallthrough]];
            case 1:
                CloseHandle(hFile);
        }

        return false;
    };

    hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return functionError("Failed to open file", 0);

    // Create a file mapping
    hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hFileMapping == NULL)
        return functionError("Failed to create file mapping", 1);

    // Map the PE file into memory
    mapView = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    if (mapView == NULL)
        return functionError("Failed to map view of file", 2);

    // Get the DOS header
    dosHeader = (PIMAGE_DOS_HEADER)mapView;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return functionError("Invalid DOS signature", 3);

    // Get the NT headers
    ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)mapView + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return functionError("Invalid NT signature", 3);

    // Get the section headers
    sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i)
    {
        if (i == section)
        {
            *size = sectionHeader->SizeOfRawData;
            if (buffer.size() < sectionHeader->SizeOfRawData)
                buffer.resize(sectionHeader->SizeOfRawData);

            memcpy(&buffer[0], (BYTE*)mapView + sectionHeader->PointerToRawData, *size);
            UnmapViewOfFile(mapView);
            CloseHandle(hFileMapping);
            CloseHandle(hFile);

            return true;
        }
        sectionHeader++;
    }

    // Clean up resources
    UnmapViewOfFile(mapView);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);

    return false;
}

void logVariationsChange(const char* msg)
{
    auto player = FindPlayerPed();
    auto pPos = FindPlayerCoors(-1);
    auto wanted = FindPlayerWanted(-1);
    CZone* zInfo = NULL;
    CTheZones::GetZoneInfo(&pPos, &zInfo);

    if (zInfo == NULL || wanted == NULL)
        return;

    Log::Write("\n%s (%s)\n", msg, getDatetime(false, true, true).c_str());
    Log::Write("Streaming Memory usage: %u/%u MB  Total Memory usage: %u MB\n", CStreaming__ms_memoryUsed/1024/1024, CStreaming__ms_memoryAvailable/1024/1024, getMemoryUsage()/1024/1024);
    Log::Write("Updating variations. pPos = {%f, %f, %f}\n", pPos.x, pPos.y, pPos.z);
    Log::Write("currentWanted = %u wanted->m_nWantedLevel = %u\n", currentWanted, wanted->m_nWantedLevel);
    Log::Write("currentZone = %.8s zInfo->m_szLabel = %.8s\n", currentZone, zInfo->m_szLabel);

    if (player->m_pEnex)
        Log::Write("player->m_pEnex = %.8s\n", player->m_pEnex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////   DATA   /////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void clearEverything()
{
    iniSettings.data.clear();

    PedVariations::ClearData();
    PedWeaponVariations::ClearData();
    VehicleVariations::ClearData();
}

void loadIniData()
{
    iniSettings.Load(dataFileName);

    enablePeds = iniSettings.ReadBoolean("Settings", "EnablePeds", false);
    enableVehicles = iniSettings.ReadBoolean("Settings", "EnableVehicles", false);
    enablePedWeapons = iniSettings.ReadBoolean("Settings", "EnablePedWeapons", false);

    if (enablePeds)
    {
        if (!modInitialized)
        {
            enableSpecialPeds = iniSettings.ReadBoolean("Settings", "EnableSpecialPeds", false);

            int extraObjectsDirLimit = iniSettings.ReadInteger("Limits", "ExtraObjectsDirLimit", -1);

            //Extra objects directory
            if (*reinterpret_cast<uint32_t*>(0x5B8DE0) == 550 && extraObjectsDirLimit > 0)
                WriteMemory<uint32_t>(0x5B8DE0, extraObjectsDirLimit);
            else
                Log::Write("Extra objects directory limit was not increased.\n");
        }

        if (enableSpecialPeds && !LoadedModules::IsModLoaded(MOD_FLA) && !LoadedModules::IsModLoaded(MOD_OLA))
        {
            enableSpecialPeds = false;
            if (!modInitialized)
                MessageBox(NULL, "No limit adjuster found! EnableSpecialPeds will be disabled.", "Model Variations", MB_ICONWARNING);
        }

        PedVariations::LoadData();
    }

    if (!enablePeds || !enableSpecialPeds)
        maxPedID = -1;

    if (enablePedWeapons)
        PedWeaponVariations::LoadData();

    if (enableVehicles)
        VehicleVariations::LoadData();

    static std::once_flag flag;
    if (enableSpecialPeds && CModelInfo::GetModelInfo(0))
    {
        std::call_once(flag, [] 
        {
            PedVariations::ClearData();
            PedVariations::LoadData();
        });
    }
}

void updateVariations()
{
    //zInfo->m_szTextKey = BLUEB | zInfo->m_szLabel = BLUEB1

    auto player = FindPlayerPed();
            
    if (Log::Write("CStreaming::ms_pedsLoaded: "))
    {
        for (int i = 0; i < CStreaming__ms_numPedsLoaded; i++)
            Log::Write("%d ", CStreaming__ms_pedsLoaded[i]);

        Log::Write("\nCStreaming::ms_vehiclesLoaded: ");
        for (unsigned i = 0; i < CStreaming__ms_vehiclesLoaded->CountMembers(); i++)
            Log::Write("%d ", CStreaming__ms_vehiclesLoaded->m_members[i]);

        Log::Write("\nCPopulation::m_AppropriateLoadedCars: ");
        for (unsigned i = 0; i < CPopulation__m_AppropriateLoadedCars->CountMembers(); i++)
            Log::Write("%d ", CPopulation__m_AppropriateLoadedCars->m_members[i]);

        if (player->bInVehicle)
            Log::Write("\nPlayer is in vehicle 0x%08X with model id %u\n", player->m_pVehicle, player->m_pVehicle->m_nModelIndex);

        Log::Write("\n\n");
    }

    if (enablePeds)
        PedVariations::UpdateVariations();

    if (enableVehicles)
        VehicleVariations::UpdateVariations();


    if (enablePeds)
        PedVariations::LogCurrentVariations();

    if (enableVehicles)
    {
        Log::Write("\n");
        VehicleVariations::LogCurrentVariations();
    }

    Log::Write("\n\n");
    
}


///////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////   INITIALIZE   //////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void initialize()
{
    LoadedModules::Refresh();

    auto flaModule = LoadedModules::GetModule("fastman92limitAdjuster", false);
    if (!flaModule.first.empty())
    {
        std::string flaIniPath = flaModule.first;
        flaIniPath.replace(flaIniPath.find_last_of("\\/"), std::string::npos, "\\fastman92limitAdjuster_GTASA.ini");

        DataReader flaIni(flaIniPath.c_str());
        flaMaxID = flaIni.ReadInteger("ID LIMITS", "Count of killable model IDs", -1);
        if (maxPedID == 0 && flaMaxID > -1)
            maxPedID = flaMaxID;

        Log::Write("\n");
        Log::Write("%s\n", printFilenameWithBorder(flaIniPath.substr(flaIniPath.find_last_of("/\\") + 1), '#').c_str());
        for (auto &i : flaIni.data)
        {
            Log::Write("[%s]\n", i.first.c_str());
            for (auto& j : i.second)
                Log::Write("%s = %s\n\n", j.first.c_str(), j.second.c_str());
        }
    }

    Log::Write("\n");

    auto olaModule = LoadedModules::GetModule("III.VC.SA.LimitAdjuster.asi");
    if (!olaModule.first.empty())
    {
        std::string olaIniPath = olaModule.first;
        olaIniPath.replace(olaIniPath.find_last_of("\\/"), std::string::npos, "\\III.VC.SA.LimitAdjuster.ini");

        DataReader olaIni(olaIniPath.c_str());
        auto olaStr = olaIni.ReadString("SALIMITS", "PedModels", "");
        if (!olaStr.empty())
            Log::Write("PedModels limit in OLA is %s\n\n", olaStr.c_str());
    }

    loadIniData();

    if (enablePeds)
    {
        if (maxPedID == 0)
            maxPedID = iniSettings.ReadInteger("Limits", "MaxModelID", -1);
        Log::Write("Installing ped hooks...\n");
        PedVariations::InstallHooks(enableSpecialPeds);
        Log::Write("Ped hooks installed.\n");
    }

    if (enablePedWeapons)
    {
        Log::Write("Installing ped weapon hooks...\n");
        PedWeaponVariations::InstallHooks();
        Log::Write("Ped weapon hooks installed.\n");
    }

    if (enableVehicles)
    {
        Log::Write("Installing vehicle hooks...\n");
        VehicleVariations::InstallHooks();
        Log::Write("Vehicle hooks installed.\n");
    }

    Log::Write("\nLoaded modules:\n");

    LoadedModules::Log();

    Log::Write("\n");

    modInitialized = true;
}

void refreshOnGameRestart()
{
    lastTime = std::chrono::steady_clock::time_point{};
    loadTime = std::chrono::steady_clock::now();
    gameplayTimeSinceLoad = std::chrono::milliseconds(0);

    auto startTime = std::chrono::steady_clock::now();

    if (!modInitialized && loadStage == 2)
        initialize();

    if (!modInitialized)
        MessageBox(NULL, "Could not initialize mod.", "Model Variations", MB_ICONWARNING);
        

    Log::Write("-- Restarting (%s) --\n", getDatetime(false, true, true).c_str());

    clearEverything();
    PedVariations::ProcessDrugDealers(true);
    LoadedModules::Refresh();

    *reinterpret_cast<uint64_t*>(currentZone) = 0;

    loadIniData();

    if (enablePeds)
        PedVariations::LogVariations();

    if (enableVehicles)
    {
        Log::Write("\n");
        VehicleVariations::LogVariations();
    }

    Log::Write("\n\n");

    if (!checkingForUpdates)
    {
        if (updatesThread.joinable())
            updatesThread.join();
        updatesThread = std::jthread(checkForUpdate);
    }

    int finalTime = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    if (finalTime < 1000)
        Log::Write("Time spent loading: %dms.\n", finalTime);
    else
        Log::Write("Time spent loading: %gs.\n", finalTime / 1000.0);

    Log::Write("-- Restart Finished (%s) --\n", getDatetime(false, true, true).c_str());

    static std::once_flag flag;
    std::call_once(flag, []
    {
        if (!addedIDs.empty() && Log::Write("Added IDs:\n"))
        {
            for (auto &it : addedIDs)
                Log::Write("%u %s\n", it.first, it.second.c_str());
            Log::Write("\n");
        }
    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template <std::uintptr_t address>
bool __cdecl AddToLoadedVehiclesListHooked(int model)
{
    if (model < 612 || addedIDsInGroups.contains((unsigned short)model))
        return callOriginalAndReturn<bool, address>(model);

    return 1;
}

template <std::uintptr_t address>
char __fastcall InteriorManager_c__UpdateHooked(void* _this)
{
    if (transitioning == false)
    {
        logVariationsChange("Interior changed");
        updateVariations();
    }
    return callMethodOriginalAndReturn<char, address>(_this);
}

template <std::uintptr_t address>
void __cdecl RetryLoadFileHooked(int streamNum)
{
    Log::Write("RetryLoadFile called for the following IDs in channel %d: ", streamNum);

    for (auto i : CStreaming::ms_channel[streamNum].modelIds)
        Log::Write("%d ", i);
    Log::Write("\n");

    callOriginal<address>(streamNum);
}

template <std::uintptr_t address>
char __fastcall TransitionFinishedHooked(CEntryExit* _this, void*, CPed* ped)
{
    auto retVal = callMethodOriginalAndReturn<char, address>(_this, ped);    

    if (FindPlayerPed()->m_nAreaCode == 0 || CEntryExitManager::ms_exitEnterState != 1)
        return retVal;

    if (_this && _this->m_pLink && !transitioning)
    {
        transitioning = true;
        auto exitPos = _this->m_pLink->m_vecExitPos;

        CZone* zInfo = NULL;
        CTheZones::GetZoneInfo(&exitPos, &zInfo);

        if (zInfo)
        {
            logVariationsChange("Exiting interior");

            *reinterpret_cast<uint64_t*>(currentZone) = *reinterpret_cast<uint64_t*>(zInfo->m_szLabel);
            updateVariations();
        }
    }

    return retVal;
}

template <std::uintptr_t address>
void CPopCycle__DisplayHooked()
{
    if (drawDebugText > 1 && enableVehicles)
        VehicleVariations::DrawDebugInfo();
    if ((drawDebugText == 1 || drawDebugText == 3) && enablePeds)
        PedVariations::DrawDebugInfo();

    callOriginal<address>();
}

//Model names
template <std::uintptr_t address>
int __cdecl FileLoaderLoadObject(const char* a1)
{
    if (a1)
    {
        int modelId = -1;
        char modelName[50] = {};
        if (sscanf(a1, "%d %s", &modelId, modelName) == 2 && modelId > 0 && strnlen(modelName, 49) > 0)
            modelNames[(unsigned short)modelId] = modelName;
    }

    return callOriginalAndReturn<unsigned int, address>(a1);
}

template <std::uintptr_t address>
void __cdecl CGame__ProcessHooked()
{
    if (!FrontEndMenuManager->m_bMenuActive)
    {
        auto now = std::chrono::steady_clock::now();
        if (lastTime.time_since_epoch().count() > 0)
            gameplayTimeSinceLoad += std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);
        lastTime = now;
    }
    else
        lastTime = std::chrono::steady_clock::time_point{};

    totalTimeSinceLoad = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - loadTime);

    callOriginal<address>();

    if (!jumpsLogged && logJumps && Log::Write("\nLogging JMP hooks...\n"))
    {
        std::vector<unsigned char> buffer;

        const std::vector<int> sections = isGameHOODLUM() ? std::vector<int> { 0, 1, 7, 8, 9, 10 } : std::vector<int>{ 0, 1 };
        const std::vector<std::uintptr_t> validRanges = isGameHOODLUM() ? std::vector<std::uintptr_t> { 0x401000, 0x857000, 0xCB1000, 0x12FB000, 0x1301000, 0x1556000 } : std::vector<std::uintptr_t>{ 0x401000, 0x857000 };
        std::unordered_map<std::string, std::vector<jumpInfo>> jumpsMap;

        auto gta_saModule = LoadedModules::GetExeModule();
        std::uintptr_t gta_saEndAddress = ((std::uintptr_t)gta_saModule.second.lpBaseOfDll + gta_saModule.second.SizeOfImage);
        unsigned int secCount = 0;

        for (auto rangeStart : validRanges)
        {
            unsigned int sectionSize;
            if (loadPESection(exePath.c_str(), sections[secCount++], buffer, &sectionSize))
                for (unsigned int i = rangeStart; i < rangeStart + sectionSize; i++)
                {
                    auto currentByte = *reinterpret_cast<unsigned char*>(i);
                    if (currentByte != buffer[i - rangeStart])
                    {
                        auto destination = injector::GetBranchDestination(i).as_int();
                        if (destination > gta_saEndAddress)
                        {
                            auto moduleInfo = LoadedModules::GetModuleAtAddress(destination);
                            std::string moduleName = moduleInfo.first.substr(moduleInfo.first.find_last_of("/\\") + 1);

                            if (!strcasestr(moduleInfo.first, "Windows") && _stricmp(moduleName.c_str(), MOD_NAME) != 0)
                            {
                                if (moduleName.empty())
                                    jumpsMap["unknown"].push_back({ i, destination, currentByte });
                                else
                                    jumpsMap[moduleName].push_back({ i, destination, currentByte });
                            }
                            i += 3;
                        }
                    }
                }
        }
        for (auto& i : jumpsMap)
        {
            Log::Write("\n%s:\n", i.first.c_str());
            for (auto& j : i.second)
            {
                Log::Write("0x%08X 0x%08X %X\n", j.address, j.destination, j.type);
            }
        }
        Log::Write("\n");

        jumpsLogged = true;
    }

    if (trackReferenceCounts > 0 && CModelInfo::GetModelInfo(0))
        for (int i = 7; i < std::max<int>(flaMaxID, 20000); i++)
        {
            auto mInfo = CModelInfo::GetModelInfo(i);
            if (mInfo && mInfo->m_nRefCount > trackReferenceCounts && !referenceCountModels.contains(static_cast<unsigned short>(i)))
            {
                auto modelType = (mInfo->GetModelType() == MODEL_INFO_VEHICLE) ? "(Vehicle) " : ((mInfo->GetModelType() == MODEL_INFO_PED) ? "(Ped) " : "");

                char warning_string[256] = {};
                snprintf(warning_string, 255, "WARNING: model %d %shas a reference count of %d\n", i, modelType, mInfo->m_nRefCount);
                Log::Write(warning_string);
#ifdef _DEBUG
                MessageBox(NULL, warning_string, "Model Variations", MB_ICONWARNING);
#endif
                referenceCountModels.insert(static_cast<unsigned short>(i));
            }
        }

    if (newVersionFound && gameplayTimeSinceLoad.count() > 9999)
    {
        printMessage("~y~Model Variations~s~: Update available.", 4000);
        newVersionFound = false;
    }

    if (disableKey > 0 && (GetKeyState(disableKey) & 0x8000) != 0)
    {
        if (!keyDown)
        {
            keyDown = true;
            printMessage("~y~Model Variations~s~: Mod disabled.", 2000);
            Log::Write("Disabling mod... ");
            clearEverything();
            Log::Write("OK\n");
        }
    }
    else if (reloadKey > 0 && (GetKeyState(reloadKey) & 0x8000) != 0)
    {
        if (!keyDown)
        {
            keyDown = true;
            Log::Write("Reloading settings...\n");
            clearEverything();
            printMessage("~y~Model Variations~s~: Reloading settings...", 10000);
            loadIniData();

            *reinterpret_cast<uint64_t*>(currentZone) = 0;
            printMessage("~y~Model Variations~s~: Settings reloaded.", 2000);
        }
    }
    else if (debugKey > 0 && (GetKeyState(debugKey) & 0x8000) != 0)
    {
        if (!keyDown)
        {
            keyDown = true;
            drawDebugText++;
            if (drawDebugText > 3)
                drawDebugText = 0;
        }
    }
    else
        keyDown = false;

    int seconds = static_cast<int>(totalTimeSinceLoad.count() / 1000.0);
    if (enableLog && (seconds / 30) != secondsSinceLastModCheck) //every 30 seconds
    {
        secondsSinceLastModCheck = seconds / 30;
        for (auto& it : hookedCalls)
        {
            if (it.second.name.empty())
                continue;

            const std::uintptr_t functionAddress = (it.second.isVTableAddress == false) ? injector::GetBranchDestination(it.first).as_int() : *reinterpret_cast<unsigned int*>(it.first);
            std::pair<std::string, MODULEINFO> moduleInfo = LoadedModules::GetModuleAtAddress(functionAddress);
            std::string moduleName = moduleInfo.first.substr(moduleInfo.first.find_last_of("/\\") + 1);

            if (_stricmp(moduleName.c_str(), MOD_NAME) != 0 && callChecks.insert(it.first).second)
            {
                if (functionAddress > 0 && !moduleName.empty())
                    Log::Write("Modified call detected: %s 0x%08X 0x%08X %s 0x%08X\n", it.second.name.c_str(), it.first, functionAddress, moduleName.c_str(), moduleInfo.second.lpBaseOfDll);
                else
                    Log::Write("Modified call detected: %s 0x%08X %s\n", it.second.name.c_str(), it.first, bytesToString(it.first, 5).c_str());
            }

            auto gta_saModule = LoadedModules::GetExeModule();
            std::uintptr_t gta_saEndAddress = ((std::uintptr_t)gta_saModule.second.lpBaseOfDll + gta_saModule.second.SizeOfImage);

            if ((std::uintptr_t)it.second.originalFunction < gta_saEndAddress)
            {
                auto functionStartDestination = injector::GetBranchDestination(it.second.originalFunction).as_int();
                if (functionStartDestination && functionStartDestination > gta_saEndAddress)
                {
                    auto functionStartModule = LoadedModules::GetModuleAtAddress(functionStartDestination);
                    std::string functionStartModuleName = functionStartModule.first.substr(functionStartModule.first.find_last_of("/\\") + 1);

                    if (_stricmp(functionStartModuleName.c_str(), MOD_NAME) != 0)
                        Log::LogModifiedAddress((std::uintptr_t)it.second.originalFunction, "Modified function start detected: %s 0x%08X 0x%08X %s\n", it.second.name.c_str(), it.second.originalFunction, functionStartDestination, functionStartModuleName.c_str());
                }
            }
        }

        for (auto& it : hooksASM)
        {
            const auto currentDestination = injector::GetBranchDestination(it.first).as_int();

            std::pair<std::string, MODULEINFO> moduleInfo = LoadedModules::GetModuleAtAddress(currentDestination);
            std::string moduleName = moduleInfo.first.substr(moduleInfo.first.find_last_of("/\\") + 1);

            if (_stricmp(moduleName.c_str(), MOD_NAME) != 0 && callChecks.insert(it.first).second)
            {
                if (currentDestination > 0 && !moduleName.empty())
                    Log::Write("Modified ASM hook detected: %s 0x%08X 0x%08X %s 0x%08X\n", it.second.c_str(), it.first, currentDestination, moduleName.c_str(), moduleInfo.second.lpBaseOfDll);
                else
                    Log::Write("Modified ASM hook detected: %s 0x%08X %s\n", it.second.c_str(), it.first, bytesToString(it.first, 5).c_str());
            }
        }
    }

    CVector pPos = FindPlayerCoors(-1);
    CZone* zInfo = NULL;
    CTheZones::GetZoneInfo(&pPos, &zInfo);
    const CWanted* wanted = FindPlayerWanted(-1);

    if (!CEntryExitManager::mp_Active)
        transitioning = false;

    if (wanted && wanted->m_nWantedLevel != currentWanted)
    {
        logVariationsChange("Wanted level changed");

        currentWanted = wanted->m_nWantedLevel;
        updateVariations();
    }

    if (zInfo && *reinterpret_cast<uint64_t*>(zInfo->m_szLabel) != *reinterpret_cast<uint64_t*>(currentZone) && strncmp(zInfo->m_szLabel, "SAN_AND", 7) != 0)
    {
        logVariationsChange("Zone changed");

        *reinterpret_cast<uint64_t*>(currentZone) = *reinterpret_cast<uint64_t*>(zInfo->m_szLabel);
        updateVariations();
    }

    if (enablePeds) PedVariations::Process();
    if (enablePedWeapons) PedWeaponVariations::Process();
    if (enableVehicles) VehicleVariations::Process();
}

//Fix(?) for crash on game exit when adding special peds. Something related to m_pHitColModel.
//This is needed if PedModels in OLA is set to unlimited. If set manually to a high number (e.g PedModels=5000) the game exits ok for some reason.
template <std::uintptr_t address>
void __cdecl CGame__ShutdownHooked()
{
    Log::Write("Game shutting down...\n");

    if (!addedIDs.empty())
    {
        for (unsigned int i = 0; i < pedsModelsCount; i++)
            pedsModels[i].m_pHitColModel = NULL;
    }

    if (updatesThread.joinable())
        updatesThread.join();

    callOriginal<address>();

    Log::Write("Shutdown ok.\n");
    Log::Close();
}

template <std::uintptr_t address>
void __cdecl InitialiseGameHooked()
{
    callOriginal<address>();

    Log::Write("-- InitialiseGame Start (%s) --\n", getDatetime(false, true, true).c_str());

    Log::Write("Reading zone data...\n");
    for (const auto &kvp : iniSettings.data["Areas"])
        areas[kvp.first] = splitString(kvp.second, ',');

    std::unordered_map<std::string, std::vector<CZone*>> presetMainZones;
    for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
    {
        CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);

        for (const auto &i : areas)
            for (auto j : i.second)
                if (strncmp(zone->m_szLabel, j.c_str(), 8) == 0)
                    presetMainZones[i.first].push_back(zone);
    }

    for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
    {
        CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);

        for (const auto& i : presetMainZones)
            for (auto j : i.second)
                if (strncmp(j->m_szLabel, zone->m_szLabel, 8) == 0 || CTheZones::ZoneIsEntirelyContainedWithinOtherZone(zone, j))
                    presetAllZones[i.first].push_back(zone);
    }

    presetAllZones["Global"];

    Log::Write("TotalNumberOfInfoZones = %d\n", CTheZones::TotalNumberOfInfoZones);
    if (enableStreamingFix)
    {
        Log::Write("Reading cargrp...\n");

        for (int i = 0; i < 34; i++)
            for (int j = 0; j < CPopulation__m_nNumCarsInGroup[i]; j++)
                if (CPopulation__m_CarGroups[i * CPopulation__m_iCarsPerGroup + j] > 611)
                    addedIDsInGroups.insert((unsigned short)CPopulation__m_CarGroups[i * CPopulation__m_iCarsPerGroup + j]);

        Log::Write("Found %u added IDs in cargrp.\n", addedIDsInGroups.size());
    }
    Log::Write("-- InitialiseGame End (%s) --\n", getDatetime(false, true, true).c_str());

    if (!FrontEndMenuManager->m_bWantToRestart)
        refreshOnGameRestart();
}

template <std::uintptr_t address>
void __cdecl InitialiseRenderWareHooked()
{
    callOriginal<address>();
    if (loadStage == 1)
        initialize();
}

template <std::uintptr_t address>
void __cdecl ReInitGameObjectVariablesHooked()
{
    callOriginal<address>();
    refreshOnGameRestart();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////  MAIN   ///////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

class ModelVariations {
public:
    ModelVariations() {

        GetModuleFileName(NULL, &exePath[0], 255);
        iniSettings.Load(dataFileName);

        trackReferenceCounts = iniSettings.ReadInteger("Settings", "TrackReferenceCounts", -1);
        loadSettingsImmediately = iniSettings.ReadBoolean("Settings", "LoadSettingsImmediately", true);
        enableStreamingFix = iniSettings.ReadBoolean("Settings", "EnableStreamingFix", false);
        loadStage = iniSettings.ReadInteger("Settings", "LoadStage", 1);
        disableKey = iniSettings.ReadInteger("Settings", "DisableKey", 0);
        reloadKey = iniSettings.ReadInteger("Settings", "ReloadKey", 0);
        debugKey = iniSettings.ReadInteger("Settings", "DebugKey", 0);
        enableLog = iniSettings.ReadBoolean("Settings", "EnableLog", false) && Log::Open("ModelVariations.log");
        logJumps = iniSettings.ReadBoolean("Settings", "LogJumps", false);

        std::string checkForceEnabled = iniSettings.ReadString("Settings", "ForceEnable", "");
        if (!checkForceEnabled.empty())
        {
            if (checkForceEnabled == "1" || _stricmp(checkForceEnabled.c_str(), "true") == 0)
                forceEnableGlobal = true;
            else if (checkForceEnabled != "0" && isdigit(checkForceEnabled[0]))
            {
                for (const auto& s : splitString(checkForceEnabled, ','))
                {
                    char* endptr = NULL;
                    std::uintptr_t value = strtoul(s.c_str(), &endptr, 16);
                    if (*endptr == 0)
                        forceEnable.insert(value);
                }
            }
        }

        if (enableLog)
        {
            unsigned int exeFilesize = 0;

            HANDLE hFile = CreateFile(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                exeFilesize = GetFileSize(hFile, NULL);
                CloseHandle(hFile);
            }

            std::string windowsVersion;
            char str[64] = {};
            DWORD cbData = 63;
    
            if (RegGetValue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "CurrentBuild", RRF_RT_REG_SZ, NULL, str, &cbData) == ERROR_SUCCESS)
            {
                windowsVersion += "OS build ";
                windowsVersion += str;
                windowsVersion += " ";
            }

            cbData = 63;
            if (RegGetValue(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", "PROCESSOR_ARCHITECTURE", RRF_RT_REG_SZ, NULL, str, &cbData) == ERROR_SUCCESS)
                    windowsVersion += str;


            Log::Write("Model Variations %s %s\n", MOD_VERSION, IS_DEBUG ? "DEBUG" : "");
            Log::Write("Build date: %s\n", __DATE__);
            Log::Write("%s\n", windowsVersion.c_str());
            Log::Write("%s\n\n", getDatetime(true, true, false).c_str());
            Log::Write("%s\n", exePath.c_str());

            if (isGameHOODLUM())
                Log::Write("Supported exe detected: 1.0 US HOODLUM | %u bytes\n", exeFilesize);
            else if (isGameCompact())
                Log::Write("Supported exe detected: 1.0 US Compact | %u bytes\n", exeFilesize);
            else
                Log::Write("Unsupported exe detected: %u bytes\n", exeFilesize);
            
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            Log::Write("lpMaximumApplicationAddress = 0x%08X\n", si.lpMaximumApplicationAddress);

            if (!fileExists(dataFileName))
                Log::Write("\n%s not found!\n\n", dataFileName);
            else
            {
                Log::Write("%s\n", printFilenameWithBorder(dataFileName, '#').c_str());
                Log::LogTextFile(dataFileName);
                Log::Write("\n");
            }

            PedVariations::LogDataFile();
            PedWeaponVariations::LogDataFile();
            VehicleVariations::LogDataFile();
            Log::Write("\n");
        }

        if (loadStage == 0)
            initialize();

        if (enableStreamingFix)
        {
            hookCall(0x408D43, AddToLoadedVehiclesListHooked<0x408D43>, "CStreaming::AddToLoadedVehiclesList"); //CStreaming::FinishLoadingLargeFile
            hookCall(0x40C858, AddToLoadedVehiclesListHooked<0x40C858>, "CStreaming::AddToLoadedVehiclesList"); //CStreaming::ConvertBufferToObject
        }
        else
            Log::Write("Streaming fix disabled.\n");

        hookCall(0x440840, InteriorManager_c__UpdateHooked<0x440840>, "InteriorManager_c::Update"); //CEntryExit::TransitionFinished
        hookCall(0x40E37B, RetryLoadFileHooked<0x40E37B>, "CStreaming::RetryLoadFile"); //CStreaming::ProcessLoadingChannel
        hookCall(0x440F89, TransitionFinishedHooked<0x440F89>, "CEntryExit::TransitionFinished"); //CEntryExitManager::Update
        hookCall(0x53E293, CPopCycle__DisplayHooked<0x53E293>, "CPopCycle::Display"); //Render2dStuff

        //CFileLoader::LoadObjectTypes
        hookCall(0x5B85DD, FileLoaderLoadObject<0x5B85DD>, "CFileLoader::LoadObject");
        hookCall(0x5B862C, FileLoaderLoadObject<0x5B862C>, "CFileLoader::LoadTimeObject");
        hookCall(0x5B8634, FileLoaderLoadObject<0x5B8634>, "CFileLoader::LoadWeaponObject");
        hookCall(0x5B863C, FileLoaderLoadObject<0x5B863C>, "CFileLoader::LoadClumpObject");
        hookCall(0x5B8644, FileLoaderLoadObject<0x5B8644>, "CFileLoader::LoadAnimatedClumpObject");
        hookCall(0x5B864C, FileLoaderLoadObject<0x5B864C>, "CFileLoader::LoadVehicleObject");
        hookCall(0x5B8654, FileLoaderLoadObject<0x5B8654>, "CFileLoader::LoadPedObject");

        hookCall(0x53E981, CGame__ProcessHooked<0x53E981>, "CGame::Process"); //Idle
        hookCall(0x748E6B, CGame__ShutdownHooked<0x748E6B>, "CGame::Shutdown"); //WinMain
        hookCall(0x748CFB, InitialiseGameHooked<0x748CFB>, "InitialiseGame"); //WinMain
        hookCall(0x5BF3A1, InitialiseRenderWareHooked<0x5BF3A1>, "CGame::InitialiseRenderWare"); //RwInitialize
        hookCall(0x53C6DB, ReInitGameObjectVariablesHooked<0x53C6DB>, "CGame::ReInitGameObjectVariables"); //CGame::InitialiseWhenRestarting
    }
} modelVariations;
