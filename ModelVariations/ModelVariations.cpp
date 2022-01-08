#include "plugin.h"
#include "IniParse.hpp"
#include "LogUtil.hpp"
#include "Vehicles.hpp"

#include "extensions/ScriptCommands.h"

#include "CGeneral.h"
#include "CMessages.h"
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

#include <Psapi.h>
#include <shlwapi.h>

#include "Hooks.hpp"

using namespace plugin;

std::string exeHashes[2] = { "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26",     //HOODLUM
                             "25580ae242c6ecb6f6175ca9b4c912aa042f33986ded87f20721b48302adc9c9" };   //Compact

std::ofstream logfile;
std::set<std::pair<unsigned int, std::string>> modulesSet;
std::set<unsigned int> callChecks;


CIniReader iniVeh("ModelVariations_Vehicles.ini");

std::array<std::vector<short>, 16> pedVariations[300];
std::array<std::vector<short>, 16> vehVariations[212];
std::array<std::vector<short>, 16> vehWantedVariations[212];

std::map<short, short> vehOriginalModels;
std::map<short, std::vector<short>> vehDrivers;
std::map<short, std::vector<short>> vehPassengers;
std::map<short, std::vector<short>> vehDriverGroups[9];
std::map<short, std::vector<short>> vehPassengerGroups[9];
std::map<short, BYTE> modelNumGroups;
std::map<short, BYTE> pedWepVariationTypes;
std::map<unsigned int, std::pair<void*, void*>> hookedCalls;
std::map<short, std::pair<CVector, float>> LightPositions;

std::vector<short> vehCurrentVariations[212];
std::vector<short> pedCurrentVariations[300];

std::vector<short> vehCarGenExclude;

std::stack<CPed*> pedStack;

BYTE dealersFixed = 0;
BYTE callsChecked = 0;
short modelIndex = -1;

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


bool isGameModelPolice(int model)
{
    switch (model)
    {
        case 427: //Enforcer
        case 430: //Predator
        case 432: //Rhino
        case 433: //Barracks
        //case 470: //Patriot
        case 490: //FBI Rancher
        case 496: //Police Maverick
        case 523: //HPV1000
        case 528: //FBI Truck
        case 596: //Police LS
        case 597: //Police SF
        case 598: //Police LV
        case 599: //Police Ranger
        case 601: //S.W.A.T.
            return true;
    }
    return false;
}

void vectorUnion(std::vector<short> &vec1, std::vector<short> &vec2, std::vector<short> &dest)
{
    dest.clear();
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(dest));
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

template <unsigned int address>
void __fastcall UpdateRpHAnimHooked(CEntity* entity)
{
    callMethodOriginal<address>(entity);
    //entity->UpdateRpHAnim();
    if (modelIndex != -1)
        entity->m_nModelIndex = modelIndex;
    modelIndex = -1;
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

        std::vector<short> vecPed = iniLineParser(PED_VARIATION, i, zInfo->m_szLabel, iniPed);
        if (!vecPed.empty())
        {
            std::vector<short> vec2;
            std::sort(vecPed.begin(), vecPed.end());
            vectorUnion(pedCurrentVariations[i], vecPed, vec2);
            pedCurrentVariations[i] = vec2;
        }

        if (i < 212)
        {
            vectorUnion(vehVariations[i][4], vehVariations[i][merge], vehCurrentVariations[i]);

            std::vector<short> vec = iniLineParser(VEHICLE_VARIATION, i+400, zInfo->m_szLabel, iniVeh);
            if (!vec.empty())
            {
                std::vector<short> vec2;
                std::sort(vec.begin(), vec.end());
                vectorUnion(vehCurrentVariations[i], vec, vec2);
                vehCurrentVariations[i] = vec2;
            }

            CWanted* wanted = FindPlayerWanted(-1);
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
                HMODULE modules[500] = {};
                HANDLE hProcess = GetCurrentProcess();
                DWORD cbNeeded = 0;

                if (EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded))
                    for (int i = 0; i < (int)(cbNeeded / sizeof(HMODULE)); i++)
                    {
                        char szModName[MAX_PATH];
                        if (GetModuleFileNameEx(hProcess, modules[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
                            modulesSet.insert(std::make_pair((unsigned int)modules[i], PathFindFileName(szModName)));
                    }

                //for (auto &i : modulesSet)
                //    logfile << std::hex << i.first << " " << i.second << std::endl;
                //logfile << std::endl;


                logfile << "Model Variations " MOD_VERSION << "\n\n\n";

                char exePath[256] = {};
                GetModuleFileName(NULL, exePath, 255);
                char* exeName = PathFindFileName(exePath);

                int filesize = 0;
                std::string hash = hashFile(exePath, filesize);
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

        //https://stackoverflow.com/questions/48467994/how-to-read-only-the-first-value-on-each-line-of-a-csv-file-in-c
        std::string str;
        std::vector <std::string> result;    
        std::ifstream zoneFile("data\\info.zon");

        if (!zoneFile.is_open())
        {
            zoneFile.open("..\\data\\info.zon"); //If asi is in scripts folder
            if (!zoneFile.is_open())
            {
                zoneFile.open("..\\..\\data\\info.zon"); //If asi is in folder in modloader
                if (!zoneFile.is_open())
                    zoneFile.open("..\\..\\..\\data\\info.zon"); //If asi is in folder in folder in modloader
            }
        }
    
        if (enableLog == 1)
        {
            if (zoneFile.is_open())
                logfile << "Zone file 'info.zon' found.\n" << std::endl;
            else
                logfile << "Zone file 'info.zon' NOT found!\n" << std::endl;
        }

        while (std::getline(zoneFile, str)) 
        {
            std::stringstream ss(str);
            if (ss.good())
            {
                std::string substr;
                std::getline(ss, substr, ','); // Grab first names till first comma
                if (substr != "zone" && substr != "end")
                {
                    std::for_each(substr.begin(), substr.end(), [](char& c) {
                        c = ::toupper(c);
                    });
                    result.push_back(substr);
                }
            }
        }
        for (auto &i : result) //for every zone name
        {
            for (int j = 0; j < 212; j++) //for every vehicle id
            {
                std::vector<short> vec = iniLineParser(PED_VARIATION, j+400, i.c_str(), &iniVeh); //get zone name 'i' of veh id 'j+400'

                if (!vec.empty()) //if veh id 'j+400' has variations in zone 'i'
                    for (auto& k : vec) //for every variation 'k' of veh id 'j+400' in zone 'i'
                        if (j + 400 != k && !(isGameModelPolice(j+400) && isGameModelPolice(k)))
                            vehOriginalModels.insert({k, j+400});
            }
        }

        for (int i = 0; i < 300; i++)
        {
            std::vector<short> vec = iniLineParser(PED_VARIATION, i, "Countryside", &iniPed);
            pedVariations[i][0] = vec;
            std::sort(pedVariations[i][0].begin(), pedVariations[i][0].end());

            vec = iniLineParser(PED_VARIATION, i, "LosSantos", &iniPed);
            pedVariations[i][1] = vec;
            std::sort(pedVariations[i][1].begin(), pedVariations[i][1].end());

            vec = iniLineParser(PED_VARIATION, i, "SanFierro", &iniPed);
            pedVariations[i][2] = vec;
            std::sort(pedVariations[i][2].begin(), pedVariations[i][2].end());

            vec = iniLineParser(PED_VARIATION, i, "LasVenturas", &iniPed);
            pedVariations[i][3] = vec;
            std::sort(pedVariations[i][3].begin(), pedVariations[i][3].end());

            vec = iniLineParser(PED_VARIATION, i, "Global", &iniPed);
            pedVariations[i][4] = vec;
            std::sort(pedVariations[i][4].begin(), pedVariations[i][4].end());

            vec = iniLineParser(PED_VARIATION, i, "Desert", &iniPed);
            pedVariations[i][5] = vec;
            std::sort(pedVariations[i][5].begin(), pedVariations[i][5].end());

            vec = iniLineParser(PED_VARIATION, i, "TierraRobada", &iniPed);
            pedVariations[i][6] = vec;
            std::sort(pedVariations[i][6].begin(), pedVariations[i][6].end());
            vectorUnion(pedVariations[i][6], pedVariations[i][5], vec);
            pedVariations[i][6] = vec;

            vec = iniLineParser(PED_VARIATION, i, "BoneCounty", &iniPed);
            pedVariations[i][7] = vec;
            std::sort(pedVariations[i][7].begin(), pedVariations[i][7].end());
            vectorUnion(pedVariations[i][7], pedVariations[i][5], vec);
            pedVariations[i][7] = vec;

            vec = iniLineParser(PED_VARIATION, i, "RedCounty", &iniPed);
            pedVariations[i][8] = vec;
            std::sort(pedVariations[i][8].begin(), pedVariations[i][8].end());
            vectorUnion(pedVariations[i][8], pedVariations[i][0], vec);
            pedVariations[i][8] = vec;

            vec = iniLineParser(PED_VARIATION, i, "Blueberry", &iniPed);
            pedVariations[i][9] = vec;
            std::sort(pedVariations[i][9].begin(), pedVariations[i][9].end());
            vectorUnion(pedVariations[i][9], pedVariations[i][8], vec);
            pedVariations[i][9] = vec;

            vec = iniLineParser(PED_VARIATION, i, "Montgomery", &iniPed);
            pedVariations[i][10] = vec;
            std::sort(pedVariations[i][10].begin(), pedVariations[i][10].end());
            vectorUnion(pedVariations[i][10], pedVariations[i][8], vec);
            pedVariations[i][10] = vec;

            vec = iniLineParser(PED_VARIATION, i, "Dillimore", &iniPed);
            pedVariations[i][11] = vec;
            std::sort(pedVariations[i][11].begin(), pedVariations[i][11].end());
            vectorUnion(pedVariations[i][11], pedVariations[i][8], vec);
            pedVariations[i][10] = vec;

            vec = iniLineParser(PED_VARIATION, i, "PalominoCreek", &iniPed);
            pedVariations[i][12] = vec;
            std::sort(pedVariations[i][12].begin(), pedVariations[i][12].end());
            vectorUnion(pedVariations[i][12], pedVariations[i][8], vec);
            pedVariations[i][10] = vec;

            vec = iniLineParser(PED_VARIATION, i, "FlintCounty", &iniPed);
            pedVariations[i][13] = vec;
            std::sort(pedVariations[i][13].begin(), pedVariations[i][13].end());
            vectorUnion(pedVariations[i][13], pedVariations[i][0], vec);
            pedVariations[i][13] = vec;

            vec = iniLineParser(PED_VARIATION, i, "Whetstone", &iniPed);
            pedVariations[i][14] = vec;
            std::sort(pedVariations[i][14].begin(), pedVariations[i][14].end());
            vectorUnion(pedVariations[i][14], pedVariations[i][0], vec);
            pedVariations[i][14] = vec;

            vec = iniLineParser(PED_VARIATION, i, "AngelPine", &iniPed);
            pedVariations[i][15] = vec;
            std::sort(pedVariations[i][15].begin(), pedVariations[i][15].end());
            vectorUnion(pedVariations[i][15], pedVariations[i][14], vec);
            pedVariations[i][15] = vec;
        }

        if (enableLog == 1)
        {
            std::ifstream pedIni("ModelVariations_Peds.ini");
            if (!pedIni.is_open())
                logfile << "ModelVariations_Peds.ini not found!\n" << std::endl;
            else
                logfile << "--ModelVariations_Peds.ini--\n" << pedIni.rdbuf() << "\n" << std::endl;


            std::ifstream wepIni("ModelVariations_PedWeapons.ini");
            if (!wepIni.is_open())
                logfile << "ModelVariations_PedWeapons.ini not found!\n" << std::endl;
            else
                logfile << "--ModelVariations_PedWeapons.ini--\n" << wepIni.rdbuf() << "\n" << std::endl;


            std::ifstream vehIni("ModelVariations_Vehicles.ini");
            if (!vehIni.is_open())
                logfile << "ModelVariations_Vehicles.ini not found!" << "\n" << std::endl;
            else
                logfile << "--ModelVariations_Vehicles.ini--\n" << vehIni.rdbuf() << "\n" << std::endl;

            logfile << std::endl;
        }

        for (short i = 0; i < 32000; i++)
        {
            BYTE wepVariationType = iniWeap.ReadInteger(std::to_string(i), "VariationType", -1);
            if (wepVariationType == 1 || wepVariationType == 2)
                pedWepVariationTypes.insert(std::make_pair(i, wepVariationType));
        }

        if (enableLog == 1)
        {
            logfile << "pedWepVariationTypes\n";
            for (int i = 0; i < 32000; i++)
            {
                auto it = pedWepVariationTypes.find(i);
                if (it != pedWepVariationTypes.end())
                    logfile << std::dec << i << ": " << (int)(it->second) << "\n";
            }
            logfile << std::endl;
        }

        if (enableVehicles = iniVeh.ReadInteger("Settings", "Enable", 0))
        {
            readVehicleIni();
            installVehicleHooks();
        }

        hookCall(0x5E49EF, UpdateRpHAnimHooked<0x5E49EF>);

        Events::initScriptsEvent += []
        {
            if (loadAllVehicles)
                for (int i = 400; i < 612; i++)
                    CStreaming::RequestModel(i, KEEP_IN_MEMORY);

            dealersFixed = 0;
            callsChecked = 0;
        };

        Events::processScriptsEvent += []
        {
            if (dealersFixed < 10)
                dealersFixed++;
            if (callsChecked < 100)
                callsChecked++;

            if (callsChecked == 100 && enableLog == 1)
            {
                checkAllCalls();
                callsChecked = 101;
            }

            if (dealersFixed == 10)
            {
                if (enableLog == 1)
                    logfile << "Applying drug dealer fix..." << std::endl;
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
                    ((CEntity*)ped)->SetModelIndex(variationModel);
                    ped->m_nModelIndex = index;
                    modelIndex = variationModel;
                }
            }
        };

        Events::gameProcessEvent += []
        {
            CVector pPos = FindPlayerCoors(-1);
            CZone *zInfo = NULL;
            CTheZones::GetZoneInfo(&pPos, &zInfo);
            CPlayerPed* player = FindPlayerPed();
            CWanted* wanted = FindPlayerWanted(-1);
            if (wanted && wanted->m_nWantedLevel != currentWanted)
            {
                if (enableLog == 1)
                {
                    logfile << "Wanted level changed. Updating variations...\n";
                    logfile << "currentWanted = " << currentWanted << " wanted->m_nWantedLevel = " << wanted->m_nWantedLevel << "\n";
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << "\n" << std::endl;
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
                    logfile << "currentZone = " << currentZone << " zInfo->m_szLabel = " << zInfo->m_szLabel << "\n" << std::endl;
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
                CPed* ped = pedStack.top();
                pedStack.pop();
                auto it = pedWepVariationTypes.find(ped->m_nModelIndex);
                if (it != pedWepVariationTypes.end())
                {
                    int activeSlot = ped->m_nActiveWeaponSlot;
                    int loopMax = it->second == 1 ? 13 : 47;

                    for (int i = 0; i < loopMax; i++)
                    {
                        std::vector<short> wvec = iniLineParser(PED_WEAPON_VARIATION, ped->m_nModelIndex, (const char *)i, &iniWeap);

                        if (!wvec.empty())
                        {
                            int slot = i;
                            int random = CGeneral::GetRandomNumberInRange(0, wvec.size());
                            if (it->second == 2)
                                slot = ped->GetWeaponSlot((eWeaponType)i);

                            if (ped->m_aWeapons[slot].m_nType > 0 && ped->m_aWeapons[slot].m_nType != wvec[random])
                            {
                                ped->ClearWeapon(ped->m_aWeapons[slot].m_nType);

                                CStreaming::RequestModel(CWeaponInfo::GetWeaponInfo((eWeaponType)(wvec[random]), 0)->m_nModelId1, 2);
                                CStreaming::LoadAllRequestedModels(false);

                                ped->GiveWeapon((eWeaponType)(wvec[random]), 9999, 0);
                            }
                        }
                    }

                    ped->SetCurrentWeapon(activeSlot);
                }
            }
        };

        
    }
} modelVariations;
