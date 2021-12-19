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
    return (ambulanceModels.find(model) != ambulanceModels.end()) ? true : false;
}

bool isModelFiretruck(int model)
{
    return (firetruckModels.find(model) != firetruckModels.end()) ? true : false;
}

bool isModelTaxi(int model)
{
    return (taxiModels.find(model) != taxiModels.end()) ? true : false;
}

int getCopVariationOriginalModel(int modelIndex)
{
    int originalModel = modelIndex;

    if (copModels.find(modelIndex) != copModels.end())
        originalModel = 596;
    else if (copBikeModels.find(modelIndex) != copBikeModels.end())
        originalModel = 523;
    else if (swatModels.find(modelIndex) != swatModels.end())
        originalModel = 427;
    else if (fbiModels.find(modelIndex) != fbiModels.end())
        originalModel = 490;
    else if (tankModels.find(modelIndex) != tankModels.end())
        originalModel = 432;
    else if (barracksModels.find(modelIndex) != barracksModels.end())
        originalModel = 433;
    else if (patriotModels.find(modelIndex) != patriotModels.end())
        originalModel = 470;
    else if (heliModels.find(modelIndex) != heliModels.end())
        originalModel = 497;
    else if (predatorModels.find(modelIndex) != predatorModels.end())
        originalModel = 430;

    return originalModel;
}

int getVariationOriginalModel(int modelIndex)
{
    int originalModel = modelIndex;

    originalModel = getCopVariationOriginalModel(modelIndex);

    if (ambulanceModels.find(modelIndex) != ambulanceModels.end())
        originalModel = 416;
    else if (firetruckModels.find(modelIndex) != firetruckModels.end())
        originalModel = 407;
    else if (taxiModels.find(modelIndex) != taxiModels.end())
        originalModel = 420;
    else if (pimpModels.find(modelIndex) != pimpModels.end())
        originalModel = 575;
    else if (burglarModels.find(modelIndex) != burglarModels.end())
        originalModel = 609;
    else if (trainModels.find(modelIndex) != trainModels.end())
        originalModel = 538;

    return originalModel;
}

int getRandomVariation(int modelid)
{
    if (modelid < 400 || modelid > 611)
        return modelid;
    if (currentVehVariations[modelid - 400].empty())
        return modelid;

    int random = CGeneral::GetRandomNumberInRange(0, currentVehVariations[modelid - 400].size());
    int variationModel = currentVehVariations[modelid - 400][random];
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
        {
            for (int k = 0; k < (int)(vehVariations[i-400][j].size()); k++)
            {
                if (vehVariations[i-400][j][k] > 0 && vehVariations[i-400][j][k] < 30000  &&/*
                    getVariationOriginalModel(vehVariations[i-400][j][k]) == vehVariations[i-400][j][k] &&*/ vehVariations[i-400][j][k] != i)
                {
                    switch (i)
                    {
                        case 596: //Police LS
                        case 597: //Police SF
                        case 598: //Police LV
                        case 599: //Police Ranger
                            copModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 523: //HPV1000
                            copBikeModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 427: //Enforcer
                        case 601: //S.W.A.T.
                            swatModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 490: //FBI Rancher
                        case 528: //FBI Truck
                            fbiModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 433: //Barracks
                            barracksModels.insert(vehVariations[i - 400][j][k]);
                            break;
                        case 470: //Patriot
                            patriotModels.insert(vehVariations[i - 400][j][k]);
                            break;
                        case 432: //Rhino
                            tankModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 430: //Predator
                            predatorModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 497: //Police Maverick
                            heliModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 407: //Fire Truck
                            firetruckModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 416: //Ambulance
                            ambulanceModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 420: //Taxi
                        case 438: //Cabbie
                            taxiModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 575: //Broadway
                            pimpModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 609: //Boxville Mission
                            burglarModels.insert(vehVariations[i-400][j][k]);
                            break;
                        case 538: //Brown Streak
                        case 537: //Freight
                            trainModels.insert(vehVariations[i-400][j][k]);
                            break;
                    }
                }
            }
        }
    }
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

    if (currentVehVariations[model - 400].empty())
        return model;

    return getRandomVariation(model);
}

int __cdecl ChoosePoliceCarModelHooked(int a1)
{
    int model = CCarCtrl::ChoosePoliceCarModel(a1);

    if (model < 427 || model > 601)
        return model;

    if (currentVehVariations[model - 400].empty())
        return model;

    return getRandomVariation(model);
}

void __cdecl AddPoliceCarOccupantsHooked(CVehicle* a2, char a3)
{
    int model = a2->m_nModelIndex;
    a2->m_nModelIndex = getCopVariationOriginalModel(a2->m_nModelIndex);

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

    vehicle->m_nModelIndex = getCopVariationOriginalModel(vehicle->m_nModelIndex);

    char isLawEnforcement = vehicle->IsLawEnforcementVehicle();
    vehicle->m_nModelIndex = modelIndex;
    return isLawEnforcement;
}

char __cdecl IsCarSprayableHooked(CVehicle* a1)
{
    if (a1 == NULL)
        return 0;

    if (getCopVariationOriginalModel(a1->m_nModelIndex) != a1->m_nModelIndex || isModelAmbulance(a1->m_nModelIndex) || isModelFiretruck(a1->m_nModelIndex))
        return 0;

    return CallAndReturn<char, 0x4479A0>(a1);
}

char __fastcall IsLawOrEmergencyVehicle(CVehicle* vehicle)
{
    if (vehicle == NULL)
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
    a1->m_nModelIndex = getCopVariationOriginalModel(a1->m_nModelIndex);
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

    CStreaming::RequestModel(277, 2);//lafd1
    CStreaming::RequestModel(278, 2);//lvfd1
    CStreaming::RequestModel(279, 2);//sffd1
    CStreaming::RequestModel(274, 2);//laemt1
    CStreaming::RequestModel(275, 2);//lvemt1
    CStreaming::RequestModel(276, 2);//sfemt1
    CStreaming::RequestModel(285, 2);//swat
    CStreaming::RequestModel(286, 2);//fbi
    
    CStreaming::LoadAllRequestedModels(false);

    int model = car->m_nModelIndex;
    car->m_nModelIndex = getVariationOriginalModel(car->m_nModelIndex);

    CCarCtrl::SetUpDriverAndPassengersForVehicle(car, a3, a4, a5, a6, a7);

    car->m_nModelIndex = model;
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

    //Roadblocks
    patch::RedirectCall(0x42CDDD, IsLawEnforcementVehicleHooked); //CCarCtrl::RemoveDistantCars
    patch::RedirectCall(0x42CE07, GenerateRoadBlockCopsForCarHooked); //CCarCtrl::RemoveDistantCars
    patch::RedirectCall(0x4613EB, GetColModelHooked); //CCarCtrl::RemoveDistantCars

    patch::RedirectCall(0x42BBFB, AddAmbulanceOccupantsHooked); //CCarCtrl::GenerateOneEmergencyServicesCar
    patch::RedirectCall(0x42BC1A, AddFiretruckOccupantsHooked); //CCarCtrl::GenerateOneEmergencyServicesCar

    patch::RedirectCall(0x613A43, FindSpecificDriverModelForCar_ToUseHooked); //CPopulation::AddPedInCar

    patch::RedirectCall(0x431DE2, SetUpDriverAndPassengersForVehicleHooked); //CCarCtrl::GenerateOneRandomCar
    patch::RedirectCall(0x431DF9, SetUpDriverAndPassengersForVehicleHooked); //CCarCtrl::GenerateOneRandomCar
    patch::RedirectCall(0x431ED1, SetUpDriverAndPassengersForVehicleHooked); //CCarCtrl::GenerateOneRandomCar

    //DWORD** p;
    //p = reinterpret_cast<DWORD**>(0x871148);
    //*p = (DWORD*)ProcessControlHooked;

    if (iniVeh.ReadInteger("Settings", "EnableSiren", 0))
        patch::RedirectCall(0x6D8492, IsLawOrEmergencyVehicle); //CVehicle::UsesSiren

    if (iniVeh.ReadInteger("Settings", "DisablePayAndSpray", 0))
        patch::RedirectCall(0x44AC75, IsCarSprayableHooked); //CGarage::Update

    if (iniVeh.ReadInteger("Settings", "EnableSideMissions", 0))
    {
        enableSideMissions = true;
        patch::RedirectCall(0x48DA81, IsLawEnforcementVehicleHooked);
        patch::RedirectCall(0x469612, CollectParametersHooked);
    }
}
