#include <plugin.h>
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "LogUtil.hpp"
#include "Vehicles.hpp"
#include "Hooks.hpp"
#include "FileUtil.hpp"

#include <extensions/ScriptCommands.h>

#include <CGeneral.h>
#include <CMessages.h>
#include <CModelInfo.h>
#include <CPopulation.h>
#include <CStreaming.h>
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

#include <Psapi.h>
#include <shlwapi.h>

using namespace plugin;

const int MAX_PED_ID = 300;

std::string pedIniPath("ModelVariations_Peds.ini");
std::string pedWepIniPath("ModelVariations_PedWeapons.ini");
std::string vehIniPath("ModelVariations_Vehicles.ini");
std::string settingsIniPath("ModelVariations.ini");


std::string exeHash;
unsigned int exeFilesize = 0;
eExeVersion exeVersion = SA_EXE_NONE;
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
std::map<unsigned short, unsigned short> pedOriginalModels;
std::map<unsigned short, std::array<std::vector<unsigned short>, 6>> vehGroupWantedVariations;
std::map<unsigned short, std::string> wepVariationModels;
std::map<unsigned short, std::vector<unsigned short>> vehCurrentTuning;
std::map<unsigned short, std::string> vehModels;
std::map<unsigned short, std::string> pedModels;

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

std::vector<unsigned short> cloneRemoverExclusions;
std::vector<unsigned short> pedCurrentVariations[MAX_PED_ID];
std::vector<unsigned short> vehCurrentVariations[212];
std::vector<unsigned short> vehCarGenExclude;
std::vector<unsigned short> vehInheritExclude;

BYTE dealersFixed = 0;
short framesSinceCallsChecked = 0;
unsigned short modelIndex = 0;
char currentInterior[16] = {};
char lastInterior[16] = {};
char currentZone[8] = {};
char lastZone[8] = {};
BYTE currentTown = 0;

//ini options
int enableLog = 0;
unsigned int disableKey = 0;
unsigned int reloadKey = 0;

int changeCarGenerators = 0;
bool enableSideMissions = false;
int enableAllSideMissions = 0;
int enableVehicles = 0;
int loadAllVehicles = 0;
int enableLights = 0;
int enableSiren = 0;
int disablePayAndSpray = 0;
int enableSpecialFeatures = 0;
int changeScriptedCars = 0;
int enableCloneRemover = 0;
int cloneRemoverVehicleOccupants = 0;
int cloneRemoverIncludeVariations = 0;
int spawnDelay = 3;

bool keyDown = false;

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

void filterWantedVariations(std::vector<unsigned short>& vec, std::vector<unsigned short>& wantedVec)
{
    bool matchFound = false;
    std::vector<unsigned short> vec2 = vec;

    std::vector<unsigned short>::iterator it = vec.begin();
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

static unsigned short getVariationOriginalModel(unsigned short model)
{
    auto it = pedOriginalModels.find(model);
    if (it != pedOriginalModels.end())
        return it->second;

    return model;
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
    if (id <= 0 && id >= MAX_PED_ID)
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
    char* name = PathFindFileName(path);
    exeName = name;
    exeFilesize = getFilesize(path);

    if (!fileExists(path))
        return;

    if (exeHash.empty())
        exeHash = hashFile(path);

    if (exeHash == exeHashes[0])
        exeVersion = SA_EXE_HOODLUM;
    else if (exeHash == exeHashes[1])
        exeVersion = SA_EXE_COMPACT;
    else
        exeVersion = SA_EXE_UNKNOWN;
}

void drugDealerFix()
{
    bool enableFix = false;

    for (unsigned int i = 0; i < 16; i++)
        if (!pedVariations[28][i].empty() || !pedVariations[29][i].empty() || !pedVariations[30][i].empty() || !pedVariations[254][i].empty())
            enableFix = true;

    if (!enableFix)
        return;

    std::vector<unsigned short> totalVariations;
    std::set<unsigned short> drugDealers;

    for (unsigned int i = 0; i < 16; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[28][i].begin(), pedVariations[28][i].end());

    for (unsigned int i = 0; i < 16; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[29][i].begin(), pedVariations[29][i].end());

    for (unsigned int i = 0; i < 16; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[30][i].begin(), pedVariations[30][i].end());

    for (unsigned int i = 0; i < 16; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[254][i].begin(), pedVariations[254][i].end());



    for (auto& i : totalVariations)
        if (i > MAX_PED_ID)
            drugDealers.insert(i);

    for (auto& i : drugDealers)
    {
        if (enableLog == 1)
            logfile << i << "\n";
        Command<COMMAND_ALLOCATE_STREAMED_SCRIPT_TO_RANDOM_PED>(19, i, 100);
        //Command<COMMAND_ATTACH_ANIMS_TO_MODEL>(i, "DEALER");
    }
}

void updateVariations(CZone* zInfo)
{
    //zInfo->m_szTextKey = BLUEB | zInfo->m_szLabel = BLUEB1

    if (zInfo == NULL)
        return;

    currentTown = (BYTE)CTheZones::m_CurrLevel;
    if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "ROBAD"))
        currentTown = 6;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "BONE"))
        currentTown = 7;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "RED"))
        currentTown = 8;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "BLUEB"))
        currentTown = 9;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "MONT"))
        currentTown = 10;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "DILLI"))
        currentTown = 11;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "PALO"))
        currentTown = 12;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "FLINTC"))
        currentTown = 13;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "WHET"))
        currentTown = 14;
    else if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "ANGPI"))
        currentTown = 15;


    CWanted* wanted = FindPlayerWanted(-1);

    for (auto& modelid : pedHasVariations)
    {
        vectorUnion(pedVariations[modelid][4], pedVariations[modelid][currentTown], pedCurrentVariations[modelid]);

        std::string section;
        auto it = pedModels.find(modelid);
        if (it != pedModels.end())
            section = it->second;
        else
            section = std::to_string(modelid);

        std::vector<unsigned short> vec = iniPed.ReadLine(section, ((lastZone[0] == 0) ? zInfo->m_szLabel : lastZone));
        if (!vec.empty())
        {
            if (pedMergeZones.find(modelid) != pedMergeZones.end())
                pedCurrentVariations[modelid] = vectorUnion(pedCurrentVariations[modelid], vec);
            else
                pedCurrentVariations[modelid] = vec;
        }

        vec = iniPed.ReadLine(section, currentInterior);
        if (!vec.empty())
            pedCurrentVariations[modelid] = vectorUnion(pedCurrentVariations[modelid], vec);

        if (wanted)
        {
            unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            if (!pedWantedVariations[modelid][wantedLevel].empty() && !pedCurrentVariations[modelid].empty())
                filterWantedVariations(pedCurrentVariations[modelid], pedWantedVariations[modelid][wantedLevel]);
        }
    }

    for (auto& i : vehTuning)
    {
        vectorUnion(i.second[4], i.second[currentTown], vehCurrentTuning[i.first]);

        std::string section;
        auto it = vehModels.find(i.first);
        if (it != vehModels.end())
            section = it->second;
        else
            section = std::to_string(i.first);

        std::vector<unsigned short> vec = iniPed.ReadLine(section, ((lastZone[0] == 0) ? zInfo->m_szLabel : lastZone), 2);
        if (!vec.empty())
        {
            if (vehMergeZones.find(i.first) != vehMergeZones.end())
                vehCurrentTuning[i.first] = vectorUnion(vehCurrentTuning[i.first], vec);
            else
                vehCurrentTuning[i.first] = vec;
        }
    }

    for (auto& modelid : vehHasVariations)
    {
        vectorUnion(vehVariations[modelid][4], vehVariations[modelid][currentTown], vehCurrentVariations[modelid]);

        std::string section;
        auto it = vehModels.find(modelid + 400U);
        if (it != vehModels.end())
            section = it->second;
        else
            section = std::to_string(modelid + 400);

        std::vector<unsigned short> vec = iniVeh.ReadLine(section, ((lastZone[0] == 0) ? zInfo->m_szLabel : lastZone));
        if (!vec.empty())
        {
            if (vehMergeZones.find((unsigned short)(modelid + 400)) != vehMergeZones.end())
                vehCurrentVariations[modelid] = vectorUnion(vehCurrentVariations[modelid], vec);
            else
                vehCurrentVariations[modelid] = vec;
        }

        if (wanted)
        {
            unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            if (!vehWantedVariations[modelid][wantedLevel].empty() && !vehCurrentVariations[modelid].empty())
                filterWantedVariations(vehCurrentVariations[modelid], vehWantedVariations[modelid][wantedLevel]);
        }
    }
}

void printCurrentVariations()
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

    if (enableVehicles == 1)
    {

        logfile << "vehCurrentVariations\n";
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

            pedVariations[i][0] = iniPed.ReadLine(section, "Countryside");
            pedVariations[i][1] = iniPed.ReadLine(section, "LosSantos");
            pedVariations[i][2] = iniPed.ReadLine(section, "SanFierro");
            pedVariations[i][3] = iniPed.ReadLine(section, "LasVenturas");
            pedVariations[i][4] = iniPed.ReadLine(section, "Global");
            pedVariations[i][5] = iniPed.ReadLine(section, "Desert");

            std::vector<unsigned short> vec = iniPed.ReadLine(section, "TierraRobada");
            pedVariations[i][6] = vectorUnion(vec, pedVariations[i][5]);

            vec = iniPed.ReadLine(section, "BoneCounty");
            pedVariations[i][7] = vectorUnion(vec, pedVariations[i][5]);

            vec = iniPed.ReadLine(section, "RedCounty");
            pedVariations[i][8] = vectorUnion(vec, pedVariations[i][0]);

            vec = iniPed.ReadLine(section, "Blueberry");
            pedVariations[i][9] = vectorUnion(vec, pedVariations[i][8]);

            vec = iniPed.ReadLine(section, "Montgomery");
            pedVariations[i][10] = vectorUnion(vec, pedVariations[i][8]);

            vec = iniPed.ReadLine(section, "Dillimore");
            pedVariations[i][11] = vectorUnion(vec, pedVariations[i][8]);

            vec = iniPed.ReadLine(section, "PalominoCreek");
            pedVariations[i][12] = vectorUnion(vec, pedVariations[i][8]);

            vec = iniPed.ReadLine(section, "FlintCounty");
            pedVariations[i][13] = vectorUnion(vec, pedVariations[i][0]);

            vec = iniPed.ReadLine(section, "Whetstone");
            pedVariations[i][14] = vectorUnion(vec, pedVariations[i][0]);

            vec = iniPed.ReadLine(section, "AngelPine");
            pedVariations[i][15] = vectorUnion(vec, pedVariations[i][14]);


            pedWantedVariations[i][0] = iniPed.ReadLine(section, "Wanted1");
            pedWantedVariations[i][1] = iniPed.ReadLine(section, "Wanted2");
            pedWantedVariations[i][2] = iniPed.ReadLine(section, "Wanted3");
            pedWantedVariations[i][3] = iniPed.ReadLine(section, "Wanted4");
            pedWantedVariations[i][4] = iniPed.ReadLine(section, "Wanted5");
            pedWantedVariations[i][5] = iniPed.ReadLine(section, "Wanted6");


            for (unsigned int j = 0; j < 16; j++)
                for (unsigned int k = 0; k < pedVariations[i][j].size(); k++)
                    if (pedVariations[i][j][k] > 0 && pedVariations[i][j][k] < 32000 && pedVariations[i][j][k] != i)
                        pedOriginalModels.insert({ pedVariations[i][j][k], i });

            if (iniPed.ReadInteger(section, "MergeZonesWithCities", 0) == 1)
                pedMergeZones.insert((unsigned short)i);

            if (iniPed.ReadInteger(section, "DontInheritBehaviour", 0) == 1)
                dontInheritBehaviourModels.insert((unsigned short)i);
        }
    }

    for (auto& iniData : iniWeap.data)
    {
        int modelid = 0;
        std::string section = iniData.first;

        if (!(section[0] >= '0' && section[0] <= '9'))
            CModelInfo::GetModelInfo((char*)section.c_str(), &modelid);
        if (modelid > 0)
            wepVariationModels.insert({ modelid, section });
    }

    if (firstTime)
    {
        enableCloneRemover = iniPed.ReadInteger("Settings", "EnableCloneRemover", 0);
        cloneRemoverIncludeVariations = iniPed.ReadInteger("Settings", "CloneRemoverIncludeVariations", 0);
        cloneRemoverVehicleOccupants = iniPed.ReadInteger("Settings", "CloneRemoverIncludeVehicleOccupants", 0);
        cloneRemoverExclusions = iniPed.ReadLine("Settings", "CloneRemoverExcludeModels");
        spawnDelay = iniPed.ReadInteger("Settings", "SpawnDelay", 3);
        enableVehicles = iniVeh.ReadInteger("Settings", "Enable", 0);
    }

    if (enableVehicles == 1)
        readVehicleIni(firstTime);
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
    wepVariationModels.clear();
    vehTuning.clear();
    vehCurrentTuning.clear();
    vehModels.clear();
    pedModels.clear();

    //sets
    dontInheritBehaviourModels.clear();
    parkedCars.clear();
    vehUseOnlyGroups.clear();
    pedMergeZones.clear();
    vehMergeZones.clear();
    pedHasVariations.clear();
    vehHasVariations.clear();

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
}

class ModelVariations {
public:
    ModelVariations() {

        if (!fileExists(pedIniPath))
            pedIniPath = "ModelVariations\\" + pedIniPath;

        if (!fileExists(pedWepIniPath))
            pedWepIniPath = "ModelVariations\\" + pedWepIniPath;

        if (!fileExists(vehIniPath))
            vehIniPath = "ModelVariations\\" + vehIniPath;

        if (!fileExists(settingsIniPath))
            settingsIniPath = "ModelVariations\\" + settingsIniPath;

        iniPed.SetIniPath(pedIniPath);
        iniWeap.SetIniPath(pedWepIniPath);
        iniSettings.SetIniPath(settingsIniPath);
        iniVeh.SetIniPath(vehIniPath);

        static int currentWanted = 0;

        disableKey = (unsigned int)iniSettings.ReadInteger("Settings", "DisableKey", 0);
        reloadKey = (unsigned int)iniSettings.ReadInteger("Settings", "ReloadKey", 0);

        if ((enableLog = iniSettings.ReadInteger("Settings", "EnableLog", 0)) == 1)
        {
            if (folderExists("ModelVariations"))
                logfile.open("ModelVariations\\ModelVariations.log");
            else
                logfile.open("ModelVariations.log");

            if (logfile.is_open())
            {
                SYSTEMTIME systime;
                GetSystemTime(&systime);
                logfile << "Model Variations " MOD_VERSION << "\n" << getWindowsVersion() << "\n"
                        << systime.wDay << "/" << systime.wMonth << "/" << systime.wYear << " "
                        << std::setfill('0') << std::setw(2) << systime.wHour << ":"
                        << std::setfill('0') << std::setw(2) << systime.wMinute << ":"
                        << std::setfill('0') << std::setw(2) << systime.wSecond << "\n\n";

                detectExe();
                logfile << exePath << std::endl;

                if (exeVersion == SA_EXE_HOODLUM)
                    logfile << "Supported exe detected: 1.0 US HOODLUM" << std::endl;
                else if (exeVersion == SA_EXE_COMPACT)
                    logfile << "Supported exe detected: 1.0 US Compact" << std::endl;
                else
                    logfile << "Unsupported exe detected: " << exeName << " " << exeFilesize << " bytes " << exeHash << std::endl;
            }
            else
                enableLog = 0;
        }

        if (enableLog == 1)
        {
            if (!fileExists(pedIniPath))
                logfile << "\nModelVariations_Peds.ini not found!\n" << std::endl;
            else
                logfile << "##############################\n"
                           "## ModelVariations_Peds.ini ##\n"
                           "##############################\n" << fileToString(pedIniPath) << std::endl;

            if (!fileExists(pedWepIniPath))
                logfile << "\nModelVariations_PedWeapons.ini not found!\n" << std::endl;
            else
                logfile << "####################################\n"
                           "## ModelVariations_PedWeapons.ini ##\n"
                           "####################################\n" << fileToString(pedWepIniPath) << std::endl;

            if (!fileExists(vehIniPath))
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
            loadIniData(true);
            installHooks();

            if (logfile.is_open())
            {
                logfile << "\nLoaded modules:" << std::endl;

                getLoadedModules();

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
        };

        Events::processScriptsEvent += []
        {
            if (dealersFixed < 10)
                dealersFixed++;

            if (dealersFixed == 10)
            {
                if (enableLog == 1)
                    logfile << "Applying drug dealer fix...\n";
                drugDealerFix();
                if (enableLog == 1)
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
                unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)pedCurrentVariations[ped->m_nModelIndex].size());
                unsigned short variationModel = pedCurrentVariations[ped->m_nModelIndex][random];
                if (variationModel > 0 && variationModel != ped->m_nModelIndex)
                {
                    CStreaming::RequestModel(variationModel, 2);
                    CStreaming::LoadAllRequestedModels(false);
                    unsigned short index = ped->m_nModelIndex;
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
                    if ((clock() - it->second) / CLOCKS_PER_SEC < spawnDelay)
                        it++;
                    else
                        it = pedTimeSinceLastSpawned.erase(it);
            }

            CVector pPos = FindPlayerCoors(-1);
            CZone* zInfo = NULL;
            CTheZones::GetZoneInfo(&pPos, &zInfo);
            CWanted* wanted = FindPlayerWanted(-1);

            Command<COMMAND_GET_NAME_OF_ENTRY_EXIT_CHAR_USED>(FindPlayerPed(), &currentInterior);
            if (strncmp(currentInterior, lastInterior, 16) != 0)
            {
                if (enableLog == 1)
                {
                    logfile << "Interior changed. Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << " lastZone = " << lastZone << "\n";
                    logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n" << std::endl;
                }

                strncpy(lastInterior, currentInterior, 15);
                updateVariations(zInfo);

                if (enableLog == 1)
                    printCurrentVariations();
            }

            if (wanted && (int)(wanted->m_nWantedLevel) != currentWanted)
            {
                if (enableLog == 1)
                {
                    logfile << "Wanted level changed. Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << " lastZone = " << lastZone << "\n";
                    if (currentInterior[0] != 0 || lastInterior[0] != 0)
                        logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n" << std::endl;
                    else
                        logfile << std::endl;
                }

                currentWanted = (int)wanted->m_nWantedLevel;
                updateVariations(zInfo);

                if (enableLog == 1)
                    printCurrentVariations();
            }

            if (zInfo && strncmp(zInfo->m_szLabel, currentZone, 7) != 0)
            {
                if (lastZone[0] == 0 && strncmp(zInfo->m_szLabel, "SAN_AND", 7) == 0)
                    strcpy(lastZone, currentZone);
                else if (strncmp(zInfo->m_szLabel, "SAN_AND", 7) != 0)
                    lastZone[0] = 0;

                if (enableLog == 1)
                {
                    logfile << "Zone changed. Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << " lastZone = " << lastZone << "\n";
                    if (currentInterior[0] != 0 || lastInterior[0] != 0)
                        logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n" << std::endl;
                    else
                        logfile << std::endl;
                }

                strncpy(currentZone, zInfo->m_szLabel, 7);
                updateVariations(zInfo);

                if (enableLog == 1)
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
                            unsigned i = CGeneral::GetRandomNumberInRange(0, slot.size());

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
                                veh->RemovePassenger(veh->m_apPassengers[i]);
                }
            }

            while (!pedStack.empty())
            {
                CPed* ped = pedStack.top();
                pedStack.pop();
                bool pedRemoved = false;

                if (isValidPedId(ped->m_nModelIndex))
                    if (!pedCurrentVariations[ped->m_nModelIndex].empty() && pedCurrentVariations[ped->m_nModelIndex][0] == 0 && ped->m_nCreatedBy != 2) //Delete models with a 0 id variation
                    {
                        if (IsPedPointerValid(ped))
                        {
                            ped->DropEntityThatThisPedIsHolding(true);
                            Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(ped));
                        }
                        pedRemoved = true;
                        if (ped->m_pVehicle != NULL)
                        {
                            CVehicle* veh = ped->m_pVehicle;
                            CPed** passengers = ped->m_pVehicle->m_apPassengers;
                            if (IsPedPointerValid(ped->m_pVehicle->m_pDriver))
                            {
                                ped->m_pVehicle->m_pDriver->DropEntityThatThisPedIsHolding(true);
                                Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(ped->m_pVehicle->m_pDriver));
                                for (int i = 0; i < 8; i++)
                                    if (IsPedPointerValid(passengers[i]))
                                    {
                                        passengers[i]->DropEntityThatThisPedIsHolding(true);
                                        Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(passengers[i]));
                                    }
                            }
                            DestroyVehicleAndDriverAndPassengers(veh);
                        }
                    }

                if (enableCloneRemover == 1 && ped->m_nCreatedBy != 2 && CPools::ms_pPedPool && pedRemoved == false) //Clone remover
                {
                    if (pedTimeSinceLastSpawned.find((cloneRemoverIncludeVariations == 1) ? getVariationOriginalModel(ped->m_nModelIndex) : ped->m_nModelIndex) != pedTimeSinceLastSpawned.end()) //Delete peds spawned before SpawnTime
                    {
                        if (ped->m_pVehicle == NULL)
                        {
                            if (IsPedPointerValid(ped))
                            {
                                ped->DropEntityThatThisPedIsHolding(true);
                                Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(ped));
                            }
                            pedRemoved = true;
                        }
                        else if (cloneRemoverVehicleOccupants == 1)
                        {
                            CVehicle* veh = ped->m_pVehicle;
                            CPed** passengers = ped->m_pVehicle->m_apPassengers;
                            if (IsPedPointerValid(ped->m_pVehicle->m_pDriver))
                            {
                                ped->m_pVehicle->m_pDriver->DropEntityThatThisPedIsHolding(true);
                                Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(ped->m_pVehicle->m_pDriver));
                                for (int i = 0; i < 8; i++)
                                    if (IsPedPointerValid(passengers[i]))
                                    {
                                        passengers[i]->DropEntityThatThisPedIsHolding(true);
                                        Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(passengers[i]));
                                    }
                            }
                            DestroyVehicleAndDriverAndPassengers(veh);
                            pedRemoved = true;
                        }
                    }

                    if (!pedRemoved && !IdExists(cloneRemoverExclusions, ped->m_nModelIndex) && ped->m_nModelIndex > 0) //Delete peds already spawned
                    {
                        pedTimeSinceLastSpawned.insert({ ((cloneRemoverIncludeVariations == 1) ? getVariationOriginalModel(ped->m_nModelIndex) : ped->m_nModelIndex), clock() });
                        for (CPed* ped2 : CPools::ms_pPedPool)
                            if (ped2 != NULL && ped2 != ped && ((cloneRemoverIncludeVariations == 1) ?
                                                                (getVariationOriginalModel(ped->m_nModelIndex) == getVariationOriginalModel(ped2->m_nModelIndex)) :
                                                                (ped->m_nModelIndex == ped2->m_nModelIndex)) && ped->m_nModelIndex == ped2->m_nModelIndex)
                            {
                                if (ped->m_pVehicle == NULL)
                                {
                                    if (IsPedPointerValid(ped))
                                    {
                                        ped->DropEntityThatThisPedIsHolding(true);
                                        Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(ped));
                                    }
                                    pedRemoved = true;
                                    break;
                                }
                                else if (cloneRemoverVehicleOccupants == 1)
                                {
                                    CVehicle* veh = ped->m_pVehicle;
                                    CPed** passengers = ped->m_pVehicle->m_apPassengers;
                                    if (IsPedPointerValid(ped->m_pVehicle->m_pDriver))
                                    {
                                        ped->m_pVehicle->m_pDriver->DropEntityThatThisPedIsHolding(true);
                                        Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(ped->m_pVehicle->m_pDriver));
                                        for (int i = 0; i < 8; i++)
                                            if (IsPedPointerValid(passengers[i]))
                                            {
                                                passengers[i]->DropEntityThatThisPedIsHolding(true);
                                                Command<COMMAND_DELETE_CHAR>(CPools::GetPedRef(passengers[i]));
                                            }
                                    }
                                    DestroyVehicleAndDriverAndPassengers(veh);
                                    pedRemoved = true;
                                    break;
                                }
                            }
                    }
                }


                if (!pedRemoved)
                {
                    auto wepFound = [ped](eWeaponType weaponId, eWeaponType originalWeaponId) -> bool {
                        int weapModel = 0;
                        Command<COMMAND_GET_WEAPONTYPE_MODEL>(weaponId, &weapModel);
                        if (weapModel >= 321)
                        {
                            CStreaming::RequestModel(weapModel, 2);
                            CStreaming::LoadAllRequestedModels(false);

                            if (originalWeaponId > 0)
                                ped->ClearWeapon(originalWeaponId);
                            else
                                ped->ClearWeapons();
                            Command<COMMAND_GIVE_WEAPON_TO_CHAR>(CPools::GetPedRef(ped), weaponId, 9999);
                            //ped->GiveWeapon(weaponId, 9999, 0);                                
                            return true;
                        }
                        return false;
                    };

                    std::string section = std::to_string(ped->m_nModelIndex);
                    auto wepModel = wepVariationModels.find(ped->m_nModelIndex);
                    if (wepModel != wepVariationModels.end())
                        section = wepModel->second;
                    std::string currentZoneString(currentZone);
                    int mergeWeapons = iniWeap.ReadInteger(section, "MergeZonesWithGlobal", 0);

                    //if (ped->m_pVehicle != NULL)
                        //currentVehicle = reinterpret_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(ped->m_pVehicle->m_nModelIndex))->m_szGameName;

                    bool changeWeapon = true;
                    bool wepChanged = false;

                    std::vector<unsigned short> vec = iniWeap.ReadLine(section, "WEAPONFORCE");
                    if (!vec.empty())
                    {
                        eWeaponType forceWeapon = (eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, vec.size())];
                        if ((wepChanged = wepFound(forceWeapon, (eWeaponType)0)) == true)
                            changeWeapon = (bool)CGeneral::GetRandomNumberInRange(0, 2);
                    }

                    if ((changeWeapon || mergeWeapons == 0) && !(vec = iniWeap.ReadLine(section, currentZoneString + "_WEAPONFORCE")).empty())
                    {
                        eWeaponType forceWeapon = (eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, vec.size())];
                        wepChanged |= wepFound(forceWeapon, (eWeaponType)0);
                    }

                    if (!wepChanged)
                        for (int i = 0; i < 13; i++)
                            if (ped->m_aWeapons[i].m_nType > 0)
                            {
                                eWeaponType weaponId = ped->m_aWeapons[i].m_nType;
                                bool changeZoneWeapon = true;
                                bool changeZoneSlot = true;
                                int currentSlot = ped->m_nActiveWeaponSlot;

                                std::string slot = "SLOT" + std::to_string(i);
                                vec = iniWeap.ReadLine(section, slot);
                                if (!vec.empty() && (wepChanged = wepFound((eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())], ped->m_aWeapons[i].m_nType)) == true)
                                    changeZoneSlot = (bool)CGeneral::GetRandomNumberInRange(0, 2);

                                if (changeZoneSlot || mergeWeapons == 0)
                                {
                                    slot = currentZone;
                                    slot += "_SLOT" + std::to_string(i);
                                    vec = iniWeap.ReadLine(section, slot);
                                    if (!vec.empty())
                                        wepChanged |= wepFound((eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())], ped->m_aWeapons[i].m_nType);
                                }

                                std::string wep = "WEAPON" + std::to_string(weaponId);
                                vec = iniWeap.ReadLine(section, wep);
                                if (!vec.empty() && (wepChanged = wepFound((eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())], ped->m_aWeapons[i].m_nType)) == true)
                                    changeZoneWeapon = (bool)CGeneral::GetRandomNumberInRange(0, 2);

                                if (changeZoneWeapon || mergeWeapons == 0)
                                {
                                    wep = currentZone;
                                    wep += "_WEAPON" + std::to_string(weaponId);
                                    vec = iniWeap.ReadLine(section, wep);
                                    if (!vec.empty())
                                        wepChanged |= wepFound((eWeaponType)vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())], ped->m_aWeapons[i].m_nType);
                                }
                                if (wepChanged)
                                    ped->SetCurrentWeapon(currentSlot);
                            }
                }
            }
        };

    }
} modelVariations;
