#include <plugin.h>
#include "IniParse.hpp"
#include "LogUtil.hpp"
#include "Vehicles.hpp"
#include "Hooks.hpp"
#include "FileUtil.hpp"

#include <extensions/ScriptCommands.h>

#include <CGeneral.h>
#include <CMessages.h>
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

std::string pedIniPath("ModelVariations_Peds.ini");
std::string pedWepIniPath("ModelVariations_PedWeapons.ini");
std::string vehIniPath("ModelVariations_Vehicles.ini");

std::string exeHashes[2] = { "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26",     //HOODLUM
                             "25580ae242c6ecb6f6175ca9b4c912aa042f33986ded87f20721b48302adc9c9" };   //Compact

std::ofstream logfile;
std::set<std::pair<unsigned int, std::string>> modulesSet;
std::set<std::pair<unsigned int, std::string>> callChecks;

CIniReader iniVeh;

std::array<std::vector<unsigned short>, 16> pedVariations[300];
std::array<std::vector<unsigned short>, 16> vehVariations[212];
std::array<std::vector<unsigned short>, 6> pedWantedVariations[300];
std::array<std::vector<unsigned short>, 6> vehWantedVariations[212];

std::map<unsigned short, std::array<std::vector<unsigned short>, 16>> vehGroups;

std::map<unsigned int, hookinfo> hookedCalls;
std::map<unsigned short, unsigned short> vehOriginalModels;
std::map<unsigned short, std::vector<unsigned short>> vehDrivers;
std::map<unsigned short, std::vector<unsigned short>> vehPassengers;
std::map<unsigned short, std::vector<unsigned short>> vehDriverGroups[9];
std::map<unsigned short, std::vector<unsigned short>> vehPassengerGroups[9];
std::map<unsigned short, BYTE> modelNumGroups;
std::map<unsigned short, std::pair<CVector, float>> LightPositions;
std::map<unsigned short, int> pedTimeSinceLastSpawned;
std::map<unsigned short, unsigned short> pedOriginalModels;
std::map<unsigned short, std::array<std::vector<unsigned short>, 6>> vehGroupWantedVariations;

std::set<unsigned short> parkedCars;
std::set<unsigned short> vehUseOnlyGroups;
std::set<unsigned short> pedMergeZones;
std::set<unsigned short> vehMergeZones;

std::stack<CPed*> pedStack;
std::stack<CVehicle*> vehStack;

std::vector<unsigned short> cloneRemoverExclusions;
std::vector<unsigned short> pedCurrentVariations[300];
std::vector<unsigned short> vehCurrentVariations[212];
std::vector<unsigned short> vehCarGenExclude;
std::vector<unsigned short> vehInheritExclude;

BYTE dealersFixed = 0;
short framesSinceCallsChecked = 0;
unsigned short modelIndex = 0;
char currentInterior[16] = {};
char lastInterior[16] = {};
char currentZone[8] = {};
BYTE currentTown = 0;

//ini options
int enableLog = 0;
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

bool isPlayerInDesert()
{
    if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "ROBAD") ||
        Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "BONE"))
        return true;

    return false;
}

bool isPlayerInCountry()
{
    if (Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "RED") ||
        Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "FLINTC") ||
        Command<COMMAND_IS_CHAR_IN_ZONE>(FindPlayerPed(), "WHET"))
        return true;

    return false;
}

bool IdExists(std::vector<unsigned short>& vec, int id)
{
    if (vec.size() < 1)
        return false;

    if (std::find(vec.begin(), vec.end(), id) != vec.end())
        return true;

    return false;
}

void drugDealerFix(void)
{
    bool enableFix = false;

    for (unsigned int i = 0; i < 6; i++)
        if (!pedVariations[28][i].empty() || !pedVariations[29][i].empty() || !pedVariations[30][i].empty() || !pedVariations[254][i].empty())
            enableFix = true;

    if (!enableFix)
        return;       

    std::vector<unsigned short> totalVariations;

    for (unsigned int i = 0; i < 6; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[28][i].begin(), pedVariations[28][i].end());

    for (unsigned int i = 0; i < 6; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[29][i].begin(), pedVariations[29][i].end());

    for (unsigned int i = 0; i < 6; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[30][i].begin(), pedVariations[30][0].end());

    for (unsigned int i = 0; i < 6; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[254][i].begin(), pedVariations[254][0].end());

    std::vector<unsigned short> variationsProcessed;

    for (unsigned int i = 0; i < totalVariations.size(); i++)
    {
        unsigned short variationModel = totalVariations[i];
        if (variationModel > 300 && IdExists(variationsProcessed, variationModel) == false)
            variationsProcessed.push_back(variationModel);
    }

    for (unsigned int i = 0; i < variationsProcessed.size(); i++)
    {
        if (enableLog == 1)
            logfile << variationsProcessed[i] << "\n";
        Command<COMMAND_ALLOCATE_STREAMED_SCRIPT_TO_RANDOM_PED>(19, variationsProcessed[i], 100);
        Command<COMMAND_ATTACH_ANIMS_TO_MODEL>(variationsProcessed[i], "DEALER");
    }
}

void updateVariations(CZone* zInfo, CIniReader* iniPed)
{
    //zInfo->m_szTextKey = BLUEB | zInfo->m_szLabel = BLUEB1

    if (zInfo == NULL || iniPed == NULL)
        return;

    currentTown = (BYTE)CTheZones::m_CurrLevel;
    if (strncmp(zInfo->m_szTextKey, "ROBAD", 7) == 0)
        currentTown = 6;
    else if (strncmp(zInfo->m_szTextKey, "BONE", 7) == 0)
        currentTown = 7;
    else if (strncmp(zInfo->m_szTextKey, "RED", 7) == 0)
        currentTown = 8;
    else if (strncmp(zInfo->m_szTextKey, "BLUEB", 7) == 0)
        currentTown = 9;
    else if (strncmp(zInfo->m_szTextKey, "MONT", 7) == 0)
        currentTown = 10;
    else if (strncmp(zInfo->m_szTextKey, "DILLI", 7) == 0)
        currentTown = 11;
    else if (strncmp(zInfo->m_szTextKey, "PALO", 7) == 0)
        currentTown = 12;
    else if (strncmp(zInfo->m_szTextKey, "FLINTC", 7) == 0)
        currentTown = 13;
    else if (strncmp(zInfo->m_szTextKey, "WHET", 7) == 0)
        currentTown = 14;
    else if (strncmp(zInfo->m_szTextKey, "ANGPI", 7) == 0)
        currentTown = 15;


    CWanted* wanted = FindPlayerWanted(-1);

    for (auto& i : iniPed->data)
        if (i.first[0] >= '0' && i.first[0] <= '9')
        {
            int modelid = std::stoi(i.first);
            if (modelid > 0 && modelid < 300)
            {
                vectorUnion(pedVariations[modelid][4], pedVariations[modelid][currentTown], pedCurrentVariations[modelid]);

                std::vector<unsigned short> vec = iniLineParser(i.first, zInfo->m_szLabel, iniPed);
                if (!vec.empty())
                {
                    if (pedMergeZones.find(modelid) != pedMergeZones.end())
                        pedCurrentVariations[modelid] = vectorUnion(pedCurrentVariations[modelid], vec);
                    else
                        pedCurrentVariations[modelid] = vec;
                }

                vec = iniLineParser(i.first, currentInterior, iniPed);
                if (!vec.empty())
                    pedCurrentVariations[modelid] = vectorUnion(pedCurrentVariations[modelid], vec);

                if (wanted)
                {
                    unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
                    if (!pedWantedVariations[modelid][wantedLevel].empty() && !pedCurrentVariations[modelid].empty())
                        filterWantedVariations(pedCurrentVariations[modelid], pedWantedVariations[modelid][wantedLevel]);
                }
            }
        }


    for (auto& i : iniVeh.data)
        if (i.first[0] >= '0' && i.first[0] <= '9')
        {
            int modelid = std::stoi(i.first) - 400;
            if (modelid > 0 && modelid < 212)
            {
                vectorUnion(vehVariations[modelid][4], vehVariations[modelid][currentTown], vehCurrentVariations[modelid]);

                std::vector<unsigned short> vec = iniLineParser(i.first, zInfo->m_szLabel, &iniVeh);
                if (!vec.empty())
                {
                    if (vehMergeZones.find(modelid) != vehMergeZones.end())
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
}

void printCurrentVariations()
{
    logfile << std::dec << "pedCurrentVariations\n";
    for (int i = 0; i < 300; i++)
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

template <unsigned int address>
void __fastcall UpdateRpHAnimHooked(CEntity* entity)
{
    callMethodOriginal<address>(entity);
    //entity->UpdateRpHAnim();
    if (modelIndex > 0)
        entity->m_nModelIndex = modelIndex;
    modelIndex = 0;
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

        static CIniReader iniPed(pedIniPath);
        static CIniReader iniWeap(pedWepIniPath);
        iniVeh.SetIniPath(vehIniPath);

        static int currentWanted = 0;

        if ((enableLog = iniVeh.ReadInteger("Settings", "EnableLog", 0)) == 1)
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
                        << std::setfill('0') << std::setw(2) << systime.wHour <<  ":" 
                        << std::setfill('0') << std::setw(2) << systime.wMinute << ":"
                        << std::setfill('0') << std::setw(2) << systime.wSecond << "\n\n";
                

                char exePath[256] = {};
                GetModuleFileName(NULL, exePath, 255);
                char* exeName = PathFindFileName(exePath);
                unsigned int filesize = getFilesize(exePath);
                std::string hash = hashFile(exePath);
                if (hash == exeHashes[0])
                    logfile << "Supported exe detected: 1.0 US HOODLUM" << std::endl;
                else if (hash == exeHashes[1])
                    logfile << "Supported exe detected: 1.0 US Compact" << std::endl;
                else
                    logfile << "Unsupported exe detected: " << exeName << " " << filesize << " bytes " << hash << std::endl;

                logfile << std::endl;
            }
            else
                enableLog = 0;
        }

        for (int i = 0; i < 300; i++)
        {
            std::string section = std::to_string(i);

            if (iniPed.data.find(section) != iniPed.data.end())
            {
                pedVariations[i][0] = iniLineParser(section, "Countryside", &iniPed);
                pedVariations[i][1] = iniLineParser(section, "LosSantos", &iniPed);
                pedVariations[i][2] = iniLineParser(section, "SanFierro", &iniPed);
                pedVariations[i][3] = iniLineParser(section, "LasVenturas", &iniPed);
                pedVariations[i][4] = iniLineParser(section, "Global", &iniPed);
                pedVariations[i][5] = iniLineParser(section, "Desert", &iniPed);

                std::vector<unsigned short> vec = iniLineParser(section, "TierraRobada", &iniPed);
                pedVariations[i][6] = vectorUnion(vec, pedVariations[i][5]);

                vec = iniLineParser(section, "BoneCounty", &iniPed);
                pedVariations[i][7] = vectorUnion(vec, pedVariations[i][5]);

                vec = iniLineParser(section, "RedCounty", &iniPed);
                pedVariations[i][8] = vectorUnion(vec, pedVariations[i][0]);

                vec = iniLineParser(section, "Blueberry", &iniPed);
                pedVariations[i][9] = vectorUnion(vec, pedVariations[i][8]);

                vec = iniLineParser(section, "Montgomery", &iniPed);
                pedVariations[i][10] = vectorUnion(vec, pedVariations[i][8]);

                vec = iniLineParser(section, "Dillimore", &iniPed);
                pedVariations[i][11] = vectorUnion(vec, pedVariations[i][8]);

                vec = iniLineParser(section, "PalominoCreek", &iniPed);
                pedVariations[i][12] = vectorUnion(vec, pedVariations[i][8]);

                vec = iniLineParser(section, "FlintCounty", &iniPed);
                pedVariations[i][13] = vectorUnion(vec, pedVariations[i][0]);

                vec = iniLineParser(section, "Whetstone", &iniPed);
                pedVariations[i][14] = vectorUnion(vec, pedVariations[i][0]);

                vec = iniLineParser(section, "AngelPine", &iniPed);
                pedVariations[i][15] = vectorUnion(vec, pedVariations[i][14]);


                pedWantedVariations[i][0] = iniLineParser(section, "Wanted1", &iniPed);
                pedWantedVariations[i][1] = iniLineParser(section, "Wanted2", &iniPed);
                pedWantedVariations[i][2] = iniLineParser(section, "Wanted3", &iniPed);
                pedWantedVariations[i][3] = iniLineParser(section, "Wanted4", &iniPed);
                pedWantedVariations[i][4] = iniLineParser(section, "Wanted5", &iniPed);
                pedWantedVariations[i][5] = iniLineParser(section, "Wanted6", &iniPed);


                for (unsigned int j = 0; j < 16; j++)
                    for (unsigned int k = 0; k < pedVariations[i][j].size(); k++)
                        if (pedVariations[i][j][k] > 0 && pedVariations[i][j][k] < 32000 && pedVariations[i][j][k] != i)
                            pedOriginalModels.insert({ pedVariations[i][j][k], i });

                if (iniPed.ReadInteger(section, "MergeZonesWithCities", 0) == 1)
                    pedMergeZones.insert(i);
            }
        }

        if (enableLog == 1)
        {
            if (!fileExists(pedIniPath))
                logfile << "\nModelVariations_Peds.ini not found!\n" << std::endl;
            else
                logfile <<  "##############################\n"
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
        }

        if ((enableVehicles = iniVeh.ReadInteger("Settings", "Enable", 0)) == 1)
        {
            readVehicleIni();
            installVehicleHooks();
        }

        enableCloneRemover = iniPed.ReadInteger("Settings", "EnableCloneRemover", 0);
        cloneRemoverIncludeVariations = iniPed.ReadInteger("Settings", "CloneRemoverIncludeVariations", 0);
        cloneRemoverVehicleOccupants = iniPed.ReadInteger("Settings", "CloneRemoverIncludeVehicleOccupants", 0);
        cloneRemoverExclusions = iniLineParser("Settings", "CloneRemoverExcludeModels", &iniPed);
        spawnDelay = iniPed.ReadInteger("Settings", "SpawnDelay", 3);
        

        hookCall(0x5E49EF, UpdateRpHAnimHooked<0x5E49EF>, "UpdateRpHAnim");

        Events::initScriptsEvent += []
        {
            if (loadAllVehicles)
                for (int i = 400; i < 612; i++)
                    CStreaming::RequestModel(i, KEEP_IN_MEMORY);

            dealersFixed = 0;
            framesSinceCallsChecked = 700;

            if (logfile.is_open())
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
                            modulesSet.insert(std::make_pair((unsigned int)modules[i], PathFindFileName(szModName)));
                    }

                //for (auto& i : modulesSet)
                //    logfile << std::hex << i.first << " " << i.second << std::endl;
                //logfile << std::endl;
            }

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
            if (ped->m_nModelIndex > 0 && ped->m_nModelIndex < 300 && !pedCurrentVariations[ped->m_nModelIndex].empty())
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
                    ped->m_nModelIndex = index;
                    modelIndex = variationModel;
                }
            }
        };

        Events::gameProcessEvent += []
        {
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
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << "\n";
                    logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n" << std::endl;
                }

                strncpy(lastInterior, currentInterior, 15);
                updateVariations(zInfo, &iniPed);

                if (enableLog == 1)
                    printCurrentVariations();
            }

            if (wanted && (int)(wanted->m_nWantedLevel) != currentWanted)
            {
                if (enableLog == 1)
                {
                    logfile << "Wanted level changed. Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << "\n";
                    if (currentInterior[0] != 0 || lastInterior[0] != 0)
                        logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n" << std::endl;
                    else
                        logfile << std::endl;
                }

                currentWanted = (int)wanted->m_nWantedLevel;
                updateVariations(zInfo, &iniPed);

                if (enableLog == 1)
                    printCurrentVariations();
            }

            if (zInfo && strncmp(zInfo->m_szLabel, currentZone, 7) != 0)
            {
                if (enableLog == 1)
                {
                    logfile << "Zone changed. Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << "\n";
                    if (currentInterior[0] != 0 || lastInterior[0] != 0)
                        logfile << "currentInterior = " << currentInterior << " lastInterior = " << lastInterior << "\n" << std::endl;
                    else
                        logfile << std::endl;
                }

                strncpy(currentZone, zInfo->m_szLabel, 7);
                updateVariations(zInfo, &iniPed);

                if (enableLog == 1)
                    printCurrentVariations();
            }


            if (enableVehicles == 1)
                hookTaxi();

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
                CPed *ped = pedStack.top();
                pedStack.pop();
                bool pedRemoved = false;

                if (ped->m_nModelIndex > 0 && ped->m_nModelIndex < 300)
                    if (!pedCurrentVariations[ped->m_nModelIndex].empty() && pedCurrentVariations[ped->m_nModelIndex][0] == 0 && ped->m_nCreatedBy != 2)
                    {
                        ped->m_nPedFlags.bDontRender = 1;
                        ped->m_nPedFlags.bFadeOut = 1;
                        pedRemoved = true;
                        if (ped->m_pVehicle != NULL)
                            DestroyVehicleAndDriverAndPassengers(ped->m_pVehicle);
                    }

                if (enableCloneRemover == 1 && ped->m_nCreatedBy != 2 && CPools::ms_pPedPool && pedRemoved == false)
                {
                    if (pedTimeSinceLastSpawned.find((cloneRemoverIncludeVariations == 1) ? getVariationOriginalModel(ped->m_nModelIndex) : ped->m_nModelIndex) != pedTimeSinceLastSpawned.end())
                    {
                        if (ped->m_pVehicle == NULL)
                        {
                            ped->m_nPedFlags.bDontRender = 1;
                            ped->m_nPedFlags.bFadeOut = 1;
                            pedRemoved = true;
                        }
                        else if (cloneRemoverVehicleOccupants == 1)
                        {
                            DestroyVehicleAndDriverAndPassengers(ped->m_pVehicle);
                            pedRemoved = true;
                        }
                    }

                    if (!pedRemoved && !IdExists(cloneRemoverExclusions, ped->m_nModelIndex) && ped->m_nModelIndex > 0)
                    {
                        pedTimeSinceLastSpawned.insert({ ((cloneRemoverIncludeVariations == 1) ? getVariationOriginalModel(ped->m_nModelIndex) : ped->m_nModelIndex), clock() });
                        for (CPed* ped2 : CPools::ms_pPedPool)
                            if (ped2 != NULL  &&  ped2 != ped  &&  ((cloneRemoverIncludeVariations == 1) ?
                                                                    (getVariationOriginalModel(ped->m_nModelIndex) == getVariationOriginalModel(ped2->m_nModelIndex)) :
                                                                    (ped->m_nModelIndex == ped2->m_nModelIndex)) &&  ped->m_nModelIndex == ped2->m_nModelIndex)
                            {
                                if (ped->m_pVehicle == NULL)
                                {
                                    ped->m_nPedFlags.bDontRender = 1;
                                    ped->m_nPedFlags.bFadeOut = 1;
                                    pedRemoved = true;
                                    break;
                                }
                                else if (cloneRemoverVehicleOccupants == 1)
                                {
                                    DestroyVehicleAndDriverAndPassengers(ped->m_pVehicle);
                                    pedRemoved = true;
                                    break;
                                }
                            }
                    }
                }


                if (!pedRemoved)
                    for (int i = 0; i < 13; i++)
                    {
                        if (ped->m_aWeapons[i].m_nType > 0)
                        {
                            bool slotChanged = false;
                            bool wepChanged = false;

                            std::string slot = "SLOT" + std::to_string(i);
                            std::vector<unsigned short> vec = iniLineParser(std::to_string(ped->m_nModelIndex), slot, &iniWeap);
                            if (!vec.empty())
                            {
                                int activeSlot = ped->m_nActiveWeaponSlot;
                                unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)vec.size());

                                ped->ClearWeapon(ped->m_aWeapons[i].m_nType);

                                CStreaming::RequestModel(CWeaponInfo::GetWeaponInfo((eWeaponType)(vec[random]), 0)->m_nModelId1, 2);
                                CStreaming::LoadAllRequestedModels(false);

                                ped->GiveWeapon((eWeaponType)(vec[random]), 9999, 0);
                                ped->SetCurrentWeapon(activeSlot);
                                slotChanged = true;
                            }

                            std::string wep = "WEAPON" + std::to_string(ped->m_aWeapons[i].m_nType);
                            vec = iniLineParser(std::to_string(ped->m_nModelIndex), wep, &iniWeap);
                            if (!vec.empty())
                            {
                                int activeSlot = ped->m_nActiveWeaponSlot;
                                unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)vec.size());

                                ped->ClearWeapon(ped->m_aWeapons[i].m_nType);

                                CStreaming::RequestModel(CWeaponInfo::GetWeaponInfo((eWeaponType)(vec[random]), 0)->m_nModelId1, 2);
                                CStreaming::LoadAllRequestedModels(false);

                                ped->GiveWeapon((eWeaponType)(vec[random]), 9999, 0);
                                ped->SetCurrentWeapon(activeSlot);
                                wepChanged = true;
                            }
                        
                            if ((slotChanged && CGeneral::GetRandomNumberInRange(0, 100) > 50) || !slotChanged)
                            {
                                std::string curZone(currentZone);
                                slot = curZone + "_SLOT" + std::to_string(i);
                                vec = iniLineParser(std::to_string(ped->m_nModelIndex), slot, &iniWeap);
                                if (!vec.empty())
                                {
                                    int activeSlot = ped->m_nActiveWeaponSlot;
                                    unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)vec.size());

                                    ped->ClearWeapon(ped->m_aWeapons[i].m_nType);

                                    CStreaming::RequestModel(CWeaponInfo::GetWeaponInfo((eWeaponType)(vec[random]), 0)->m_nModelId1, 2);
                                    CStreaming::LoadAllRequestedModels(false);

                                    ped->GiveWeapon((eWeaponType)(vec[random]), 9999, 0);
                                    ped->SetCurrentWeapon(activeSlot);
                                }
                            }

                            if ((wepChanged && CGeneral::GetRandomNumberInRange(0, 100) > 50) || !wepChanged)
                            {
                                std::string curZone(currentZone);
                                wep = curZone + "_WEAPON" + std::to_string(ped->m_aWeapons[i].m_nType);
                                vec = iniLineParser(std::to_string(ped->m_nModelIndex), wep, &iniWeap);
                                if (!vec.empty())
                                {
                                    int activeSlot = ped->m_nActiveWeaponSlot;
                                    unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)vec.size());

                                    ped->ClearWeapon(ped->m_aWeapons[i].m_nType);

                                    CStreaming::RequestModel(CWeaponInfo::GetWeaponInfo((eWeaponType)(vec[random]), 0)->m_nModelId1, 2);
                                    CStreaming::LoadAllRequestedModels(false);

                                    ped->GiveWeapon((eWeaponType)(vec[random]), 9999, 0);
                                    ped->SetCurrentWeapon(activeSlot);
                                    wepChanged = true;
                                }
                            }
                        }
                    }
            }
        };

    }
} modelVariations;
