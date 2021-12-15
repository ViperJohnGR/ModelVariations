#include "plugin.h"
#include "Vehicles.h"
#include "commonDef.h"

#include "IniReader/IniReader.h"
#include "extensions/ScriptCommands.h"

#include "CGeneral.h"
#include "CMessages.h"
#include "CStreaming.h"
#include "CTheZones.h"

#include <stack>
#include <array>


using namespace plugin;

CIniReader iniVeh("ModelVariations_Vehicles.ini");

std::array<std::vector<short>, 6> pedVariations[300];
std::array<std::vector<short>, 6> vehVariations[212];

std::vector<short> currentVehVariations[212];

BYTE pedWepVariationTypes[300] = {};
BYTE vehNumVariations[212] = {};

std::stack<CPed*> pedStack;

BYTE dealersFixed = 0;
short modelIndex = -1;

//veh models
std::vector<short> copModels;
std::vector<short> copBikeModels;
std::vector<short> swatModels;
std::vector<short> fbiModels;
std::vector<short> tankModels;
std::vector<short> barracksModels;
std::vector<short> patriotModels;
std::vector<short> heliModels;
std::vector<short> predatorModels;
std::vector<short> ambulanceModels;
std::vector<short> firetruckModels;
std::vector<short> taxiModels;
std::vector<short> pimpModels;
std::vector<short> burglarModels;
std::vector<short> trainModels;


bool isPlayerInTaxi = false;
bool enableSideMissions = false;
int enableVehicles = 0;

//debug
//std::ofstream myfile("output.txt");

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
    if (pedVariations[28][0].empty() && pedVariations[28][1].empty() && pedVariations[28][2].empty() &&
        pedVariations[28][3].empty() && pedVariations[28][4].empty() && pedVariations[28][5].empty() &&

        pedVariations[29][0].empty() && pedVariations[29][1].empty() && pedVariations[29][2].empty() &&
        pedVariations[29][3].empty() && pedVariations[29][4].empty() && pedVariations[29][5].empty() &&

        pedVariations[30][0].empty() && pedVariations[30][1].empty() && pedVariations[30][2].empty() &&
        pedVariations[30][3].empty() && pedVariations[30][4].empty() && pedVariations[30][5].empty() &&

        pedVariations[254][0].empty() && pedVariations[254][1].empty() && pedVariations[254][2].empty() &&
        pedVariations[254][3].empty() && pedVariations[254][4].empty() && pedVariations[254][5].empty())
        return;
       

    std::vector<short> totalVariations;

    totalVariations.insert(totalVariations.end(), pedVariations[28][0].begin(), pedVariations[28][0].end());
    totalVariations.insert(totalVariations.end(), pedVariations[28][1].begin(), pedVariations[28][1].end());
    totalVariations.insert(totalVariations.end(), pedVariations[28][2].begin(), pedVariations[28][2].end());
    totalVariations.insert(totalVariations.end(), pedVariations[28][3].begin(), pedVariations[28][3].end());
    totalVariations.insert(totalVariations.end(), pedVariations[28][4].begin(), pedVariations[28][4].end());
    totalVariations.insert(totalVariations.end(), pedVariations[28][5].begin(), pedVariations[28][5].end());

    totalVariations.insert(totalVariations.end(), pedVariations[29][0].begin(), pedVariations[29][0].end());
    totalVariations.insert(totalVariations.end(), pedVariations[29][1].begin(), pedVariations[29][1].end());
    totalVariations.insert(totalVariations.end(), pedVariations[29][2].begin(), pedVariations[29][2].end());
    totalVariations.insert(totalVariations.end(), pedVariations[29][3].begin(), pedVariations[29][3].end());
    totalVariations.insert(totalVariations.end(), pedVariations[29][4].begin(), pedVariations[29][4].end());
    totalVariations.insert(totalVariations.end(), pedVariations[29][5].begin(), pedVariations[29][5].end());

    totalVariations.insert(totalVariations.end(), pedVariations[30][0].begin(), pedVariations[30][0].end());
    totalVariations.insert(totalVariations.end(), pedVariations[30][1].begin(), pedVariations[30][1].end());
    totalVariations.insert(totalVariations.end(), pedVariations[30][2].begin(), pedVariations[30][2].end());
    totalVariations.insert(totalVariations.end(), pedVariations[30][3].begin(), pedVariations[30][3].end());
    totalVariations.insert(totalVariations.end(), pedVariations[30][4].begin(), pedVariations[30][4].end());
    totalVariations.insert(totalVariations.end(), pedVariations[30][5].begin(), pedVariations[30][5].end());

    totalVariations.insert(totalVariations.end(), pedVariations[254][0].begin(), pedVariations[254][0].end());
    totalVariations.insert(totalVariations.end(), pedVariations[254][1].begin(), pedVariations[254][1].end());
    totalVariations.insert(totalVariations.end(), pedVariations[254][2].begin(), pedVariations[254][2].end());
    totalVariations.insert(totalVariations.end(), pedVariations[254][3].begin(), pedVariations[254][3].end());
    totalVariations.insert(totalVariations.end(), pedVariations[254][4].begin(), pedVariations[254][4].end());
    totalVariations.insert(totalVariations.end(), pedVariations[254][5].begin(), pedVariations[254][5].end());

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

class ModelVariations {
public:
    ModelVariations() {

        static CIniReader iniPed("ModelVariations_Peds.ini");
        static CIniReader iniWeap("ModelVariations_PedWeapons.ini");
    
        static int currentTown = -1;
        static std::vector<short> currentPedVariations[300];

        for (int i = 0; i < 300; i++)
        {
            std::vector<short> vec = iniLineParser(PED_VARIATION, i, "Countryside", &iniPed);
            pedVariations[i][0] = vec;
            vec = iniLineParser(PED_VARIATION, i, "LosSantos", &iniPed);
            pedVariations[i][1] = vec;
            vec = iniLineParser(PED_VARIATION, i, "SanFierro", &iniPed);
            pedVariations[i][2] = vec;
            vec = iniLineParser(PED_VARIATION, i, "LasVenturas", &iniPed);
            pedVariations[i][3] = vec;
            vec = iniLineParser(PED_VARIATION, i, "Global", &iniPed);
            pedVariations[i][4] = vec;
            vec = iniLineParser(PED_VARIATION, i, "Desert", &iniPed);
            pedVariations[i][5] = vec;

            int wepVariationType = iniWeap.ReadInteger(std::to_string(i), "VariationType", -1);
            if (wepVariationType == 1 || wepVariationType == 2)
                pedWepVariationTypes[i] = wepVariationType;
        }

        for (int i = 0; i < 300; i++)
        {
            std::sort(pedVariations[i][0].begin(), pedVariations[i][0].end());
            std::sort(pedVariations[i][1].begin(), pedVariations[i][1].end());
            std::sort(pedVariations[i][2].begin(), pedVariations[i][2].end());
            std::sort(pedVariations[i][3].begin(), pedVariations[i][3].end());
            std::sort(pedVariations[i][4].begin(), pedVariations[i][4].end());
            std::sort(pedVariations[i][5].begin(), pedVariations[i][5].end());
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
            CPlayerPed *player = FindPlayerPed();
            if (player != NULL)
            {
                if (CTheZones::m_CurrLevel == LEVEL_NAME_COUNTRY_SIDE)
                {
                    if (currentTown != LV_DESERT)
                    {
                        if (player->IsWithinArea(-883.0, 2979.0, 847.0, 456.0) ||
                            player->IsWithinArea(-1127.0, 2979.0, -880.0, 924.0) ||
                            player->IsWithinArea(-1823.0, 2979.0, -1128.0, 1580.0))
                        {
                            currentTown = LV_DESERT;
                            for (int i = 0; i < 300; i++)
                            {
                                currentPedVariations[i].clear();
                                std::set_union(pedVariations[i][LV_GLOBAL].begin(), pedVariations[i][LV_GLOBAL].end(),
                                               pedVariations[i][LV_DESERT].begin(), pedVariations[i][LV_DESERT].end(), std::back_inserter(currentPedVariations[i]));
                                if (i < 212)
                                {
                                    currentVehVariations[i].clear();
                                    std::set_union(vehVariations[i][LV_GLOBAL].begin(), vehVariations[i][LV_GLOBAL].end(),
                                                   vehVariations[i][LV_DESERT].begin(), vehVariations[i][LV_DESERT].end(), std::back_inserter(currentVehVariations[i]));
                                }
                            }
                        }
                        else if (CTheZones::m_CurrLevel != currentTown)
                        {
                            currentTown = CTheZones::m_CurrLevel;
                            for (int i = 0; i < 300; i++)
                            {
                                currentPedVariations[i].clear();
                                std::set_union(pedVariations[i][LV_GLOBAL].begin(), pedVariations[i][LV_GLOBAL].end(),
                                               pedVariations[i][currentTown].begin(), pedVariations[i][currentTown].end(), std::back_inserter(currentPedVariations[i]));
                                if (i < 212)
                                {
                                    currentVehVariations[i].clear();
                                    std::set_union(vehVariations[i][LV_GLOBAL].begin(), vehVariations[i][LV_GLOBAL].end(),
                                                   vehVariations[i][currentTown].begin(), vehVariations[i][currentTown].end(), std::back_inserter(currentVehVariations[i]));
                                }
                            }
                        }
                    }
                    else if (!(player->IsWithinArea(-883.0, 2979.0, 847.0, 456.0) ||
                               player->IsWithinArea(-1127.0, 2979.0, -880.0, 924.0) ||
                               player->IsWithinArea(-1823.0, 2979.0, -1128.0, 1580.0)))
                    {
                        currentTown = CTheZones::m_CurrLevel;
                        for (int i = 0; i < 300; i++)
                        {
                            currentPedVariations[i].clear();
                            std::set_union(pedVariations[i][LV_GLOBAL].begin(), pedVariations[i][LV_GLOBAL].end(),
                                            pedVariations[i][currentTown].begin(), pedVariations[i][currentTown].end(), std::back_inserter(currentPedVariations[i]));
                            if (i < 212)
                            {
                                currentVehVariations[i].clear();
                                std::set_union(vehVariations[i][LV_GLOBAL].begin(), vehVariations[i][LV_GLOBAL].end(),
                                               vehVariations[i][currentTown].begin(), vehVariations[i][currentTown].end(), std::back_inserter(currentVehVariations[i]));
                            }
                        }
                    }
                }
                else if (CTheZones::m_CurrLevel != currentTown)
                {
                    currentTown = CTheZones::m_CurrLevel;
                    for (int i = 0; i < 300; i++)
                    {
                        currentPedVariations[i].clear();
                        std::set_union(pedVariations[i][LV_GLOBAL].begin(), pedVariations[i][LV_GLOBAL].end(),
                                       pedVariations[i][currentTown].begin(), pedVariations[i][currentTown].end(), std::back_inserter(currentPedVariations[i]));
                        if (i < 212)
                        {
                            currentVehVariations[i].clear();
                            std::set_union(vehVariations[i][LV_GLOBAL].begin(), vehVariations[i][LV_GLOBAL].end(),
                                           vehVariations[i][currentTown].begin(), vehVariations[i][currentTown].end(), std::back_inserter(currentVehVariations[i]));
                        }
                    }
                }
            }

            if (dealersFixed < 10)
                dealersFixed++;
            if (dealersFixed == 10)
            {
                drugDealerFix();
                dealersFixed = 11;
            }
        };

        Events::pedCtorEvent += [](CPed* ped) {pedStack.push(ped);};

        Events::pedSetModelEvent.after += [](CPed* ped, int model)
        {
            if (ped->m_nModelIndex > 0 && ped->m_nModelIndex < 300 && !currentPedVariations[ped->m_nModelIndex].empty())
            {
                int random = CGeneral::GetRandomNumberInRange(0, currentPedVariations[ped->m_nModelIndex].size());
                int variationModel = currentPedVariations[ped->m_nModelIndex][random];
                if (variationModel > -1)
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
            if (enableVehicles == 1)
                hookTaxi();
            while (!pedStack.empty())
            {
                CPed* ped = pedStack.top();
                pedStack.pop();
                if (ped->m_nModelIndex >= 0 && ped->m_nModelIndex < 300 && pedWepVariationTypes[ped->m_nModelIndex] > 0)
                {
                    int activeSlot = ped->m_nActiveWeaponSlot;
                    int loopMax = pedWepVariationTypes[ped->m_nModelIndex] == 1 ? 13 : 47;

                    for (int i = 0; i < loopMax; i++)
                    {
                        std::vector<short> wvec = iniLineParser(PED_WEAPON_VARIATION, ped->m_nModelIndex, (const char *)i, &iniWeap);

                        if (!wvec.empty())
                        {
                            int slot = i;
                            int random = CGeneral::GetRandomNumberInRange(0, wvec.size());
                            if (pedWepVariationTypes[ped->m_nModelIndex] == 2)
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
