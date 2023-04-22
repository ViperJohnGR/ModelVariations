#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "Log.hpp"

#include "Peds.hpp"
#include "PedWeapons.hpp"
#include "Vehicles.hpp"

#include <plugin.h>
#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CPopulation.h>
#include <CTheScripts.h>
#include <CTheZones.h>
#include <CVector.h>
#include <CWorld.h>

#include <array>
#include <iomanip>
#include <map>
#include <set>
#include <stack>

#include <ntstatus.h>
#include <Psapi.h>
#include <urlmon.h>

#pragma comment (lib, "bcrypt.lib")
#pragma comment (lib, "urlmon.lib")


#define MOD_VERSION "8.7"
#ifdef _DEBUG
#define MOD_NAME "ModelVariations_d.asi"
#define DEBUG_STRING " DEBUG"
#else
#define MOD_NAME "ModelVariations.asi"
#define DEBUG_STRING ""
#endif


using namespace plugin;

std::string exeHash;
unsigned int exeFilesize = 0;
std::string exePath;
std::string exeName;

std::map<std::uintptr_t, hookinfo> hookedCalls;


std::set<std::pair<std::uintptr_t, std::string>> callChecks;
std::set<std::pair<std::uintptr_t, std::string>> modulesSet;

std::vector<unsigned short> addedIDs;
int maxPedID = 0;

static const char* dataFileName = "ModelVariations.ini";
DataReader iniSettings(dataFileName);

struct {
    bool fastman92LimitAdjuster = false;
    bool openLimitAdjuster = false;
} loadedMods;

short framesSinceCallsChecked = 0;
char lastInterior[8] = {};
const char* currentInterior = lastInterior;
char currentZone[8] = {};
unsigned int currentTown = 0;
unsigned int currentWanted = 0;

bool keyDown = false;

int timeUpdate = -1;

//INI Options
bool enableLog = false;
bool enablePeds = false;
bool enableSpecialPeds = false;
bool enableVehicles = false;
bool enablePedWeapons = false;
bool forceEnable = false;
int loadStage = 1;
unsigned int disableKey = 0;
unsigned int reloadKey = 0;

bool modInitialized = false;


bool checkForUpdate()
{
    IStream* stream;

    if (URLOpenBlockingStream(0, "http://api.github.com/repos/ViperJohnGR/ModelVariations/tags", &stream, 0, 0) != S_OK)
    {
        Log::Write("Check for updates failed.\n");
        return false;
    }

    std::string str(51, 0);
    if (stream->Read(&str[0], 50, NULL) != S_OK)
    {
        Log::Write("Check for updates failed.\n");
        return false;
    }

    stream->Release();
    str = str.substr(str.find("\"name\":\"v")+9, 10);
    str.erase(str.find('"'));
    for (auto ch : str)
        if (!((ch >= '0' && ch <= '9') || (ch == '.')))
        {
            Log::Write("Check for updates failed. Invalid version string.\n");
            return false;
        }

    const char *newV = str.c_str();
    const char *oldV = MOD_VERSION;

    return std::lexicographical_compare(oldV, oldV+strlen(oldV), newV, newV+strlen(newV));
}

void detectExe()
{
    char path[256] = {};
    GetModuleFileName(NULL, path, 255);
    exePath = path;
    exeName = getFilenameFromPath(path);

    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    
    exeFilesize = GetFileSize(hFile, NULL);

    if (exeFilesize != INVALID_FILE_SIZE && exeHash.empty())
    {
        DWORD lpNumberOfBytesRead = 0;
        BCRYPT_ALG_HANDLE hProvider = NULL;
        BCRYPT_HASH_HANDLE ctx = NULL;
        auto filebuf = std::vector<BYTE>(exeFilesize + 1);

        if (ReadFile(hFile, filebuf.data(), exeFilesize, &lpNumberOfBytesRead, NULL))
            if (BCryptOpenAlgorithmProvider(&hProvider, BCRYPT_SHA256_ALGORITHM, NULL, 0) == STATUS_SUCCESS)
                if (BCryptCreateHash(hProvider, &ctx, NULL, 0, NULL, 0, 0) == STATUS_SUCCESS && ctx != NULL)
                {
                    auto hash = std::vector<BYTE>(32);
                    std::stringstream stream;
                    BCryptHashData(ctx, filebuf.data(), exeFilesize, 0);
                    BCryptFinishHash(ctx, hash.data(), 32, 0);
                    BCryptDestroyHash(ctx);
                    BCryptCloseAlgorithmProvider(hProvider, 0);

                    for (auto& i : hash)
                        stream << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(i);

                    exeHash = stream.str();
                }
    }

    CloseHandle(hFile);
}

std::string getDatetime(bool printDate, bool printTime, bool printMs)
{
    SYSTEMTIME systime;
    GetSystemTime(&systime);
    std::stringstream ss;
    std::string ms;

    if (printMs)
        ms += "." + std::to_string(systime.wMilliseconds);

    if (printDate)
    {
        ss << systime.wDay << "/" << systime.wMonth << "/" << systime.wYear;

        if (!printTime)
            return ss.str();

        ss << " ";
    }

    ss << std::setfill('0') << std::setw(2) << systime.wHour << ":"
       << std::setfill('0') << std::setw(2) << systime.wMinute << ":"
       << std::setfill('0') << std::setw(2) << systime.wSecond << ms;

    return ss.str();
}

void getLoadedModules()
{
    modulesSet.clear();

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
                    loadedMods.openLimitAdjuster = true;
                else if (strcasestr(szModName, "fastman92limitAdjuster"))
                {
                    loadedMods.fastman92LimitAdjuster = true;

                    if (!modInitialized)
                    {
                        std::string flaIniPath = szModName;
                        flaIniPath = flaIniPath.substr(0, flaIniPath.find_last_of("\\/")) + "\\" + "fastman92limitAdjuster_GTASA.ini";

                        DataReader flaIni(flaIniPath);
                        int flaMaxID = flaIni.ReadInteger("ID LIMITS", "Count of killable model IDs", -1);
                        if (maxPedID == 0)
                            maxPedID = flaMaxID;

                        Log::Write("Enable special features = %d\n", flaIni.ReadInteger("VEHICLE SPECIAL FEATURES", "Enable special features", -1));
                        Log::Write("Apply ID limit patch = %d\n", flaIni.ReadInteger("ID LIMITS", "Apply ID limit patch", -1));
                        Log::Write("Count of killable model IDs = %d\n", flaMaxID);
                        Log::Write("Enable model special feature loader = %d\n", flaIni.ReadInteger("ADDONS", "Enable model special feature loader", -1));                                        
                    }
                }   
#ifdef _DEBUG
                assert(!compareUpper("ModelVariations.asi", getFilenameFromPath(szModName).c_str()));
#endif

                modulesSet.insert(std::make_pair((std::uintptr_t)modules[i], szModName));
            }
        }
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
    iniSettings.SetIniPath(iniSettings.GetIniPath());

    enablePeds = iniSettings.ReadBoolean("Settings", "EnablePeds", false);
    enableVehicles = iniSettings.ReadBoolean("Settings", "EnableVehicles", false);
    enablePedWeapons = iniSettings.ReadBoolean("Settings", "EnablePedWeapons", false);

    if (enablePeds)
    {
        if (!modInitialized)
            enableSpecialPeds = iniSettings.ReadBoolean("Settings", "EnableSpecialPeds", false);

        if (enableSpecialPeds && !loadedMods.fastman92LimitAdjuster && !loadedMods.openLimitAdjuster)
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
        VehicleVariations::LoadData(exePath.substr(0, exePath.find_last_of("/\\")));
}

void updateVariations()
{
    //zInfo->m_szTextKey = BLUEB | zInfo->m_szLabel = BLUEB1

    auto isPlayerInZone = [](const char* zoneName)
    {
        if (FindPlayerPed() != NULL)
        {
            CVector position = FindPlayerCoors(-1);
            return CTheZones::FindZone(&position, (*(int32_t*)zoneName), (*(int32_t*)(zoneName + 4)), ZONE_TYPE_NAVI);
        }
        return false;
    };

    currentTown = CTheZones::m_CurrLevel;
    if (currentTown == LEVEL_NAME_COUNTRY_SIDE)
    {
        //COUNTRY_LA
        if (isPlayerInZone("BLUEB"))
            currentTown = 9;
        else if (isPlayerInZone("MONT"))
            currentTown = 10;
        else if (isPlayerInZone("DILLI"))
            currentTown = 11;
        else if (isPlayerInZone("PALO"))
            currentTown = 12;
        else if (isPlayerInZone("RED"))
            currentTown = 8;
        //COUNTRY_SF
        else if (isPlayerInZone("ANGPI"))
            currentTown = 15;
        else if (isPlayerInZone("FLINTC"))
            currentTown = 13;
        else if (isPlayerInZone("WHET"))
            currentTown = 14;
        //COUNTRY_LV
        else if (isPlayerInZone("ROBAD"))
            currentTown = 6;
        else if (isPlayerInZone("BONE"))
            currentTown = 7;
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
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template <std::uintptr_t address>
void __cdecl CGame__ShutdownHooked()
{
    Log::Write("Game shutting down...\n");

    if (!addedIDs.empty())
    {
        CPedModelInfo* start = reinterpret_cast<CPedModelInfo*>(0xB478FC);
        for (int i = 0; i < 278; i++)
            start[i].m_pHitColModel = NULL;
    }

    callOriginal<address>();

    Log::Write("Shutdown ok.\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////  MAIN   ///////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void initialize()
{
    getLoadedModules();

    loadIniData();

    if (enablePeds)
    {
        Log::Write("Installing ped hooks...\n");
        PedVariations::InstallHooks(enableSpecialPeds, loadedMods.fastman92LimitAdjuster);
        Log::Write("Ped hooks installed.\n");
    }

    if (enableVehicles)
    {
        Log::Write("Installing vehicle hooks...\n");
        VehicleVariations::InstallHooks();
        Log::Write("Vehicle hooks installed.\n");
    }

    hookCall(0x748E6B, CGame__ShutdownHooked<0x748E6B>, "CGame::Shutdown");

    Log::Write("\nLoaded modules:\n");

    for (const auto& i : modulesSet)
        Log::Write("0x%08X %s\n", i.first, i.second.c_str());

    Log::Write("\n");

    modInitialized = true;
}

class ModelVariations {
public:
    ModelVariations() {

        iniSettings.SetIniPath(dataFileName);

        forceEnable = iniSettings.ReadBoolean("Settings", "ForceEnable", false);
        loadStage = iniSettings.ReadInteger("Settings", "LoadStage", 1);
        disableKey = (unsigned int)iniSettings.ReadInteger("Settings", "DisableKey", 0);
        reloadKey = (unsigned int)iniSettings.ReadInteger("Settings", "ReloadKey", 0);
        enableLog = iniSettings.ReadBoolean("Settings", "EnableLog", false);

        if (enableLog)
        {
            enableLog = Log::Open("ModelVariations.log");

            detectExe();

            std::string windowsVersion;
            char str[255] = {};
            DWORD cbData = 254;
            HKEY hkey;

            if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS)
            {
                if (RegQueryValueEx(hkey, "CurrentBuild", NULL, NULL, (LPBYTE)str, &cbData) == ERROR_SUCCESS)
                {
                    windowsVersion += "OS build ";
                    windowsVersion += str;
                    windowsVersion += " ";
                }
                RegCloseKey(hkey);
            }

            if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS)
            {
                if (RegQueryValueEx(hkey, "PROCESSOR_ARCHITECTURE", NULL, NULL, (LPBYTE)str, &cbData) == ERROR_SUCCESS)
                    windowsVersion += str;
                RegCloseKey(hkey);
            }

            Log::Write("Model Variations %s\n%s\n%s\n\n%s\n", MOD_VERSION DEBUG_STRING, windowsVersion.c_str(), getDatetime(true, true, false).c_str(), exePath.c_str());

            if (GetGameVersion() == GAME_10US_HOODLUM)
                Log::Write("Supported exe detected: 1.0 US HOODLUM\n");
            else if (GetGameVersion() == GAME_10US_COMPACT)
                Log::Write("Supported exe detected: 1.0 US Compact\n");
            else
                Log::Write("Unsupported exe detected: %s %u bytes %s\n", exeName.c_str(), exeFilesize, exeHash.c_str());
            
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            Log::Write("lpMaximumApplicationAddress = 0x%X\n", si.lpMaximumApplicationAddress);

            if (GetFileAttributes(dataFileName) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
                Log::Write("\n%s not found!\n\n", dataFileName);
            else
                Log::Write("#########################\n"
                           "## ModelVariations.ini ##\n"
                           "#########################\n%s\n", Log::FileToString(dataFileName).c_str());

            PedVariations::LogDataFile();
            PedWeaponVariations::LogDataFile();
            VehicleVariations::LogDataFile();
            Log::Write("\n");
        }

        if (loadStage == 0)
            initialize();

        Events::initRwEvent += []
        {
            if (loadStage == 1)
                initialize();
        };

        Events::shutdownRwEvent += []
        {
            Log::Close();
        };

        Events::initScriptsEvent.after += []
        {
            if (!modInitialized && loadStage == 2)
                initialize();

            assert(modInitialized);

            Log::Write("-- initScriptsEvent (%s) --\n", getDatetime(false, true, true).c_str());

            clearEverything();

            loadIniData();

            if (enablePeds)
                PedVariations::LogVariations();

            if (enableVehicles)
            {
                Log::Write("\n");
                VehicleVariations::LogVariations();
            }

            Log::Write("\n\n");

            PedVariations::ProcessDrugDealers(true);
            framesSinceCallsChecked = 900;

            getLoadedModules();

            if (checkForUpdate())
                timeUpdate = clock();
            else
                timeUpdate = -1;
        };

        Events::pedCtorEvent += [](CPed* ped)
        {
            PedVariations::AddToStack(ped);
            PedWeaponVariations::AddToStack(ped);
        };

        Events::vehicleCtorEvent += [](CVehicle* veh)
        {
            VehicleVariations::AddToStack(veh);
        };

        Events::gameProcessEvent += []
        {
            CVector pPos = FindPlayerCoors(-1);
            CZone* zInfo = NULL;
            CTheZones::GetZoneInfo(&pPos, &zInfo);
            const CWanted* wanted = FindPlayerWanted(-1);
            const CPlayerPed* player = FindPlayerPed();

            const auto logVariationChange = [zInfo, wanted](const char* msg)
            {
                Log::Write("\n");
                Log::Write("%s (%s). Updating variations...\n", msg, getDatetime(false, true, true).c_str());
                Log::Write("currentWanted = %u wanted->m_nWantedLevel = %u\n", currentWanted, wanted->m_nWantedLevel);
                Log::Write("currentZone = %s zInfo->m_szLabel = %s\n", currentZone, zInfo->m_szLabel);

                if (currentInterior[0] != 0 || lastInterior[0] != 0)
                    Log::Write("currentInterior = %s lastInterior = %s\n", currentInterior, lastInterior);

                Log::Write("\n");
            };

            if (timeUpdate > -1 && ((clock() - timeUpdate) / CLOCKS_PER_SEC > 6))
            {
                printMessage("~y~Model Variations~s~: Update available.", 4000);
                timeUpdate = -1;
            }

            if (disableKey > 0 && KeyPressed(disableKey))
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
            else if (reloadKey > 0 && KeyPressed(reloadKey))
            {
                if (!keyDown)
                {
                    keyDown = true;
                    Log::Write("Reloading settings...\n");
                    clearEverything();
                    loadIniData();
                    updateVariations();
                    logVariationChange("Settings reloaded.");
                    printMessage("~y~Model Variations~s~: Settings reloaded.", 2000);
                }
            }
            else
                keyDown = false;

            if (framesSinceCallsChecked < 1000)
                framesSinceCallsChecked++;

            if (framesSinceCallsChecked == 1000)
            {
                if (enableLog)
                {
                    for (auto it : hookedCalls)
                    {
                        const std::uintptr_t functionAddress = (it.second.isVTableAddress == false) ? injector::GetBranchDestination(it.first).as_int() : *reinterpret_cast<unsigned int*>(it.first);
                        std::pair<std::uintptr_t, std::string> moduleInfo = { modulesSet.begin()->first, modulesSet.begin()->second };

                        for (auto it2 = modulesSet.begin(); it2 != modulesSet.end(); it2++)
                        {
                            if (it2->first > functionAddress)
                                break;

                            moduleInfo.first = it2->first;
                            moduleInfo.second = it2->second;
                        }
                        std::string moduleName = moduleInfo.second.substr(moduleInfo.second.find_last_of("/\\") + 1);

                        if (compareUpper(moduleName.c_str(), MOD_NAME) == false && callChecks.find({ it.first , moduleName }) == callChecks.end())
                        {
                            callChecks.insert({ it.first, moduleName });
                            if (functionAddress > 0)
                                Log::Write("Modified call found: %s 0x%08X 0x%08X %s 0x%08X\n", it.second.name.data(), it.first, functionAddress, moduleName.c_str(), moduleInfo.first);
                            else
                                Log::Write("Modified call found: %s 0x%08X\n", it.second.name.data(), it.first);
                        }
                    }

                    framesSinceCallsChecked = 0;
                }
                else
                    framesSinceCallsChecked = 1001;
            }

            if (player && player->m_pEnex)
                currentInterior = reinterpret_cast<const char*>(player->m_pEnex);
            else 
                currentInterior = "";

            if (strncmp(currentInterior, lastInterior, 7) != 0)
            {
                logVariationChange("Interior changed");

                strncpy(lastInterior, currentInterior, 7);
                updateVariations();
            }

            if (wanted && wanted->m_nWantedLevel != currentWanted)
            {
                logVariationChange("Wanted level changed");

                currentWanted = wanted->m_nWantedLevel;
                updateVariations();
            }

            if (zInfo && strncmp(zInfo->m_szLabel, currentZone, 7) != 0 && strncmp(zInfo->m_szLabel, "SAN_AND", 7) != 0)
            {
                logVariationChange("Zone changed");

                strncpy(currentZone, zInfo->m_szLabel, 7);
                updateVariations();
            }

            if (enablePeds) PedVariations::Process();
            if (enablePedWeapons) PedWeaponVariations::Process();
            if (enableVehicles) VehicleVariations::Process();
        };

    }
} modelVariations;
