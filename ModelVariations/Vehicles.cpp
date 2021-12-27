#include "Vehicles.h"
#include "plugin.h"
#include "IniReader/IniReader.h"

#include "CBoat.h"
#include "CCarAI.h"
#include "CCarCtrl.h"
#include "CCarGenerator.h"
#include "CGarages.h"
#include "CGeneral.h"
#include "CHeli.h"
#include "CPopulation.h"
#include "CRoadBlocks.h"
#include "CRunningScript.h"
#include "CStreaming.h"
#include "CTaskComplexCopInCar.h"
#include "CTheScripts.h"
#include "CTrain.h"

#include <array>

using namespace plugin;

int roadblockModel = -1;

int passengerModelIndex = -1;
const unsigned int jmp613B7E = 0x613B7E;


int hasModelSideMission(int model)
{
    switch (model)
    {
        case 407: //Fire Truck
        case 416: //Ambulance
        case 537: //Freight
        case 538: //Brown Streak
        case 575: //Broadway
        case 609: //Boxville Mission
            return 1;
    }
    return 0;
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
    if (it != vehOriginalModels.end() && (it->second == 407 || it->second == 544))
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

    for (int i = 400; i < 32000; i++)
    {
        std::vector<short> vec = iniLineParser(VEHICLE_VARIATION, i, "Drivers", &iniVeh);
        if (!vec.empty())
            vehDrivers.insert({ i, vec });

        vec = iniLineParser(VEHICLE_VARIATION, i, "Passengers", &iniVeh);
        if (!vec.empty())
            vehPassengers.insert({ i, vec });
    }

    changeCarGenerators = iniVeh.ReadInteger("Settings", "ChangeCarGenerators", 0);
    vehCarGenExclude = iniLineParser(MODEL_SETTINGS, (int)"Settings", "ExcludeCarGeneratorVehicles", &iniVeh);
    loadAllVehicles = iniVeh.ReadInteger("Settings", "LoadAllVehicles", 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////    HOOKS    /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void hookTaxi()
{
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

int __cdecl ChooseModelHooked(int* a1)
{
    int model = CCarCtrl::ChooseModel(a1);

    if (model < 400 || model > 611)
        return model;

    if (vehCurrentVariations[model - 400].empty())
        return model;

    return getRandomVariation(model);
}

int __cdecl ChoosePoliceCarModelHooked(int a1)
{
    int model = CCarCtrl::ChoosePoliceCarModel(a1);

    if (model < 427 || model > 601)
        return model;

    if (vehCurrentVariations[model - 400].empty())
        return model;

    return getRandomVariation(model);
}

void __cdecl AddPoliceCarOccupantsHooked(CVehicle* a2, char a3)
{
    int model = a2->m_nModelIndex;
    a2->m_nModelIndex = getVariationOriginalModel(a2->m_nModelIndex);

    CCarAI::AddPoliceCarOccupants(a2, a3);

    a2->m_nModelIndex = model;
}

CAutomobile* __fastcall CAutomobileHooked(CAutomobile* automobile, void*, int modelIndex, char usageType, char bSetupSuspensionLines)
{
    return CallMethodAndReturn<CAutomobile*, 0x6B0A90>(automobile, getRandomVariation(modelIndex), usageType, bSetupSuspensionLines);
}

signed int __fastcall PickRandomCarHooked(CLoadedCarGroup* cargrp, void*, char a2, char a3) //for random parked cars
{
    if (cargrp == NULL)
        return -1;
    return getRandomVariation(cargrp->PickRandomCar(a2, a3));
}

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
                    park->DoInternalProcessing();
                    return;
                }

            park->m_nModelId = getRandomVariation(park->m_nModelId);
            park->DoInternalProcessing();
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
                park->DoInternalProcessing();
                park->m_nModelId = model;
                break;
            default:
                park->DoInternalProcessing();
        }
    }
}

CTrain* __fastcall CTrainHooked(CTrain* train, void*, int modelIndex, int createdBy)
{
    return CallMethodAndReturn<CTrain*, 0x6F6030>(train, getRandomVariation(modelIndex), createdBy);
}

CVehicle* __fastcall CBoatHooked(CBoat* boat, void*, int modelId, char a3)
{
    return CallMethodAndReturn<CVehicle*, 0x6F2940>(boat, getRandomVariation(modelId), a3);
}

CAutomobile* __fastcall CHeliHooked(CHeli* heli, void*, int a2, char usageType)
{
    return CallMethodAndReturn<CAutomobile*, 0x6C4190>(heli, getRandomVariation(a2), usageType);
}

char __fastcall IsLawEnforcementVehicleHooked(CVehicle* vehicle)
{
    if (vehicle == NULL)
        return 0;
    int modelIndex = vehicle->m_nModelIndex;

    vehicle->m_nModelIndex = getVariationOriginalModel(vehicle->m_nModelIndex);

    char isLawEnforcement = vehicle->IsLawEnforcementVehicle();
    vehicle->m_nModelIndex = modelIndex;
    return isLawEnforcement;
}

char __fastcall IsLawOrEmergencyVehicle(CVehicle* vehicle)
{
    if (vehicle == NULL)
        return 0;
    if (isModelAmbulance(vehicle->m_nModelIndex) || isModelFiretruck(vehicle->m_nModelIndex) || IsLawEnforcementVehicleHooked(vehicle))
        return 1;

    return 0;
}

char __cdecl IsCarSprayableHooked(CVehicle* a1)
{
    if (a1 == NULL)
        return 0;

    if (IsLawOrEmergencyVehicle(a1))
        return 0;

    return CallAndReturn<char, 0x4479A0>(a1);
}

char __fastcall HasCarSiren(CVehicle* vehicle)
{
    if (vehicle == NULL)
        return 0;
    auto it = vehOriginalModels.find(vehicle->m_nModelIndex);
    if (it != vehOriginalModels.end() && (it->second == 432 || it->second == 564))
        return 0;

    if (isModelAmbulance(vehicle->m_nModelIndex) || isModelFiretruck(vehicle->m_nModelIndex) || IsLawEnforcementVehicleHooked(vehicle))
        return 1;

    return 0;
}

void __cdecl AddAmbulanceOccupantsHooked(CVehicle* pVehicle)
{
    if (pVehicle == NULL)
        return;
    
    int model = pVehicle->m_nModelIndex;

    if (isModelAmbulance(pVehicle->m_nModelIndex))
        pVehicle->m_nModelIndex = 416;
    CCarAI::AddAmbulanceOccupants(pVehicle);
    pVehicle->m_nModelIndex = model;
}

void __cdecl AddFiretruckOccupantsHooked(CVehicle* pVehicle)
{
    if (pVehicle == NULL)
        return;

    int model = pVehicle->m_nModelIndex;

    if (isModelFiretruck(pVehicle->m_nModelIndex))
        pVehicle->m_nModelIndex = 407;
    CCarAI::AddFiretruckOccupants(pVehicle);
    pVehicle->m_nModelIndex = model;
}

DWORD __cdecl FindSpecificDriverModelForCar_ToUseHooked(int carModel)
{
    auto it = vehDrivers.find(carModel);
    int replaceDriver = iniVeh.ReadInteger(std::to_string(carModel), "ReplaceDriver", 0);
    if (it != vehDrivers.end())
        if ((replaceDriver == 0 && CGeneral::GetRandomNumberInRange(0, 100) > 50) || replaceDriver == 1)
        {
            int random = CGeneral::GetRandomNumberInRange(0, it->second.size());
            CStreaming::RequestModel(it->second[random], 2);
            CStreaming::LoadAllRequestedModels(false);
            return it->second[random];
        }

    return CPopulation::FindSpecificDriverModelForCar_ToUse(getVariationOriginalModel(carModel));
}

void __fastcall CollectParametersHooked(CRunningScript* script, void*, unsigned __int16 a2)
{
    script->CollectParameters(a2);
    if (!hasModelSideMission(CTheScripts::ScriptParams[1].uParam))
        return;

    if (strcmp(script->m_szName, "r3") != 0 && strcmp(script->m_szName, "ambulan") != 0 && strcmp(script->m_szName, "firetru") != 0 &&
        strcmp(script->m_szName, "freight") != 0)
        return;

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

char __cdecl GenerateRoadBlockCopsForCarHooked(CVehicle* a1, int pedsPositionsType, int type)
{
    if (a1 == NULL)
        return 0;

    roadblockModel = a1->m_nModelIndex;
    a1->m_nModelIndex = getVariationOriginalModel(a1->m_nModelIndex);
    CRoadBlocks::GenerateRoadBlockCopsForCar(a1, pedsPositionsType, type);
    if (roadblockModel >= 400)
        a1->m_nModelIndex = roadblockModel;
    roadblockModel = -1;

    return 1;
}
/*
void __fastcall ProcessControlHooked(CVehicle* veh)
{
    int model = veh->m_nModelIndex;
    veh->m_nModelIndex = getVariationOriginalModel(veh->m_nModelIndex);
    plugin::CallMethod<0x6B1880>(veh);
    veh->m_nModelIndex = model;
}
*/

CColModel* __fastcall GetColModelHooked(CVehicle* entity)
{
    if (roadblockModel >= 400)
        entity->m_nModelIndex = roadblockModel;
    return entity->GetColModel();
}

void __cdecl SetUpDriverAndPassengersForVehicleHooked(CVehicle* car, int a3, int a4, char a5, char a6, int a7)
{
    if (car == NULL)
        return;

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

    CCarCtrl::SetUpDriverAndPassengersForVehicle(car, a3, a4, a5, a6, a7);

    car->m_nModelIndex = model;
}

CHeli* __cdecl GenerateHeliHooked(CPed* ped, char newsHeli)
{
    if (FindPlayerWanted(-1)->m_nWantedLevel < 4)
        return CHeli::GenerateHeli(ped, 0);

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

    return CHeli::GenerateHeli(ped, newsHeli);
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

CPed* __cdecl AddPedInCarHooked(CVehicle* a1, char a2, int a3, signed int a4, int a5, char a6)
{
    if (a1)
    {
        auto it = vehPassengers.find(a1->m_nModelIndex);
        if (it != vehPassengers.end())
        {
            int replacePassenger = iniVeh.ReadInteger(std::to_string(a1->m_nModelIndex), "ReplacePassengers", 0);

            if ((replacePassenger == 0 && CGeneral::GetRandomNumberInRange(0, 100) > 50) || replacePassenger == 1)
            {
                int random = CGeneral::GetRandomNumberInRange(0, it->second.size());
                passengerModelIndex = it->second[random];
            }
            else
                return CPopulation::AddPedInCar(a1, a2, a3, a4, a5, a6);

            unsigned char originalData[5] = { 0x8D, 0x54, 0x24, 0x10, 0x52 };
            CStreaming::RequestModel(passengerModelIndex, 2);
            CStreaming::LoadAllRequestedModels(false);
            injector::MakeJMP(0x613B78, patchPassengerModel);
            CPed* ped = CPopulation::AddPedInCar(a1, a2, a3, a4, a5, a6);
            memcpy((void*)0x613B78, originalData, 5);
            passengerModelIndex = -1;
            return ped;
        }
    }


    return CPopulation::AddPedInCar(a1, a2, a3, a4, a5, a6);
}


void installVehicleHooks()
{
    patch::RedirectCall(0x43022A, ChooseModelHooked); //CCarCtrl::GenerateOneRandomCar
   //patch::RedirectJump(0x424E20, ChoosePoliceCarModelHooked);
    patch::RedirectCall(0x42C320, ChoosePoliceCarModelHooked); //CCarCtrl::CreatePoliceChase
    patch::RedirectCall(0x43020E, ChoosePoliceCarModelHooked); //CCarCtrl::GenerateOneRandomCar
    patch::RedirectCall(0x430283, ChoosePoliceCarModelHooked); //CCarCtrl::GenerateOneRandomCar

/*****************************************************************************************************/

    patch::RedirectCall(0x42BC26, AddPoliceCarOccupantsHooked); //CCarCtrl::GenerateOneEmergencyServicesCar
    patch::RedirectCall(0x42C620, AddPoliceCarOccupantsHooked); //CCarCtrl::CreatePoliceChase
    patch::RedirectCall(0x431EE5, AddPoliceCarOccupantsHooked); //CCarCtrl::GenerateOneRandomCar
    patch::RedirectCall(0x499CBB, AddPoliceCarOccupantsHooked); //CSetPiece::Update
    patch::RedirectCall(0x499D6A, AddPoliceCarOccupantsHooked); //CSetPiece::Update
    patch::RedirectCall(0x49A5EB, AddPoliceCarOccupantsHooked); //CSetPiece::Update
    patch::RedirectCall(0x49A85E, AddPoliceCarOccupantsHooked); //CSetPiece::Update
    patch::RedirectCall(0x49A9AF, AddPoliceCarOccupantsHooked); //CSetPiece::Update


/*****************************************************************************************************/

    patch::RedirectCall(0x42B909, CAutomobileHooked); //CCarCtrl::GenerateOneEmergencyServicesCar
    patch::RedirectCall(0x4998F0, CAutomobileHooked); //CSetPiece::TryToGenerateCopCar
    patch::RedirectCall(0x462217, CAutomobileHooked); //CRoadBlocks::CreateRoadBlockBetween2Points
    patch::RedirectCall(0x61354A, CAutomobileHooked); //CPopulation::CreateWaitingCoppers

    patch::RedirectCall(0x6F3583, PickRandomCarHooked); //CCarGenerator::DoInternalProcessing
    patch::RedirectCall(0x6F3EC1, DoInternalProcessingHooked); //CCarGenerator::Process

    //Trains
    //patch::RedirectCall(0x4214DC, CTrainHooked); //CCarCtrl::GetNewVehicleDependingOnCarModel
    //patch::RedirectCall(0x5D2B15, CTrainHooked); //CPools::LoadVehiclePool
    patch::RedirectCall(0x6F7634, CTrainHooked); //CTrain::CreateMissionTrain 

    //Boats
    patch::RedirectCall(0x42149E, CBoatHooked); //CCarCtrl::GetNewVehicleDependingOnCarModel
    patch::RedirectCall(0x431FD0, CBoatHooked); //CCarCtrl::CreateCarForScript
    patch::RedirectCall(0x5D2ADC, CBoatHooked); //CPools::LoadVehiclePool

    //Helis
    patch::RedirectCall(0x6CD3C3, CHeliHooked); //CPlane::DoPlaneGenerationAndRemoval
    patch::RedirectCall(0x6C6590, CHeliHooked); //CHeli::GenerateHeli
    patch::RedirectCall(0x6C6568, CHeliHooked); //CHeli::GenerateHeli
    patch::RedirectCall(0x5D2C46, CHeliHooked); //CPools::LoadVehiclePool
    patch::RedirectCall(0x6C7ACA, GenerateHeliHooked); //CHeli::UpdateHelis



    //Roadblocks
    patch::RedirectCall(0x42CDDD, IsLawEnforcementVehicleHooked); //CCarCtrl::RemoveDistantCars
    patch::RedirectCall(0x42CE07, GenerateRoadBlockCopsForCarHooked); //CCarCtrl::RemoveDistantCars
    patch::RedirectCall(0x4613EB, GetColModelHooked); //CCarCtrl::RemoveDistantCars

    patch::RedirectCall(0x42BBFB, AddAmbulanceOccupantsHooked); //CCarCtrl::GenerateOneEmergencyServicesCar
    patch::RedirectCall(0x42BC1A, AddFiretruckOccupantsHooked); //CCarCtrl::GenerateOneEmergencyServicesCar

    patch::RedirectCall(0x613A43, FindSpecificDriverModelForCar_ToUseHooked); //CPopulation::AddPedInCar
    patch::RedirectCall(0x6D1B0E, AddPedInCarHooked); //CVehicle::SetupPassenger

    patch::RedirectCall(0x431DE2, SetUpDriverAndPassengersForVehicleHooked); //CCarCtrl::GenerateOneRandomCar
    patch::RedirectCall(0x431DF9, SetUpDriverAndPassengersForVehicleHooked); //CCarCtrl::GenerateOneRandomCar
    patch::RedirectCall(0x431ED1, SetUpDriverAndPassengersForVehicleHooked); //CCarCtrl::GenerateOneRandomCar

    //DWORD** p;
    //p = reinterpret_cast<DWORD**>(0x871148);
    //*p = (DWORD*)ProcessControlHooked;

    if (iniVeh.ReadInteger("Settings", "EnableSiren", 0))
        patch::RedirectCall(0x6D8492, HasCarSiren); //CVehicle::UsesSiren

    if (iniVeh.ReadInteger("Settings", "DisablePayAndSpray", 0))
        patch::RedirectCall(0x44AC75, IsCarSprayableHooked); //CGarage::Update

    if (iniVeh.ReadInteger("Settings", "EnableSideMissions", 0))
    {
        enableSideMissions = true;
        patch::RedirectCall(0x48DA81, IsLawEnforcementVehicleHooked);
        patch::RedirectCall(0x469612, CollectParametersHooked);
    }
}
