#include "plugin.h"
#include "IniParse.hpp"
#include "LogUtil.hpp"
#include "Vehicles.hpp"
#include "Hooks.hpp"

#include "extensions/ScriptCommands.h"

#include "CGeneral.h"
#include "CMessages.h"
#include "CPopulation.h"
#include "CStreaming.h"
#include "CTheZones.h"
#include "CVector.h"
#include "CWorld.h"

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

std::string exeHashes[2] = { "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26",     //HOODLUM
                             "25580ae242c6ecb6f6175ca9b4c912aa042f33986ded87f20721b48302adc9c9" };   //Compact

std::ofstream logfile;
std::set<std::pair<unsigned int, std::string>> modulesSet;
std::set<std::pair<unsigned int, std::string>> callChecks;


CIniReader iniVeh("ModelVariations_Vehicles.ini");

std::array<std::vector<short>, 16> pedVariations[300];
std::array<std::vector<short>, 16> vehVariations[212];
std::array<std::vector<short>, 6> pedWantedVariations[300];
std::array<std::vector<short>, 6> vehWantedVariations[212];

std::map<short, short> vehOriginalModels;
std::map<short, std::vector<short>> vehDrivers;
std::map<short, std::vector<short>> vehPassengers;
std::map<short, std::vector<short>> vehDriverGroups[9];
std::map<short, std::vector<short>> vehPassengerGroups[9];
std::map<short, BYTE> modelNumGroups;
std::map<unsigned int, std::pair<void*, void*>> hookedCalls;
std::map<short, std::pair<CVector, float>> LightPositions;
std::map<short, int> pedTimeSinceLastSpawned;
std::map<short, short> pedOriginalModels;
std::map<short, std::array<std::vector<short>, 6>> vehGroupWantedVariations;

std::set<short> parkedCars;
std::set<short> vehUseOnlyGroups;

std::stack<CPed*> pedStack;

std::vector<short> cloneRemoverExclusions;
std::vector<short> pedCurrentVariations[300];
std::vector<short> vehCurrentVariations[212];
std::vector<short> vehCarGenExclude;
std::vector<short> vehInheritExclude;

BYTE dealersFixed = 0;
short callsChecked = 0;
short modelIndex = -1;
char currentInterior[16] = {};
char lastInterior[16] = {};

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

void(__fastcall *ProcessControlOriginal)(CAutomobile*) = NULL;
void(__fastcall *PreRenderOriginal)(CAutomobile*) = NULL;
/*
void(*ProcessSuspensionOriginal)(CAutomobile*) = NULL;
void(*SetupSuspensionLinesOriginal)(CAutomobile*) = NULL;
void(*DoBurstAndSoftGroundRatiosOriginal)(CAutomobile*) = NULL;
void(*CAutomobileRenderOriginal)(CAutomobile*) = NULL;
void(__fastcall *VehicleDamageOriginal)(CAutomobile*, void*, float, __int16, int, RwV3d*, RwV3d*, signed int) = NULL;
char(__fastcall* BurstTyreOriginal)(CAutomobile*, void*, char, char) = NULL;
int(__fastcall* ProcessEntityCollisionOriginal)(CAutomobile*, void*, CVehicle*, CColPoint*) = NULL;
*/

static int getVariationOriginalModel(int modelIndex)
{
    int originalModel = modelIndex;

    auto it = pedOriginalModels.find(modelIndex);
    if (it != pedOriginalModels.end())
        return it->second;

    return originalModel;
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

bool IdExists(std::vector<short>& vec, int id)
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

    for (int i = 0; i < 6; i++)
        if (!pedVariations[28][i].empty() || !pedVariations[29][i].empty() || !pedVariations[30][i].empty() || !pedVariations[254][i].empty())
            enableFix = true;

    if (!enableFix)
        return;       

    std::vector<short> totalVariations;

    for (int i = 0; i < 6; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[28][i].begin(), pedVariations[28][i].end());

    for (int i = 0; i < 6; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[29][i].begin(), pedVariations[29][i].end());

    for (int i = 0; i < 6; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[30][i].begin(), pedVariations[30][0].end());

    for (int i = 0; i < 6; i++)
        totalVariations.insert(totalVariations.end(), pedVariations[254][i].begin(), pedVariations[254][0].end());

    std::vector<short> variationsProcessed;

    for (int i = 0; i < (int)totalVariations.size(); i++)
    {
        short variationModel = totalVariations[i];
        if (variationModel > 300 && IdExists(variationsProcessed, variationModel) == false)
            variationsProcessed.push_back(variationModel);
    }

    for (int i = 0; i < (int)(variationsProcessed.size()); i++)
    {
        if (enableLog == 1)
            logfile << variationsProcessed[i] << "\n";
        Command<COMMAND_ALLOCATE_STREAMED_SCRIPT_TO_RANDOM_PED>(19, variationsProcessed[i], 100);
        Command<COMMAND_ATTACH_ANIMS_TO_MODEL>(variationsProcessed[i], "DEALER");
    }
}

void updateVariations(CZone *zInfo, CIniReader *iniPed, CIniReader *iniVeh)
{
    //zInfo->m_szTextKey = BLUEB | zInfo->m_szLabel = BLUEB1

    if (zInfo == NULL || iniPed == NULL || iniVeh == NULL)
        return;

    int merge = CTheZones::m_CurrLevel;
    if (strncmp(zInfo->m_szTextKey, "ROBAD", 7) == 0)
        merge = 6;
    else if (strncmp(zInfo->m_szTextKey, "BONE", 7) == 0)
        merge = 7;
    else if (strncmp(zInfo->m_szTextKey, "RED", 7) == 0)
        merge = 8;
    else if (strncmp(zInfo->m_szTextKey, "BLUEB", 7) == 0)
        merge = 9;
    else if (strncmp(zInfo->m_szTextKey, "MONT", 7) == 0)
        merge = 10;
    else if (strncmp(zInfo->m_szTextKey, "DILLI", 7) == 0)
        merge = 11;
    else if (strncmp(zInfo->m_szTextKey, "PALO", 7) == 0)
        merge = 12;
    else if (strncmp(zInfo->m_szTextKey, "FLINTC", 7) == 0)
        merge = 13;
    else if (strncmp(zInfo->m_szTextKey, "WHET", 7) == 0)
        merge = 14;
    else if (strncmp(zInfo->m_szTextKey, "ANGPI", 7) == 0)
        merge = 15;


    for (int i = 0; i < 300; i++)
    {
        vectorUnion(pedVariations[i][4], pedVariations[i][merge], pedCurrentVariations[i]);
        CWanted* wanted = FindPlayerWanted(-1);

        std::vector<short> vecPed = iniLineParser(std::to_string(i), zInfo->m_szLabel, iniPed);
        if (!vecPed.empty())
        {
            std::vector<short> vec2;
            vectorUnion(pedCurrentVariations[i], vecPed, vec2);
            pedCurrentVariations[i] = vec2;
        }

        vecPed = iniLineParser(std::to_string(i), currentInterior, iniPed);
        if (!vecPed.empty())
        {
            std::vector<short> vec2;
            vectorUnion(pedCurrentVariations[i], vecPed, vec2);
            pedCurrentVariations[i] = vec2;
        }

        if (wanted)
        {
            int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            if (!pedWantedVariations[i][wantedLevel].empty() && !pedCurrentVariations[i].empty())
            {
                std::vector<short>::iterator it = pedCurrentVariations[i].begin();
                while (it != pedCurrentVariations[i].end())
                    if (std::find(pedWantedVariations[i][wantedLevel].begin(), pedWantedVariations[i][wantedLevel].end(), *it) != pedWantedVariations[i][wantedLevel].end())
                        ++it;
                    else
                        it = pedCurrentVariations[i].erase(it);
            }
        }

        if (i < 212)
        {
            vectorUnion(vehVariations[i][4], vehVariations[i][merge], vehCurrentVariations[i]);

            std::vector<short> vec = iniLineParser(std::to_string(i+400), zInfo->m_szLabel, iniVeh);
            if (!vec.empty())
            {
                std::vector<short> vec2;
                vectorUnion(vehCurrentVariations[i], vec, vec2);
                vehCurrentVariations[i] = vec2;
            }

            if (wanted)
            {
                int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
                if (!vehWantedVariations[i][wantedLevel].empty() && !vehCurrentVariations[i].empty())
                {
                    std::vector<short>::iterator it = vehCurrentVariations[i].begin();
                    while (it != vehCurrentVariations[i].end())
                        if (std::find(vehWantedVariations[i][wantedLevel].begin(), vehWantedVariations[i][wantedLevel].end(), *it) != vehWantedVariations[i][wantedLevel].end())
                            ++it;
                        else 
                            it = vehCurrentVariations[i].erase(it);
                }       
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
            for (short j : pedCurrentVariations[i])
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
                for (short j : vehCurrentVariations[i])
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
    if (modelIndex != -1)
        entity->m_nModelIndex = modelIndex;
    modelIndex = -1;
}

class ModelVariations {
public:
    ModelVariations() {
        static CIniReader iniPed("ModelVariations_Peds.ini");
        static CIniReader iniWeap("ModelVariations_PedWeapons.ini");

        static char currentZone[8] = {};

        static int currentWanted = 0;

        if (enableLog = iniVeh.ReadInteger("Settings", "EnableLog", 0))
        {
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
                int filesize = getFilesize(exePath);
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

                std::vector<short> vec = iniLineParser(section, "TierraRobada", &iniPed);
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


                for (int j = 0; j < 16; j++)
                    for (int k = 0; k < (int)(pedVariations[i][j].size()); k++)
                        if (pedVariations[i][j][k] > 0 && pedVariations[i][j][k] < 32000 && pedVariations[i][j][k] != i)
                            pedOriginalModels.insert({ pedVariations[i][j][k], i });
            }
        }

        if (enableLog == 1)
        {
            if (!fileExists("ModelVariations_Peds.ini"))
                logfile << "\nModelVariations_Peds.ini not found!\n" << std::endl;
            else
                logfile <<  "##############################\n"
                            "## ModelVariations_Peds.ini ##\n" 
                            "##############################\n" << fileToString("ModelVariations_Peds.ini") << std::endl;

            if (!fileExists("ModelVariations_PedWeapons.ini"))
                logfile << "\nModelVariations_PedWeapons.ini not found!\n" << std::endl;
            else
                logfile << "####################################\n"
                           "## ModelVariations_PedWeapons.ini ##\n" 
                           "####################################\n" << fileToString("ModelVariations_PedWeapons.ini") << std::endl;

            if (!fileExists("ModelVariations_Vehicles.ini"))
                logfile << "\nModelVariations_Vehicles.ini not found!\n" << std::endl;
            else
                logfile << "##################################\n"
                           "## ModelVariations_Vehicles.ini ##\n" 
                           "##################################\n" << fileToString("ModelVariations_Vehicles.ini") << std::endl;


            logfile << std::endl;
        }

        if (enableVehicles = iniVeh.ReadInteger("Settings", "Enable", 0))
        {
            readVehicleIni();
            installVehicleHooks();
        }

        enableCloneRemover = iniPed.ReadInteger("Settings", "EnableCloneRemover", 0);
        cloneRemoverIncludeVariations = iniPed.ReadInteger("Settings", "CloneRemoverIncludeVariations", 0);
        cloneRemoverVehicleOccupants = iniPed.ReadInteger("Settings", "CloneRemoverIncludeVehicleOccupants", 0);
        cloneRemoverExclusions = iniLineParser("Settings", "CloneRemoverExcludeModels", &iniPed);
        spawnDelay = iniPed.ReadInteger("Settings", "SpawnDelay", 3);
        

        hookCall(0x5E49EF, UpdateRpHAnimHooked<0x5E49EF>);

        Events::initScriptsEvent += []
        {
            if (loadAllVehicles)
                for (int i = 400; i < 612; i++)
                    CStreaming::RequestModel(i, KEEP_IN_MEMORY);

            dealersFixed = 0;
            callsChecked = 0;

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

        Events::pedSetModelEvent.after += [](CPed* ped, int model)
        {
            if (ped->m_nModelIndex > 0 && ped->m_nModelIndex < 300 && !pedCurrentVariations[ped->m_nModelIndex].empty())
            {
                int random = CGeneral::GetRandomNumberInRange(0, pedCurrentVariations[ped->m_nModelIndex].size());
                int variationModel = pedCurrentVariations[ped->m_nModelIndex][random];
                if (variationModel > -1 && variationModel != ped->m_nModelIndex)
                {
                    CStreaming::RequestModel(variationModel, 2);
                    CStreaming::LoadAllRequestedModels(false);
                    unsigned short index = ped->m_nModelIndex;
                    ped->DeleteRwObject();
                    (reinterpret_cast<CEntity*>(ped))->SetModelIndex(variationModel);
                    ped->m_nModelIndex = index;
                    modelIndex = variationModel;
                }
            }
        };

        Events::gameProcessEvent += []
        {
            if (callsChecked < 1000)
                callsChecked++;

            if (callsChecked == 1000 && logfile.is_open())
            {
                checkAllCalls();
                callsChecked = 0;
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
            CPlayerPed* player = FindPlayerPed();
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
                updateVariations(zInfo, &iniPed, &iniVeh);

                if (enableLog == 1)
                    printCurrentVariations();
            }

            if (wanted && wanted->m_nWantedLevel != currentWanted)
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

                currentWanted = wanted->m_nWantedLevel;
                updateVariations(zInfo, &iniPed, &iniVeh);

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
                updateVariations(zInfo, &iniPed, &iniVeh);

                if (enableLog == 1)
                    printCurrentVariations();
            }


            if (enableVehicles == 1)
                hookTaxi();

            while (!pedStack.empty())
            {
                CPed *ped = pedStack.top();
                pedStack.pop();
                bool pedRemoved = false;

                if (enableCloneRemover == 1 && ped->m_nCreatedBy != 2 && CPools::ms_pPedPool)
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
                            for (CPed* occupant : ped->m_pVehicle->m_apPassengers)
                                if (occupant != NULL)
                                {
                                    occupant->m_nPedFlags.bDontRender = 1;
                                    occupant->m_nPedFlags.bFadeOut = 1;
                                }

                            ped->m_pVehicle->m_nVehicleFlags.bFadeOut = 1;
                            ped->m_nPedFlags.bDontRender = 1;
                            ped->m_nPedFlags.bFadeOut = 1;
                            pedRemoved = true;
                        }
                    }

                    if (!pedRemoved)
                    {
                        pedTimeSinceLastSpawned.insert({ ((cloneRemoverIncludeVariations == 1) ? getVariationOriginalModel(ped->m_nModelIndex) : ped->m_nModelIndex), clock() });
                        for (CPed* ped2 : CPools::ms_pPedPool)
                            if (ped2 != NULL  &&  ped2 != ped  &&  ((cloneRemoverIncludeVariations == 1) ?
                                                                    (getVariationOriginalModel(ped->m_nModelIndex) == getVariationOriginalModel(ped2->m_nModelIndex)) :
                                                                    (ped->m_nModelIndex == ped2->m_nModelIndex)) &&
                                ped->m_nModelIndex == ped2->m_nModelIndex  &&  ped2->m_nModelIndex > 0  &&  !IdExists(cloneRemoverExclusions, ped2->m_nModelIndex))
                            {
                                if (ped->m_pVehicle != NULL && cloneRemoverVehicleOccupants == 1)
                                {
                                    for (CPed* occupant : ped->m_pVehicle->m_apPassengers)
                                        if (occupant != NULL)
                                        {
                                            occupant->m_nPedFlags.bDontRender = 1;
                                            occupant->m_nPedFlags.bFadeOut = 1;
                                        }

                                    ped->m_pVehicle->m_nVehicleFlags.bFadeOut = 1;

                                    ped->m_nPedFlags.bDontRender = 1;
                                    ped->m_nPedFlags.bFadeOut = 1;
                                    pedRemoved = true;
                                    break;
                                }
                                else if (ped->m_pVehicle == NULL)
                                {
                                    ped->m_nPedFlags.bDontRender = 1;
                                    ped->m_nPedFlags.bFadeOut = 1;
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
                            std::vector<short> vec = iniLineParser(std::to_string(ped->m_nModelIndex), slot, &iniWeap);
                            if (!vec.empty())
                            {
                                int activeSlot = ped->m_nActiveWeaponSlot;
                                int random = CGeneral::GetRandomNumberInRange(0, vec.size());

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
                                int random = CGeneral::GetRandomNumberInRange(0, vec.size());

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
                                    int random = CGeneral::GetRandomNumberInRange(0, vec.size());

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
                                    int random = CGeneral::GetRandomNumberInRange(0, vec.size());

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
