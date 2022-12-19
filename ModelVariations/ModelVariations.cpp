#include <plugin.h>
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "LogUtil.hpp"
#include "Vehicles.hpp"
#include "Hooks.hpp"

#include <extensions/ScriptCommands.h>

#include <CMessages.h>
#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CPopulation.h>
#include <CStreaming.h>
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

constexpr int MAX_PED_ID = 300;

const char* pedIniPath("ModelVariations_Peds.ini");
const char* pedWepIniPath("ModelVariations_PedWeapons.ini");
const char* vehIniPath("ModelVariations_Vehicles.ini");
const char* settingsIniPath("ModelVariations.ini");

int16_t* destroyedModelCounters = NULL;

std::string exeHash;
unsigned int exeFilesize = 0;
std::string exePath;
std::string exeName;

std::vector<unsigned short> addedIDs;
std::vector<unsigned short> unusedIDs;

std::set<std::pair<unsigned int, std::string>> modulesSet;
std::set<std::pair<unsigned int, std::string>> callChecks;

DataReader iniPed;
DataReader iniWeap;
DataReader iniVeh;
DataReader iniSettings;

std::array<std::vector<unsigned short>, 16> pedVariations[MAX_PED_ID];
std::array<std::vector<unsigned short>, 6> pedWantedVariations[MAX_PED_ID];

std::map<unsigned int, hookinfo> hookedCalls;
std::map<unsigned short, int> pedTimeSinceLastSpawned;
std::map<unsigned short, std::vector<unsigned short>> pedOriginalModels;
std::map<unsigned short, std::string> wepPedModels;
std::map<unsigned short, std::string> wepVehModels;
std::map<unsigned short, std::string> pedModels;

std::set<unsigned short> dontInheritBehaviourModels;
std::set<unsigned short> pedMergeZones;

std::set<unsigned short> pedHasVariations;
std::set<unsigned int> modifiedAddresses;

std::stack<CPed*> pedStack;

std::vector<unsigned short> pedCurrentVariations[MAX_PED_ID];


bool unusedIDsChecked = false;
struct {
    bool fastman92LimitAdjuster = false;
    bool openLimitAdjuster = false;
} loadedMods;

BYTE dealersFrames = 0;
short framesSinceCallsChecked = 0;
unsigned short modelIndex = 0;
char lastInterior[8] = {};
const char* currentInterior = lastInterior;
char currentZone[8] = {};
char lastZone[8] = {};
unsigned int currentTown = 0;
int currentWanted = 0;

//INI Options
//General
bool enableLog = false;
bool enablePeds = false;
bool enableSpecialPeds = false;
bool enableVehicles = false;
bool enablePedWeapons = false;
unsigned int disableKey = 0;
unsigned int reloadKey = 0;
//Peds
bool useParentVoices = false;
bool enableCloneRemover = false;
bool cloneRemoverVehicleOccupants = false;
int cloneRemoverSpawnDelay = 3;
std::vector<unsigned short> cloneRemoverIncludeVariations;
std::vector<unsigned short> cloneRemoverExclusions;


bool keyDown = false;

int timeUpdate = -1;

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

bool isOnMission()
{
    return (CTheScripts::OnAMissionFlag && *(CTheScripts::ScriptSpace + CTheScripts::OnAMissionFlag));
}

bool pedDelaySpawn(unsigned short model, bool includeParentModels)
{
    if (!includeParentModels)
    {
        if (pedTimeSinceLastSpawned.find(model) != pedTimeSinceLastSpawned.end())
            return true;
    }
    else
    {
        auto it = pedOriginalModels.find(model);
        if (it != pedOriginalModels.end())
            for (auto &i : it->second)
                if (pedTimeSinceLastSpawned.find(i) != pedTimeSinceLastSpawned.end())
                    return true;
    }
    return false;
}

bool compareOriginalModels(unsigned short model1, unsigned short model2, bool includeVariations = false)
{
    if (model1 == model2)
        return true;

    if (includeVariations)
    {
        auto it1 = pedOriginalModels.find(model1);
        auto it2 = pedOriginalModels.find(model2);
        if (it1 != pedOriginalModels.end() && it2 != pedOriginalModels.end())
            return std::find_first_of(it1->second.begin(), it1->second.end(), it2->second.begin(), it2->second.end()) != it1->second.end();
        else
        {
            unsigned short model = 0;
            std::vector<unsigned short>* vec = NULL;
            if (it1 != pedOriginalModels.end())
            {
                model = model2;
                vec = &it1->second;
            }
            else if (it2 != pedOriginalModels.end())
            {
                model = model1;
                vec = &it2->second;
            }
            else
                return false;

            if (std::find(vec->begin(), vec->end(), model) != vec->end())
                return true;
        }
    }

    return false;
}

void filterWantedVariations(std::vector<unsigned short>& vec, std::vector<unsigned short>& wantedVec)
{
    bool matchFound = false;
    std::vector<unsigned short> vec2 = vec;

    auto it = vec.begin();
    while (it != vec.end())
        if (std::find(wantedVec.begin(), wantedVec.end(), *it) != wantedVec.end())
        {
            matchFound = true;
            ++it;
        }
        else
            it = vec.erase(it);

    if (matchFound == false)
        vec = vec2;
}

bool isValidPedId(int id)
{
    if (id <= 0 || id >= MAX_PED_ID)
        return false;
    if (id >= 190 && id <= 195)
        return false;

    return true;
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

void drugDealerFix()
{
    int id = 28;

    while (id < 255)
    {
        for (unsigned int i = 0; i < 16; i++)
            if (!pedVariations[id][i].empty())
                for (auto& j : pedVariations[id][i])
                    if (j > MAX_PED_ID)
                    {
                        if (logfile.is_open())
                            logfile << j << "\n";
                        CTheScripts::ScriptsForBrains.AddNewScriptBrain(CTheScripts::StreamedScripts.GetProperIndexFromIndexUsedByScript(19), (short)j, 100, 0, -1, -1.0);
                    }

        if (id < 30) id++;
        else if (id == 30) id = 254;
        else id = 255;
    }
}

void updateVariations(CZone* zInfo)
{
    //zInfo->m_szTextKey = BLUEB | zInfo->m_szLabel = BLUEB1

    if (zInfo == NULL)
        return;

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

    const CWanted* wanted = FindPlayerWanted(-1);

    if (enablePeds)
        for (auto& modelid : pedHasVariations)
        {
            pedCurrentVariations[modelid] = vectorUnion(pedVariations[modelid][4], pedVariations[modelid][currentTown]);

            std::string section;
            auto it = pedModels.find(modelid);
            if (it != pedModels.end())
                section = it->second;
            else
                section = std::to_string(modelid);

            std::vector<unsigned short> vec = iniPed.ReadLine(section, ((lastZone[0] == 0) ? zInfo->m_szLabel : lastZone), READ_PEDS);
            if (!vec.empty())
            {
                if (pedMergeZones.find(modelid) != pedMergeZones.end())
                    pedCurrentVariations[modelid] = vectorUnion(pedCurrentVariations[modelid], vec);
                else
                    pedCurrentVariations[modelid] = vec;
            }

            vec = iniPed.ReadLine(section, currentInterior, READ_PEDS);
            if (!vec.empty())
                pedCurrentVariations[modelid] = vectorUnion(pedCurrentVariations[modelid], vec);

            if (wanted)
            {
                const unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
                if (!pedWantedVariations[modelid][wantedLevel].empty() && !pedCurrentVariations[modelid].empty())
                    filterWantedVariations(pedCurrentVariations[modelid], pedWantedVariations[modelid][wantedLevel]);
            }
        }

    if (enableVehicles)
        updateVehicleVariations(zInfo);
}

void printCurrentVariations()
{
    if (enablePeds)
    {
        logfile << std::dec << "pedCurrentVariations\n";
        for (int i = 0; i < MAX_PED_ID; i++)
            if (!pedCurrentVariations[i].empty())
            {
                logfile << i << ": ";
                for (auto j : pedCurrentVariations[i])
                {
                    const char *suffix = " ";
                    if (std::find(addedIDs.begin(), addedIDs.end(), j) != addedIDs.end())
                        suffix = "SP ";
                    logfile << j << suffix;
                }
                logfile << "\n";
            }
    }

    if (enableVehicles)
    {
        logfile << "\n";
        printCurrentVehicleVariations();
    }

    logfile << "\n" << std::endl;
}

void printVariations()
{
    logfile << std::dec << "\nPed Variations:\n";
    for (unsigned int i = 0; i < MAX_PED_ID; i++)
        for (unsigned int j = 0; j < 16; j++)
            if (!pedVariations[i][j].empty())
            {
                logfile << i << ": ";
                for (unsigned int k = 0; k < 16; k++)
                    if (!pedVariations[i][k].empty())
                    {
                        logfile << "(" << k << ") ";
                        for (const auto& l : pedVariations[i][k])
                            logfile << l << " ";
                    }

                logfile << "\n";
                break;
            }

    if (enableVehicles)
    {
        logfile << "\n";
        printVehicleVariations();
    }

    logfile << "\n" << std::endl;
}

template <unsigned int address>
void __fastcall UpdateRpHAnimHooked(CEntity* entity)
{
    callMethodOriginal<address>(entity);
    //entity->UpdateRpHAnim();
    if (modelIndex > 0)
        entity->m_nModelIndex = modelIndex;
    modelIndex = 0;
}

template <unsigned int address>
char __fastcall CAEPedSpeechAudioEntity__InitialiseHooked(CAEPedSpeechAudioEntity* _this, void*, CPed* ped) 
{
    if (ped != NULL)
    {
        auto it = pedOriginalModels.find(ped->m_nModelIndex);
        if (it != pedOriginalModels.end() && !it->second.empty())
        {
            auto parentModel = it->second[0];
            auto currentModel = ped->m_nModelIndex;
            ped->m_nModelIndex = parentModel;
            char retVal = callMethodOriginalAndReturn<char, address>(_this, ped);
            ped->m_nModelIndex = currentModel;
            return retVal;
        }
    }

    return callMethodOriginalAndReturn<char, address>(_this, ped);
}

template <unsigned int address>
void __cdecl CGame__ShutdownHooked()
{
    if (logfile.is_open())
        logfile << "Game shutting down..." << std::endl;

    if (enableSpecialPeds)
    {
        delete[] destroyedModelCounters;

        CPedModelInfo* start = reinterpret_cast<CPedModelInfo*>(0xB478FC);
        for (int i = 0; i < 278; i++)
            start[i].m_pHitColModel = NULL;
    }

    callOriginal<address>();

    if (logfile.is_open())
        logfile << "Shutdown ok." << std::endl;
}

void installHooks()
{
    //Count of killable model IDs
    if (enablePeds && enableSpecialPeds)
    {
        bool gameHOODLUM = GetGameVersion() != GAME_10US_COMPACT;
        bool notModified = true;

        destroyedModelCounters = new int16_t[20000]();

        if ((*(uint32_t*)0x43DE6C != 0x4504FF66 || *(uint32_t*)0x43DE70 != 0x00969A50 ||
             *(uint32_t*)0x43DF5B != 0x4504FF66 || *(uint32_t*)0x43DF5F != 0x00969A50) &&
            (*(uint32_t*)(gameHOODLUM ? 0x1561634U : 0x43D6A4) != 0x5045048D || *(uint32_t*)(gameHOODLUM ? 0x1561638U : 0x43D6A8) != 0xB900969A ||
             *(uint32_t*)(gameHOODLUM ? 0x1564C2BU : 0x43D6CB) != 0x55048B66 || *(uint32_t*)(gameHOODLUM ? 0x1564C2FU : 0x43D6CF) != 0x00969A50))
        {
            notModified = false;
        }

        if (notModified)
        {
            injector::MakeInline<0x43DE6C, 0x43DE6C + 8>([](injector::reg_pack& regs)
            {
                destroyedModelCounters[regs.eax * 2]++;
            });

            injector::MakeInline<0x43DF5B, 0x43DF5B + 8>([](injector::reg_pack& regs)
            {
                destroyedModelCounters[regs.eax * 2]++;
            });

            auto leaEAX = [](injector::reg_pack& regs)
            {
                regs.eax = reinterpret_cast<uint32_t>(&(destroyedModelCounters[regs.eax * 2]));
            };

            auto movAX = [](injector::reg_pack& regs)
            {
                regs.eax = (regs.eax & 0xFFFF0000) | static_cast<uint32_t>(destroyedModelCounters[regs.edx * 2]);
            };

            if (gameHOODLUM)
            {
                injector::MakeInline<0x1561634U, 0x1561634U + 7>(leaEAX);
                injector::MakeInline<0x1564C2BU, 0x1564C2BU + 8>(movAX);
            }
            else
            {
                injector::MakeInline<0x43D6A4, 0x43D6A4 + 7>(leaEAX);
                injector::MakeInline<0x43D6CB, 0x43D6CB + 8>(movAX);
            }
        }
        else if (logfile.is_open())
            logfile << "Count of killable model IDs not increased." << (loadedMods.fastman92LimitAdjuster ? " FLA is loaded." : " FLA is NOT loaded.") << std::endl;
    }

    if (enablePeds)
    {
        hookCall(0x5E49EF, UpdateRpHAnimHooked<0x5E49EF>, "UpdateRpHAnim");
        if (useParentVoices)
        {
            hookCall(0x5DDBB8, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DDBB8>, "CAEPedSpeechAudioEntity__Initialise"); //CCivilianPed
            hookCall(0x5DDD24, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DDD24>, "CAEPedSpeechAudioEntity__Initialise"); //CCopPed
            hookCall(0x5DE388, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DE388>, "CAEPedSpeechAudioEntity__Initialise"); //CEmergencyPed
        }
    }

    if (enableVehicles)
    {
        if (logfile.is_open())
            logfile << "Installing vehicle hooks..." << std::endl;

        installVehicleHooks();

        if (logfile.is_open())
            logfile << "Vehicle hooks installed." << std::endl;
    }

    hookCall(0x748E6B, CGame__ShutdownHooked<0x748E6B>, "CGame::Shutdown");
}

void loadIniData(bool firstTime)
{
    enablePeds = iniSettings.ReadBoolean("Settings", "EnablePeds", false);
    enableVehicles = iniSettings.ReadBoolean("Settings", "EnableVehicles", false);
    enablePedWeapons = iniSettings.ReadBoolean("Settings", "EnablePedWeapons", false);

    if (enablePeds)
        enableSpecialPeds = iniSettings.ReadBoolean("Settings", "EnableSpecialPeds", false);

    if (enableSpecialPeds && !loadedMods.fastman92LimitAdjuster && !loadedMods.openLimitAdjuster)
    {
        enableSpecialPeds = false;
        if (firstTime)
            MessageBox(NULL, "No limit adjuster found! EnableSpecialPeds will be disabled.", "Model Variations", MB_ICONWARNING);
    }

    if (enablePeds)
        for (auto& iniData : iniPed.data)
        {
            int i = 0;
            std::string section = iniData.first;

            if (section[0] >= '0' && section[0] <= '9')
                i = std::stoi(iniData.first);
            else
            {
                CModelInfo::GetModelInfo((char*)section.c_str(), &i);
                pedModels.insert({ i, section });
            }

            if (isValidPedId(i))
            {
                pedHasVariations.insert((unsigned short)i);

                pedVariations[i][0] = iniPed.ReadLine(section, "Countryside", READ_PEDS);
                pedVariations[i][1] = iniPed.ReadLine(section, "LosSantos", READ_PEDS);
                pedVariations[i][2] = iniPed.ReadLine(section, "SanFierro", READ_PEDS);
                pedVariations[i][3] = iniPed.ReadLine(section, "LasVenturas", READ_PEDS);
                pedVariations[i][4] = iniPed.ReadLine(section, "Global", READ_PEDS);
                pedVariations[i][5] = iniPed.ReadLine(section, "Desert", READ_PEDS);

                pedVariations[i][6] = vectorUnion(iniPed.ReadLine(section, "TierraRobada", READ_PEDS), pedVariations[i][5]);
                pedVariations[i][7] = vectorUnion(iniPed.ReadLine(section, "BoneCounty", READ_PEDS), pedVariations[i][5]);
                pedVariations[i][8] = vectorUnion(iniPed.ReadLine(section, "RedCounty", READ_PEDS), pedVariations[i][0]);
                pedVariations[i][9] = vectorUnion(iniPed.ReadLine(section, "Blueberry", READ_PEDS), pedVariations[i][8]);
                pedVariations[i][10] = vectorUnion(iniPed.ReadLine(section, "Montgomery", READ_PEDS), pedVariations[i][8]);
                pedVariations[i][11] = vectorUnion(iniPed.ReadLine(section, "Dillimore", READ_PEDS), pedVariations[i][8]);
                pedVariations[i][12] = vectorUnion(iniPed.ReadLine(section, "PalominoCreek", READ_PEDS), pedVariations[i][8]);
                pedVariations[i][13] = vectorUnion(iniPed.ReadLine(section, "FlintCounty", READ_PEDS), pedVariations[i][0]);
                pedVariations[i][14] = vectorUnion(iniPed.ReadLine(section, "Whetstone", READ_PEDS), pedVariations[i][0]);
                pedVariations[i][15] = vectorUnion(iniPed.ReadLine(section, "AngelPine", READ_PEDS), pedVariations[i][14]);


                pedWantedVariations[i][0] = iniPed.ReadLine(section, "Wanted1", READ_PEDS);
                pedWantedVariations[i][1] = iniPed.ReadLine(section, "Wanted2", READ_PEDS);
                pedWantedVariations[i][2] = iniPed.ReadLine(section, "Wanted3", READ_PEDS);
                pedWantedVariations[i][3] = iniPed.ReadLine(section, "Wanted4", READ_PEDS);
                pedWantedVariations[i][4] = iniPed.ReadLine(section, "Wanted5", READ_PEDS);
                pedWantedVariations[i][5] = iniPed.ReadLine(section, "Wanted6", READ_PEDS);


                for (unsigned int j = 0; j < 16; j++)
                    for (unsigned int k = 0; k < pedVariations[i][j].size(); k++)
                        if (pedVariations[i][j][k] > 0 && pedVariations[i][j][k] != i)
                        {
                            if (pedOriginalModels.find(pedVariations[i][j][k]) != pedOriginalModels.end())
                                pedOriginalModels[pedVariations[i][j][k]].push_back((unsigned short)i);
                            else
                                pedOriginalModels.insert({ pedVariations[i][j][k], {(unsigned short)i} });
                        }

                for (auto it : pedOriginalModels)
                    std::sort(it.second.begin(), it.second.end());

                if (iniPed.ReadBoolean(section, "MergeZonesWithCities", false))
                    pedMergeZones.insert((unsigned short)i);

                if (iniPed.ReadBoolean(section, "DontInheritBehaviour", false))
                    dontInheritBehaviourModels.insert((unsigned short)i);
            }
        }

    if (enablePedWeapons)
        for (auto& iniData : iniWeap.data)
        {
            int modelid = 0;
            std::string section = iniData.first;

            if (!(section[0] >= '0' && section[0] <= '9'))
                CModelInfo::GetModelInfo((char*)section.c_str(), &modelid);
            if (modelid > 0)
                wepPedModels.insert({ modelid, section });

            for (auto& keys : iniData.second)
            {
                std::string name = keys.first.substr(0, keys.first.find("_"));
                if (CModelInfo::GetModelInfo((char*)name.c_str(), &modelid))
                    wepVehModels.insert({ modelid, name });
            }
        }

    if (enablePeds)
    {
        useParentVoices = iniPed.ReadBoolean("Settings", "UseParentVoices", false);
        enableCloneRemover = iniPed.ReadBoolean("Settings", "EnableCloneRemover", false);
        cloneRemoverVehicleOccupants = iniPed.ReadBoolean("Settings", "CloneRemoverIncludeVehicleOccupants", false);
        cloneRemoverSpawnDelay = iniPed.ReadInteger("Settings", "CloneRemoverSpawnDelay", 3);
        cloneRemoverIncludeVariations = iniPed.ReadLine("Settings", "CloneRemoverIncludeVariations", READ_PEDS);
        cloneRemoverExclusions = iniPed.ReadLine("Settings", "CloneRemoverExcludeModels", READ_PEDS);
    }

    if (enableVehicles)
        readVehicleIni(firstTime, exePath.substr(0, exePath.find_last_of("/\\")));
}

void clearEverything()
{
    for (int i = 0; i < MAX_PED_ID; i++)
        for (unsigned short j = 0; j < 16; j++)
        {
            pedVariations[i][j].clear();
            if (j < 6)
                pedWantedVariations[i][j].clear();
        }

    //maps
    pedTimeSinceLastSpawned.clear();
    pedOriginalModels.clear();
    wepPedModels.clear();
    wepVehModels.clear();
    pedModels.clear();

    //sets
    dontInheritBehaviourModels.clear();
    pedMergeZones.clear();
    pedHasVariations.clear();
    cloneRemoverIncludeVariations.clear();

    //stacks
    while (!pedStack.empty()) pedStack.pop();

    //vectors
    cloneRemoverExclusions.clear();
    for (int i = 0; i < MAX_PED_ID; i++)
        pedCurrentVariations[i].clear();

    iniPed.data.clear();
    iniWeap.data.clear();
    iniSettings.data.clear();

    iniPed.SetIniPath(pedIniPath);
    iniWeap.SetIniPath(pedWepIniPath);
    iniSettings.SetIniPath(settingsIniPath);

    enableCloneRemover = 0;

    clearVehicles();
}

class ModelVariations {
public:
    ModelVariations() {

        iniPed.SetIniPath(pedIniPath);
        iniWeap.SetIniPath(pedWepIniPath);
        iniSettings.SetIniPath(settingsIniPath);
        iniVeh.SetIniPath(vehIniPath);

        disableKey = (unsigned int)iniSettings.ReadInteger("Settings", "DisableKey", 0);
        reloadKey = (unsigned int)iniSettings.ReadInteger("Settings", "ReloadKey", 0);

        if ((enableLog = iniSettings.ReadBoolean("Settings", "EnableLog", false)) == true)
        {
            logfile.open("ModelVariations.log");

            if (logfile.is_open())
            {
                SYSTEMTIME systime;
                GetSystemTime(&systime);
                const char *isDebug = "";
#ifdef _DEBUG
                isDebug = " DEBUG";
#endif

                logfile << "Model Variations " MOD_VERSION << isDebug << "\n" << getWindowsVersion() << "\n"
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
                enableLog = 0;
        }

        if (logfile.is_open())
        {
            if (GetFileAttributes(pedIniPath) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
                logfile << "\nModelVariations_Peds.ini not found!\n" << std::endl;
            else
                logfile << "##############################\n"
                           "## ModelVariations_Peds.ini ##\n"
                           "##############################\n" << fileToString(pedIniPath) << std::endl;

            if (GetFileAttributes(pedWepIniPath) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
                logfile << "\nModelVariations_PedWeapons.ini not found!\n" << std::endl;
            else
                logfile << "####################################\n"
                           "## ModelVariations_PedWeapons.ini ##\n"
                           "####################################\n" << fileToString(pedWepIniPath) << std::endl;

            if (GetFileAttributes(vehIniPath) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
                logfile << "\nModelVariations_Vehicles.ini not found!\n" << std::endl;
            else
                logfile << "##################################\n"
                           "## ModelVariations_Vehicles.ini ##\n"
                           "##################################\n" << fileToString(vehIniPath) << std::endl;


            logfile << std::endl;

            if (!iniPed.data.empty())
            {
                logfile << "\nPed sections detected:\n";
                for (const auto& i : iniPed.data)
                    logfile << std::dec << i.first << "\n";
            }

            if (!iniWeap.data.empty())
            {
                logfile << "\nPed weapon sections detected:\n";
                for (const auto& i : iniWeap.data)
                    logfile << i.first << "\n";
            }

            if (!iniVeh.data.empty())
            {
                logfile << "\nVehicle sections detected:\n";
                for (const auto& i : iniVeh.data)
                    logfile << i.first << "\n";
            }

            logfile << "\n" << std::endl;

        }

        Events::initRwEvent += []
        {
            getLoadedModules(loadedMods.openLimitAdjuster, loadedMods.fastman92LimitAdjuster);

            loadIniData(true);
            installHooks();

            if (logfile.is_open())
            {
                logfile << "\nLoaded modules:" << std::endl;

                for (const auto& i : modulesSet)
                    logfile << "0x" << std::setfill('0') << std::setw(8) << std::hex << i.first << " " << i.second << "\n";
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
                if (logfile.is_open())
                    logfile << "Checking unused IDs...\n";

                for (uint16_t i = 1326; i < 20000; i++)
                {
                    if (CModelInfo::GetModelInfo(i) == NULL)
                        unusedIDs.push_back(i);
                }
                unusedIDsChecked = true;
            }

            loadIniData(false);
            printVariations();

            if (loadAllVehicles)
                loadModels(400, 611, KEEP_IN_MEMORY, false);

            dealersFrames = 0;
            framesSinceCallsChecked = 900;

            if (logfile.is_open())
                getLoadedModules(loadedMods.openLimitAdjuster, loadedMods.fastman92LimitAdjuster);

            if (checkForUpdate())
                timeUpdate = clock();
            else
                timeUpdate = -1;
        };

        Events::processScriptsEvent += []
        {
            if (dealersFrames < 10)
                dealersFrames++;

            if (dealersFrames == 10)
            {
                if (logfile.is_open())
                    logfile << "Applying drug dealer fix...\n";
                drugDealerFix();
                if (logfile.is_open())
                    logfile << std::endl;
                dealersFrames = 11;
            }
        };

        Events::pedCtorEvent += [](CPed* ped)
        {
            pedStack.push(ped);
        };

        Events::vehicleCtorEvent += [](CVehicle* veh)
        {
            addToVehicleStack(veh);
        };

        Events::pedSetModelEvent.after += [](CPed* ped, int)
        {
            if (isValidPedId(ped->m_nModelIndex) && !pedCurrentVariations[ped->m_nModelIndex].empty())
            {
                const unsigned int random = rand<uint32_t>(0, pedCurrentVariations[ped->m_nModelIndex].size());
                const unsigned short variationModel = pedCurrentVariations[ped->m_nModelIndex][random];
                if (variationModel > 0 && variationModel != ped->m_nModelIndex)
                {
                    loadModels({ variationModel }, GAME_REQUIRED, true);
                    const unsigned short index = ped->m_nModelIndex;
                    ped->DeleteRwObject();
                    (static_cast<CEntity*>(ped))->SetModelIndex(variationModel);
                    if (dontInheritBehaviourModels.find(index) == dontInheritBehaviourModels.end())
                        ped->m_nModelIndex = index;
                    modelIndex = variationModel;
                }
            }
        };

        Events::gameProcessEvent += []
        {
            CVector pPos = FindPlayerCoors(-1);
            CZone* zInfo = NULL;
            CTheZones::GetZoneInfo(&pPos, &zInfo);
            const CWanted* wanted = FindPlayerWanted(-1);
            const CPlayerPed* player = FindPlayerPed();

            const auto printVariationChange = [zInfo, wanted](const char* msg)
            {
                if (logfile.is_open())
                {
                    logfile << "\n";
                    logfile << getDatetime(false, true, true) << " - " << msg << " Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << " lastZone = " << lastZone << "\n";
                    if (currentInterior[0] != 0 || lastInterior[0] != 0)
                        logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n";

                    logfile << std::endl;                    
                }
            };

            if (timeUpdate > -1 && ((clock() - timeUpdate) / CLOCKS_PER_SEC > 6))
            {
                CMessages::AddMessageJumpQ((char*)"~y~Model Variations~s~: Update available.", 4000, 0, false);
                timeUpdate = -1;
            }

            if (disableKey > 0 && KeyPressed(disableKey))
            {
                if (!keyDown)
                {
                    keyDown = true;
                    CMessages::AddMessageJumpQ((char*)"~y~Model Variations~s~: Mod disabled.", 2000, 0, false);
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
                    updateVariations(zInfo);
                    printVariationChange("Settings reloaded.");
                    CMessages::AddMessageJumpQ((char*)"~y~Model Variations~s~: Settings reloaded.", 2000, 0, false);
                }
            }
            else
                keyDown = false;

            if (framesSinceCallsChecked < 1000)
                framesSinceCallsChecked++;

            if (framesSinceCallsChecked == 1000 && logfile.is_open())
            {
                checkAllCalls();
                framesSinceCallsChecked = 0;
            }

            if (enableCloneRemover)
            {
                auto it = pedTimeSinceLastSpawned.begin();
                while (it != pedTimeSinceLastSpawned.end())
                    if ((clock() - it->second) / CLOCKS_PER_SEC < cloneRemoverSpawnDelay)
                        it++;
                    else
                        it = pedTimeSinceLastSpawned.erase(it);
            }

            if (player && player->m_pEnex)
                currentInterior = (const char*)player->m_pEnex;
            else 
                currentInterior = "";

            if (strncmp(currentInterior, lastInterior, 7) != 0)
            {
                printVariationChange("Interior changed.");

                strncpy(lastInterior, currentInterior, 7);
                updateVariations(zInfo);

                if (logfile.is_open())
                    printCurrentVariations();
            }

            if (wanted && (int)(wanted->m_nWantedLevel) != currentWanted)
            {
                printVariationChange("Wanted level changed.");

                currentWanted = (int)wanted->m_nWantedLevel;
                updateVariations(zInfo);

                if (logfile.is_open())
                    printCurrentVariations();
            }

            if (zInfo && strncmp(zInfo->m_szLabel, currentZone, 7) != 0)
            {
                if (lastZone[0] == 0 && strncmp(zInfo->m_szLabel, "SAN_AND", 7) == 0)
                    strncpy(lastZone, currentZone, 7);
                else if (strncmp(zInfo->m_szLabel, "SAN_AND", 7) != 0)
                    lastZone[0] = 0;

                printVariationChange("Zone changed.");

                strncpy(currentZone, zInfo->m_szLabel, 7);
                updateVariations(zInfo);

                if (logfile.is_open())
                    printCurrentVariations();
            }

            if (enableVehicles)
                hookTaxi();

            processVehicleStacks();

            while (!pedStack.empty())
            {
                const auto deletePed = [](CPed *ped) 
                {
                    if (IsPedPointerValid(ped))
                    {
                        if (ped->m_pIntelligence)
                            ped->m_pIntelligence->FlushImmediately(false);
                        CTheScripts::RemoveThisPed(ped);
                    }
                };

                const auto pedDeleteVeh = [deletePed](CPed *ped)
                {
                    CVehicle* veh = ped->m_pVehicle;
                    if (ped->m_pVehicle->m_pDriver == ped)
                    {
                        deletePed(veh->m_pDriver);
                        for (auto& i : veh->m_apPassengers)
                            deletePed(i);
                        DestroyVehicleAndDriverAndPassengers(veh);
                    }
                    else
                        for (auto& i : veh->m_apPassengers)
                            deletePed(i);
                };

                const auto isCarEmpty = [](CVehicle* veh)
                {
                    if (IsPedPointerValid(veh->m_pDriver))
                        return false;
                    else
                        for (int i = 0; i < 8; i++)
                            if (IsPedPointerValid(veh->m_apPassengers[i]))
                                return false;
   
                    return true;
                };

                CPed* ped = pedStack.top();
                pedStack.pop();

                if (IsPedPointerValid(ped) && isValidPedId(ped->m_nModelIndex))
                    if (!pedCurrentVariations[ped->m_nModelIndex].empty() && pedCurrentVariations[ped->m_nModelIndex][0] == 0 && ped->m_nCreatedBy != 2) //Delete models with a 0 id variation
                    {
                        if (IsVehiclePointerValid(ped->m_pVehicle))
                            pedDeleteVeh(ped);
                        else
                            deletePed(ped);
                    }

                if (IsPedPointerValid(ped) && enableCloneRemover && ped->m_nCreatedBy != 2 && CPools::ms_pPedPool) //Clone remover
                {
                    bool includeVariations = std::find(cloneRemoverIncludeVariations.begin(), cloneRemoverIncludeVariations.end(), ped->m_nModelIndex) != cloneRemoverIncludeVariations.end();
                    if (pedDelaySpawn(ped->m_nModelIndex, includeVariations)) //Delete peds spawned before SpawnTime
                    {
                        if (!IsVehiclePointerValid(ped->m_pVehicle))
                            deletePed(ped);
                        else if (cloneRemoverVehicleOccupants && !isCarEmpty(ped->m_pVehicle))                           
                            pedDeleteVeh(ped);
                    }

                    if (IsPedPointerValid(ped) && !vectorHasId(cloneRemoverExclusions, ped->m_nModelIndex) && ped->m_nModelIndex > 0) //Delete peds already spawned
                    {
                        if (includeVariations)
                        {
                            auto it = pedOriginalModels.find(ped->m_nModelIndex);
                            if (it != pedOriginalModels.end())
                                for (auto& i : it->second)
                                    pedTimeSinceLastSpawned.insert({ i, clock() });
                        }
                        else
                            pedTimeSinceLastSpawned.insert({ ped->m_nModelIndex, clock() });

                        for (CPed* ped2 : CPools::ms_pPedPool)
                            if (IsPedPointerValid(ped2) && ped2 != ped && compareOriginalModels(ped->m_nModelIndex, ped2->m_nModelIndex, includeVariations))
                            {
                                if (!IsVehiclePointerValid(ped->m_pVehicle))
                                {
                                    deletePed(ped); 
                                    break;
                                }
                                else if (cloneRemoverVehicleOccupants && !isCarEmpty(ped->m_pVehicle))
                                {
                                    pedDeleteVeh(ped);
                                    break;
                                }
                            }
                    }
                }

                if (IsPedPointerValid(ped) && enablePedWeapons)
                {
                    const auto changeWeapon = [ped](const std::string section, const std::string key, eWeaponType originalWeaponId = WEAPON_UNARMED) -> bool
                    {
                        std::vector<unsigned short> vec = iniWeap.ReadLine(section, key, READ_WEAPONS);
                        if (!vec.empty())
                        {
                            eWeaponType weaponId = (eWeaponType)vectorGetRandom(vec);
                            int weaponModel = 0;
                            const CWeaponInfo* wInfo = CWeaponInfo::GetWeaponInfo(weaponId, 1);
                            if (wInfo != NULL)
                                weaponModel = wInfo->m_nModelId1;

                            if (weaponModel >= 321)
                            {
                                loadModels({ weaponModel }, GAME_REQUIRED, true);

                                if (originalWeaponId > 0)
                                    ped->ClearWeapon(originalWeaponId);
                                else
                                    ped->ClearWeapons();

                                ped->GiveWeapon(weaponId, 9999, true);
                                return true;
                            }
                        }

                        return false;
                    };

                    std::string section = std::to_string(ped->m_nModelIndex);
                    auto wepModel = wepPedModels.find(ped->m_nModelIndex);
                    if (wepModel != wepPedModels.end())
                        section = wepModel->second;
                    const auto mergeWeapons = iniWeap.ReadBoolean(section, "MergeZonesWithGlobal", false);
                    const auto disableOnMission = iniWeap.ReadBoolean(section, "DisableOnMission", false) & isOnMission();
                    std::string vehId;

                    for (int j = 0; j < 2; j++)
                    {
                        bool wepChanged = false;

                        if (j == 1)
                        {
                            if (IsVehiclePointerValid(ped->m_pVehicle))
                            {
                                auto it = wepVehModels.find(ped->m_pVehicle->m_nModelIndex);
                                if (it != wepVehModels.end())
                                    vehId = it->second + "_";
                                else
                                    vehId = std::to_string(ped->m_pVehicle->m_nModelIndex) + "_";
                            }
                            else
                                break;
                        }

                        if (!disableOnMission)
                        {
                            bool changeZoneWeapon = true;
                            if ((wepChanged = changeWeapon(section, vehId + "WEAPONFORCE")) == true)
                                changeZoneWeapon = rand<bool>();

                            if ((changeZoneWeapon || mergeWeapons == 0))
                                wepChanged |= changeWeapon(section, vehId + currentZone + "_WEAPONFORCE");
                        }

                        if (!wepChanged && !disableOnMission)
                            for (int i = 0; i < 13; i++)
                                if (ped->m_aWeapons[i].m_eWeaponType > 0) 
                                {
                                    const eWeaponType weaponId = ped->m_aWeapons[i].m_eWeaponType;
                                    bool changeZoneWeapon = true;
                                    bool changeZoneSlot = true;
                                    const int currentSlot = ped->m_nActiveWeaponSlot;

                                    if ((wepChanged = changeWeapon(section, vehId + "SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType)) == true)
                                        changeZoneSlot = rand<bool>();

                                    if (changeZoneSlot || mergeWeapons == 0)
                                        wepChanged |= changeWeapon(section, vehId + currentZone + "_SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType);

                                    if ((changeWeapon(section, vehId + "WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType) == true) ? (wepChanged = true) : false)
                                        changeZoneWeapon = rand<bool>();

                                    if (changeZoneWeapon || mergeWeapons == 0)                                       
                                        wepChanged |= changeWeapon(section, vehId + currentZone + "_WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType);

                                    if (wepChanged)
                                        ped->SetCurrentWeapon(currentSlot);
                                }
                    }
                }
            }
        };

    }
} modelVariations;
