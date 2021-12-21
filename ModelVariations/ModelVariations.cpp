#include "plugin.h"
#include "Vehicles.h"
#include "commonDef.h"

#include "IniReader/IniReader.h"
#include "extensions/ScriptCommands.h"

#include "CGeneral.h"
#include "CMessages.h"
#include "CStreaming.h"
#include "CTheZones.h"
#include "CWorld.h"

#include <stack>
#include <array>
#include <unordered_set>

/*
--ZONES--

DESERT
    TIERRA ROBADA
        ROBAD
        ROBAD1
    BONE COUNTY
        BONE

COUNTRYSIDE
    RED COUNTY
            RED
        BLUEBERRY
            BLUEB
            BLUEB1
        MONTGOMERY
            MONT
        DILLIMORE
            DILLI
        PALOMINO CREEK
            PALO

    FLINT COUNTY
        FLINTC

    WHETSTONE
            WHET
        ANGEL PINE
            ANGPI
*/

using namespace plugin;

CIniReader iniVeh("ModelVariations_Vehicles.ini");

std::array<std::vector<short>, 16> pedVariations[300];

std::array<std::vector<short>, 16> vehVariations[212];
std::array<std::vector<short>, 16> vehWantedVariations[212];

std::vector<short> currentVehVariations[212];

std::map<short, BYTE> pedWepVariationTypes;

BYTE vehNumVariations[212] = {};

std::stack<CPed*> pedStack;

BYTE dealersFixed = 0;
short modelIndex = -1;

//veh models
std::unordered_set<short> copModels;
std::unordered_set<short> copBikeModels;
std::unordered_set<short> swatModels;
std::unordered_set<short> fbiModels;
std::unordered_set<short> tankModels;
std::unordered_set<short> barracksModels;
std::unordered_set<short> patriotModels;
std::unordered_set<short> heliModels;
std::unordered_set<short> predatorModels;
std::unordered_set<short> ambulanceModels;
std::unordered_set<short> firetruckModels;
std::unordered_set<short> taxiModels;
std::unordered_set<short> pimpModels;
std::unordered_set<short> burglarModels;
std::unordered_set<short> trainModels;

bool isPlayerInTaxi = false;
bool enableSideMissions = false;
int enableVehicles = 0;

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
        Command<COMMAND_ALLOCATE_STREAMED_SCRIPT_TO_RANDOM_PED>(19, variationsProcessed[i], 100);
        Command<COMMAND_ATTACH_ANIMS_TO_MODEL>(variationsProcessed[i], "DEALER");
    }
}


std::vector<short> iniLineParser(eVariationType type, int section, const char key[12], CIniReader *ini)
{
    std::vector<short> retVector;
    if (ini == NULL)
        return retVector;

    std::string sectionString = std::to_string(section);

    std::string keyString;
    if (type == PED_VARIATION || type == VEHICLE_VARIATION) 
        keyString = key;
    else
        keyString = std::to_string((int)(key));

    std::string iniString = ini->ReadString(sectionString, keyString, "");

    if (!iniString.empty())
    {
        char* tkString = new char[iniString.size() + 1];
        strcpy(tkString, iniString.c_str());

        char* token = strtok(tkString, ",");

        while (token != NULL)
        {
            retVector.push_back(atoi(token));
            token = strtok(NULL, ",");
        }

        delete[] tkString;
    }
    return retVector;
}

void __fastcall UpdateRpHAnimHooked(CEntity* entity)
{
    entity->UpdateRpHAnim();
    if (modelIndex != -1)
        entity->m_nModelIndex = modelIndex;
    modelIndex = -1;
}

void updateVariations(std::vector<short> *currentPedVariations, CZone *zInfo, CIniReader *iniPed, CIniReader *iniVeh)
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
        vectorUnion(pedVariations[i][LV_GLOBAL], pedVariations[i][merge], currentPedVariations[i]);

        std::vector<short> vec = iniLineParser(PED_VARIATION, i, zInfo->m_szLabel, iniPed);
        if (!vec.empty())
        {
            std::vector<short> vec2;
            std::sort(vec.begin(), vec.end());
            vectorUnion(currentPedVariations[i], vec, vec2);
            currentPedVariations[i] = vec2;
        }

        if (i < 212)
        {
            vectorUnion(vehVariations[i][LV_GLOBAL], vehVariations[i][merge], currentVehVariations[i]);

            std::vector<short> vec = iniLineParser(VEHICLE_VARIATION, i+400, zInfo->m_szLabel, iniVeh);
            if (!vec.empty())
            {
                std::vector<short> vec2;
                std::sort(vec.begin(), vec.end());
                vectorUnion(currentVehVariations[i], vec, vec2);
                currentVehVariations[i] = vec2;
            }

            CWanted* wanted = FindPlayerWanted(-1);
            if (wanted)
            {
                int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
                if (!vehWantedVariations[i][wantedLevel].empty() && !currentVehVariations[i].empty())
                {
                    std::vector<short>::iterator it = currentVehVariations[i].begin();
                    while (it != currentVehVariations[i].end())
                        if (std::find(vehWantedVariations[i][wantedLevel].begin(), vehWantedVariations[i][wantedLevel].end(), *it) != vehWantedVariations[i][wantedLevel].end())
                            ++it;
                        else 
                            it = currentVehVariations[i].erase(it);
                }       
            }
        }
    }
}

class ModelVariations {
public:
    ModelVariations() {

        static CIniReader iniPed("ModelVariations_Peds.ini");
        static CIniReader iniWeap("ModelVariations_PedWeapons.ini");

        static char currentZone[8] = {};

        static int currentWanted = 0;
        static std::vector<short> currentPedVariations[300];

        //https://stackoverflow.com/questions/48467994/how-to-read-only-the-first-value-on-each-line-of-a-csv-file-in-c
        std::string str;
        std::vector <std::string> result;    
        std::ifstream zoneFile("data\\info.zon");
    
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
                    for (auto &k : vec) //for every variation 'k' of veh id 'j+400' in zone 'i'
                        if (j + 400 != k)
                            switch (j + 400)
                            {
                                case 596: //Police LS
                                case 597: //Police SF
                                case 598: //Police LV
                                case 599: //Police Ranger
                                    copModels.insert(k);
                                    break;
                                case 523: //HPV1000
                                    copBikeModels.insert(k);
                                    break;
                                case 427: //Enforcer
                                case 601: //S.W.A.T.
                                    swatModels.insert(k);
                                    break;
                                case 490: //FBI Rancher
                                case 528: //FBI Truck
                                    fbiModels.insert(k);
                                    break;
                                case 433: //Barracks
                                    barracksModels.insert(k);
                                    break;
                                case 470: //Patriot
                                    patriotModels.insert(k);
                                    break;
                                case 432: //Rhino
                                    tankModels.insert(k);
                                    break;
                                case 430: //Predator
                                    predatorModels.insert(k);
                                    break;
                                case 497: //Police Maverick
                                    heliModels.insert(k);
                                    break;
                                case 407: //Fire Truck
                                    firetruckModels.insert(k);
                                    break;
                                case 416: //Ambulance
                                    ambulanceModels.insert(k);
                                    break;
                                case 420: //Taxi
                                case 438: //Cabbie
                                    taxiModels.insert(k);
                                    break;
                                case 575: //Broadway
                                    pimpModels.insert(k);
                                    break;
                                case 609: //Boxville Mission
                                    burglarModels.insert(k);
                                    break;
                                case 538: //Brown Streak
                                case 537: //Freight
                                    trainModels.insert(k);
                                    break;
                            }
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

        for (short i = 0; i < 32000; i++)
        {
            BYTE wepVariationType = iniWeap.ReadInteger(std::to_string(i), "VariationType", -1);
            if (wepVariationType == 1 || wepVariationType == 2)
                pedWepVariationTypes.insert(std::make_pair(i, wepVariationType));
        }

        if (enableVehicles = iniVeh.ReadInteger("Settings", "Enable", 0))
        {
            readVehicleIni();
            installVehicleHooks();
        }
        patch::RedirectCall(0x5E49EF, UpdateRpHAnimHooked);            

        Events::initScriptsEvent += []
        {
            dealersFixed = 0;
        };

        Events::processScriptsEvent += []
        {
            if (dealersFixed < 10)
                dealersFixed++;
            if (dealersFixed == 10)
            {
                drugDealerFix();
                dealersFixed = 11;
            }
        };

        Events::pedCtorEvent += [](CPed* ped) 
        {
            pedStack.push(ped);
        };

        Events::pedSetModelEvent.after += [](CPed* ped, int model)
        {
            if (ped->m_nModelIndex > 0 && ped->m_nModelIndex < 300 && !currentPedVariations[ped->m_nModelIndex].empty())
            {
                int random = CGeneral::GetRandomNumberInRange(0, currentPedVariations[ped->m_nModelIndex].size());
                int variationModel = currentPedVariations[ped->m_nModelIndex][random];
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
                currentWanted = wanted->m_nWantedLevel;
                updateVariations(currentPedVariations, zInfo, &iniPed, &iniVeh);
            }

            if (zInfo && strncmp(zInfo->m_szLabel, currentZone, 7) != 0)
            {
                strncpy(currentZone, zInfo->m_szLabel, 7);

                updateVariations(currentPedVariations, zInfo, &iniPed, &iniVeh);
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
