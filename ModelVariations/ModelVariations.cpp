#include "DataReader.hpp"
#include "Helpers.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "Memory.hpp"
#include "SA.hpp"
#include "Timer.hpp"
#include "VariationData.hpp"

#include "Peds.hpp"
#include "PedWeapons.hpp"
#include "Vehicles.hpp"

#include <plugin.h>
#include <CCollisionData.h>
#include <CEntryExit.h>
#include <CFont.h>
#include <CGame.h>
#include <CLoadedCarGroup.h>
#include <CMessages.h>
#include <CModelInfo.h>
#include <CRunningScript.h>
#include <CStreaming.h>
#include <CTheZones.h>
#include <CVector.h>
#include <CWeather.h>

#include <chrono>
#include <map>
#include <set>

#include <urlmon.h>

#pragma comment (lib, "bcrypt.lib")
#pragma comment (lib, "urlmon.lib")


#define MOD_VERSION "11.1"
//Using Plugin-SDK: 34ba198

struct jumpInfo {
    std::uintptr_t address;
    std::uintptr_t destination;
    unsigned char type;
};

char(*InitialiseRenderWareOriginal)() = reinterpret_cast<char(*)()>(0x5BD600);

std::unordered_map<std::string, std::vector<CZone*>> presetAllZones;

std::vector<Timer::TimerID> timers;
bool restartTimers = false;

std::set<unsigned short> referenceCountModels;
std::set<unsigned short> addedIDsInGroups;

std::string versionPath;

std::unordered_map<unsigned short, std::string> addedIDs;
std::unordered_map<unsigned short, std::string> modelNames;
int maxPedID = 0;

static const char* dataFileName = "ModelVariations.ini";
DataReader iniSettings;

std::chrono::steady_clock::time_point lastTime;
std::chrono::steady_clock::time_point loadTime;
std::chrono::steady_clock::duration totalTimeSinceLoad(0);
std::chrono::steady_clock::duration gameplayTimeSinceLoad(0);

int drawDebugText = 0;

CEntryExit* currentSpawnPoint = NULL;
CZone* currentZone = NULL;
unsigned int currentWanted = 0;

bool transitioning = false;

bool keyDown = false;

bool newVersionFound = false;

int flaMaxID = -1;

char lastMissionLoaded[9] = {};
char currentMission[9] = {};

//INI Options
int enableLog = 0;
bool logJumps = false;
bool enablePeds = false;
bool enableSpecialPeds = false;
bool enableVehicles = false;
bool enablePedWeapons = false;
bool forceEnableGlobal = false;
bool enableStreamingFix = false;
bool enableNullGuards = false;
int lowMemoryProtection = 3300;
int loadStage = 1;
int trackReferenceCounts = -1;
int disableKey = 0;
int reloadKey = 0;
int debugKey = 0;

//debugDrawOptions
float debugDrawSize = 0.28f;
float debugDrawX = 20.0f;
float debugDrawY = 340.0f;
uint32_t debugDrawPeds = 0xFFFFFFFF;
uint32_t debugDrawVehicles = 0xFFFFFFFF;

std::set<std::uintptr_t> forceEnable;

bool modInitialized = false;

bool checkForUpdate()
{
    std::string str = fileToString(versionPath);

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
                int n1 = INT_MAX, n2 = INT_MAX;
                fromString<int>(newV[i], n1);
                fromString<int>(oldV[i], n2);

                if (n1 == INT_MAX || n2 == INT_MAX) 
                    return false;
                if (n1 > n2)
                    return true;
                if (n1 < n2) 
                    return false;
            }

            return false; // equal
        }

    return false;
}

struct Download {
    std::string url, path;

    static void CALLBACK run(PTP_CALLBACK_INSTANCE, void* p)
    {
        auto d = static_cast<Download*>(p);
        URLDownloadToFileA(nullptr, d->url.c_str(), d->path.c_str(), 0, nullptr);
        delete d;
    }
};

void download_async(std::string url, std::string path)
{
    auto d = new Download{ url, path };
    if (!TrySubmitThreadpoolCallback(Download::run, d, nullptr))
        delete d;
}

void logVariationsChange(const char* msg)
{
    auto player = FindPlayerPed();
    CVector pPos = FindPlayerCoors(-1);
    CWanted* wanted = FindPlayerWanted(-1);
    CZone* zInfo = NULL;
    CTheZones::GetZoneInfo(&pPos, &zInfo);

    if (zInfo == NULL || wanted == NULL)
        return;

    Log::Write("\n%s (%s)\n", msg, getDatetime(false, true, true).c_str());
    Log::Write("Streaming Memory usage: %u/%u MB  Total Memory usage: %u MB\n", CStreaming__ms_memoryUsed/1024/1024, CStreaming__ms_memoryAvailable/1024/1024, getMemoryUsage()/1024/1024);
    Log::Write("Updating variations. pPos = {%f, %f, %f}\n", pPos.x, pPos.y, pPos.z);
    Log::Write("currentMission = %s lastMissionLoaded = %s\n", currentMission, lastMissionLoaded);
    Log::Write("currentWanted = %u wanted->m_nWantedLevel = %u\n", currentWanted, wanted->m_nWantedLevel);
    Log::Write("currentZone = %.8s zInfo->m_szLabel = %.8s\n", currentZone ? currentZone->m_szLabel : NULL, zInfo->m_szLabel);

    if (player && player->m_pEnex)
        Log::Write("player->m_pEnex = %.8s\n", player->m_pEnex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////   DATA   /////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void clearEverything()
{
    iniSettings.Clear();

    resetOriginalModels();
    variations.clear();
    interiorVariations.clear();
    currentZoneVariations = variations.end();
    variationSets.clear();
    currentSpawnPoint = NULL;

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
                MessageBox(NULL, "No limit adjuster found! EnableSpecialPeds will be disabled.", "Model Variations", MB_ICONWARNING | MB_SETFOREGROUND);
        }

        PedVariations::LoadData();
    }

    if (!enablePeds || !enableSpecialPeds)
        maxPedID = -1;

    if (enablePedWeapons)
        PedWeaponVariations::LoadData();

    if (enableVehicles)
        VehicleVariations::LoadData();

    static bool flag = false;
    if (!flag && enableSpecialPeds && CModelInfo::GetModelInfo(0))
    {
        flag = true;
        PedVariations::ClearData();
        PedVariations::LoadData();
    }
}

void updateVariations()
{
    //zInfo->m_szTextKey = BLUEB | zInfo->m_szLabel = BLUEB1

    currentZoneVariations = currentZone ? variations.find(zoneGetIndex(currentZone)) : variations.end();

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

        if (player && player->bInVehicle)
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

        if (Log::Write("\n"))
        {
            Log::Write("%s\n", printFilenameWithBorder(flaIniPath.substr(flaIniPath.find_last_of("/\\") + 1), '#').c_str());
            for (auto& i : flaIni.data)
            {
                const std::string section(i.first);
                Log::Write("[%s]\n", section.c_str());
                for (auto& j : i.second)
                {
                    const std::string key(j.first);
                    const std::string value(j.second);
                    Log::Write("%s = %s\n\n", key.c_str(), value.c_str());
                }
            }
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

    if (Log::Write("\nLoaded modules:\n"))
        LoadedModules::Log();

    Log::Write("\n");

    uint32_t* streamingMemoryOriginal = (uint32_t*)0x5B8E6A;
    uint32_t streamingMemoryNew = 0;
    fromString<uint32_t>(iniSettings.ReadString("Limits", "StreamingMemory", ""), streamingMemoryNew);

    streamingMemoryNew *= (streamingMemoryNew < 4000) ? 1048576 : 0;
    if (streamingMemoryNew > 52428800)
    {
        if (*streamingMemoryOriginal == 52428800)
        {
            injector::WriteMemory<uint32_t>(streamingMemoryOriginal, streamingMemoryNew, true);
            Log::Write("Streaming memory was set to %u\n", streamingMemoryNew);
        }
        else
            Log::Write("Streaming memory not increased. Current streaming memory is %u\n", *streamingMemoryOriginal);
    }


    modInitialized = true;
}

void refreshOnGameRestart()
{
    lastTime = std::chrono::steady_clock::time_point{};
    loadTime = std::chrono::steady_clock::now();
    gameplayTimeSinceLoad = std::chrono::steady_clock::duration(0);

    auto startTime = std::chrono::steady_clock::now();

    if (!modInitialized && loadStage == 1)
        initialize();

    if (!modInitialized)
    {
        MessageBox(NULL, "Could not initialize mod.", "Model Variations", MB_ICONWARNING | MB_SETFOREGROUND);
        return;
    }
        

    Log::Write("-- Restarting (%s) --\n", getDatetime(false, true, true).c_str());

    clearEverything();
    PedVariations::ProcessDrugDealers(true);
    LoadedModules::Refresh();

    currentZone = NULL;

    loadIniData();

    if (enablePeds)
        PedVariations::LogVariations();

    if (enableVehicles)
    {
        Log::Write("\n");
        VehicleVariations::LogVariations();
    }

    Log::Write("\n\n");

    newVersionFound = checkForUpdate();

    int finalTime = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    if (finalTime < 1000)
        Log::Write("Time spent loading: %dms.\n", finalTime);
    else
        Log::Write("Time spent loading: %fs.\n", finalTime / 1000.0);

    Log::Write("-- Restart Finished (%s) --\n", getDatetime(false, true, true).c_str());

    if (!addedIDs.empty() && Log::Write("Added IDs:\n"))
    {
        for (auto &it : addedIDs)
            Log::Write("%u %s\n", it.first, it.second.c_str());
        Log::Write("\n");
    }

    restartTimers = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

__declspec(noinline) bool __cdecl AddToLoadedVehiclesListHooked(int model)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (model < 612 || addedIDsInGroups.contains((unsigned short)model))
        return originalCall.callAndReturn<bool>(model);

    return 1;
}

__declspec(noinline) void __cdecl RetryLoadFileHooked(int streamNum)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (Log::Write("RetryLoadFile called for the following IDs in channel %d: ", streamNum))
    {
        for (int i = 0; i < 16; i++)
            Log::Write("%d ", *reinterpret_cast<int*>(0x8E4A60 + streamNum * 0x98 + i * sizeof(int)));
        Log::Write("\n");
    }

    originalCall.call(streamNum);
}

__declspec(noinline) void CPopCycle__DisplayHooked()
{
    const auto originalCall = captureCurrentOriginalCall();

    if (drawDebugText > 2 && debugDrawVehicles > 0)
        VehicleVariations::DrawDebugInfo(debugDrawSize, debugDrawVehicles);
    if ((drawDebugText == 2 || drawDebugText == 4) && debugDrawPeds > 0)
        PedVariations::DrawDebugInfo(debugDrawSize, debugDrawPeds);

    if (drawDebugText > 0)
    {
        CPlayerPed* player = FindPlayerPed();

        float fontSize = debugDrawSize*1.8f;

        float fontSizew = RsGlobal.maximumHeight / 640.0f * fontSize;
        float fontSizeh = fontSizew * 2.2f;

        CFont::SetFontStyle(FONT_SUBTITLES);
        CFont::SetScale(fontSizew, fontSizeh);
        CFont::SetColor(CRGBA(255, 255, 255, 255));
        CFont::SetDropColor(CRGBA(0, 0, 0, 255));
        CFont::SetEdge(1);
        CFont::SetProportional(true);
        CFont::SetBackground(false, false);
        CFont::SetJustify(false);
        CFont::SetOrientation(ALIGN_RIGHT);

        std::string text;

        float x = SCREEN_COORD_RIGHT(debugDrawX);
        float y = SCREEN_COORD_TOP(debugDrawY);
        float lineOffset = (RsGlobal.maximumHeight / 640.0f) * fontSize * 38.0f;
        float offsetMultiplier = 1.0f;

        auto PrintDebugLine = [&](const char* format, auto&&... args)
        {
            text = msprintf(format, std::forward<decltype(args)>(args)...);

            CFont::PrintString(x, y + lineOffset * offsetMultiplier, text.c_str());
            offsetMultiplier += 1.0f;
        };

        bool isRainy = CWeather__IsRainy();
        bool isSandstorm = CWeather::Sandstorm > 0.29;
        bool isFoggy = CWeather::Foggyness > 0.3;
        bool isWindy = CWeather::Wind > 0.29;

        PrintDebugLine("Model Variations v" MOD_VERSION);
        PrintDebugLine("Debug state: %d", drawDebugText);
        PrintDebugLine("Call hooks: %u/%u/%u", getSharedCallStateCount(), getHookedCalls().size(), getNumMaxHooks());
        PrintDebugLine("%d MB %d MB", CStreaming__ms_memoryUsed / 1024 / 1024, getMemoryUsage() / 1024 / 1024);
        if (CTheScripts__IsPlayerOnAMission())
            PrintDebugLine("Mission: %s", lastMissionLoaded);

        if (CGame::currArea)
            PrintDebugLine("currArea: %d", CGame::currArea);
        if (currentZone)
            PrintDebugLine("Current zone: %s", currentZone->m_szLabel);
        if (player && player->m_pEnex)
            PrintDebugLine("Current interior: %.8s", player->m_pEnex);
        if (CWeather::Rain > 0.001)
            PrintDebugLine("%s: %.3f", isRainy ? "~y~Rain~s~" : "Rain", CWeather::Rain);
        if (CWeather::Sandstorm > 0.001)
            PrintDebugLine("%s: %.3f", isSandstorm ? "~y~Sandstorm~s~" : "Sandstorm", CWeather::Sandstorm);
        if (CWeather::Foggyness > 0.001)
            PrintDebugLine("%s: %.3f", isFoggy ? "~y~Foggyness~s~" : "Foggyness", CWeather::Foggyness);
        if (CWeather::Wind > 0.001)
            PrintDebugLine("%s: %.3f", isWindy ? "~y~Wind~s~" : "Wind", CWeather::Wind);
    }

    originalCall.call();
}

__declspec(noinline) CZone* __cdecl FindSmallestZoneForPositionHooked(void* point, char checkType)
{
    const auto originalCall = captureCurrentOriginalCall();
    auto retVal = originalCall.callAndReturn<CZone*>(point, checkType);

    auto player = FindPlayerPed();
    static CZone *zoneStart = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray);

    if (currentZone != retVal && CGame::currArea == 0)
    {
        logVariationsChange("Zone changed");

        currentZone = retVal;
        updateVariations();
    }

    if (currentSpawnPoint != CEntryExit::ms_spawnPoint && ((player && player->m_pEnex) ? CGame::currArea == 0 : CGame::currArea != 0))
    {
        logVariationsChange("Interior changed");

        currentSpawnPoint = CEntryExit::ms_spawnPoint;
        updateVariations();
    }

    return retVal;
}

//Model names
__declspec(noinline) int __cdecl FileLoaderLoadObject(const char* a1)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (a1)
    {
        int id = -1;
        const char* p = std::strchr(a1, ' ');

        if (p)
        {
            std::string_view sv{ a1, static_cast<std::size_t>(p - a1) };

            if (fromString(sv, id) && id > 0 && id < 65536)
            {
                while (*p == ' ') ++p;

                const char* n = p;
                while (*p && *p != ' ') ++p;

                if (p != n)
                    modelNames[static_cast<unsigned short>(id)] = std::string(n, p);
            }
        }
    }

    return originalCall.callAndReturn<unsigned int>(a1);
}

__declspec(noinline) void __cdecl RemoveTrianglePlanesHooked(CCollisionData* a2)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (!isAddressValid(a2))
    {
        Log::Write("RemoveTrianglePlanesHooked Error! a2 is invalid (0x%X)\n", a2);
        return;
    }

    if (a2->m_pTrianglePlanes == NULL)
    {
        originalCall.call(a2);
        return;
    }

    if (!isAddressValid(a2->m_pTrianglePlanes))
    {
        Log::Write("RemoveTrianglePlanesHooked Error! a2 is (0x%X) a2->m_pTrianglePlanes is invalid (0x%X)\n", a2, a2->m_pTrianglePlanes);
        a2->m_pTrianglePlanes = NULL;
        return;
    }

    auto* link = a2->GetLinkPtr();

    if (!isAddressValid(link))
    {
        Log::Write("RemoveTrianglePlanesHooked Error! link is invalid (0x%X)\n", link);
        a2->m_pTrianglePlanes = NULL;
        return;
    }

    if (!isAddressValid(link->prev) || !isAddressValid(link->next))
    {
        Log::Write("RemoveTrianglePlanesHooked Error! link chain is invalid (prev:0x%X next:0x%X)\n", link->prev, link->next);
        a2->m_pTrianglePlanes = NULL;
        return;
    }

    if (link->prev->next != link || link->next->prev != link)
    {
        Log::Write("RemoveTrianglePlanesHooked Error! link chain does not point to link (prev->next: 0x%x next->prev: 0x%X)\n", link->prev->next, link->next->prev);
        a2->m_pTrianglePlanes = NULL;
        return;
    }

    originalCall.call(a2);
}

__declspec(noinline) void __cdecl CGame__ProcessHooked()
{
    const auto originalCall = captureCurrentOriginalCall();

    auto now = std::chrono::steady_clock::now();

    if (!FrontEndMenuManager->m_bMenuActive)
    {
        if (lastTime.time_since_epoch().count() > 0)
            gameplayTimeSinceLoad += (now - lastTime);
        lastTime = now;
    }
    else
        lastTime = std::chrono::steady_clock::time_point{};

    totalTimeSinceLoad = (now - loadTime);
    

    originalCall.call();

    if (restartTimers)
    {
        for (auto timer : timers)
            Timer::Start(timer, true);

        restartTimers = false;
    }

    Timer::Process();

    if (logJumps && Log::Write("\nLogging JMP hooks...\n"))
    {
        std::unordered_map<std::string, std::vector<jumpInfo>> jumpsMap;

        auto gta_saModule = LoadedModules::GetExeModule();
        std::uintptr_t gta_saEndAddress = ((std::uintptr_t)gta_saModule.second.lpBaseOfDll + gta_saModule.second.SizeOfImage);

        for (const auto& section : getOriginalExeSections())
        {
            for (std::size_t offset = 0; offset < section.data.size(); ++offset)
            {
                const std::uintptr_t currentAddress = section.address + offset;
                auto currentByte = *reinterpret_cast<unsigned char*>(currentAddress);
                if (currentByte != section.data[offset])
                {
                    auto destination = injector::GetBranchDestination(currentAddress).as_int();
                    if (destination > gta_saEndAddress)
                    {
                        auto moduleInfo = LoadedModules::GetModuleAtAddress(destination);
                        std::string moduleName = moduleInfo.first.substr(moduleInfo.first.find_last_of("/\\") + 1);

                        if (!strcasestr(moduleInfo.first, "Windows") && !strcasecmp(moduleName, MOD_NAME))
                        {
                            if (moduleName.empty())
                                jumpsMap["unknown"].push_back({ currentAddress, destination, currentByte });
                            else
                                jumpsMap[moduleName].push_back({ currentAddress, destination, currentByte });
                        }
                        offset += 3;
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

        logJumps = false;
    }

    if (newVersionFound && gameplayTimeSinceLoad > std::chrono::seconds(10))
    {
        CMessages::AddMessageJumpQ("~y~Model Variations~s~: Update available.", 4000, 0, false);
        newVersionFound = false;
    }

    if (disableKey > 0 && (GetKeyState(disableKey) & 0x8000) != 0)
    {
        if (!keyDown)
        {
            keyDown = true;
            CMessages::AddMessageJumpQ("~y~Model Variations~s~: Mod disabled.", 2000, 0, false);
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
            CMessages::AddMessageJumpQ("~y~Model Variations~s~: Reloading settings...", 10000, 0, false);
            loadIniData();

            currentZone = NULL;
            CMessages::AddMessageJumpQ("~y~Model Variations~s~: Settings reloaded.", 2000, 0, false);
        }
    }
    else if (debugKey > 0 && (GetKeyState(debugKey) & 0x8000) != 0)
    {
        if (!keyDown)
        {
            keyDown = true;
            drawDebugText++;
            if (drawDebugText > 4)
                drawDebugText = 0;
        }
    }
    else
        keyDown = false;

    const CWanted* wanted = FindPlayerWanted(-1);

    if (!CTheScripts__IsPlayerOnAMission())
        lastMissionLoaded[0] = 0;
    else
    {
        for (CRunningScript* script = CTheScripts__pActiveScripts; script; script = script->m_pNext)
            if (script->m_bIsActive && script->m_bIsMission)
                strncpy(lastMissionLoaded, script->m_szName, 8);
    }


    if (wanted && wanted->m_nWantedLevel != currentWanted)
    {
        logVariationsChange("Wanted level changed");

        currentWanted = wanted->m_nWantedLevel;
        updateVariations();
    }

    if (!strcasecmp(currentMission, lastMissionLoaded))
    {
        logVariationsChange("Mission changed");

        strcpy(currentMission, lastMissionLoaded);
        for (auto& c : currentMission)
            c = toUpper(c);

        updateVariations();
    }

    if (enablePeds) PedVariations::Process();
    if (enablePedWeapons) PedWeaponVariations::Process();
    if (enableVehicles) VehicleVariations::Process();
}

//Fix(?) for crash on game exit when adding special peds. Something related to m_pHitColModel.
//This is needed if PedModels in OLA is set to unlimited. If set manually to a high number (e.g PedModels=5000) the game exits ok for some reason.
__declspec(noinline) void __cdecl CGame__ShutdownHooked()
{
    const auto originalCall = captureCurrentOriginalCall();

    Log::Write("Game shutting down...\n");

    if (!addedIDs.empty())
    {
        for (unsigned int i = 0; i < pedsModelsCount; i++)
            pedsModels[i].m_pHitColModel = NULL;
    }

    originalCall.call();

    Log::Write("Shutdown ok.\n");
    Log::Close();
}

__declspec(noinline) void __cdecl InitialiseGameHooked()
{
    const auto originalCall = captureCurrentOriginalCall();
    originalCall.call();

    std::unordered_map<std::string, std::vector<std::string>> areas;

    Log::Write("-- InitialiseGame Start (%s) --\n", getDatetime(false, true, true).c_str());

    Log::Write("Reading zone data...\n");
    if (auto areaSection = iniSettings.data.find("Areas"); areaSection != iniSettings.data.end())
        for (const auto& kvp : areaSection->second)
            areas[std::string(kvp.first)] = splitString(std::string(kvp.second), ',');

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

    if (enableLog)
    {
        timers.push_back(Timer::Add(std::chrono::seconds(30), [&]()
        {
            static std::set<std::uintptr_t> callChecks;

            for (const auto& it : getHookedCalls())
            {
                if (it.name == NULL || it.name[0] == 0)
                    continue;

                const std::uintptr_t functionAddress = !it.isVTableAddress ? injector::GetBranchDestination(it.address).as_int() : *reinterpret_cast<const std::uintptr_t*>(it.address);

                const std::uintptr_t expectedFunctionAddress = reinterpret_cast<std::uintptr_t>(it.changedFunction);

                if (functionAddress != expectedFunctionAddress && callChecks.insert(it.address).second)
                {
                    const auto moduleInfo = LoadedModules::GetModuleAtAddress(functionAddress);

                    const std::string moduleName = getFilenameFromPath(moduleInfo.first);

                    if (functionAddress > 0 && !moduleName.empty())
                        Log::Write("Modified call detected: %s 0x%08X 0x%08X %s 0x%08X\n", it.name, it.address, functionAddress, moduleName.c_str(), moduleInfo.second.lpBaseOfDll);
                    else
                        Log::Write("Modified call detected: %s 0x%08X %s\n", it.name, it.address, bytesToString(it.address, 5).c_str());
                }

                const auto& gtaSaModule = LoadedModules::GetExeModule();
                const std::uintptr_t gtaSaBase = reinterpret_cast<std::uintptr_t>(gtaSaModule.second.lpBaseOfDll);
                const std::uintptr_t gtaSaEnd = gtaSaBase + gtaSaModule.second.SizeOfImage;

                const std::uintptr_t originalFunction = reinterpret_cast<std::uintptr_t>(it.originalFunction);

                if (gtaSaBase && originalFunction >= gtaSaBase && originalFunction < gtaSaEnd)
                {
                    const std::uintptr_t functionStartDestination = injector::GetBranchDestination(originalFunction).as_int();

                    if (functionStartDestination && (functionStartDestination < gtaSaBase || functionStartDestination >= gtaSaEnd))
                    {
                        const auto functionStartModule = LoadedModules::GetModuleAtAddress(functionStartDestination);

                        const std::string functionStartModuleName = getFilenameFromPath(functionStartModule.first);

                        if (!strcasecmp(functionStartModuleName, MOD_NAME))
                            Log::LogModifiedAddress(originalFunction, "Modified function start detected: %s 0x%08X 0x%08X %s\n", it.name, originalFunction, functionStartDestination, functionStartModuleName.c_str());
                    }
                }
            }

            for (const auto& it : getASMHooks())
            {
                const auto currentDestination = injector::GetBranchDestination(it.address).as_int();

                std::pair<std::string, MODULEINFO> moduleInfo = LoadedModules::GetModuleAtAddress(currentDestination);
                std::string moduleName = moduleInfo.first.substr(moduleInfo.first.find_last_of("/\\") + 1);

                if (!strcasecmp(moduleName, MOD_NAME) && callChecks.insert(it.address).second)
                {
                    if (currentDestination > 0 && !moduleName.empty())
                        Log::Write("Modified ASM hook detected: %s 0x%08X 0x%08X %s 0x%08X\n", it.name, it.address, currentDestination, moduleName.c_str(), moduleInfo.second.lpBaseOfDll);
                    else
                        Log::Write("Modified ASM hook detected: %s 0x%08X %s\n", it.name, it.address, bytesToString(it.address, 5).c_str());
                }
            }
        }));
    }

    if (lowMemoryProtection > 0)
    {
        timers.push_back(Timer::Add(std::chrono::seconds(2), [&]()
        {
            int totalMemory = getMemoryUsage() / 1024 / 1024;

            if (totalMemory > lowMemoryProtection && (enablePeds || enablePedWeapons || enableVehicles))
            {
                CMessages::AddMessageJumpQ("~y~Model Variations~s~: Mod disabled due to low memory. Reload manually.", 4000, 0, false);
                reinterpret_cast<void (*)()>(0x40CF80)(); //CStreaming::RemoveAllUnusedModels
                clearEverything();
                enablePeds = false;
                enablePedWeapons = false;
                enableVehicles = false;
            }
        }));
    }

    if (trackReferenceCounts > 0)
    {
        timers.push_back(Timer::Add(std::chrono::seconds(2), [&]()
        {
            if (CModelInfo::GetModelInfo(0))
                for (int i = 7; i < std::max<int>(flaMaxID, 20000); i++)
                {
                    auto mInfo = CModelInfo::GetModelInfo(i);
                    if (mInfo && mInfo->m_nRefCount > trackReferenceCounts && !referenceCountModels.contains(static_cast<unsigned short>(i)))
                    {
                        auto modelType = (mInfo->GetModelType() == MODEL_INFO_VEHICLE) ? "(Vehicle) " : ((mInfo->GetModelType() == MODEL_INFO_PED) ? "(Ped) " : "");

                        std::string warning_string = msprintf("WARNING: model %d %shas a reference count of %d\n", i, modelType, mInfo->m_nRefCount);
                        Log::Write("%s", warning_string.c_str());
#ifdef _DEBUG
                        MessageBox(NULL, warning_string.c_str(), "Model Variations", MB_ICONWARNING | MB_SETFOREGROUND);
#endif
                        referenceCountModels.insert(static_cast<unsigned short>(i));
                    }
                }
        }));
    }

    restartTimers = true;

    if (!FrontEndMenuManager->m_bWantToRestart)
        refreshOnGameRestart();
}

__declspec(noinline) void __cdecl ReInitGameObjectVariablesHooked()
{
    const auto originalCall = captureCurrentOriginalCall();
    originalCall.call();
    refreshOnGameRestart();
}

char __cdecl InitialiseRenderWareHooked()
{
    char retVal = InitialiseRenderWareOriginal();

    char exePath[MAX_PATH] = {};
    GetModuleFileName(NULL, exePath, MAX_PATH-1);

    char buffer[MAX_PATH] = {};
    DWORD len = GetTempPathA(MAX_PATH, buffer);
    if (len != 0 && len < MAX_PATH)
    {
        versionPath = std::string(buffer) + "version.json";
        download_async("http://api.github.com/repos/ViperJohnGR/ModelVariations/tags", versionPath);
    }

    iniSettings.Load(dataFileName);

    enableNullGuards = iniSettings.ReadBoolean("Settings", "EnableNullGuards", false);
    trackReferenceCounts = iniSettings.ReadInteger("Settings", "TrackReferenceCounts", -1);
    enableStreamingFix = iniSettings.ReadBoolean("Settings", "EnableStreamingFix", false);
    lowMemoryProtection = iniSettings.ReadInteger("Settings", "LowMemoryProtection", 0);
    loadStage = iniSettings.ReadInteger("Settings", "LoadStage", 0);
    disableKey = iniSettings.ReadInteger("Settings", "DisableKey", 0);
    reloadKey = iniSettings.ReadInteger("Settings", "ReloadKey", 0);
    debugKey = iniSettings.ReadInteger("Settings", "DebugKey", 0);
    enableLog = iniSettings.ReadInteger("Settings", "EnableLog", 0);
    logJumps = iniSettings.ReadBoolean("Settings", "LogJumps", false);
    debugDrawSize = iniSettings.ReadFloat("Settings", "DebugDrawSize", 0.28f);
    debugDrawX = iniSettings.ReadFloat("Settings", "DebugDrawX", 20.0f);
    debugDrawY = iniSettings.ReadFloat("Settings", "DebugDrawY", 340.0f);
    debugDrawPeds = iniSettings.ReadHex("Settings", "DebugDrawPeds", 0);
    debugDrawVehicles = iniSettings.ReadHex("Settings", "DebugDrawVehicles", 0);

    if (enableLog > 0)
        if (!Log::Open("ModelVariations.log", enableLog == 2))
            enableLog = 0;

    std::string checkForceEnabled = iniSettings.ReadString("Settings", "ForceEnable", "");
    if (!checkForceEnabled.empty())
    {
        if (checkForceEnabled == "1" || strcasecmp(checkForceEnabled, "true"))
            forceEnableGlobal = true;
        else if (checkForceEnabled != "0")
        {
            for (const auto& s : splitString(checkForceEnabled, ','))
            {
                std::uintptr_t value;
                if (fromString<std::uintptr_t>(trimString(s), value, 16))
                    forceEnable.insert(value);
            }
        }
    }

    if (enableLog)
    {
        unsigned int exeFilesize = 0;

        HANDLE hFile = CreateFile(exePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
        Log::Write("%s\n", exePath);

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
            Log::Write("%s\n", fileToString(dataFileName).c_str());
        }

        PedVariations::LogDataFile();
        PedWeaponVariations::LogDataFile();
        VehicleVariations::LogDataFile();
        Log::Write("\n");
    }

    const std::vector<int> sections = isGameHOODLUM() ? std::vector<int>{ 0, 1, 7, 8, 9, 10 } : std::vector<int>{ 0, 1 };
    const std::vector<std::uintptr_t> sectionAddresses = isGameHOODLUM() ? std::vector<std::uintptr_t>{ 0x401000, 0x857000, 0xCB1000, 0x12FB000, 0x1301000, 0x1556000 } :
                                                                           std::vector<std::uintptr_t>{ 0x401000, 0x857000 };

    if (!loadOriginalExeSections(exePath, sections, sectionAddresses))
    {
        Log::Write("Error! Failed to retain the original executable sections. Mod initialization aborted.\n");
        MessageBox(NULL, "Failed to load the original executable sections.", "Model Variations", MB_ICONERROR | MB_SETFOREGROUND);
        return retVal;
    }

    if (loadStage == 0)
        initialize();

    if (enableStreamingFix)
    {
        hookSharedCall<0x408D43, AddToLoadedVehiclesListHooked>("CStreaming::AddToLoadedVehiclesList"); //CStreaming::FinishLoadingLargeFile
        hookSharedCall<0x40C858, AddToLoadedVehiclesListHooked>("CStreaming::AddToLoadedVehiclesList"); //CStreaming::ConvertBufferToObject
    }
    else
        Log::Write("Streaming fix disabled.\n");

    hookSharedCall<0x40E37B, RetryLoadFileHooked>("CStreaming::RetryLoadFile"); //CStreaming::ProcessLoadingChannel
    hookSharedCall<0x53E293, CPopCycle__DisplayHooked>("CPopCycle::Display"); //Render2dStuff
    hookSharedCall<0x571FA6, FindSmallestZoneForPositionHooked>("CTheZones::FindSmallestZoneForPosition"); //CPlaceName::Process

    //CFileLoader::LoadObjectTypes
    if (enableLog || debugKey > 0)
    {
        hookSharedCall<0x5B85DD, FileLoaderLoadObject>("CFileLoader::LoadObject");
        hookSharedCall<0x5B862C, FileLoaderLoadObject>("CFileLoader::LoadTimeObject");
        hookSharedCall<0x5B8634, FileLoaderLoadObject>("CFileLoader::LoadWeaponObject");
        hookSharedCall<0x5B863C, FileLoaderLoadObject>("CFileLoader::LoadClumpObject");
        hookSharedCall<0x5B8644, FileLoaderLoadObject>("CFileLoader::LoadAnimatedClumpObject");
        hookSharedCall<0x5B864C, FileLoaderLoadObject>("CFileLoader::LoadVehicleObject");
        hookSharedCall<0x5B8654, FileLoaderLoadObject>("CFileLoader::LoadPedObject");
    }

    if (enableNullGuards)
    {
        hookSharedCall<0x40F716, RemoveTrianglePlanesHooked>("CCollision::RemoveTrianglePlanes"); //CColModel::~CColModel
        hookSharedCall<0x40F9F1, RemoveTrianglePlanesHooked>("CCollision::RemoveTrianglePlanes"); //CColModel::RemoveCollisionVolumes
        //hookSharedCall<0x4185AF, &RemoveTrianglePlanesHooked>("CCollision::RemoveTrianglePlanes"); //CCollision::RemoveTrianglePlanes
        if (isGameHOODLUM())
            hookSharedCall<0x156FB57, RemoveTrianglePlanesHooked>("CCollision::RemoveTrianglePlanes"); //CCollisionData::RemoveCollisionVolumes
        else
            hookSharedCall<0x40F0E7, RemoveTrianglePlanesHooked>("CCollision::RemoveTrianglePlanes"); //CCollisionData::RemoveCollisionVolumes
    }

    hookSharedCall<0x53E981, CGame__ProcessHooked>("CGame::Process"); //Idle
    hookSharedCall<0x748E6B, CGame__ShutdownHooked>("CGame::Shutdown"); //WinMain
    hookSharedCall<0x748CFB, InitialiseGameHooked>("InitialiseGame"); //WinMain
    hookSharedCall<0x53C6DB, ReInitGameObjectVariablesHooked>("CGame::ReInitGameObjectVariables"); //CGame::InitialiseWhenRestarting

    return retVal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////  MAIN   ///////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

class ModelVariations {
public:
    ModelVariations() {

        if (!plugin::IsGameVersion10us())
        {
            MessageBox(NULL, "Error! Unsupported EXE version detected!\nThis mod supports only the US v1.0 EXE.", "Model Variations", MB_ICONERROR | MB_SETFOREGROUND);
            return;
        }

        InitialiseRenderWareOriginal = injector::MakeCALL(0x5BF3A1, InitialiseRenderWareHooked, true).get();
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(0x5BF3A1), 5);
 
    }
} modelVariations;
