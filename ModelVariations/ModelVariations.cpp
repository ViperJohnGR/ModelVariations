#include <plugin.h>
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LogUtil.hpp"

#include "Peds.hpp"
#include "PedWeapons.hpp"
#include "Vehicles.hpp"

#include <extensions/ScriptCommands.h>

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
#include <urlmon.h>

#include <shlwapi.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment (lib, "shlwapi.lib")

using namespace plugin;

std::string exeHash;
unsigned int exeFilesize = 0;
std::string exePath;
std::string exeName;

std::map<unsigned int, hookinfo> hookedCalls;

std::set<unsigned int> modifiedAddresses;
std::set<std::pair<unsigned int, std::string>> callChecks;
std::set<std::pair<unsigned int, std::string>> modulesSet;

std::vector<unsigned short> addedIDs;
std::vector<unsigned short> unusedIDs;

DataReader iniSettings("ModelVariations.ini");

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
bool unusedIDsChecked = false;

int timeUpdate = -1;

//INI Options
bool enableLog = false;
bool enablePeds = false;
bool enableSpecialPeds = false;
bool enableVehicles = false;
bool enablePedWeapons = false;
unsigned int disableKey = 0;
unsigned int reloadKey = 0;


bool checkForUpdate()
{
    const auto funcFail = []() {
        if (logfile.is_open())
            logfile << "Check for updates failed." << std::endl;

        return false;
    };

    IStream* stream;

    if (URLOpenBlockingStream(0, "http://api.github.com/repos/ViperJohnGR/ModelVariations/tags", &stream, 0, 0) != S_OK)
        return funcFail();

    std::string str(51, 0);
    if (stream->Read(&str[0], 50, NULL) != S_OK)
        return funcFail();

    stream->Release();
    str = str.substr(str.find("\"name\":\"v")+9, 10);
    str.erase(str.find('"'));
    for (auto ch : str)
        if ( !((ch >= '0' && ch <= '9') || (ch == '.')) )
        {
            if (logfile.is_open())
                logfile << "Check for updates failed. Invalid version string." << std::endl;

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
    const char* name = PathFindFileName(path);
    exeName = name;

    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        exeFilesize = GetFileSize(hFile, NULL);
        CloseHandle(hFile);
    }

    if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
        return;

    if (exeHash.empty())
        exeHash = hashFile(path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void clearEverything()
{
    iniSettings.data.clear();

    PedVariations::ClearData();
    PedWeaponVariations::ClearData();
    VehicleVariations::ClearData();
}

void loadIniData(bool firstTime)
{
    iniSettings.SetIniPath(iniSettings.GetIniPath());

    enablePeds = iniSettings.ReadBoolean("Settings", "EnablePeds", false);
    enableVehicles = iniSettings.ReadBoolean("Settings", "EnableVehicles", false);
    enablePedWeapons = iniSettings.ReadBoolean("Settings", "EnablePedWeapons", false);

    if (enablePeds)
    {
        if (firstTime)
            enableSpecialPeds = iniSettings.ReadBoolean("Settings", "EnableSpecialPeds", false);

        if (enableSpecialPeds && !loadedMods.fastman92LimitAdjuster && !loadedMods.openLimitAdjuster)
        {
            enableSpecialPeds = false;
            if (firstTime)
                MessageBox(NULL, "No limit adjuster found! EnableSpecialPeds will be disabled.", "Model Variations", MB_ICONWARNING);
        }

        PedVariations::LoadData();
    }

    if (enablePedWeapons)
        PedWeaponVariations::LoadData();

    if (enableVehicles)
        VehicleVariations::LoadData(firstTime, exePath.substr(0, exePath.find_last_of("/\\")));
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

    if (logfile.is_open())
    {
        if (enablePeds)
            PedVariations::LogCurrentVariations();

        if (enableVehicles)
        {
            logfile << "\n";
            VehicleVariations::LogCurrentVariations();
        }

        logfile << "\n" << std::endl;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template <unsigned int address>
void __cdecl CGame__ShutdownHooked()
{
    if (logfile.is_open())
        logfile << "Game shutting down..." << std::endl;

    if (enableSpecialPeds)
    {
        CPedModelInfo* start = reinterpret_cast<CPedModelInfo*>(0xB478FC);
        for (int i = 0; i < 278; i++)
            start[i].m_pHitColModel = NULL;
    }

    callOriginal<address>();

    if (logfile.is_open())
        logfile << "Shutdown ok." << std::endl;
}

class ModelVariations {
public:
    ModelVariations() {

        iniSettings.SetIniPath(iniSettings.GetIniPath());

        disableKey = (unsigned int)iniSettings.ReadInteger("Settings", "DisableKey", 0);
        reloadKey = (unsigned int)iniSettings.ReadInteger("Settings", "ReloadKey", 0);

        if ((enableLog = iniSettings.ReadBoolean("Settings", "EnableLog", false)) == true)
        {
            logfile.open("ModelVariations.log");

            if (logfile.is_open())
            {
                const char *debugString = "";
#ifdef _DEBUG
                debugString = " DEBUG";
#endif

                logfile << "Model Variations " MOD_VERSION << debugString << "\n" << getWindowsVersion() << "\n"
                        << getDatetime(true, true, false) << "\n\n";

                detectExe();
                logfile << exePath << std::endl;

                if (GetGameVersion() == GAME_10US_HOODLUM)
                    logfile << "Supported exe detected: 1.0 US HOODLUM" << std::endl;
                else if (GetGameVersion() == GAME_10US_COMPACT)
                    logfile << "Supported exe detected: 1.0 US Compact" << std::endl;
                else
                    logfile << "Unsupported exe detected: " << exeName << " " << exeFilesize << " bytes " << exeHash << std::endl;
            }
            else
                enableLog = false;
        }

        if (logfile.is_open())
        {
            PedVariations::LogDataFile();
            PedWeaponVariations::LogDataFile();
            VehicleVariations::LogDataFile();
            logfile << std::endl;
        }

        Events::initRwEvent += []
        {
            getLoadedModules(loadedMods.openLimitAdjuster, loadedMods.fastman92LimitAdjuster);

            loadIniData(true);

            if (enablePeds)
            {
                if (logfile.is_open())
                    logfile << "Installing ped hooks..." << std::endl;

                PedVariations::InstallHooks(enableSpecialPeds, loadedMods.fastman92LimitAdjuster);

                if (logfile.is_open())
                    logfile << "Ped hooks installed." << std::endl;
            }

            if (enableVehicles)
            {
                if (logfile.is_open())
                    logfile << "Installing vehicle hooks..." << std::endl;

                VehicleVariations::InstallHooks();

                if (logfile.is_open())
                    logfile << "Vehicle hooks installed." << std::endl;
            }

            hookCall(0x748E6B, CGame__ShutdownHooked<0x748E6B>, "CGame::Shutdown");

            if (logfile.is_open())
            {
                logfile << "\nLoaded modules:" << std::endl;

                for (const auto& i : modulesSet)
                    logfile << "0x" << std::setfill('0') << std::setw(8) << std::hex << std::uppercase << i.first << " " << i.second << "\n";
                logfile << std::endl;
            }
        };

        Events::initScriptsEvent.after += []
        {
            if (logfile.is_open())
                logfile << "-- initScriptsEvent --" << std::endl;

            clearEverything();

            if (!unusedIDsChecked && enableSpecialPeds)
            {
                int numUnused = 0;

                if (logfile.is_open())
                    logfile << "Checking unused IDs...\n";

                for (uint16_t i = 1326; i < 20000; i++)
                    if (CModelInfo::GetModelInfo(i) == NULL)
                    {
                        unusedIDs.push_back(i);
                        numUnused++;
                    }

                if (logfile.is_open())
                    logfile << "Unused IDs found: " << std::dec << numUnused << std::endl;

                unusedIDsChecked = true;
            }

            loadIniData(false);
            if (logfile.is_open())
            {
                if (enablePeds)
                    PedVariations::LogVariations();

                if (enableVehicles)
                {
                    logfile << "\n";
                    VehicleVariations::LogVariations();
                }

                logfile << "\n" << std::endl;
            }

            PedVariations::ProcessDrugDealers(true);
            framesSinceCallsChecked = 900;

            if (logfile.is_open())
                getLoadedModules(loadedMods.openLimitAdjuster, loadedMods.fastman92LimitAdjuster);

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
                if (logfile.is_open())
                {
                    logfile << "\n";
                    logfile << msg << " (" << getDatetime(false, true, true) << "). Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << "\n";
                    if (currentInterior[0] != 0 || lastInterior[0] != 0)
                        logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n";

                    logfile << std::endl;                    
                }
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
                    if (logfile.is_open())
                        logfile << "Disabling mod... ";
                    clearEverything();
                    if (logfile.is_open())
                        logfile << "OK" << std::endl;
                }
            }
            else if (reloadKey > 0 && KeyPressed(reloadKey))
            {
                if (!keyDown)
                {
                    keyDown = true;
                    if (logfile.is_open())
                        logfile << "Reloading settings..." << std::endl;
                    clearEverything();
                    loadIniData(false);
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
                if (logfile.is_open())
                {
                    checkAllCalls();
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
            if (enablePedWeapons) PedWeaponVariations::Process(currentZone);
            if (enableVehicles) VehicleVariations::Process();
        };

    }
} modelVariations;
