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
#include <CLoadedCarGroup.h>
#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CPopulation.h>
#include <CTheZones.h>
#include <CVector.h>

#include <array>
#include <future>
#include <iomanip>
#include <map>
#include <set>
#include <stack>

#include <ntstatus.h>
#include <urlmon.h>

#pragma comment (lib, "bcrypt.lib")
#pragma comment (lib, "urlmon.lib")


#define MOD_VERSION "9.6"
#ifdef _DEBUG
#define MOD_NAME "ModelVariations_d.asi"
#define DEBUG_STRING " DEBUG"
#else
#define MOD_NAME "ModelVariations.asi"
#define DEBUG_STRING ""
#endif


using namespace plugin;

struct jumpInfo {
    std::uintptr_t address;
    std::uintptr_t destination;
    unsigned char type;
};

const std::pair<std::string, unsigned> areas[] = { {"Countryside", 0},
                                                   {"LosSantos", 0},
                                                   {"SanFierro", 0},
                                                   {"LasVenturas", 0},
                                                   {"Global", 0},
                                                   {"Desert", 0},
                                                   {"TierraRobada", 5},
                                                   {"BoneCounty", 5},
                                                   {"RedCounty", 0},
                                                   {"Blueberry", 8},
                                                   {"Montgomery", 8},
                                                   {"Dillimore", 8},
                                                   {"PalominoCreek", 8},
                                                   {"FlintCounty", 0},
                                                   {"Whetstone", 0},
                                                   {"AngelPine", 14} };

std::string exeHash;
unsigned int exeFilesize = 0;
std::string exePath;
std::string exeName;

std::unordered_map<std::uintptr_t, std::string> hooksASM;
std::unordered_map<std::uintptr_t, hookinfo> hookedCalls;

std::set<std::pair<std::uintptr_t, std::string>> callChecks;
std::set<unsigned short> referenceCountModels;
std::set<std::string> zones;

std::vector<unsigned short> addedIDs;
int maxPedID = 0;

static const char* dataFileName = "ModelVariations.ini";
DataReader iniSettings(dataFileName);

short framesSinceCallsChecked = 1001;
char lastInterior[8] = {};
const char* currentInterior = lastInterior;
char currentZone[8] = {};
unsigned int currentTown = 0;
unsigned int currentWanted = 0;
int isFLASpecialFeaturesEnabled = 0;

bool jumpsLogged = false;
bool keyDown = false;

bool zonesRead = false;
bool reloadingSettings = false;
bool queuedReload = false;

int timeUpdate = -1;

int flaMaxID = -1;

//INI Options
bool enableLog = false;
bool logJumps = false;
bool enablePeds = false;
bool enableSpecialPeds = false;
bool enableVehicles = false;
bool enablePedWeapons = false;
bool forceEnable = false;
bool loadSettingsImmediately = false;
int loadStage = 1;
int trackReferenceCounts = -1;
unsigned int disableKey = 0;
unsigned int reloadKey = 0;

bool modInitialized = false;

std::future<void> future;


bool checkForUpdate()
{
    IStream* stream;

    if (URLOpenBlockingStream(0, "http://api.github.com/repos/ViperJohnGR/ModelVariations/tags", &stream, 0, 0) != S_OK)
    {
        Log::Write("Check for updates failed. Cannot open connection.\n");
        return false;
    }

    std::string str(51, 0);
    if (stream->Read(&str[0], 50, NULL) != S_OK)
    {
        Log::Write("Check for updates failed.\n");
        return false;
    }

    stream->Release();
    auto versionStringPos = str.find("\"name\":\"v") + 9;
    if (versionStringPos >= str.size())
    {
        Log::Write("Check for updates failed. Invalid version string.\n");
        return false;
    }

    str = str.substr(versionStringPos, 10);
    str.erase(str.find('"'));
    for (auto ch : str)
        if ((ch < '0' || ch > '9') && ch != '.')
        {
            Log::Write("Check for updates failed. Invalid version number.\n");
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

    if (exeFilesize != INVALID_FILE_SIZE)
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


bool LoadPESection(const char* filePath, int section, std::vector<unsigned char>& buffer, unsigned int* size) {
    HANDLE hFile;
    HANDLE hFileMapping;
    LPVOID mapView;
    PIMAGE_DOS_HEADER dosHeader;
    PIMAGE_NT_HEADERS ntHeaders;
    PIMAGE_SECTION_HEADER sectionHeader;

    auto functionError = [&](const char* msg, int errorType)
    {
        std::cerr << msg << std::endl;
        switch (errorType)
        {
            case 3:
                UnmapViewOfFile(mapView);
            case 2:
                CloseHandle(hFileMapping);
            case 1:
                CloseHandle(hFile);
        }

        return false;
    };

    hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) 
    {
        std::cerr << "Failed to open file" << std::endl;
        return false;
    }

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
            break;
        }
        sectionHeader++;
    }

    // Clean up resources
    UnmapViewOfFile(mapView);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);

    return true;
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
    iniSettings.SetIniPath(dataFileName);

    enablePeds = iniSettings.ReadBoolean("Settings", "EnablePeds", false);
    enableVehicles = iniSettings.ReadBoolean("Settings", "EnableVehicles", false);
    enablePedWeapons = iniSettings.ReadBoolean("Settings", "EnablePedWeapons", false);

    if (enablePeds)
    {
        if (!modInitialized)
            enableSpecialPeds = iniSettings.ReadBoolean("Settings", "EnableSpecialPeds", false);

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
}

void updateVariations()
{
    //zInfo->m_szTextKey = BLUEB | zInfo->m_szLabel = BLUEB1

    CVector position = FindPlayerCoors(-1);

    auto isPlayerInZone = [&position](const char* zoneName)
    {
        return CTheZones::FindZone(&position, (*(int32_t*)zoneName), (*(int32_t*)(zoneName + 4)), ZONE_TYPE_NAVI);
    };

    currentTown = CTheZones::m_CurrLevel;
    if (currentTown == LEVEL_NAME_COUNTRY_SIDE)
    {
        if (position.y > 595) //COUNTRY_LV
        {
            if (isPlayerInZone("ROBAD"))
                currentTown = 6;
            else if (isPlayerInZone("BONE"))
                currentTown = 7;
        }
        else if (position.y > -770) //COUNTRY_LA
        {
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
        }

        if (currentTown == LEVEL_NAME_COUNTRY_SIDE) //COUNTRY_SF
        {
            if (isPlayerInZone("ANGPI"))
                currentTown = 15;
            else if (isPlayerInZone("FLINTC"))
                currentTown = 13;
            else if (isPlayerInZone("WHET"))
                currentTown = 14;
        }
    }

    Log::Write("CTheZones::m_CurrLevel = %d currentTown = %d\n", CTheZones::m_CurrLevel, currentTown);
    Log::Write("CStreaming::ms_pedsLoaded: ");
    for (int i = 0; i < CStreaming__ms_numPedsLoaded; i++)
    {
        Log::Write("%d ", CStreaming__ms_pedsLoaded[i]);
    }
    Log::Write("\nCStreaming::ms_vehiclesLoaded: ");
    for (unsigned i = 0; i < CStreaming__ms_vehiclesLoaded->CountMembers(); i++)
    {
        Log::Write("%d ", CStreaming__ms_vehiclesLoaded->m_members[i]);
    }
    Log::Write("\n\n");

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

//Fix for crash on game exit when adding special peds. Something related to m_pHitColModel.
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

    callOriginal<address>();

    Log::Write("Shutdown ok.\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////  MAIN   ///////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void initialize()
{
    LoadedModules::Refresh();

    auto flaModule = LoadedModules::GetModule("fastman92limitAdjuster", false);
    if (!flaModule.first.empty())
    {
        std::string flaIniPath = flaModule.first;
        flaIniPath.replace(flaIniPath.find_last_of("\\/"), std::string::npos, "\\fastman92limitAdjuster_GTASA.ini");

        DataReader flaIni(flaIniPath);
        flaMaxID = flaIni.ReadInteger("ID LIMITS", "Count of killable model IDs", -1);
        if (maxPedID == 0)
            maxPedID = flaMaxID;

        isFLASpecialFeaturesEnabled = flaIni.ReadInteger("ADDONS", "Enable model special feature loader", -1);

        Log::Write("\nFLA settings:\n");
        Log::Write("Enable special features = %d\n", flaIni.ReadInteger("VEHICLE SPECIAL FEATURES", "Enable special features", -1));
        Log::Write("Apply ID limit patch = %d\n", flaIni.ReadInteger("ID LIMITS", "Apply ID limit patch", -1));
        Log::Write("Count of killable model IDs = %d\n", flaMaxID);
        Log::Write("Enable model special feature loader = %d\n\n", isFLASpecialFeaturesEnabled);
    }

    auto olaModule = LoadedModules::GetModule("III.VC.SA.LimitAdjuster.asi");
    if (!olaModule.first.empty())
    {
        std::string olaIniPath = olaModule.first;
        olaIniPath.replace(olaIniPath.find_last_of("\\/"), std::string::npos, "\\III.VC.SA.LimitAdjuster.ini");

        DataReader olaIni(olaIniPath);
        Log::Write("PedModels limit in OLA is %s\n\n", olaIni.ReadString("SALIMITS", "PedModels", "").c_str());
    }

    loadIniData();

    if (enablePeds)
    {
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

    hookCall(0x748E6B, CGame__ShutdownHooked<0x748E6B>, "CGame::Shutdown");

    Log::Write("\nLoaded modules:\n");

    LoadedModules::Log();

    Log::Write("\n");

    modInitialized = true;
}

class ModelVariations {
public:
    ModelVariations() {

        iniSettings.SetIniPath(dataFileName);

        trackReferenceCounts = iniSettings.ReadInteger("Settings", "TrackReferenceCounts", -1);
        loadSettingsImmediately = iniSettings.ReadBoolean("Settings", "LoadSettingsImmediately", true);
        forceEnable = iniSettings.ReadBoolean("Settings", "ForceEnable", false);
        loadStage = iniSettings.ReadInteger("Settings", "LoadStage", 1);
        disableKey = (unsigned int)iniSettings.ReadInteger("Settings", "DisableKey", 0);
        reloadKey = (unsigned int)iniSettings.ReadInteger("Settings", "ReloadKey", 0);
        enableLog = iniSettings.ReadBoolean("Settings", "EnableLog", false);
        logJumps = iniSettings.ReadBoolean("Settings", "LogJumps", false);

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

            SYSTEM_INFO nsi;
            GetNativeSystemInfo(&nsi);

            if (nsi.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
                windowsVersion += " (32-bit processor)";
            else if (nsi.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
                windowsVersion += " (64-bit processor)";


            Log::Write("Model Variations %s\n%s\n%s\n\n%s\n", MOD_VERSION DEBUG_STRING, windowsVersion.c_str(), getDatetime(true, true, false).c_str(), exePath.c_str());

            if (GetGameVersion() == GAME_10US_HOODLUM)
                Log::Write("Supported exe detected: 1.0 US HOODLUM | %u bytes | %s\n", exeFilesize, exeHash.c_str());
            else if (GetGameVersion() == GAME_10US_COMPACT)
                Log::Write("Supported exe detected: 1.0 US Compact | %u bytes | %s\n", exeFilesize, exeHash.c_str());
            else
                Log::Write("Unsupported exe detected: %u bytes | %s\n", exeFilesize, exeHash.c_str());
            
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            Log::Write("lpMaximumApplicationAddress = 0x%X\n", si.lpMaximumApplicationAddress);

            if (GetFileAttributes(dataFileName) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
                Log::Write("\n%s not found!\n\n", dataFileName);
            else
                Log::Write("#########################\n"
                           "## ModelVariations.ini ##\n"
                           "#########################\n%s\n", fileToString(dataFileName).c_str());

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

        auto gameLoadEvent = []
        {
            if (!loadSettingsImmediately && reloadingSettings)
            {
                queuedReload = true;
                return;
            }

            auto startTime = clock();

            if (!modInitialized && loadStage == 2)
                initialize();

            assert(modInitialized);

            Log::Write("-- gameLoadEvent (%s) --\n", getDatetime(false, true, true).c_str());

            if (!zonesRead)
            {
                Log::Write("\nReading zone data...\n");
                for (int i = 0; i < CTheZones::TotalNumberOfInfoZones; i++)
                {
                    char zoneLabel[9] = {};
                    memcpy(&zoneLabel[0], (char*)(CTheZones__NavigationZoneArray) + i * 0x20, 8);

                    //CZone* zone = reinterpret_cast<CZone*>((char*)(CTheZones__NavigationZoneArray)+i * 0x20);

                    zones.insert(zoneLabel);
                }
                zonesRead = true;

                Log::Write("Finished reading %u zones.\n", zones.size());
            }

            clearEverything();
            PedVariations::ProcessDrugDealers(true);
            LoadedModules::Refresh();

            if (enableLog)
                framesSinceCallsChecked = 900;

            *reinterpret_cast<uint64_t*>(currentZone) = 0;
            lastInterior[0] = 0;

            reloadingSettings = true;
            auto doAsyncStuff = [startTime] {
                loadIniData();

                if (enablePeds)
                    PedVariations::LogVariations();

                if (enableVehicles)
                {
                    Log::Write("\n");
                    VehicleVariations::LogVariations();
                }

                Log::Write("\n\n");

                if (checkForUpdate())
                    timeUpdate = clock();
                else
                    timeUpdate = -1;

                auto finalTime = clock() - startTime;
                if (finalTime < 1000)
                    Log::Write("Time spent loading: %dms.\n", finalTime);
                else
                    Log::Write("Time spent loading: %gs.\n", finalTime / 1000.0);

                reloadingSettings = false;
            };

            if (loadSettingsImmediately)
                doAsyncStuff();
            else
                future = std::async(std::launch::async, doAsyncStuff);
        };
        Events::initGameEvent += gameLoadEvent;
        Events::reInitGameEvent += gameLoadEvent;

        Events::pedCtorEvent += [](CPed* ped)
        {
            PedVariations::AddToStack(ped);
            PedWeaponVariations::AddToStack(ped);
        };

        Events::vehicleCtorEvent += [](CVehicle* veh)
        {
            VehicleVariations::AddToStack(veh);
        };

        Events::gameProcessEvent += [&gameLoadEvent]
        {
            if (reloadingSettings)
                return;

            if (queuedReload)
            {
                gameLoadEvent();
                queuedReload = false;
                return;
            }

            if (!jumpsLogged && logJumps)
            {
                Log::Write("\nLogging JMP hooks...\n");
                std::vector<unsigned char> buffer;

                const std::vector<int> sections = (GetGameVersion() == GAME_10US_HOODLUM) ? std::vector<int> { 0, 1, 7, 8, 9, 10 } : std::vector<int>{ 0, 1 };
                const std::vector<std::uintptr_t> validRanges = (GetGameVersion() == GAME_10US_HOODLUM) ? std::vector<std::uintptr_t> { 0x401000, 0x857000, 0xCB1000, 0x12FB000, 0x1301000, 0x1556000 } : std::vector<std::uintptr_t>{ 0x401000, 0x857000 };
                std::unordered_map<std::string, std::vector<jumpInfo>> jumpsMap;

                auto gta_saModule = LoadedModules::GetModule("gta_sa.exe");
                std::uintptr_t gta_saEndAddress = ((std::uintptr_t)gta_saModule.second.lpBaseOfDll + gta_saModule.second.SizeOfImage);
                unsigned int secCount = 0;

                for (auto& rangeStart : validRanges)
                {
                    unsigned int sectionSize;
                    LoadPESection("gta_sa.exe", sections[secCount++], buffer, &sectionSize);
                    for (unsigned int i = rangeStart; i < sectionSize+0x400000;i++)
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

                jumpsLogged = true;
            }

            CVector pPos = FindPlayerCoors(-1);
            CZone* zInfo = NULL;
            CTheZones::GetZoneInfo(&pPos, &zInfo);
            const CWanted* wanted = FindPlayerWanted(-1);
            const CPlayerPed* player = FindPlayerPed();

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

            auto logVariationChange = [zInfo, wanted](const char* msg)
            {
                Log::Write("\n%s (%s). Updating variations...\n", msg, getDatetime(false, true, true).c_str());
                Log::Write("currentWanted = %u wanted->m_nWantedLevel = %u\n", currentWanted, wanted->m_nWantedLevel);
                Log::Write("currentZone = %.8s zInfo->m_szLabel = %.8s\n", currentZone, zInfo->m_szLabel);

                if (currentInterior[0] != 0 || lastInterior[0] != 0)
                    Log::Write("currentInterior = %.8s lastInterior = %.8s\n", currentInterior, lastInterior);
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
                    reloadingSettings = true;
                    Log::Write("Reloading settings...\n");
                    clearEverything();
                    printMessage("~y~Model Variations~s~: Reloading settings...", 10000);
                    auto doAsyncStuff = [logVariationChange] {
                        loadIniData();
                        logVariationChange("Settings reloaded.");
                        updateVariations();
                        printMessage("~y~Model Variations~s~: Settings reloaded.", 2000);
                        reloadingSettings = false;
                    };

                    if (loadSettingsImmediately)
                        doAsyncStuff();
                    else
                        future = std::async(std::launch::async, doAsyncStuff);
                }
            }
            else
                keyDown = false;

            if (framesSinceCallsChecked < 1000)
                framesSinceCallsChecked++;

            if (framesSinceCallsChecked == 1000)
            {
                for (auto &it : hookedCalls)
                {
                    const std::uintptr_t functionAddress = (it.second.isVTableAddress == false) ? injector::GetBranchDestination(it.first).as_int() : *reinterpret_cast<unsigned int*>(it.first);
                    std::pair<std::string, MODULEINFO> moduleInfo = LoadedModules::GetModuleAtAddress(functionAddress);
                    std::string moduleName = moduleInfo.first.substr(moduleInfo.first.find_last_of("/\\") + 1);

                    if (_stricmp(moduleName.c_str(), MOD_NAME) != 0 && callChecks.insert({ it.first, moduleName }).second)
                    {
                        if (functionAddress > 0 && !moduleName.empty())
                            Log::Write("Modified call detected: %s 0x%08X 0x%08X %s 0x%08X\n", it.second.name.data(), it.first, functionAddress, moduleName.c_str(), moduleInfo.second.lpBaseOfDll);
                        else
                            Log::Write("Modified call detected: %s 0x%08X %s\n", it.second.name.data(), it.first, bytesToString(it.first, 5).c_str());
                    }

                    auto gta_saModule = LoadedModules::GetModule("gta_sa.exe");
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

                    if (_stricmp(moduleName.c_str(), MOD_NAME) != 0 && callChecks.insert({ it.first, moduleName }).second)
                    {
                        if (currentDestination > 0 && !moduleName.empty())
                            Log::Write("Modified ASM hook detected: %s 0x%08X 0x%08X %s 0x%08X\n", it.second.c_str(), it.first, currentDestination, moduleName.c_str(), moduleInfo.second.lpBaseOfDll);
                        else
                            Log::Write("Modified ASM hook detected: %s 0x%08X %s\n", it.second.c_str(), it.first, bytesToString(it.first, 5).c_str());
                    }
                }

                framesSinceCallsChecked = 0;
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

                *reinterpret_cast<uint64_t*>(currentZone) = 0;
                strncpy(currentZone, zInfo->m_szLabel, 7);
                updateVariations();
            }

            if (enablePeds) PedVariations::Process();
            if (enablePedWeapons) PedWeaponVariations::Process();
            if (enableVehicles) VehicleVariations::Process();
        };

    }
} modelVariations;
