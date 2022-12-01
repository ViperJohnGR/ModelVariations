#include <plugin.h>
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "LogUtil.hpp"
#include "Vehicles.hpp"
#include "Hooks.hpp"

#include <extensions/ScriptCommands.h>

#include <CGeneral.h>
#include <CMessages.h>
#include <CModelInfo.h>
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
#include <unordered_set>
#include <ctime>
#include <urlmon.h>

#include <Psapi.h>
#include <shlwapi.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment (lib, "shlwapi.lib")

using namespace plugin;

constexpr int MAX_PED_ID = 300;

const char* pedIniPath("ModelVariations_Peds.ini");
const char* pedWepIniPath("ModelVariations_PedWeapons.ini");
const char* vehIniPath("ModelVariations_Vehicles.ini");
const char* settingsIniPath("ModelVariations.ini");


const std::string exeHashes[3] = { "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26",     //HOODLUM
                                   "25580ae242c6ecb6f6175ca9b4c912aa042f33986ded87f20721b48302adc9c9",     //Compact
                                   "f01a00ce950fa40ca1ed59df0e789848c6edcf6405456274965885d0929343ac" };   //HOODLUM LARGEADDRESSAWARE


std::string exeHash;
unsigned int exeFilesize = 0;
int exeVersion = 0;
std::string exePath;
std::string exeName;

std::ofstream logfile;
std::set<std::pair<unsigned int, std::string>> modulesSet;
std::set<std::pair<unsigned int, std::string>> callChecks;

DataReader iniPed;
DataReader iniWeap;
DataReader iniVeh;
DataReader iniSettings;

std::array<std::vector<unsigned short>, 16> pedVariations[MAX_PED_ID];
std::array<std::vector<unsigned short>, 16> vehVariations[212];
std::array<std::vector<unsigned short>, 6> pedWantedVariations[MAX_PED_ID];
std::array<std::vector<unsigned short>, 6> vehWantedVariations[212];

std::map<unsigned short, std::array<std::vector<unsigned short>, 16>> vehGroups;
std::map<unsigned short, std::array<std::vector<unsigned short>, 16>> vehTuning;
std::map<unsigned int, hookinfo> hookedCalls;
std::map<unsigned short, unsigned short> vehOriginalModels;
std::map<unsigned short, std::vector<unsigned short>> vehDrivers;
std::map<unsigned short, std::vector<unsigned short>> vehPassengers;
std::map<unsigned short, std::vector<unsigned short>> vehDriverGroups[9];
std::map<unsigned short, std::vector<unsigned short>> vehPassengerGroups[9];
std::map<unsigned short, BYTE> modelNumGroups;
std::map<unsigned short, std::pair<CVector, float>> LightPositions;
std::map<unsigned short, rgba> LightColors;
std::map<unsigned short, rgba> LightColors2;
std::map<unsigned short, int> pedTimeSinceLastSpawned;
std::map<unsigned short, std::vector<unsigned short>> pedOriginalModels;
std::map<unsigned short, std::array<std::vector<unsigned short>, 6>> vehGroupWantedVariations;
std::map<unsigned short, std::string> wepPedModels;
std::map<unsigned short, std::string> wepVehModels;
std::map<unsigned short, std::vector<unsigned short>> vehCurrentTuning;
std::map<unsigned short, std::string> vehModels;
std::map<unsigned short, std::string> pedModels;
std::map<unsigned short, BYTE> tuningRarities;

std::set<unsigned short> dontInheritBehaviourModels;
std::set<unsigned short> parkedCars;
std::set<unsigned short> vehUseOnlyGroups;
std::set<unsigned short> pedMergeZones;
std::set<unsigned short> vehMergeZones;
std::set<unsigned short> pedHasVariations;
std::set<unsigned short> vehHasVariations;
std::set<unsigned int> modifiedAddresses;

std::stack<CPed*> pedStack;
std::stack<CVehicle*> vehStack;
std::stack<std::pair<CVehicle*, std::array<std::vector<unsigned short>, 17>>> tuningStack;
//std::stack<std::pair<CVehicle*, std::array<int, 16>>> tuningStack;

std::vector<unsigned short> pedCurrentVariations[MAX_PED_ID];
std::vector<unsigned short> vehCurrentVariations[212];

BYTE dealersFixed = 0;
short framesSinceCallsChecked = 0;
unsigned short modelIndex = 0;
char lastInterior[8] = {};
const char* currentInterior = lastInterior;
char currentZone[8] = {};
char lastZone[8] = {};
BYTE currentTown = 0;

//INI Options
//General
int enableLog = 0;
int enablePeds = 0;
int enableVehicles = 0;
int enablePedWeapons = 0;
unsigned int disableKey = 0;
unsigned int reloadKey = 0;
//Peds
int enableCloneRemover = 0;
int cloneRemoverVehicleOccupants = 0;
int cloneRemoverSpawnDelay = 3;
std::vector<unsigned short> cloneRemoverIncludeVariations;
std::vector<unsigned short> cloneRemoverExclusions;
//Vehicles
int changeCarGenerators = 0;
int changeScriptedCars = 0;
int disablePayAndSpray = 0;
int enableLights = 0;
int enableSideMissions = 0;
int enableAllSideMissions = 0;
int enableSiren = 0;
int enableSpecialFeatures = 0;
int loadAllVehicles = 0;
std::vector<unsigned short> vehCarGenExclude;
std::vector<unsigned short> vehInheritExclude;        


bool keyDown = false;

int timeUpdate = -1;

std::string fileToString(const std::string& filename)
{
    std::stringstream ss;
    std::ifstream file(filename);

    if (file.is_open())
        ss << file.rdbuf();

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
            char szModName[MAX_PATH];
            if (GetModuleFileNameEx(hProcess, modules[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
                modulesSet.insert(std::make_pair((unsigned int)modules[i], szModName));
        }
}

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
        if (!((ch >= '0' && ch <= '9') || (ch == '.')))
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

void insertPedSpawnedOriginalModels(unsigned short model)
{
    auto it = pedOriginalModels.find(model);
    if (it != pedOriginalModels.end())
        for (auto& i : it->second)
            pedTimeSinceLastSpawned.insert({ i, clock() });
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

bool IdExists(std::vector<unsigned short>& vec, int id)
{
    if (vec.size() < 1)
        return false;

    if (std::find(vec.begin(), vec.end(), id) != vec.end())
        return true;

    return false;
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

    if (exeHash == exeHashes[0])
        exeVersion = 1;
    else if (exeHash == exeHashes[1])
        exeVersion = 2;
    else if (exeHash == exeHashes[2])
        exeVersion = 3;
    else
        exeVersion = 4;
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

    currentTown = (BYTE)CTheZones::m_CurrLevel;
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
        for (auto& i : vehTuning)
        {
            vehCurrentTuning[i.first] = vectorUnion(i.second[4], i.second[currentTown]);

            std::string section;
            auto it = vehModels.find(i.first);
            if (it != vehModels.end())
                section = it->second;
            else
                section = std::to_string(i.first);

            std::vector<unsigned short> vec = iniVeh.ReadLine(section, ((lastZone[0] == 0) ? zInfo->m_szLabel : lastZone), READ_TUNING);
            if (!vec.empty())
            {
                if (vehMergeZones.find(i.first) != vehMergeZones.end())
                    vehCurrentTuning[i.first] = vectorUnion(vehCurrentTuning[i.first], vec);
                else
                    vehCurrentTuning[i.first] = vec;
            }
        }

    if (enableVehicles)
        for (auto& modelid : vehHasVariations)
        {
            vehCurrentVariations[modelid] = vectorUnion(vehVariations[modelid][4], vehVariations[modelid][currentTown]);

            std::string section;
            auto it = vehModels.find(modelid + 400U);
            if (it != vehModels.end())
                section = it->second;
            else
                section = std::to_string(modelid + 400);

            std::vector<unsigned short> vec = iniVeh.ReadLine(section, ((lastZone[0] == 0) ? zInfo->m_szLabel : lastZone), READ_VEHICLES);
            if (!vec.empty())
            {
                if (vehMergeZones.find((unsigned short)(modelid + 400)) != vehMergeZones.end())
                    vehCurrentVariations[modelid] = vectorUnion(vehCurrentVariations[modelid], vec);
                else
                    vehCurrentVariations[modelid] = vec;
            }

            if (wanted)
            {
                const unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
                if (!vehWantedVariations[modelid][wantedLevel].empty() && !vehCurrentVariations[modelid].empty())
                    filterWantedVariations(vehCurrentVariations[modelid], vehWantedVariations[modelid][wantedLevel]);
            }
        }
}

void printCurrentVariations()
{
    if (enablePeds == 1)
    {
        logfile << std::dec << "pedCurrentVariations\n";
        for (int i = 0; i < MAX_PED_ID; i++)
            if (!pedCurrentVariations[i].empty())
            {
                logfile << i << ": ";
                for (auto j : pedCurrentVariations[i])
                    logfile << j << " ";
                logfile << "\n";
            }

        logfile << std::endl;
    }

    if (enableVehicles == 1)
    {
        logfile << std::dec << "vehCurrentVariations\n";
        for (int i = 0; i < 212; i++)
            if (!vehCurrentVariations[i].empty())
            {
                logfile << i + 400 << ": ";
                for (auto j : vehCurrentVariations[i])
                    logfile << j << " ";
                logfile << "\n";
            }
        logfile << "\n" << std::endl;
    }
    else
        logfile << std::endl;
}

void printVariations()
{
    logfile << std::dec << "\nPed Variations:\n";
    for (unsigned int i = 0; i < MAX_PED_ID; i++)
    {
        for (unsigned int j = 0; j < 16; j++)
            if (!pedVariations[i][j].empty())
            {
                logfile << i << ": ";
                for (unsigned int k = 0; k < 16; k++)
                    if (!pedVariations[i][k].empty())
                    {
                        logfile << "(" << k << ") ";
                        for (auto& l : pedVariations[i][k])
                            logfile << l << " ";
                    }

                logfile << "\n";
                break;
            }
    }
    logfile << "\nVehicle Variations:\n";
    for (unsigned int i = 0; i < 212; i++)
    {
        for (unsigned int j = 0; j < 16; j++)
            if (!vehVariations[i][j].empty())
            {
                logfile << i + 400 << ": ";
                for (unsigned int k = 0; k < 16; k++)
                    if (!vehVariations[i][k].empty())
                    {
                        logfile << "(" << k << ") ";
                        for (auto& l : vehVariations[i][k])
                            logfile << l << " ";
                    }

                logfile << "\n";
                break;
            }
    }

    logfile << std::endl;
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

void installHooks()
{
    if (enablePeds == 1)
        hookCall(0x5E49EF, UpdateRpHAnimHooked<0x5E49EF>, "UpdateRpHAnim");

    if (enableVehicles == 1)
    {
        if (logfile.is_open())
            logfile << "Installing vehicle hooks..." << std::endl;

        installVehicleHooks();

        if (logfile.is_open())
            logfile << "Vehicle hooks installed." << std::endl;
    }
}

void loadIniData(bool firstTime)
{
    //for (unsigned short i = 0; i < MAX_PED_ID; i++)
    enablePeds = iniSettings.ReadInteger("Settings", "EnablePeds", 0);
    enableVehicles = iniSettings.ReadInteger("Settings", "EnableVehicles", 0);
    enablePedWeapons = iniSettings.ReadInteger("Settings", "EnablePedWeapons", 0);

    if (enablePeds == 1)
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

                if (iniPed.ReadInteger(section, "MergeZonesWithCities", 0) == 1)
                    pedMergeZones.insert((unsigned short)i);

                if (iniPed.ReadInteger(section, "DontInheritBehaviour", 0) == 1)
                    dontInheritBehaviourModels.insert((unsigned short)i);
            }
        }

    if (enablePedWeapons == 1)
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

    if (enablePeds == 1)
    {
        enableCloneRemover = iniPed.ReadInteger("Settings", "EnableCloneRemover", 0);
        cloneRemoverVehicleOccupants = iniPed.ReadInteger("Settings", "CloneRemoverIncludeVehicleOccupants", 0);
        cloneRemoverSpawnDelay = iniPed.ReadInteger("Settings", "CloneRemoverSpawnDelay", 3);
        cloneRemoverIncludeVariations = iniPed.ReadLine("Settings", "CloneRemoverIncludeVariations", READ_PEDS);
        cloneRemoverExclusions = iniPed.ReadLine("Settings", "CloneRemoverExcludeModels", READ_PEDS);
    }

    if (enableVehicles == 1)
        readVehicleIni(firstTime, exePath.substr(0, exePath.find_last_of("/\\")));
}

void clearEverything()
{
    for (int i = 0; i < MAX_PED_ID; i++)
    {
        for (unsigned short j = 0; j < 16; j++)
        {
            pedVariations[i][j].clear();
            if (i < 212)
                vehVariations[i][j].clear();
            if (j < 6)
            {
                pedWantedVariations[i][j].clear();
                if (i < 212)
                    vehWantedVariations[i][j].clear();
            }
        }
    }

    //maps
    vehGroups.clear();
    vehOriginalModels.clear();
    vehDrivers.clear();
    vehPassengers.clear();

    for (int i = 0; i < 9; i++)
    {
        vehDriverGroups[i].clear();
        vehPassengerGroups[i].clear();
    }

    modelNumGroups.clear();
    LightPositions.clear();
    LightColors.clear();
    LightColors2.clear();
    pedTimeSinceLastSpawned.clear();
    pedOriginalModels.clear();
    vehGroupWantedVariations.clear();
    wepPedModels.clear();
    wepVehModels.clear();
    vehTuning.clear();
    vehCurrentTuning.clear();
    vehModels.clear();
    pedModels.clear();
    tuningRarities.clear();

    //sets
    dontInheritBehaviourModels.clear();
    parkedCars.clear();
    vehUseOnlyGroups.clear();
    pedMergeZones.clear();
    vehMergeZones.clear();
    pedHasVariations.clear();
    vehHasVariations.clear();
    cloneRemoverIncludeVariations.clear();

    //stacks
    while (!pedStack.empty()) pedStack.pop();
    while (!vehStack.empty()) vehStack.pop();
    while (!tuningStack.empty()) tuningStack.pop();

    //vectors
    cloneRemoverExclusions.clear();
    for (int i = 0; i < MAX_PED_ID; i++)
    {
        pedCurrentVariations[i].clear();
        if (i < 212)
            vehCurrentVariations[i].clear();
    }
    vehCarGenExclude.clear();
    vehInheritExclude.clear();

    iniPed.data.clear();
    iniWeap.data.clear();
    iniSettings.data.clear();
    iniVeh.data.clear();

    iniPed.SetIniPath(pedIniPath);
    iniWeap.SetIniPath(pedWepIniPath);
    iniSettings.SetIniPath(settingsIniPath);
    iniVeh.SetIniPath(vehIniPath);

    enableCloneRemover = 0;
}

class ModelVariations {
public:
    ModelVariations() {

        iniPed.SetIniPath(pedIniPath);
        iniWeap.SetIniPath(pedWepIniPath);
        iniSettings.SetIniPath(settingsIniPath);
        iniVeh.SetIniPath(vehIniPath);

        static int currentWanted = 0;

        disableKey = (unsigned int)iniSettings.ReadInteger("Settings", "DisableKey", 0);
        reloadKey = (unsigned int)iniSettings.ReadInteger("Settings", "ReloadKey", 0);

        if ((enableLog = iniSettings.ReadInteger("Settings", "EnableLog", 0)) == 1)
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
                        << systime.wDay << "/" << systime.wMonth << "/" << systime.wYear << " "
                        << std::setfill('0') << std::setw(2) << systime.wHour << ":"
                        << std::setfill('0') << std::setw(2) << systime.wMinute << ":"
                        << std::setfill('0') << std::setw(2) << systime.wSecond << "\n\n";

                detectExe();
                logfile << exePath << std::endl;

                if (exeVersion == 1)
                    logfile << "Supported exe detected: 1.0 US HOODLUM" << std::endl;
                else if (exeVersion == 2)
                    logfile << "Supported exe detected: 1.0 US Compact" << std::endl;
                else if (exeVersion == 3)
                    logfile << "Supported exe detected: 1.0 US HOODLUM LARGEADDRESSAWARE" << std::endl;
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
                for (auto& i : iniPed.data)
                    logfile << std::dec << i.first << "\n";
            }

            if (!iniWeap.data.empty())
            {
                logfile << "\nPed weapon sections detected:\n";
                for (auto& i : iniWeap.data)
                    logfile << i.first << "\n";
            }

            if (!iniVeh.data.empty())
            {
                logfile << "\nVehicle sections detected:\n";
                for (auto& i : iniVeh.data)
                    logfile << i.first << "\n";
            }

            logfile << "\n" << std::endl;

        }

        Events::initRwEvent += []
        {
            //if (checkForUpdate())
                //MessageBox(NULL, "Model Variations: New version available!\nhttps://github.com/ViperJohnGR/ModelVariations", "Update available", MB_ICONINFORMATION);
            getLoadedModules();

            loadIniData(true);
            installHooks();

            if (logfile.is_open())
            {
                logfile << "\nLoaded modules:" << std::endl;

                for (auto& i : modulesSet)
                    logfile << "0x" << std::setfill('0') << std::setw(8) << std::hex << i.first << " " << i.second << "\n";
                logfile << std::endl;
            }
        };

        Events::initScriptsEvent += []
        {
            if (logfile.is_open())
                logfile << "-- initScriptsEvent --" << std::endl;

            clearEverything();
            loadIniData(false);
            printVariations();

            if (loadAllVehicles)
                for (int i = 400; i < 612; i++)
                    CStreaming::RequestModel(i, KEEP_IN_MEMORY);

            dealersFixed = 0;
            framesSinceCallsChecked = 900;

            if (logfile.is_open())
                getLoadedModules();

            if (checkForUpdate())
                timeUpdate = clock();
            else
                timeUpdate = -1;
        };

        Events::processScriptsEvent += []
        {
            if (dealersFixed < 10)
                dealersFixed++;

            if (dealersFixed == 10)
            {
                if (logfile.is_open())
                    logfile << "Applying drug dealer fix...\n";
                drugDealerFix();
                if (logfile.is_open())
                    logfile << std::endl;
                dealersFixed = 11;
            }
        };

        Events::pedCtorEvent += [](CPed* ped)
        {
            pedStack.push(ped);
        };

        Events::vehicleCtorEvent += [](CVehicle* veh)
        {
            vehStack.push(veh);
        };

        Events::pedSetModelEvent.after += [](CPed* ped, int)
        {
            if (isValidPedId(ped->m_nModelIndex) && !pedCurrentVariations[ped->m_nModelIndex].empty())
            {
                const unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)pedCurrentVariations[ped->m_nModelIndex].size());
                const unsigned short variationModel = pedCurrentVariations[ped->m_nModelIndex][random];
                if (variationModel > 0 && variationModel != ped->m_nModelIndex)
                {
                    CStreaming::RequestModel(variationModel, GAME_REQUIRED);
                    CStreaming::LoadAllRequestedModels(false);
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
            //if (timeUpdate > -1 && strncmp(currentZone, "GAN1", 8) == 0 && *(int*)0xB7CE50 == 0)
                //timeUpdate = clock();

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

            if (enableCloneRemover == 1)
            {
                auto it = pedTimeSinceLastSpawned.begin();
                while (it != pedTimeSinceLastSpawned.end())
                    if ((clock() - it->second) / CLOCKS_PER_SEC < cloneRemoverSpawnDelay)
                        it++;
                    else
                        it = pedTimeSinceLastSpawned.erase(it);
            }

            CVector pPos = FindPlayerCoors(-1);
            CZone* zInfo = NULL;
            CTheZones::GetZoneInfo(&pPos, &zInfo);
            const CWanted* wanted = FindPlayerWanted(-1);

            CPlayerPed* player = FindPlayerPed();

            if (player && player->m_pEnex)
                currentInterior = (const char*)player->m_pEnex;
            else 
                currentInterior = "";

            const auto printVariationChange = [zInfo, wanted](const char *msg)
            {
                if (logfile.is_open())
                {
                    logfile << msg << " Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << " lastZone = " << lastZone << "\n";
                    if (currentInterior[0] != 0 || lastInterior[0] != 0)
                        logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n" << std::endl;
                    else
                        logfile << std::endl;
                }
            };

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


            if (enableVehicles == 1)
                hookTaxi();

            while (!tuningStack.empty())
            {
                auto it = tuningStack.top();
                tuningStack.pop();

                if (IsVehiclePointerValid(it.first))
                    for (auto& slot : it.second)
                    {
                        if (!slot.empty())
                        {
                            const unsigned i = CGeneral::GetRandomNumberInRange(0, (int)slot.size());

                            CStreaming::RequestVehicleUpgrade(slot[i], 2);
                            CStreaming::LoadAllRequestedModels(false);

                            it.first->AddVehicleUpgrade(slot[i]);
                            Command<COMMAND_MARK_VEHICLE_MOD_AS_NO_LONGER_NEEDED>(slot[i]);
                        }
                    }
            }

            while (!vehStack.empty())
            {
                CVehicle* veh = vehStack.top();
                vehStack.pop();

                if (veh->m_nModelIndex >= 400 && veh->m_nModelIndex < 612 && !vehCurrentVariations[veh->m_nModelIndex - 400].empty() &&
                    vehCurrentVariations[veh->m_nModelIndex - 400][0] == 0 && veh->m_nCreatedBy != eVehicleCreatedBy::MISSION_VEHICLE)
                    veh->m_nVehicleFlags.bFadeOut = 1;
                else
                {
                    auto it = vehPassengers.find(veh->m_nModelIndex);
                    if (it != vehPassengers.end() && it->second[0] == 0)
                        for (int i = 0; i < 8; i++)
                            if (veh->m_apPassengers[i] != NULL)
                            {
                                CPed* passenger = veh->m_apPassengers[i];
                                if (passenger->m_pIntelligence)
                                    passenger->m_pIntelligence->FlushImmediately(false);
                                CTheScripts::RemoveThisPed(passenger);
                            }
                }
            }

            while (!pedStack.empty())
            {
                const auto vehDeleteDriver = [](CVehicle *veh) 
                {
                    if (IsPedPointerValid(veh->m_pDriver))
                    {
                        CPed *driver = veh->m_pDriver;
                        if (driver->m_pIntelligence)
                            driver->m_pIntelligence->FlushImmediately(false);
                        CTheScripts::RemoveThisPed(driver);
                    }
                };

                const auto vehDeletePassengers = [](CVehicle* veh) 
                {
                    for (int i = 0; i < 8; i++)
                        if (IsPedPointerValid(veh->m_apPassengers[i]))
                        {
                            CPed* passenger = veh->m_apPassengers[i];
                            if (passenger->m_pIntelligence)
                                passenger->m_pIntelligence->FlushImmediately(false);
                            CTheScripts::RemoveThisPed(passenger);
                        }
                };

                const auto pedDeleteVeh = [vehDeleteDriver, vehDeletePassengers](CPed *ped)
                {
                    CVehicle* veh = ped->m_pVehicle;
                    if (ped->m_pVehicle->m_pDriver == ped)
                    {
                        vehDeleteDriver(veh);
                        vehDeletePassengers(veh);
                        DestroyVehicleAndDriverAndPassengers(veh);
                    }
                    else
                        vehDeletePassengers(veh);
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
                        {
                            if (ped->m_pIntelligence)
                                ped->m_pIntelligence->FlushImmediately(false);
                            CTheScripts::RemoveThisPed(ped);
                        }
                    }

                if (IsPedPointerValid(ped) && enableCloneRemover == 1 && ped->m_nCreatedBy != 2 && CPools::ms_pPedPool) //Clone remover
                {
                    bool includeVariations = std::find(cloneRemoverIncludeVariations.begin(), cloneRemoverIncludeVariations.end(), ped->m_nModelIndex) != cloneRemoverIncludeVariations.end();
                    if (pedDelaySpawn(ped->m_nModelIndex, includeVariations)) //Delete peds spawned before SpawnTime
                    {
                        if (!IsVehiclePointerValid(ped->m_pVehicle))
                        {
                            if (ped->m_pIntelligence)
                                ped->m_pIntelligence->FlushImmediately(false);
                            CTheScripts::RemoveThisPed(ped);
                        }
                        else if (cloneRemoverVehicleOccupants == 1 && !isCarEmpty(ped->m_pVehicle))
                        {                            
                            pedDeleteVeh(ped);
                        }
                    }

                    if (IsPedPointerValid(ped) && !IdExists(cloneRemoverExclusions, ped->m_nModelIndex) && ped->m_nModelIndex > 0) //Delete peds already spawned
                    {
                        if (includeVariations)
                            insertPedSpawnedOriginalModels(ped->m_nModelIndex);
                        else
                            pedTimeSinceLastSpawned.insert({ ped->m_nModelIndex, clock() });

                        for (CPed* ped2 : CPools::ms_pPedPool)
                            if (IsPedPointerValid(ped2) && ped2 != ped && compareOriginalModels(ped->m_nModelIndex, ped2->m_nModelIndex, includeVariations))
                            {
                                if (!IsVehiclePointerValid(ped->m_pVehicle))
                                {
                                    if (ped->m_pIntelligence)
                                        ped->m_pIntelligence->FlushImmediately(false);
                                    CTheScripts::RemoveThisPed(ped);
 
                                    break;
                                }
                                else if (cloneRemoverVehicleOccupants == 1 && !isCarEmpty(ped->m_pVehicle))
                                {
                                    pedDeleteVeh(ped);
                                    break;
                                }
                            }
                    }
                }

                if (IsPedPointerValid(ped) && enablePedWeapons == 1)
                {
                    const auto wepFound = [ped](eWeaponType weaponId, eWeaponType originalWeaponId) -> bool {
                        int weapModel = 0;
                        CWeaponInfo *wInfo = CWeaponInfo::GetWeaponInfo(weaponId, 1);
                        if (wInfo != NULL)
                            weapModel = wInfo->m_nModelId1;

                        if (weapModel >= 321)
                        {
                            CStreaming::RequestModel(weapModel, GAME_REQUIRED);
                            CStreaming::LoadAllRequestedModels(false);

                            if (originalWeaponId > 0)
                                ped->ClearWeapon(originalWeaponId);
                            else
                                ped->ClearWeapons();

                            ped->GiveWeapon(weaponId, 9999, true);                             
                            return true;
                        }
                        return false;
                    };

                    std::string section = std::to_string(ped->m_nModelIndex);
                    auto wepModel = wepPedModels.find(ped->m_nModelIndex);
                    if (wepModel != wepPedModels.end())
                        section = wepModel->second;
                    std::string currentZoneString(currentZone);
                    const int mergeWeapons = iniWeap.ReadInteger(section, "MergeZonesWithGlobal", 0);

                    std::vector<unsigned short> vec;
                    const int disableOnMission = iniWeap.ReadInteger(section, "DisableOnMission", 0);

                    //CVehicleModelInfo* vehModelInfo = static_cast<CVehicleModelInfo*>(CModelInfo::ms_modelInfoPtrs[ped->m_pVehicle->m_nModelIndex]);
                    //vehName = vehModelInfo->m_szGameName;

                    std::string vehId = "";

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

                        if (!(disableOnMission > 0 && isOnMission()))
                        {
                            bool changeWeapon = true;
                            vec = iniWeap.ReadLine(section, vehId + "WEAPONFORCE", READ_WEAPONS);
                            if (!vec.empty())
                            {
                                const eWeaponType forceWeapon = (eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
                                if ((wepChanged = wepFound(forceWeapon, (eWeaponType)0)) == true)
                                    changeWeapon = (bool)CGeneral::GetRandomNumberInRange(0, 2);
                            }

                            if ((changeWeapon || mergeWeapons == 0) && !(vec = iniWeap.ReadLine(section, vehId + currentZoneString + "_WEAPONFORCE", READ_WEAPONS)).empty())
                            {
                                const eWeaponType forceWeapon = (eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
                                wepChanged |= wepFound(forceWeapon, (eWeaponType)0);
                            }
                        }

                        if (!wepChanged && !(disableOnMission > 0 && isOnMission()))
                            for (int i = 0; i < 13; i++)
                                if (ped->m_aWeapons[i].m_eWeaponType > 0)
                                {
                                    const eWeaponType weaponId = ped->m_aWeapons[i].m_eWeaponType;
                                    bool changeZoneWeapon = true;
                                    bool changeZoneSlot = true;
                                    const int currentSlot = ped->m_nActiveWeaponSlot;

                                    std::string slot = "SLOT" + std::to_string(i);
                                    vec = iniWeap.ReadLine(section, vehId + slot, READ_WEAPONS);
                                    if (!vec.empty() && (wepChanged = wepFound((eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())], ped->m_aWeapons[i].m_eWeaponType)) == true)
                                        changeZoneSlot = (bool)CGeneral::GetRandomNumberInRange(0, 2);

                                    if (changeZoneSlot || mergeWeapons == 0)
                                    {
                                        slot = currentZone;
                                        slot += "_SLOT" + std::to_string(i);
                                        vec = iniWeap.ReadLine(section, vehId + slot, READ_WEAPONS);
                                        if (!vec.empty())
                                            wepChanged |= wepFound((eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())], ped->m_aWeapons[i].m_eWeaponType);
                                    }

                                    std::string wep = "WEAPON" + std::to_string(weaponId);
                                    vec = iniWeap.ReadLine(section, vehId + wep, READ_WEAPONS);
                                    if (!vec.empty() && (wepChanged = wepFound((eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())], ped->m_aWeapons[i].m_eWeaponType)) == true)
                                        changeZoneWeapon = (bool)CGeneral::GetRandomNumberInRange(0, 2);

                                    if (changeZoneWeapon || mergeWeapons == 0)
                                    {
                                        wep = currentZone;
                                        wep += "_WEAPON" + std::to_string(weaponId);
                                        vec = iniWeap.ReadLine(section, vehId + wep, READ_WEAPONS);
                                        if (!vec.empty())
                                            wepChanged |= wepFound((eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())], ped->m_aWeapons[i].m_eWeaponType);
                                    }
                                    if (wepChanged)
                                        ped->SetCurrentWeapon(currentSlot);
                                }
                    }
                }
            }
        };

    }
} modelVariations;
