#include "Vehicles.hpp"
#include "Hooks.hpp"
#include "LogUtil.hpp"
#include "plugin.h"
#include "../../injector/assembly.hpp"

#include "CBoat.h"
#include "CCarGenerator.h"
#include "CCoronas.h"
#include "CGeneral.h"
#include "CHeli.h"
#include "CPopulation.h"
#include "CStreaming.h"
#include "CTheScripts.h"
#include "CTrain.h"
#include "CVector.h"

#include <array>

using namespace plugin;

int roadblockModel = -1;
int sirenModel = -1;
int lightsModel = -1;
int currentOccupantsGroup = -1;

int fireCmpModel = -1;

int passengerModelIndex = -1;
const unsigned int jmp613B7E = 0x613B7E;
const unsigned int jmp6AB35A = 0x6AB35A;

bool hasModelSideMission(int model)
{
    switch (model)
    {
        case 407: //Fire Truck
        case 416: //Ambulance
        case 537: //Freight
        case 538: //Brown Streak
        case 575: //Broadway
        case 609: //Boxville Mission
            return true;
    }
    return false;
}

bool isModelAmbulance(int model)
{
    auto it = vehOriginalModels.find(model);
    if (it != vehOriginalModels.end() && it->second == 416)
        return true;

    return false;
}

bool isModelFiretruck(int model)
{
    auto it = vehOriginalModels.find(model);
    if (it != vehOriginalModels.end() && it->second == 407)
        return true;

    return false;
}

bool isModelTaxi(int model)
{
    auto it = vehOriginalModels.find(model);
    if (it != vehOriginalModels.end() && (it->second == 438 || it->second == 420))
        return true;

    return false;
}

int getVariationOriginalModel(int modelIndex)
{
    int originalModel = modelIndex;

    auto it = vehOriginalModels.find(modelIndex);
    if (it != vehOriginalModels.end())
        return it->second;

    return originalModel;
}

int getRandomVariation(int modelid)
{
    if (modelid < 400 || modelid > 611)
        return modelid;
    if (vehCurrentVariations[modelid - 400].empty())
        return modelid;

    int random = CGeneral::GetRandomNumberInRange(0, vehCurrentVariations[modelid - 400].size());
    int variationModel = vehCurrentVariations[modelid - 400][random];
    if (variationModel > -1)
    {
        CStreaming::RequestModel(variationModel, 2);
        CStreaming::LoadAllRequestedModels(false);
        return variationModel;
    }
    return modelid;
}


void readVehicleIni()
{
    for (int i = 400; i < 612; i++)
    {
        std::vector<short> vec = iniLineParser(VEHICLE_VARIATION, i, "Countryside", &iniVeh);
        vehVariations[i - 400][0] = vec;
        std::sort(vehVariations[i - 400][0].begin(), vehVariations[i - 400][0].end());

        vec = iniLineParser(VEHICLE_VARIATION, i, "LosSantos", &iniVeh);
        vehVariations[i - 400][1] = vec;
        std::sort(vehVariations[i - 400][1].begin(), vehVariations[i - 400][1].end());

        vec = iniLineParser(VEHICLE_VARIATION, i, "SanFierro", &iniVeh);
        vehVariations[i - 400][2] = vec;
        std::sort(vehVariations[i - 400][2].begin(), vehVariations[i - 400][2].end());

        vec = iniLineParser(VEHICLE_VARIATION, i, "LasVenturas", &iniVeh);
        vehVariations[i - 400][3] = vec;
        std::sort(vehVariations[i - 400][3].begin(), vehVariations[i - 400][3].end());

        vec = iniLineParser(VEHICLE_VARIATION, i, "Global", &iniVeh);
        vehVariations[i - 400][4] = vec;
        std::sort(vehVariations[i - 400][4].begin(), vehVariations[i - 400][4].end());

        vec = iniLineParser(VEHICLE_VARIATION, i, "Desert", &iniVeh);
        vehVariations[i - 400][5] = vec;
        std::sort(vehVariations[i - 400][5].begin(), vehVariations[i - 400][5].end());

        vec = iniLineParser(VEHICLE_VARIATION, i, "TierraRobada", &iniVeh);
        vehVariations[i - 400][6] = vec;
        std::sort(vehVariations[i - 400][6].begin(), vehVariations[i - 400][6].end());
        vectorUnion(vehVariations[i - 400][6], vehVariations[i - 400][5], vec);
        vehVariations[i - 400][6] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "BoneCounty", &iniVeh);
        vehVariations[i - 400][7] = vec;
        std::sort(vehVariations[i - 400][7].begin(), vehVariations[i - 400][7].end());
        vectorUnion(vehVariations[i - 400][7], vehVariations[i - 400][5], vec);
        vehVariations[i - 400][7] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "RedCounty", &iniVeh);
        vehVariations[i - 400][8] = vec;
        std::sort(vehVariations[i - 400][8].begin(), vehVariations[i - 400][8].end());
        vectorUnion(vehVariations[i - 400][8], vehVariations[i - 400][0], vec);
        vehVariations[i - 400][8] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "Blueberry", &iniVeh);
        vehVariations[i - 400][9] = vec;
        std::sort(vehVariations[i - 400][9].begin(), vehVariations[i - 400][9].end());
        vectorUnion(vehVariations[i-400][9], vehVariations[i - 400][8], vec);
        vehVariations[i - 400][9] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "Montgomery", &iniVeh);
        vehVariations[i - 400][10] = vec;
        std::sort(vehVariations[i - 400][10].begin(), vehVariations[i - 400][10].end());
        vectorUnion(vehVariations[i - 400][10], vehVariations[i - 400][8], vec);
        vehVariations[i - 400][10] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "Dillimore", &iniVeh);
        vehVariations[i - 400][11] = vec;
        std::sort(vehVariations[i - 400][11].begin(), vehVariations[i - 400][11].end());
        vectorUnion(vehVariations[i - 400][11], vehVariations[i - 400][8], vec);
        vehVariations[i - 400][11] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "PalominoCreek", &iniVeh);
        vehVariations[i - 400][12] = vec;
        std::sort(vehVariations[i - 400][12].begin(), vehVariations[i - 400][12].end());
        vectorUnion(vehVariations[i - 400][12], vehVariations[i - 400][8], vec);
        vehVariations[i - 400][12] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "FlintCounty", &iniVeh);
        vehVariations[i - 400][13] = vec;
        std::sort(vehVariations[i - 400][13].begin(), vehVariations[i - 400][13].end());
        vectorUnion(vehVariations[i - 400][13], vehVariations[i - 400][0], vec);
        vehVariations[i - 400][13] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "Whetstone", &iniVeh);
        vehVariations[i - 400][14] = vec;
        std::sort(vehVariations[i - 400][14].begin(), vehVariations[i - 400][14].end());
        vectorUnion(vehVariations[i - 400][14], vehVariations[i - 400][0], vec);
        vehVariations[i - 400][14] = vec;

        vec = iniLineParser(VEHICLE_VARIATION, i, "AngelPine", &iniVeh);
        vehVariations[i - 400][15] = vec;
        std::sort(vehVariations[i - 400][15].begin(), vehVariations[i - 400][15].end());
        vectorUnion(vehVariations[i - 400][15], vehVariations[i - 400][14], vec);
        vehVariations[i - 400][15] = vec;


        vec = iniLineParser(VEHICLE_VARIATION, i, "Wanted1", &iniVeh);
        vehWantedVariations[i - 400][0] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "Wanted2", &iniVeh);
        vehWantedVariations[i - 400][1] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "Wanted3", &iniVeh);
        vehWantedVariations[i - 400][2] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "Wanted4", &iniVeh);
        vehWantedVariations[i - 400][3] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "Wanted5", &iniVeh);
        vehWantedVariations[i - 400][4] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "Wanted6", &iniVeh);
        vehWantedVariations[i - 400][5] = vec;


        for (int j = 0; j < 16; j++)
            for (int k = 0; k < (int)(vehVariations[i-400][j].size()); k++)
                if (vehVariations[i-400][j][k] > 0 && vehVariations[i-400][j][k] < 30000  && vehVariations[i-400][j][k] != i && 
                    !(isGameModelPolice(i) && isGameModelPolice(vehVariations[i - 400][j][k])))
                    vehOriginalModels.insert({ vehVariations[i - 400][j][k], i });
    }

    enableLights = iniVeh.ReadInteger("Settings", "EnableLights", 0);

    for (int i = 400; i < 32000; i++)
    {
        if (enableLights)
        {
            float lightWidth = iniVeh.ReadFloat(std::to_string(i), "LightWidth", -999.0);
            float lightX = iniVeh.ReadFloat(std::to_string(i), "LightX", -999.0);
            float lightY = iniVeh.ReadFloat(std::to_string(i), "LightY", -999.0);
            float lightZ = iniVeh.ReadFloat(std::to_string(i), "LightZ", -999.0);

            if (lightX > -900.0 || lightY > -900.0 || lightZ > -900.0)
                LightPositions.insert({ (short)i, {{ lightX, lightY, lightZ }, lightWidth} });
        }
        int numGroups = 0;
        for (int j = 0; j < 9; j++)
        {
            std::string str = "DriverGroup" + std::to_string(j+1);
            std::vector<short> vec = iniLineParser(VEHICLE_VARIATION, i, str.c_str(), &iniVeh);
            if (!vec.empty())
            {
                vehDriverGroups[j].insert({ i, vec });
                numGroups++;
            }
            if (numGroups > 0)
                modelNumGroups.insert({ i, numGroups });
            else
                continue;

            str = "PassengerGroup" + std::to_string(j + 1);
            vec = iniLineParser(VEHICLE_VARIATION, i, str.c_str(), &iniVeh);
            if (!vec.empty())
                vehPassengerGroups[j].insert({ i, vec });
        }


        std::vector<short> vec = iniLineParser(VEHICLE_VARIATION, i, "Drivers", &iniVeh);
        if (!vec.empty())
            vehDrivers.insert({ i, vec });

        vec = iniLineParser(VEHICLE_VARIATION, i, "Passengers", &iniVeh);
        if (!vec.empty())
            vehPassengers.insert({ i, vec });
    }

    changeCarGenerators = iniVeh.ReadInteger("Settings", "ChangeCarGenerators", 0);
    vehCarGenExclude = iniLineParser(MODEL_SETTINGS, (int)"Settings", "ExcludeCarGeneratorModels", &iniVeh);
    loadAllVehicles = iniVeh.ReadInteger("Settings", "LoadAllVehicles", 0);
    enableAllSideMissions = iniVeh.ReadInteger("Settings", "EnableSideMissionsForAllScripts", 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void hookTaxi()
{
    static bool isPlayerInTaxi = false;
    if (enableSideMissions == false)
        return;
    CPlayerPed* player = FindPlayerPed();
    if (player == NULL)
        return;
    CVehicle* vehicle = player->m_pVehicle;
    if (vehicle == NULL)
        return;

    if (isPlayerInTaxi == false && (isModelTaxi(vehicle->m_nModelIndex) || vehicle->m_nModelIndex == 420 || vehicle->m_nModelIndex == 438))
    {
        injector::MakeJMP(0x4912D4, 0x4912DC);
        isPlayerInTaxi = true;
    }
    else if (isPlayerInTaxi == true)
    {
        if (!(isModelTaxi(vehicle->m_nModelIndex) || vehicle->m_nModelIndex == 420 || vehicle->m_nModelIndex == 438))
        {
            injector::MakeJMP(0x4912D4, 0x4912E1);
            isPlayerInTaxi = false;
        }
    }
}

template <unsigned int address>
int __cdecl ChooseModelHooked(int* a1)
{
    int model = callOriginalAndReturn<int, address>(a1);
    //int model = CCarCtrl::ChooseModel(a1);

    if (model < 400 || model > 611)
        return model;

    if (vehCurrentVariations[model - 400].empty())
        return model;

    return getRandomVariation(model);
}

template <unsigned int address>
int __cdecl ChoosePoliceCarModelHooked(int a1)
{
    int model = callOriginalAndReturn<int, address>(a1);
    //int model = CCarCtrl::ChoosePoliceCarModel(a1);

    if (model < 427 || model > 601)
        return model;

    if (vehCurrentVariations[model - 400].empty())
        return model;

    return getRandomVariation(model);
}

template <unsigned int address>
void __cdecl AddPoliceCarOccupantsHooked(CVehicle* a2, char a3)
{
    int model = a2->m_nModelIndex;
    a2->m_nModelIndex = getVariationOriginalModel(a2->m_nModelIndex);

    callOriginal<address>(a2, a3);
    //CCarAI::AddPoliceCarOccupants(a2, a3);

    a2->m_nModelIndex = model;
}

template <unsigned int address>
CAutomobile* __fastcall CAutomobileHooked(CAutomobile* automobile, void*, int modelIndex, char usageType, char bSetupSuspensionLines)
{
    return callMethodOriginalAndReturn<CAutomobile*, address>(automobile, getRandomVariation(modelIndex), usageType, bSetupSuspensionLines);
    //return CallMethodAndReturn<CAutomobile*, 0x6B0A90>(automobile, getRandomVariation(modelIndex), usageType, bSetupSuspensionLines);
    /*
    if (modelIndex != 432)
        return CallMethodAndReturn<CAutomobile*, 0x6B0A90>(automobile, getRandomVariation(modelIndex), usageType, bSetupSuspensionLines);
    else
    {
        int randomVariation = getRandomVariation(modelIndex);
        *((short*)0x6B0CF4) = randomVariation;
        *((short*)0x6B12D8) = randomVariation;
        CAutomobile* retVal = CallMethodAndReturn<CAutomobile*, 0x6B0A90>(automobile, randomVariation, usageType, bSetupSuspensionLines);
        *((short*)0x6B0CF4) = 0x1B0;
        *((short*)0x6B12D8) = 0x1B0;
        return retVal;
    }
    */
}

template <unsigned int address>
signed int __fastcall PickRandomCarHooked(CLoadedCarGroup* cargrp, void*, char a2, char a3) //for random parked cars
{
    if (cargrp == NULL)
        return -1;
    return getRandomVariation(callMethodOriginalAndReturn<signed int, address>(cargrp, a2, a3));
}

template <unsigned int address>
void __fastcall DoInternalProcessingHooked(CCarGenerator* park) //for non-random parked cars
{
    if (park != NULL)
    {
        int model = park->m_nModelId;
        if (changeCarGenerators == 1)
        {
            if (!vehCarGenExclude.empty())
                if (std::find(vehCarGenExclude.begin(), vehCarGenExclude.end(), model) != vehCarGenExclude.end())
                {
                    callMethodOriginal<address>(park);
                    return;
                }

            park->m_nModelId = getRandomVariation(park->m_nModelId);
            callMethodOriginal<address>(park);
            park->m_nModelId = model;
            return;
        }

        switch (park->m_nModelId)
        {
            case 416: //Ambulance
            case 407: //Fire Truck
            case 596: //Police LS
            case 597: //Police SF
            case 598: //Police LV
            case 599: //Police Ranger
            case 523: //HPV1000
            case 427: //Enforcer
            case 601: //S.W.A.T.
            case 490: //FBI Rancher
            case 528: //FBI Truck
            case 433: //Barracks
            case 470: //Patriot
            case 432: //Rhino
            case 430: //Predator
            case 496: //Police Maverick
            case 488: //News Chopper
                park->m_nModelId = getRandomVariation(park->m_nModelId);
                callMethodOriginal<address>(park);
                park->m_nModelId = model;
                break;
            default:
                callMethodOriginal<address>(park);
        }
    }
}

template <unsigned int address>
CTrain* __fastcall CTrainHooked(CTrain* train, void*, int modelIndex, int createdBy)
{
    return callMethodOriginalAndReturn<CTrain*, address>(train, getRandomVariation(modelIndex), createdBy);
}

template <unsigned int address>
CVehicle* __fastcall CBoatHooked(CBoat* boat, void*, int modelId, char a3)
{
    return callMethodOriginalAndReturn<CVehicle*, address>(boat, getRandomVariation(modelId), a3);
}

template <unsigned int address>
CAutomobile* __fastcall CHeliHooked(CHeli* heli, void*, int a2, char usageType)
{
    return callMethodOriginalAndReturn<CAutomobile*, address>(heli, getRandomVariation(a2), usageType);
}

template <unsigned int address>
char __fastcall IsLawEnforcementVehicleHooked(CVehicle* vehicle)
{
    if (vehicle == NULL)
        return 0;
    int modelIndex = vehicle->m_nModelIndex;

    vehicle->m_nModelIndex = getVariationOriginalModel(vehicle->m_nModelIndex);

    char isLawEnforcement = 0;
    if (address == 0)
        isLawEnforcement = vehicle->IsLawEnforcementVehicle();
    else
        isLawEnforcement = callMethodOriginalAndReturn<char, address>(vehicle);

    vehicle->m_nModelIndex = modelIndex;
    return isLawEnforcement;
}

template <unsigned int address>
char __cdecl IsCarSprayableHooked(CVehicle* a1)
{
    if (a1 == NULL)
        return 0;

    if (isModelAmbulance(a1->m_nModelIndex) || isModelFiretruck(a1->m_nModelIndex) || IsLawEnforcementVehicleHooked<0>(a1))
        return 0;

    return callOriginalAndReturn<char, address>(a1);
    //return CallAndReturn<char, 0x4479A0>(a1);
}

char __fastcall HasCarSiren(CVehicle* vehicle)
{
    if (vehicle == NULL)
        return 0;
    auto it = vehOriginalModels.find(vehicle->m_nModelIndex);
    if (it != vehOriginalModels.end() && (it->second == 432 || it->second == 564))
        return 0;

    if (isModelAmbulance(vehicle->m_nModelIndex) || isModelFiretruck(vehicle->m_nModelIndex) || IsLawEnforcementVehicleHooked<0>(vehicle))
        return 1;

    return 0;
}

template <unsigned int address>
void __cdecl AddAmbulanceOccupantsHooked(CVehicle* pVehicle)
{
    if (pVehicle == NULL)
        return;
    
    int model = pVehicle->m_nModelIndex;

    if (isModelAmbulance(pVehicle->m_nModelIndex))
        pVehicle->m_nModelIndex = 416;
    callOriginal<address>(pVehicle);
    //CCarAI::AddAmbulanceOccupants(pVehicle);
    pVehicle->m_nModelIndex = model;
}

template <unsigned int address>
void __cdecl AddFiretruckOccupantsHooked(CVehicle* pVehicle)
{
    if (pVehicle == NULL)
        return;

    int model = pVehicle->m_nModelIndex;

    if (isModelFiretruck(pVehicle->m_nModelIndex))
        pVehicle->m_nModelIndex = 407;
    callOriginal<address>(pVehicle);
    //CCarAI::AddFiretruckOccupants(pVehicle);
    pVehicle->m_nModelIndex = model;
}

template <unsigned int address>
DWORD __cdecl FindSpecificDriverModelForCar_ToUseHooked(int carModel)
{
    auto it = vehDrivers.find(carModel);
    int replaceDriver = iniVeh.ReadInteger(std::to_string(carModel), "ReplaceDriver", 0);
    if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9)
    {
        auto itGroup = vehDriverGroups[currentOccupantsGroup].find(carModel);
        if (itGroup != vehDriverGroups[currentOccupantsGroup].end())
        {
            int random = CGeneral::GetRandomNumberInRange(0, itGroup->second.size());
            CStreaming::RequestModel(itGroup->second[random], 2);
            CStreaming::LoadAllRequestedModels(false);
            return itGroup->second[random];
        }
    }
    if (it != vehDrivers.end() && ((replaceDriver == 0 && CGeneral::GetRandomNumberInRange(0, 100) > 50) || replaceDriver == 1))
    {
        int random = CGeneral::GetRandomNumberInRange(0, it->second.size());
        CStreaming::RequestModel(it->second[random], 2);
        CStreaming::LoadAllRequestedModels(false);
        return it->second[random];
    }

    return callOriginalAndReturn<int, address>(getVariationOriginalModel(carModel));
    //return CPopulation::FindSpecificDriverModelForCar_ToUse(getVariationOriginalModel(carModel));
}

template <unsigned int address>
void __fastcall CollectParametersHooked(CRunningScript* script, void*, unsigned __int16 a2)
{
    //script->CollectParameters(a2);
    callMethodOriginal<address>(script, a2);
    if (enableAllSideMissions == 0)
    {
        if (!hasModelSideMission(CTheScripts::ScriptParams[1].uParam))
            return;

        if (strcmp(script->m_szName, "r3") != 0 && strcmp(script->m_szName, "ambulan") != 0 && strcmp(script->m_szName, "firetru") != 0 &&
            strcmp(script->m_szName, "freight") != 0)
            return;
    }

    CPlayerPed* player = FindPlayerPed();  
    if (player == NULL)
        return;

    int hplayer = CPools::GetPedRef(player);

    if (CTheScripts::ScriptParams[0].uParam != hplayer)
        return;

    if (player->m_pVehicle)
    {
        int originalModel = getVariationOriginalModel(player->m_pVehicle->m_nModelIndex);
        if (CTheScripts::ScriptParams[1].uParam == originalModel)
            CTheScripts::ScriptParams[1].uParam = player->m_pVehicle->m_nModelIndex;
    }
}

template <unsigned int address>
char __cdecl GenerateRoadBlockCopsForCarHooked(CVehicle* a1, int pedsPositionsType, int type)
{
    if (a1 == NULL)
        return 0;

    roadblockModel = a1->m_nModelIndex;
    a1->m_nModelIndex = getVariationOriginalModel(a1->m_nModelIndex);
    //CRoadBlocks::GenerateRoadBlockCopsForCar(a1, pedsPositionsType, type);
    callOriginal<address>(a1, pedsPositionsType, type);
    if (roadblockModel >= 400)
        a1->m_nModelIndex = roadblockModel;
    roadblockModel = -1;

    return 1;
}
template <unsigned int address>
CColModel* __fastcall GetColModelHooked(CVehicle* entity)
{
    if (roadblockModel >= 400)
        entity->m_nModelIndex = roadblockModel;
    return callMethodOriginalAndReturn<CColModel*, address>(entity);
    //return entity->GetColModel();
}

template <unsigned int address>
void __cdecl SetUpDriverAndPassengersForVehicleHooked(CVehicle* car, int a3, int a4, char a5, char a6, int a7)
{
    if (car == NULL)
        return;

    int useOnlyGroups = iniVeh.ReadInteger(std::to_string(car->m_nModelIndex), "UseOnlyGroups", 0);

    if (useOnlyGroups == 1 || (CGeneral::GetRandomNumberInRange(0, 100) > 50))
    {
        auto it = modelNumGroups.find(car->m_nModelIndex);
        if (it != modelNumGroups.end())
            currentOccupantsGroup = CGeneral::GetRandomNumberInRange(0, it->second);            
    }

    CStreaming::RequestModel(274, 2);//laemt1
    CStreaming::RequestModel(275, 2);//lvemt1
    CStreaming::RequestModel(276, 2);//sfemt1
    CStreaming::RequestModel(277, 2);//lafd1
    CStreaming::RequestModel(278, 2);//lvfd1
    CStreaming::RequestModel(279, 2);//sffd1
    CStreaming::RequestModel(280, 2);//lapd1
    CStreaming::RequestModel(281, 2);//sfpd1
    CStreaming::RequestModel(282, 2);//lvpd1
    CStreaming::RequestModel(283, 2);//csher
    CStreaming::RequestModel(284, 2);//lapdm1
    CStreaming::RequestModel(285, 2);//swat
    CStreaming::RequestModel(286, 2);//fbi
    CStreaming::RequestModel(287, 2);//army
    CStreaming::RequestModel(288, 2);//dsher
    
    CStreaming::LoadAllRequestedModels(false);

    int model = car->m_nModelIndex;

    car->m_nModelIndex = getVariationOriginalModel(car->m_nModelIndex);

    callOriginal<address>(car, a3, a4, a5, a6, a7);
    //CCarCtrl::SetUpDriverAndPassengersForVehicle(car, a3, a4, a5, a6, a7);

    car->m_nModelIndex = model;
    currentOccupantsGroup = -1;
}

template <unsigned int address>
CHeli* __cdecl GenerateHeliHooked(CPed* ped, char newsHeli)
{
    if (FindPlayerWanted(-1)->m_nWantedLevel < 4)
        return callOriginalAndReturn<CHeli*, address>(ped, 0);
        //return CHeli::GenerateHeli(ped, 0);

    if (CHeli::pHelis)
    {
        CStreaming::RequestModel(488, 2);
        CStreaming::RequestModel(497, 2);
        CStreaming::LoadAllRequestedModels(false);

        newsHeli = 1;
        if (CHeli::pHelis[0] && (CHeli::pHelis[0]->m_nModelIndex == 488 || getVariationOriginalModel(CHeli::pHelis[0]->m_nModelIndex) == 488))
            newsHeli = 0;

        if (CHeli::pHelis[1] && (CHeli::pHelis[1]->m_nModelIndex == 488 || getVariationOriginalModel(CHeli::pHelis[1]->m_nModelIndex) == 488))
            newsHeli = 0;
    }

    return callOriginalAndReturn<CHeli*, address>(ped, newsHeli);
    //return CHeli::GenerateHeli(ped, newsHeli);
}

void __declspec(naked) patchPassengerModel()
{
    __asm {
        lea edx, [esp + 0x10]
        push edx
        push passengerModelIndex
        jmp jmp613B7E
    }
}

template <unsigned int address>
CPed* __cdecl AddPedInCarHooked(CVehicle* a1, char a2, int a3, signed int a4, int a5, char a6)
{
    const unsigned char originalData[5] = { 0x8D, 0x54, 0x24, 0x10, 0x52 };
    int random = 0;
    if (a1)
    {
        int replacePassenger = iniVeh.ReadInteger(std::to_string(a1->m_nModelIndex), "ReplacePassengers", 0);
        auto it = vehPassengers.find(a1->m_nModelIndex);
        if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9)
        {
            auto itGroup = vehPassengerGroups[currentOccupantsGroup].find(a1->m_nModelIndex);
            if (itGroup != vehPassengerGroups[currentOccupantsGroup].end())
            {
                random = CGeneral::GetRandomNumberInRange(0, itGroup->second.size());
                passengerModelIndex = itGroup->second[random];
                goto ModelChosen;
            }
        }
        if (it != vehPassengers.end() && ((replacePassenger == 0 && CGeneral::GetRandomNumberInRange(0, 100) > 50) || replacePassenger == 1))
        {
            random = CGeneral::GetRandomNumberInRange(0, it->second.size());
            passengerModelIndex = it->second[random];
ModelChosen:
            CStreaming::RequestModel(passengerModelIndex, 2);
            CStreaming::LoadAllRequestedModels(false);
            injector::MakeJMP(0x613B78, patchPassengerModel);
            CPed* ped = callOriginalAndReturn<CPed*, address>(a1, a2, a3, a4, a5, a6);
            memcpy((void*)0x613B78, originalData, 5);
            passengerModelIndex = -1;
            return ped;
        }
    }

    return callOriginalAndReturn<CPed*, address>(a1, a2, a3, a4, a5, a6);
    //return CPopulation::AddPedInCar(a1, a2, a3, a4, a5, a6);
}

template <unsigned int address>
void __cdecl RegisterCoronaHooked(CCoronas* _this, unsigned int a2, CEntity* a3, unsigned __int8 a4, unsigned __int8 a5, unsigned __int8 a6, CVector* a7, const CVector* a8,
                                  float a9, void* texture, unsigned __int8 a11, unsigned __int8 a12, unsigned __int8 a13, int a14, float a15, float a16, float a17, float a18,
                                  float a19, float a20, bool a21)
{
    if (a7 && lightsModel > 0)
    {
        auto it = LightPositions.find(lightsModel);
        if (it != LightPositions.end())
        {
            if (it->second.second > -900.0)
                a7->x *= it->second.second;
            if (it->second.first.x > -900.0)
                a7->x += it->second.first.x;
            if (it->second.first.y > -900.0)
                a7->y *= it->second.first.y;
            if (it->second.first.z > -900.0)
                a7->z *= it->second.first.z;
        }
    }
    callOriginal<address>(_this, a2, a3, a4, a5, a6, a7, a8, a9, texture, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
}

template <unsigned int address>
void __cdecl AddLightHooked(char type, float x, float y, float z, float dir_x, float dir_y, float dir_z, float radius, float r, float g, float b, 
                            char fogType, char generateExtraShadows, int attachedTo)
{
    if (lightsModel > 0)
    {
        auto it = LightPositions.find(lightsModel);
        if (it != LightPositions.end())
        {
            if (it->second.second > -900.0)
                x *= it->second.second;
            if (it->second.first.x > -900.0)
                x += it->second.first.x;
            if (it->second.first.y > -900.0)
                y *= it->second.first.y;
            if (it->second.first.z > -900.0)
                z *= it->second.first.z;
        }
    }

    callOriginal<address>(type, x, y, z, dir_x, dir_y, dir_z, radius, r, g, b, fogType, generateExtraShadows, attachedTo);
}

template <unsigned int address>
void __cdecl PossiblyRemoveVehicleHooked(CVehicle* car)
{
    if (car == NULL)
        return;

    int originalModel = getVariationOriginalModel(car->m_nModelIndex);
    if (originalModel != 407 && originalModel != 416)
    {
        callOriginal<address>(car);
        return;
    }
    *((short*)0x4250AC) = car->m_nModelIndex;
    callOriginal<address>(car);
    *((short*)0x4250AC) = 0x1A0;

}

/*
0x053A926
void __declspec(naked) ()
{
    __asm {
        pushad
        movsx eax, word ptr[edi + 22h]
        push eax
        call getVariationOriginalModel
        cmp eax, 0x197
        pop eax
        popad
    }
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////  VTABLE HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void __fastcall ProcessControlHooked(CAutomobile* veh)
{
    if (veh && getVariationOriginalModel(veh->m_nModelIndex) == 407 || getVariationOriginalModel(veh->m_nModelIndex) == 601)
    {
        //EB 0A
        *((BYTE*)0x6B1F4F) = 0xEB;
        *((BYTE*)0x6B1F50) = 0x0A;
        ProcessControlOriginal(veh);
        *((BYTE*)0x6B1F4F) = 0x66;
        *((BYTE*)0x6B1F50) = 0x3D;
    }/*
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        injector::MakeJMP(0x6B1F7B, 0x6B2026);
        injector::MakeNOP(0x6B36DA, 6);
        plugin::CallMethod<0x6B1880>(veh);
        ((BYTE*)0x6B1F7B)[0] = 0x66;
        ((BYTE*)0x6B1F7B)[1] = 0x3D;
        ((BYTE*)0x6B1F7B)[2] = 0xB0;
        ((BYTE*)0x6B1F7B)[3] = 0x01;
        ((BYTE*)0x6B1F7B)[4] = 0x0F;

        ((BYTE*)0x6B36DA)[0] = 0x0F;
        ((BYTE*)0x6B36DA)[1] = 0x85;
        ((BYTE*)0x6B36DA)[2] = 0x95;
        ((BYTE*)0x6B36DA)[3] = 0x00;
        ((BYTE*)0x6B36DA)[4] = 0x00;
        ((BYTE*)0x6B36DA)[5] = 0x00;
    }*/
    else
        ProcessControlOriginal(veh);

}

void __declspec(naked) enableSirenLights()
{
    __asm {
        mov eax, sirenModel
        lea edi, [eax - 0x197]
        jmp jmp6AB35A
    }
}

void __fastcall PreRenderHooked(CAutomobile* veh)
{
    auto sirenRestore = []()
    {
        ((BYTE*)0x6AB350)[0] = 0x0F;
        ((BYTE*)0x6AB350)[1] = 0xBF;
        ((BYTE*)0x6AB350)[2] = 0x46;
        ((BYTE*)0x6AB350)[3] = 0x22;
        ((BYTE*)0x6AB350)[4] = 0x8D;
    };

    if (veh == NULL)
        return;

    bool hasSiren = false;

    if (enableLights && HasCarSiren(veh))
    {
        sirenModel = getVariationOriginalModel(veh->m_nModelIndex);
        lightsModel = veh->m_nModelIndex;
        injector::MakeJMP(0x6AB350, enableSirenLights);
        hasSiren = true;
    }

    if (getVariationOriginalModel(veh->m_nModelIndex) == 407)
    {
        *((BYTE*)0x6ACA57) = 0xEB;
        *((BYTE*)0x6ACA58) = 0x04;
        PreRenderOriginal(veh);
        *((BYTE*)0x6ACA57) = 0x66;
        *((BYTE*)0x6ACA58) = 0x3D;
        if (hasSiren)
            sirenRestore();
    }
    /*else if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        *((short*)0x6ABC83) = veh->m_nModelIndex;
        *((short*)0x6ABD11) = veh->m_nModelIndex;
        *((short*)0x6ABFCC) = veh->m_nModelIndex;
        *((short*)0x6AC029) = veh->m_nModelIndex;
        *((short*)0x6ACA4D) = veh->m_nModelIndex;
        PreRenderOriginal(veh);
        *((short*)0x6ABC83) = 0x1B0;
        *((short*)0x6ABD11) = 0x1B0;
        *((short*)0x6ABFCC) = 0x1B0;
        *((short*)0x6AC029) = 0x1B0;
        *((short*)0x6ACA4D) = 0x1B0;
    }*/
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 601)
    {
        *((short*)0x6ACA53) = veh->m_nModelIndex;
        PreRenderOriginal(veh);
        *((short*)0x6ACA53) = 0x259;
        if (hasSiren)
            sirenRestore();
    }
    else
    {
        PreRenderOriginal(veh);
        if (hasSiren)
            sirenRestore();
    }
}
/*
void __fastcall ProcessSuspensionHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        injector::MakeNOP(0x6B02A2, 6);
        *((short*)0x6AFB48) = veh->m_nModelIndex;

        ProcessSuspensionOriginal(veh);
        ((BYTE*)0x6B02A2)[0] = 0x0F;
        ((BYTE*)0x6B02A2)[1] = 0x85;
        ((BYTE*)0x6B02A2)[2] = 0x7B;
        ((BYTE*)0x6B02A2)[3] = 0x01;
        ((BYTE*)0x6B02A2)[4] = 0x00;
        ((BYTE*)0x6B02A2)[5] = 0x00;
        *((short*)0x6AFB48) = 0x1B0;
    }
    else
        ProcessSuspensionOriginal(veh);
}

void __fastcall SetupSuspensionLinesHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        *((short*)0x6A6606) = veh->m_nModelIndex;
        *((short*)0x6A6999) = veh->m_nModelIndex;
        SetupSuspensionLinesOriginal(veh);
        *((short*)0x6A6606) = 0x1B0;
        *((short*)0x6A6999) = 0x1B0;
    }
    else
        SetupSuspensionLinesOriginal(veh);
}

void __fastcall TankControlHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        *((short*)0x6AE9CB) = veh->m_nModelIndex;
        veh->TankControl();
        *((short*)0x6AE9CB) = 0x1B0;
    }
    else
        veh->TankControl();
}

void __fastcall VehicleDamageHooked(CAutomobile* veh, void* edx, float fDamageIntensity, __int16 tCollisionComponent, int Damager, RwV3d* vecCollisionCoors,  RwV3d* vecCollisionDirection, signed int a7)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        *((short*)0x6A80C0) = veh->m_nModelIndex;
        *((short*)0x6A8384) = veh->m_nModelIndex;
        VehicleDamageOriginal(veh, edx, fDamageIntensity, tCollisionComponent, Damager, vecCollisionCoors, vecCollisionDirection, a7);
        *((short*)0x6A80C0) = 0x1B0;
        *((short*)0x6A8384) = 0x1B0;
    }
    else
        VehicleDamageOriginal(veh, edx, fDamageIntensity, tCollisionComponent, Damager, vecCollisionCoors, vecCollisionDirection, a7);
}

void __fastcall DoSoftGroundResistanceHooked(CAutomobile *veh, void*, unsigned int &a3)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        *((short*)0x6A4BBA) = veh->m_nModelIndex;
        *((short*)0x6A4E0E) = veh->m_nModelIndex;
        veh->DoSoftGroundResistance(a3);
        *((short*)0x6A4BBA) = 0x1B0;
        *((short*)0x6A4E0E) = 0x1B0;
    }
    else
        veh->DoSoftGroundResistance(a3);
}

void __fastcall DoBurstAndSoftGroundRatiosHooked(CAutomobile* a1)
{
    if (getVariationOriginalModel(a1->m_nModelIndex) == 432)
    {
        *((short*)0x6A4917) = a1->m_nModelIndex;
        DoBurstAndSoftGroundRatiosOriginal(a1);
        *((short*)0x6A4917) = 0x1B0;
    }
    else
        DoBurstAndSoftGroundRatiosOriginal(a1);
}

char __fastcall BurstTyreHooked(CAutomobile* veh, void *edx, char componentId, char a3)
{
    char retVal = 0;
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        *((short*)0x6A32BB) = veh->m_nModelIndex;
        retVal = BurstTyreOriginal(veh, edx, componentId, a3);
        *((short*)0x6A32BB) = 0x1B0;
    }
    else
        retVal = BurstTyreOriginal(veh, edx, componentId, a3);

    return retVal;
}

void __fastcall CAutomobileRenderHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        *((short*)0x6A2C2D) = veh->m_nModelIndex;
        *((short*)0x6A2EAD) = veh->m_nModelIndex;
        CAutomobileRenderOriginal(veh);
        *((short*)0x6A2C2D) = 0x1B0;
        *((short*)0x6A2EAD) = 0x1B0;
    }
    else
        CAutomobileRenderOriginal(veh);
}

//int __fastcall ProcessEntityCollisionHooked(CAutomobile* veh, void* edx, CVehicle* collEntity, CColPoint* colPoint)
//{
//    if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
//    {
//        *((short*)0x6ACEE9) = veh->m_nModelIndex;
//        *((short*)0x6AD242) = veh->m_nModelIndex;
//        int retVal = ProcessEntityCollisionOriginal(veh, edx, collEntity, colPoint);
//        *((short*)0x6ACEE9) = 0x1B0;
//        *((short*)0x6AD242) = 0x1B0;
//        return retVal;
//    }
//
//    return ProcessEntityCollisionOriginal(veh, edx, collEntity, colPoint);
//}
*/

void installVehicleHooks()
{
    if (enableLog == 1)
        logfile << "Installing vehicle hooks..." << std::endl;


    hookCall(0x43022A, ChooseModelHooked<0x43022A>); //CCarCtrl::GenerateOneRandomCar
   //patch::RedirectJump(0x424E20, ChoosePoliceCarModelHooked);

    hookCall(0x42C320, ChoosePoliceCarModelHooked<0x42C320>); //CCarCtrl::CreatePoliceChase
    hookCall(0x43020E, ChoosePoliceCarModelHooked<0x43020E>); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x430283, ChoosePoliceCarModelHooked<0x430283>); //CCarCtrl::GenerateOneRandomCar

/*****************************************************************************************************/

    hookCall(0x42BC26, AddPoliceCarOccupantsHooked<0x42BC26>); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x42C620, AddPoliceCarOccupantsHooked<0x42C620>); //CCarCtrl::CreatePoliceChase
    hookCall(0x431EE5, AddPoliceCarOccupantsHooked<0x431EE5>); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x499CBB, AddPoliceCarOccupantsHooked<0x499CBB>); //CSetPiece::Update
    hookCall(0x499D6A, AddPoliceCarOccupantsHooked<0x499D6A>); //CSetPiece::Update
    hookCall(0x49A5EB, AddPoliceCarOccupantsHooked<0x49A5EB>); //CSetPiece::Update
    hookCall(0x49A85E, AddPoliceCarOccupantsHooked<0x49A85E>); //CSetPiece::Update
    hookCall(0x49A9AF, AddPoliceCarOccupantsHooked<0x49A9AF>); //CSetPiece::Update

/*****************************************************************************************************/
    
    hookCall(0x42B909, CAutomobileHooked<0x42B909>); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x4998F0, CAutomobileHooked<0x4998F0>); //CSetPiece::TryToGenerateCopCar
    hookCall(0x462217, CAutomobileHooked<0x462217>); //CRoadBlocks::CreateRoadBlockBetween2Points
    hookCall(0x61354A, CAutomobileHooked<0x61354A>); //CPopulation::CreateWaitingCoppers

    hookCall(0x6F3583, PickRandomCarHooked<0x6F3583>); //CCarGenerator::DoInternalProcessing
    hookCall(0x6F3EC1, DoInternalProcessingHooked<0x6F3EC1>); //CCarGenerator::Process 

    //Trains
    //patch::RedirectCall(0x4214DC, CTrainHooked); //CCarCtrl::GetNewVehicleDependingOnCarModel
    //patch::RedirectCall(0x5D2B15, CTrainHooked); //CPools::LoadVehiclePool
    hookCall(0x6F7634, CTrainHooked<0x6F7634>); //CTrain::CreateMissionTrain 

    //Boats
    hookCall(0x42149E, CBoatHooked<0x42149E>); //CCarCtrl::GetNewVehicleDependingOnCarModel
    hookCall(0x431FD0, CBoatHooked<0x431FD0>); //CCarCtrl::CreateCarForScript
    hookCall(0x5D2ADC, CBoatHooked<0x5D2ADC>); //CPools::LoadVehiclePool

    //Helis
    hookCall(0x6CD3C3, CHeliHooked<0x6CD3C3>); //CPlane::DoPlaneGenerationAndRemoval
    hookCall(0x6C6590, CHeliHooked<0x6C6590>); //CHeli::GenerateHeli
    hookCall(0x6C6568, CHeliHooked<0x6C6568>); //CHeli::GenerateHeli
    hookCall(0x5D2C46, CHeliHooked<0x5D2C46>); //CPools::LoadVehiclePool
    hookCall(0x6C7ACA, GenerateHeliHooked<0x6C7ACA>); //CHeli::UpdateHelis

    //Roadblocks
    hookCall(0x42CDDD, IsLawEnforcementVehicleHooked<0x42CDDD>); //CCarCtrl::RemoveDistantCars
    hookCall(0x42CE07, GenerateRoadBlockCopsForCarHooked<0x42CE07>); //CCarCtrl::RemoveDistantCars
    hookCall(0x4613EB, GetColModelHooked<0x4613EB>); //CCarCtrl::RemoveDistantCars

    hookCall(0x42BBFB, AddAmbulanceOccupantsHooked<0x42BBFB>); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x42BC1A, AddFiretruckOccupantsHooked<0x42BC1A>); //CCarCtrl::GenerateOneEmergencyServicesCar

    hookCall(0x613A43, FindSpecificDriverModelForCar_ToUseHooked<0x613A43>); //CPopulation::AddPedInCar
    hookCall(0x6D1B0E, AddPedInCarHooked<0x6D1B0E>); //CVehicle::SetupPassenger 

    hookCall(0x431DE2, SetUpDriverAndPassengersForVehicleHooked<0x431DE2>); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x431DF9, SetUpDriverAndPassengersForVehicleHooked<0x431DF9>); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x431ED1, SetUpDriverAndPassengersForVehicleHooked<0x431ED1>); //CCarCtrl::GenerateOneRandomCar

    hookCall(0x6B11C2, IsLawEnforcementVehicleHooked<0x6B11C2>); //CAutomobile::CAutomobile

    hookCall(0x60C4E8, PossiblyRemoveVehicleHooked<0x60C4E8>); //CPlayerPed::KeepAreaAroundPlayerClear
    hookCall(0x42CD55, PossiblyRemoveVehicleHooked<0x42CD55>); //CCarCtrl::RemoveDistantCars


    if (enableLights == 1)
    {
        hookCall(0x6ABA60, RegisterCoronaHooked<0x6ABA60>); //CAutomobile::PreRender
        hookCall(0x6ABB35, RegisterCoronaHooked<0x6ABB35>); //CAutomobile::PreRender
        hookCall(0x6ABC69, RegisterCoronaHooked<0x6ABC69>); //CAutomobile::PreRender

        hookCall(0x6AB80F, AddLightHooked<0x6AB80F>); //CAutomobile::PreRender
        hookCall(0x6ABBA6, AddLightHooked<0x6ABBA6>); //CAutomobile::PreRender
    }

    if (iniVeh.ReadInteger("Settings", "EnableSpecialFeatures", 0))
    {
        void(__fastcall **p)(CAutomobile*) = reinterpret_cast<void(__fastcall**)(CAutomobile*)>(0x871148);
        ProcessControlOriginal = *p;
        *p = ProcessControlHooked;

        p = reinterpret_cast<void(__fastcall **)(CAutomobile*)>(0x871164);
        PreRenderOriginal = *p;
        *p = PreRenderHooked;

        //p = reinterpret_cast<DWORD**>(0x871238);
        //ProcessSuspensionOriginal = *((void(**)(CAutomobile*))0x871238);
        //*p = (DWORD*)ProcessSuspensionHooked;

        //p = reinterpret_cast<DWORD**>(0x871200);
        //VehicleDamageOriginal = *((void(__fastcall**)(CAutomobile*, void*, float, __int16, int, RwV3d*, RwV3d*, signed int))0x871200);
        //*p = (DWORD*)VehicleDamageHooked;

        //p = reinterpret_cast<DWORD**>(0x8711E0);
        //SetupSuspensionLinesOriginal = *((void(**)(CAutomobile*))0x8711E0);
        //*p = (DWORD*)SetupSuspensionLinesHooked;

        //p = reinterpret_cast<DWORD**>(0x8711F0);
        //DoBurstAndSoftGroundRatiosOriginal = *((void(**)(CAutomobile*))0x8711F0);
        //*p = (DWORD*)DoBurstAndSoftGroundRatiosHooked;

        //p = reinterpret_cast<DWORD**>(0x8711D0);
        //BurstTyreOriginal = *((char(__fastcall**)(CAutomobile*, void*, char, char))0x8711D0);
        //*p = (DWORD*)BurstTyreHooked;

        //p = reinterpret_cast<DWORD**>(0x871168);
        //CAutomobileRenderOriginal = *((void(**)(CAutomobile*))0x871168);
        //*p = (DWORD*)CAutomobileRenderHooked;

        ////p = reinterpret_cast<DWORD**>(0x871178);
        ////ProcessEntityCollisionOriginal = *((int(__fastcall**)(CAutomobile*, void*, CVehicle*, CColPoint*))0x871178);
        ////*p = (DWORD*)ProcessEntityCollisionHooked;

        //patch::RedirectCall(0x6B2028, TankControlHooked); //CAutomobile::ProcessControl
        //patch::RedirectCall(0x6B51B8, DoSoftGroundResistanceHooked); //CAutomobile::ProcessAI

    }

    if ((enableSiren = iniVeh.ReadInteger("Settings", "EnableSiren", 0)) == 1)
        patch::RedirectCall(0x6D8492, HasCarSiren); //CVehicle::UsesSiren

    if ((disablePayAndSpray = iniVeh.ReadInteger("Settings", "DisablePayAndSpray", 0)) == 1)
        hookCall(0x44AC75, IsCarSprayableHooked<0x44AC75>); //CGarage::Update

    if (iniVeh.ReadInteger("Settings", "EnableSideMissions", 0))
    {
        enableSideMissions = true;
        hookCall(0x48DA81, IsLawEnforcementVehicleHooked<0x48DA81>);
        hookCall(0x469612, CollectParametersHooked<0x469612>);
    }
}