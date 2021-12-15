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

//int AddPoliceCarOccupantsOriginalModel;

bool elementExists(std::vector<short>* vec, int element)
{
    if (vec)
        if (std::find(vec->begin(), vec->end(), element) != vec->end())
            return true;

    return false;
}

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

int isModelAmbulance(int model)
{
    if (std::find(ambulanceModels.begin(), ambulanceModels.end(), model) != ambulanceModels.end())
        return 1;

    return 0;
}

int isModelFiretruck(int model)
{
    if (std::find(firetruckModels.begin(), firetruckModels.end(), model) != firetruckModels.end())
        return 1;

    return 0;
}

int isModelTaxi(int model)
{
    if (std::find(taxiModels.begin(), taxiModels.end(), model) != taxiModels.end())
        return 1;

    return 0;
}

int getCopVariationOriginalModel(int modelIndex)
{
    int originalModel = modelIndex;

    if (elementExists(&copModels, modelIndex))
        originalModel = 596;
    else if (elementExists(&copBikeModels, modelIndex))
        originalModel = 523;
    else if (elementExists(&swatModels, modelIndex))
        originalModel = 427;
    else if (elementExists(&fbiModels, modelIndex))
        originalModel = 490;
    else if (elementExists(&tankModels, modelIndex))
        originalModel = 432;
    else if (elementExists(&barracksModels, modelIndex))
        originalModel = 433;
    else if (elementExists(&patriotModels, modelIndex))
        originalModel = 470;
    else if (elementExists(&heliModels, modelIndex))
        originalModel = 497;
    else if (elementExists(&predatorModels, modelIndex))
        originalModel = 430;

    return originalModel;
}

int getVariationOriginalModel(int modelIndex)
{
    int originalModel = modelIndex;

    originalModel = getCopVariationOriginalModel(modelIndex);

    if (elementExists(&ambulanceModels, modelIndex))
        originalModel = 416;
    else if (elementExists(&firetruckModels, modelIndex))
        originalModel = 407;
    else if (elementExists(&taxiModels, modelIndex))
        originalModel = 420;
    else if (elementExists(&pimpModels, modelIndex))
        originalModel = 575;
    else if (elementExists(&burglarModels, modelIndex))
        originalModel = 609;
    else if (elementExists(&trainModels, modelIndex))
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
        vehVariations[i-400][0] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "LosSantos", &iniVeh);
        vehVariations[i-400][1] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "SanFierro", &iniVeh);
        vehVariations[i-400][2] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "LasVenturas", &iniVeh);
        vehVariations[i-400][3] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "Global", &iniVeh);
        vehVariations[i-400][4] = vec;
        vec = iniLineParser(VEHICLE_VARIATION, i, "Desert", &iniVeh);
        vehVariations[i-400][5] = vec;

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

        for (int j = 0; j < 212; j++)
        {
            std::sort(vehVariations[j][0].begin(), vehVariations[j][0].end());
            std::sort(vehVariations[j][1].begin(), vehVariations[j][1].end());
            std::sort(vehVariations[j][2].begin(), vehVariations[j][2].end());
            std::sort(vehVariations[j][3].begin(), vehVariations[j][3].end());
            std::sort(vehVariations[j][4].begin(), vehVariations[j][4].end());
            std::sort(vehVariations[j][5].begin(), vehVariations[j][5].end());
        }


        for (int j = 0; j < 6; j++)
        {
            for (int k = 0; k < (int)(vehVariations[i-400][j].size()); k++)
            {
                if (vehVariations[i-400][j][k] > 0 && vehVariations[i-400][j][k] < 30000 &&
                    getVariationOriginalModel(vehVariations[i-400][j][k]) == vehVariations[i-400][j][k] && vehVariations[i-400][j][k] != i)
                {
                    switch (i)
                    {
                        case 596: //Police LS
                        case 597: //Police SF
                        case 598: //Police LV
                        case 599: //Police Ranger
                            copModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 523: //HPV1000
                            copBikeModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 427: //Enforcer
                        case 601: //S.W.A.T.
                            swatModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 490: //FBI Rancher
                        case 528: //FBI Truck
                            fbiModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 433: //Barracks
                            barracksModels.push_back(vehVariations[i - 400][j][k]);
                            break;
                        case 470: //Patriot
                            patriotModels.push_back(vehVariations[i - 400][j][k]);
                            break;
                        case 432: //Rhino
                            tankModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 430: //Predator
                            predatorModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 497: //Police Maverick
                            heliModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 407: //Fire Truck
                            firetruckModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 416: //Ambulance
                            ambulanceModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 420: //Taxi
                        case 438: //Cabbie
                            taxiModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 575: //Broadway
                            pimpModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 609: //Boxville Mission
                            burglarModels.push_back(vehVariations[i-400][j][k]);
                            break;
                        case 538: //Brown Streak
                        case 537: //Freight
                            trainModels.push_back(vehVariations[i-400][j][k]);
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
/*
CPed* __fastcall SetupPassengerHooked(CVehicle* vehicle, void*, int a2, signed int a3, char a4, char a5)
{
    //vehicle->m_nModelIndex = changedModel;
    CPed* ped = vehicle->SetupPassenger(a2, a3, a4, a5);
    vehicle->m_nModelIndex = AddPoliceCarOccupantsOriginalModel;

    return ped;
}
*/

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

CPed* __cdecl AddPedInCarHooked(CVehicle* a1, char a2, int a3, signed int a4, int a5, char a6)
{
    if (a1 == NULL)
        return NULL;

    int model = a1->m_nModelIndex;
    a1->m_nModelIndex = getVariationOriginalModel(a1->m_nModelIndex);
    CPed *ped = CPopulation::AddPedInCar(a1, a2, a3, a4, a5, a6);
    a1->m_nModelIndex = model;
    return ped;
}

char __cdecl GenerateRoadBlockCopsForCarHooked(CVehicle* a1, int pedsPositionsType, int type)
{
    if (a1 == NULL)
        return 0;

    int model = a1->m_nModelIndex;
    a1->m_nModelIndex = getCopVariationOriginalModel(a1->m_nModelIndex);
    CRoadBlocks::GenerateRoadBlockCopsForCar(a1, pedsPositionsType, type);
    a1->m_nModelIndex = model;

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

    ////patch::RedirectCall(0x41C0D0, SetUpDriverHooked);
    //patch::RedirectCall(0x41C107, SetupPassengerHooked);


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

    patch::RedirectCall(0x42BBFB, AddAmbulanceOccupantsHooked); //CCarCtrl::GenerateOneEmergencyServicesCar
    patch::RedirectCall(0x42BC1A, AddFiretruckOccupantsHooked); //CCarCtrl::GenerateOneEmergencyServicesCar

    patch::RedirectCall(0x613A43, FindSpecificDriverModelForCar_ToUseHooked); //CPopulation::AddPedInCar

    //DWORD** p;
    //p = reinterpret_cast<DWORD**>(0x871148);
    //*p = (DWORD*)ProcessControlHooked;

    //Crashes when it tries to add ped to a variation replacing 
    //the firetruck (probably all emergency vehicles)
    // 
    //if (iniVeh.ReadInteger("Settings", "EnableTaxiPassengers", 0) == 0)
        //patch::RedirectCall(0x6D1B0E, AddPedInCarHooked); //CVehicle::SetupPassenger

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
