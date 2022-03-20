#include "Vehicles.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LogUtil.hpp"
#include <plugin.h>
#include <../../injector/assembly.hpp>

#include <CBoat.h>
#include <CCarCtrl.h>
#include <CCarGenerator.h>
#include <CCoronas.h>
#include <CDarkel.h>
#include <CGeneral.h>
#include <CHeli.h>
#include <CModelInfo.h>
#include <CPopulation.h>
#include <CStreaming.h>
#include <CTheScripts.h>
#include <CTrain.h>
#include <CTrailer.h>
#include <CVector.h>

#include <array>

using namespace plugin;

unsigned short roadblockModel = 0;
int sirenModel = -1;
unsigned short lightsModel = 0;
int currentOccupantsGroup = -1;
unsigned short currentOccupantsModel = 0;

int fireCmpModel = -1;

int passengerModelIndex = -1;
const unsigned int jmp431BF2 = 0x431BF2;
const unsigned int jmp51E5BE = 0x51E5BE;
const unsigned int jmp52546A = 0x52546A;
const unsigned int jmp613B7E = 0x613B7E;
const unsigned int jmp644683 = 0x644683;
const unsigned int jmp6AB35A = 0x6AB35A;
const unsigned int jmp6ABA65 = 0x6ABA65;
const unsigned int jmp6B4CF1 = 0x6B4CF1;
int carGenModel = -1;

void checkNumGroups(std::vector<unsigned short>& vec, BYTE numGroups)
{
    auto it = vec.begin();
    while (it != vec.end())
    {
        if (*it > numGroups)
            it = vec.erase(it);
        else
            ++it;
    }
}

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

bool isModelAmbulance(const unsigned short model)
{
    auto it = vehOriginalModels.find(model);
    if (it != vehOriginalModels.end() && it->second == 416)
        return true;

    return false;
}

bool isModelFiretruck(const unsigned short model)
{
    auto it = vehOriginalModels.find(model);
    if (it != vehOriginalModels.end() && it->second == 407)
        return true;

    return false;
}

bool isModelTaxi(const unsigned short model)
{
    auto it = vehOriginalModels.find(model);
    if (it != vehOriginalModels.end() && (it->second == 438 || it->second == 420))
        return true;

    return false;
}

static int  __stdcall getVariationOriginalModel(const int modelIndex)
{
    if (modelIndex < 400)
        return modelIndex;

    auto it = vehOriginalModels.find((unsigned short)modelIndex);
    if (it != vehOriginalModels.end())
        return it->second;

    return modelIndex;
}

int getRandomVariation(const int modelid, bool parked = false)
{
    if (modelid < 400 || modelid > 611)
        return modelid;
    if (vehCurrentVariations[modelid - 400].empty())
        return modelid;

    if (parked == false)
    {
        auto it = parkedCars.find((unsigned short)modelid);
        if (it != parkedCars.end())
            return modelid;
    }

    unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)vehCurrentVariations[modelid - 400].size());
    unsigned short variationModel = vehCurrentVariations[modelid - 400][random];
    if (variationModel > 0)
    {
        CStreaming::RequestModel(variationModel, 2);
        CStreaming::LoadAllRequestedModels(false);
        return variationModel;
    }
    return modelid;
}


void readVehicleIni(bool firstTime)
{
    std::string str;
    std::vector <std::string> result;
    std::ifstream zoneFile("data\\info.zon");

    if (firstTime)
        vehInheritExclude = iniVeh.ReadLine("Settings", "ExcludeModelsFromInheritance");

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
            logfile << "Zone file 'info.zon' found." << std::endl;
        else
            logfile << "Zone file 'info.zon' NOT found!" << std::endl;
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
                    c = (char)::toupper(c);
                });
                result.push_back(substr);
            }
        }
    }

    for (auto& i : result) //for every zone name
        for (auto& j : iniVeh.data)
        {
            int modelid = 0;
            if (j.first[0] >= '0' && j.first[0] <= '9')
                modelid = std::stoi(j.first);
            else
                CModelInfo::GetModelInfo((char*)j.first.c_str(), &modelid);

            if (modelid > 0)
            {
                std::vector<unsigned short> vec = iniVeh.ReadLine(j.first, i); //get zone name 'i' of veh id 'j'

                if (!vec.empty()) //if veh id 'j' has variations in zone 'i'
                    for (auto& k : vec) //for every variation 'k' of veh id 'j' in zone 'i'
                        if (modelid != k && !(IdExists(vehInheritExclude, k)))
                            vehOriginalModels.insert({ k, modelid });
            }            
        }

    if (zoneFile.is_open())
        zoneFile.close();

    for (auto& inidata : iniVeh.data)
    {
        std::string section = inidata.first;
        int modelid = 0;
        if (section[0] >= '0' && section[0] <= '9')
            modelid = std::stoi(section);
        else
            CModelInfo::GetModelInfo((char*)section.c_str(), &modelid);

        unsigned short i = (unsigned short)modelid;
        if (i >= 400 && i < 612)
        {
            vehHasVariations.insert((unsigned short)(i - 400));

            if (iniVeh.ReadInteger(section, "ChangeOnlyParked", 0) == 1)
                parkedCars.insert(i);

            vehVariations[i - 400][0] = iniVeh.ReadLine(section, "Countryside");
            vehVariations[i - 400][1] = iniVeh.ReadLine(section, "LosSantos");
            vehVariations[i - 400][2] = iniVeh.ReadLine(section, "SanFierro");
            vehVariations[i - 400][3] = iniVeh.ReadLine(section, "LasVenturas");
            vehVariations[i - 400][4] = iniVeh.ReadLine(section, "Global");
            vehVariations[i - 400][5] = iniVeh.ReadLine(section, "Desert");

            std::vector<unsigned short> vec = iniVeh.ReadLine(section, "TierraRobada");
            vehVariations[i - 400][6] = vectorUnion(vec, vehVariations[i - 400][5]);

            vec = iniVeh.ReadLine(section, "BoneCounty");
            vehVariations[i - 400][7] = vectorUnion(vec, vehVariations[i - 400][5]);

            vec = iniVeh.ReadLine(section, "RedCounty");
            vehVariations[i - 400][8] = vectorUnion(vec, vehVariations[i - 400][0]);

            vec = iniVeh.ReadLine(section, "Blueberry");
            vehVariations[i - 400][9] = vectorUnion(vec, vehVariations[i - 400][8]);

            vec = iniVeh.ReadLine(section, "Montgomery");
            vehVariations[i - 400][10] = vectorUnion(vec, vehVariations[i - 400][8]);

            vec = iniVeh.ReadLine(section, "Dillimore");
            vehVariations[i - 400][11] = vectorUnion(vec, vehVariations[i - 400][8]);

            vec = iniVeh.ReadLine(section, "PalominoCreek");
            vehVariations[i - 400][12] = vectorUnion(vec, vehVariations[i - 400][8]);

            vec = iniVeh.ReadLine(section, "FlintCounty");
            vehVariations[i - 400][13] = vectorUnion(vec, vehVariations[i - 400][0]);

            vec = iniVeh.ReadLine(section, "Whetstone");
            vehVariations[i - 400][14] = vectorUnion(vec, vehVariations[i - 400][0]);

            vec = iniVeh.ReadLine(section, "AngelPine");
            vehVariations[i - 400][15] = vectorUnion(vec, vehVariations[i - 400][14]);


            vehWantedVariations[i - 400][0] = iniVeh.ReadLine(section, "Wanted1");
            vehWantedVariations[i - 400][1] = iniVeh.ReadLine(section, "Wanted2");
            vehWantedVariations[i - 400][2] = iniVeh.ReadLine(section, "Wanted3");
            vehWantedVariations[i - 400][3] = iniVeh.ReadLine(section, "Wanted4");
            vehWantedVariations[i - 400][4] = iniVeh.ReadLine(section, "Wanted5");
            vehWantedVariations[i - 400][5] = iniVeh.ReadLine(section, "Wanted6");


            for (unsigned int j = 0; j < 16; j++)
                for (unsigned int k = 0; k < vehVariations[i - 400][j].size(); k++)
                    if (vehVariations[i - 400][j][k] > 0 && vehVariations[i - 400][j][k] < 32000 && vehVariations[i - 400][j][k] != i && !(IdExists(vehInheritExclude, vehVariations[i - 400][j][k])))
                        vehOriginalModels.insert({ vehVariations[i - 400][j][k], i });
        }
    }

    if (firstTime)
        enableLights = iniVeh.ReadInteger("Settings", "EnableLights", 0);
    
    for (auto& i : iniVeh.data)
    {
        int k = 0;
        if (i.first[0] >= '0' && i.first[0] <= '9')
            k = std::stoi(i.first);
        else
            CModelInfo::GetModelInfo((char*)i.first.c_str(), &k);

        unsigned short modelid = (unsigned short)k;

        if (modelid >= 400)
        {
            vehGroups[modelid][0] = iniVeh.ReadLine(i.first, "Countryside", true);
            vehGroups[modelid][1] = iniVeh.ReadLine(i.first, "LosSantos", true);
            vehGroups[modelid][2] = iniVeh.ReadLine(i.first, "SanFierro", true);
            vehGroups[modelid][3] = iniVeh.ReadLine(i.first, "LasVenturas", true);
            vehGroups[modelid][4] = iniVeh.ReadLine(i.first, "Global", true);
            vehGroups[modelid][5] = iniVeh.ReadLine(i.first, "Desert", true);

            std::vector<unsigned short> vec = iniVeh.ReadLine(i.first, "TierraRobada", true);
            vehGroups[modelid][6] = vectorUnion(vec, vehGroups[modelid][5]);

            vec = iniVeh.ReadLine(i.first, "BoneCounty", true);
            vehGroups[modelid][7] = vectorUnion(vec, vehGroups[modelid][5]);

            vec = iniVeh.ReadLine(i.first, "RedCounty", true);
            vehGroups[modelid][8] = vectorUnion(vec, vehGroups[modelid][0]);

            vec = iniVeh.ReadLine(i.first, "Blueberry", true);
            vehGroups[modelid][9] = vectorUnion(vec, vehGroups[modelid][8]);

            vec = iniVeh.ReadLine(i.first, "Montgomery", true);
            vehGroups[modelid][10] = vectorUnion(vec, vehGroups[modelid][8]);

            vec = iniVeh.ReadLine(i.first, "Dillimore", true);
            vehGroups[modelid][11] = vectorUnion(vec, vehGroups[modelid][8]);

            vec = iniVeh.ReadLine(i.first, "PalominoCreek", true);
            vehGroups[modelid][12] = vectorUnion(vec, vehGroups[modelid][8]);

            vec = iniVeh.ReadLine(i.first, "FlintCounty", true);
            vehGroups[modelid][13] = vectorUnion(vec, vehGroups[modelid][0]);

            vec = iniVeh.ReadLine(i.first, "Whetstone", true);
            vehGroups[modelid][14] = vectorUnion(vec, vehGroups[modelid][0]);

            vec = iniVeh.ReadLine(i.first, "AngelPine", true);
            vehGroups[modelid][15] = vectorUnion(vec, vehGroups[modelid][14]);


            if (iniVeh.ReadInteger(i.first, "UseOnlyGroups", 0) == 1)
                vehUseOnlyGroups.insert(modelid);

            if (enableLights)
            {
                float lightWidth = iniVeh.ReadFloat(i.first, "LightWidth", -999.0);
                float lightX = iniVeh.ReadFloat(i.first, "LightX", -999.0);
                float lightY = iniVeh.ReadFloat(i.first, "LightY", -999.0);
                float lightZ = iniVeh.ReadFloat(i.first, "LightZ", -999.0);

                int r = iniVeh.ReadInteger(i.first, "LightR", -1);
                int g = iniVeh.ReadInteger(i.first, "LightG", -1);
                int b = iniVeh.ReadInteger(i.first, "LightB", -1);
                int a = iniVeh.ReadInteger(i.first, "LightA", -1);

                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255 && a >= 0 && a <= 255)
                {
                    rgba colors = { (BYTE)r, (BYTE)g, (BYTE)b, (BYTE)a };
                    LightColors.insert({ modelid, colors });
                }

                if (lightX > -900.0 || lightY > -900.0 || lightZ > -900.0)
                    LightPositions.insert({ modelid, {{ lightX, lightY, lightZ }, lightWidth} });

                r = iniVeh.ReadInteger(i.first, "LightR2", -1);
                g = iniVeh.ReadInteger(i.first, "LightG2", -1);
                b = iniVeh.ReadInteger(i.first, "LightB2", -1);
                a = iniVeh.ReadInteger(i.first, "LightA2", -1);

                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255 && a >= 0 && a <= 255)
                {
                    rgba colors = { (BYTE)r, (BYTE)g, (BYTE)b, (BYTE)a };
                    LightColors2.insert({ modelid, colors });
                }
            }

            if (iniVeh.ReadInteger(i.first, "MergeZonesWithCities", 0) == 1)
                vehMergeZones.insert(modelid);

            BYTE numGroups = 0;
            for (int j = 0; j < 9; j++)
            {
                str = "DriverGroup" + std::to_string(j + 1);
                vec = iniVeh.ReadLine(i.first, str);
                if (!vec.empty())
                {
                    vehDriverGroups[j].insert({ modelid, vec });
                    numGroups++;
                }
                if (numGroups > 0)
                    modelNumGroups[modelid] = numGroups;
                else
                    continue;

                str = "PassengerGroup" + std::to_string(j + 1);
                vec = iniVeh.ReadLine(i.first, str);
                if (!vec.empty())
                    vehPassengerGroups[j].insert({ modelid, vec });
            }

            vec = iniVeh.ReadLine(i.first, "Wanted1", true);
            if (!vec.empty())
            {
                vec.erase(unique(vec.begin(), vec.end()), vec.end());
                checkNumGroups(vec, modelNumGroups[modelid]);
                vehGroupWantedVariations[modelid][0] = vec;
            }

            vec = iniVeh.ReadLine(i.first, "Wanted2", true);
            if (!vec.empty())
            {
                vec.erase(unique(vec.begin(), vec.end()), vec.end());
                checkNumGroups(vec, modelNumGroups[modelid]);
                vehGroupWantedVariations[modelid][1] = vec;
            }

            vec = iniVeh.ReadLine(i.first, "Wanted3", true);
            if (!vec.empty())
            {
                vec.erase(unique(vec.begin(), vec.end()), vec.end());
                checkNumGroups(vec, modelNumGroups[modelid]);
                vehGroupWantedVariations[modelid][2] = vec;
            }

            vec = iniVeh.ReadLine(i.first, "Wanted4", true);
            if (!vec.empty())
            {
                vec.erase(unique(vec.begin(), vec.end()), vec.end());
                checkNumGroups(vec, modelNumGroups[modelid]);
                vehGroupWantedVariations[modelid][3] = vec;
            }

            vec = iniVeh.ReadLine(i.first, "Wanted5", true);
            if (!vec.empty())
            {
                vec.erase(unique(vec.begin(), vec.end()), vec.end());
                checkNumGroups(vec, modelNumGroups[modelid]);
                vehGroupWantedVariations[modelid][4] = vec;
            }

            vec = iniVeh.ReadLine(i.first, "Wanted6", true);
            if (!vec.empty())
            {
                vec.erase(unique(vec.begin(), vec.end()), vec.end());
                checkNumGroups(vec, modelNumGroups[modelid]);
                vehGroupWantedVariations[modelid][5] = vec;
            }

            if (vehGroups.find(modelid) != vehGroups.end())
                for (unsigned int j = 0; j < 16; j++)
                    checkNumGroups(vehGroups[modelid][j], modelNumGroups[modelid]);


            vec = iniVeh.ReadLine(i.first, "Drivers");
            if (!vec.empty())
                vehDrivers.insert({ modelid, vec });

            vec = iniVeh.ReadLine(i.first, "Passengers");
            if (!vec.empty())
                vehPassengers.insert({ modelid, vec });
        }
    }

    if (firstTime)
    {
        changeCarGenerators = iniVeh.ReadInteger("Settings", "ChangeCarGenerators", 0);
        vehCarGenExclude = iniVeh.ReadLine("Settings", "ExcludeCarGeneratorModels");
        loadAllVehicles = iniVeh.ReadInteger("Settings", "LoadAllVehicles", 0);
        enableAllSideMissions = iniVeh.ReadInteger("Settings", "EnableSideMissionsForAllScripts", 0);
    }
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

    return getRandomVariation((unsigned short)model);
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

    return getRandomVariation((unsigned short)model);
}

template <unsigned int address>
void __cdecl AddPoliceCarOccupantsHooked(CVehicle* a2, char a3)
{
    if (a2 == NULL)
        return;

    if (vehUseOnlyGroups.find(a2->m_nModelIndex) != vehUseOnlyGroups.end() || (CGeneral::GetRandomNumberInRange(0, 100) > 50))
    {
        auto it = modelNumGroups.find(a2->m_nModelIndex);
        if (it != modelNumGroups.end())
        {
            CWanted* wanted = FindPlayerWanted(-1);
            unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            unsigned int i = CGeneral::GetRandomNumberInRange(0, (int)vehGroupWantedVariations[a2->m_nModelIndex][wantedLevel].size());
            currentOccupantsModel = a2->m_nModelIndex;

            std::vector<unsigned short> zoneGroups = iniVeh.ReadLine(std::to_string(a2->m_nModelIndex), currentZone, true);
            checkNumGroups(zoneGroups, it->second);
            if (vehGroups.find(a2->m_nModelIndex) != vehGroups.end())
            {
                if (vehMergeZones.find(a2->m_nModelIndex) != vehMergeZones.end())
                    zoneGroups = vectorUnion(zoneGroups, vehGroups[a2->m_nModelIndex][currentTown]);
                else if (zoneGroups.empty())
                    zoneGroups = vehGroups[a2->m_nModelIndex][currentTown];
            }

            if (zoneGroups.empty() && !vehGroupWantedVariations[a2->m_nModelIndex][wantedLevel].empty())
                currentOccupantsGroup = vehGroupWantedVariations[a2->m_nModelIndex][wantedLevel][i] - 1;
            else
            {
                if (!vehGroupWantedVariations[a2->m_nModelIndex][wantedLevel].empty())
                    filterWantedVariations(zoneGroups, vehGroupWantedVariations[a2->m_nModelIndex][wantedLevel]);
                if (!zoneGroups.empty())
                    currentOccupantsGroup = zoneGroups[CGeneral::GetRandomNumberInRange(0, (int)zoneGroups.size())] - 1;
                else
                    currentOccupantsGroup = (int)CGeneral::GetRandomNumberInRange(0, (int)it->second);
            }
        }
    }

    unsigned short model = a2->m_nModelIndex;
    a2->m_nModelIndex = (unsigned short)getVariationOriginalModel(a2->m_nModelIndex);

    callOriginal<address>(a2, a3);
    //CCarAI::AddPoliceCarOccupants(a2, a3);

    a2->m_nModelIndex = model;
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
}

template <unsigned int address, bool keepOriginal = false>
CAutomobile* __fastcall CAutomobileHooked(CAutomobile* automobile, void*, int modelIndex, char usageType, char bSetupSuspensionLines)
{
    int randomVariation = modelIndex;

    if (!keepOriginal)
        randomVariation = getRandomVariation(modelIndex);

    int model = 0;
    unsigned short *modelAddress = NULL;

    if (getVariationOriginalModel(modelIndex) == 525) //Towtruck
    {
        model = 525;
        modelAddress = (unsigned short*)0x6B0EE8;
    }
    else if (getVariationOriginalModel(modelIndex) == 531) //Tractor
    {
        model = 531;
        modelAddress = (unsigned short*)0x6B0F11;
    }
    else if (getVariationOriginalModel(modelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6B0CF4) == 432 && *((unsigned short*)0x6B12D8) == 432)
        {
            *((unsigned short*)0x6B0CF4) = (unsigned short)randomVariation;
            *((unsigned short*)0x6B12D8) = (unsigned short)randomVariation;
            CAutomobile* retVal = callMethodOriginalAndReturn<CAutomobile*, address>(automobile, randomVariation, usageType, bSetupSuspensionLines);
            *((unsigned short*)0x6B0CF4) = 432;
            *((unsigned short*)0x6B12D8) = 432;
            return retVal;
        }
        else
        {
            logModified((unsigned int)0x6B0CF4, "Modified method detected : CAutomobile::CAutomobile - 0x6B0CF4 is %d" + std::to_string(*((unsigned short*)0x6B0CF4)));
            logModified((unsigned int)0x6B12D8, "Modified method detected : CAutomobile::CAutomobile - 0x6B12D8 is %d" + std::to_string(*((unsigned short*)0x6B12D8)));
        }
    }

    if (model > 0)
    {
        if (*modelAddress != model)
        {
            logModified((unsigned int)modelAddress, printToString("Modified method detected : CAutomobile::CAutomobile - 0x%X is %d", modelAddress, *modelAddress));
            return callMethodOriginalAndReturn<CAutomobile*, address>(automobile, randomVariation, usageType, bSetupSuspensionLines);
        }

        *modelAddress = (unsigned short)randomVariation;
        CAutomobile* retVal = callMethodOriginalAndReturn<CAutomobile*, address>(automobile, randomVariation, usageType, bSetupSuspensionLines);
        *modelAddress = (unsigned short)model;
        return retVal;
    }

    return callMethodOriginalAndReturn<CAutomobile*, address>(automobile, randomVariation, usageType, bSetupSuspensionLines);    
}

template <unsigned int address>
signed int __fastcall PickRandomCarHooked(CLoadedCarGroup* cargrp, void*, char a2, char a3) //for random parked cars
{
    if (cargrp == NULL)
        return -1;

    int originalModel = callMethodOriginalAndReturn<signed int, address>(cargrp, a2, a3);
    int variation = getRandomVariation(originalModel, true);

    if (originalModel == 531) //Tractor
    {
        if (*(unsigned short*)0x6F3B9A == 531)
        {
            *(unsigned short*)0x6F3B9A = (unsigned short)variation;
            carGenModel = 531;
        }
        else
            logModified((unsigned int)0x6F3B9A, "Modified method detected : CCarGenerator::DoInternalProcessing - 0x6F3B9A is %d" + std::to_string(*(unsigned short*)0x6F3B9A));
    }

    return variation;
}

template <unsigned int address>
void __fastcall DoInternalProcessingHooked(CCarGenerator* park) //for non-random parked cars
{
    if (park != NULL)
    {
        if (park->m_nModelId < 0)
        {
            callMethodOriginal<address>(park);
            if (carGenModel == 531)
                *(unsigned short*)0x6F3B9A = 531;

            carGenModel = -1;
            return;
        }

        short model = park->m_nModelId;
        if (changeCarGenerators == 1)
        {
            if (!vehCarGenExclude.empty())
                if (std::find(vehCarGenExclude.begin(), vehCarGenExclude.end(), model) != vehCarGenExclude.end())
                {
                    callMethodOriginal<address>(park);
                    return;
                }
            
            if (model == 531) //Tractor
            {
                if (*(unsigned short*)0x6F3B9A == 531)
                {
                    park->m_nModelId = (short)getRandomVariation(park->m_nModelId, true);
                    *(unsigned short*)0x6F3B9A = (unsigned short)park->m_nModelId;
                    callMethodOriginal<address>(park);
                    *(unsigned short*)0x6F3B9A = 531;
                    //park->m_nModelId = 531;
                    return;
                }
                else
                {
                    callMethodOriginal<address>(park);
                    logModified((unsigned int)0x6F3B9A, "Modified method detected : CCarGenerator::DoInternalProcessing - 0x6F3B9A is %d" + std::to_string(*(unsigned short*)0x6F3B9A));
                    return;
                }
            }

            park->m_nModelId = (short)getRandomVariation(park->m_nModelId, true);
            callMethodOriginal<address>(park);
            //park->m_nModelId = model;
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
                park->m_nModelId = (short)getRandomVariation(park->m_nModelId, true);
                callMethodOriginal<address>(park);
                //park->m_nModelId = model;
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
    unsigned short modelIndex = vehicle->m_nModelIndex;

    vehicle->m_nModelIndex = (unsigned short)getVariationOriginalModel(vehicle->m_nModelIndex);

    char isLawEnforcement = 0;
    if constexpr (address == 0)
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

template <unsigned int address>
char __fastcall HasCarSiren(CVehicle* vehicle)
{
    if (vehicle == NULL)
        return 0;
    auto it = vehOriginalModels.find(vehicle->m_nModelIndex);
    if (it != vehOriginalModels.end() && (it->second == 432 || it->second == 564))
        return 0;

    if (isModelAmbulance(vehicle->m_nModelIndex) || isModelFiretruck(vehicle->m_nModelIndex) || IsLawEnforcementVehicleHooked<0>(vehicle) || callMethodOriginalAndReturn<char, address>(vehicle))
        return 1;

    return 0;
}

template <unsigned int address>
void __cdecl AddAmbulanceOccupantsHooked(CVehicle* pVehicle)
{
    if (pVehicle == NULL)
        return;
    
    auto model = pVehicle->m_nModelIndex;

    if (isModelAmbulance(pVehicle->m_nModelIndex))
        pVehicle->m_nModelIndex = 416;
    callOriginal<address>(pVehicle);
    pVehicle->m_nModelIndex = model;
}

template <unsigned int address>
void __cdecl AddFiretruckOccupantsHooked(CVehicle* pVehicle)
{
    if (pVehicle == NULL)
        return;

    auto model = pVehicle->m_nModelIndex;

    if (isModelFiretruck(pVehicle->m_nModelIndex))
        pVehicle->m_nModelIndex = 407;
    callOriginal<address>(pVehicle);
    pVehicle->m_nModelIndex = model;
}

template <unsigned int address>
DWORD __cdecl FindSpecificDriverModelForCar_ToUseHooked(int carModel)
{
    if (carModel < 400)
        return (DWORD)callOriginalAndReturn<int, address>(getVariationOriginalModel(carModel));

    auto it = vehDrivers.find((unsigned short)carModel);
    int replaceDriver = iniVeh.ReadInteger(std::to_string(carModel), "ReplaceDriver", 0);
    if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
    {
        auto itGroup = vehDriverGroups[currentOccupantsGroup].find(currentOccupantsModel);
        if (itGroup != vehDriverGroups[currentOccupantsGroup].end())
        {
            unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)itGroup->second.size());
            CStreaming::RequestModel(itGroup->second[random], 2);
            CStreaming::LoadAllRequestedModels(false);
            return itGroup->second[random];
        }
    }
    if (it != vehDrivers.end() && ((replaceDriver == 0 && CGeneral::GetRandomNumberInRange(0, 100) > 50) || replaceDriver == 1))
    {
        unsigned int random = CGeneral::GetRandomNumberInRange(0, (int)it->second.size());
        CStreaming::RequestModel(it->second[random], 2);
        CStreaming::LoadAllRequestedModels(false);
        return it->second[random];
    }

    return (DWORD)callOriginalAndReturn<int, address>(getVariationOriginalModel(carModel));
}

template <unsigned int address>
void __fastcall CollectParametersHooked(CRunningScript* script, void*, unsigned __int16 a2)
{
    callMethodOriginal<address>(script, a2);
    if (enableAllSideMissions == 0)
    {
        if (!hasModelSideMission((int)CTheScripts::ScriptParams[1].uParam))
            return;

        if (strcmp(script->m_szName, "r3") != 0 && strcmp(script->m_szName, "ambulan") != 0 && strcmp(script->m_szName, "firetru") != 0 &&
            strcmp(script->m_szName, "freight") != 0)
            return;
    }

    CPlayerPed* player = FindPlayerPed();  
    if (player == NULL)
        return;

    int hplayer = CPools::GetPedRef(player);

    if ((int)(CTheScripts::ScriptParams[0].uParam) != hplayer)
        return;

    if (player->m_pVehicle)
    {
        int originalModel = getVariationOriginalModel(player->m_pVehicle->m_nModelIndex);
        if ((int)(CTheScripts::ScriptParams[1].uParam) == originalModel)
            CTheScripts::ScriptParams[1].uParam = player->m_pVehicle->m_nModelIndex;
    }
}

template <unsigned int address>
char __cdecl GenerateRoadBlockCopsForCarHooked(CVehicle* a1, int pedsPositionsType, int type)
{
    if (a1 == NULL)
        return 0;

    roadblockModel = a1->m_nModelIndex;
    a1->m_nModelIndex = (unsigned short)getVariationOriginalModel(a1->m_nModelIndex);
    callOriginal<address>(a1, pedsPositionsType, type);
    if (roadblockModel >= 400)
        a1->m_nModelIndex = roadblockModel;
    roadblockModel = 0;

    return 1;
}

template <unsigned int address>
CColModel* __fastcall GetColModelHooked(CVehicle* entity)
{
    if (roadblockModel >= 400)
        entity->m_nModelIndex = roadblockModel;
    return callMethodOriginalAndReturn<CColModel*, address>(entity);
}

template <unsigned int address>
void __cdecl SetUpDriverAndPassengersForVehicleHooked(CVehicle* car, int a3, int a4, char a5, char a6, int a7)
{
    if (car == NULL)
        return;

    if (vehUseOnlyGroups.find(car->m_nModelIndex) != vehUseOnlyGroups.end() || (CGeneral::GetRandomNumberInRange(0, 100) > 50))
    {
        auto it = modelNumGroups.find(car->m_nModelIndex);
        if (it != modelNumGroups.end())
        {
            CWanted* wanted = FindPlayerWanted(-1);
            unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            unsigned int i = CGeneral::GetRandomNumberInRange(0, (int)vehGroupWantedVariations[car->m_nModelIndex][wantedLevel].size());
            currentOccupantsModel = car->m_nModelIndex;

            std::vector<unsigned short> zoneGroups = iniVeh.ReadLine(std::to_string(car->m_nModelIndex), currentZone, true);
            checkNumGroups(zoneGroups, it->second);
            if (vehGroups.find(car->m_nModelIndex) != vehGroups.end())
            {
                if (vehMergeZones.find(car->m_nModelIndex) != vehMergeZones.end())
                    zoneGroups = vectorUnion(zoneGroups, vehGroups[car->m_nModelIndex][currentTown]);
                else if (zoneGroups.empty())
                    zoneGroups = vehGroups[car->m_nModelIndex][currentTown];
            }

            if (zoneGroups.empty() && !vehGroupWantedVariations[car->m_nModelIndex][wantedLevel].empty())
                currentOccupantsGroup = vehGroupWantedVariations[car->m_nModelIndex][wantedLevel][i] - 1;
            else
            {
                if (!vehGroupWantedVariations[car->m_nModelIndex][wantedLevel].empty())
                    filterWantedVariations(zoneGroups, vehGroupWantedVariations[car->m_nModelIndex][wantedLevel]);
                if (!zoneGroups.empty())
                    currentOccupantsGroup = zoneGroups[CGeneral::GetRandomNumberInRange(0, (int)zoneGroups.size())] - 1;
                else
                    currentOccupantsGroup = (int)CGeneral::GetRandomNumberInRange(0, it->second);
            }
        }
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

    auto model = car->m_nModelIndex;

    car->m_nModelIndex = (unsigned short)getVariationOriginalModel(car->m_nModelIndex);

    callOriginal<address>(car, a3, a4, a5, a6, a7);
    //CCarCtrl::SetUpDriverAndPassengersForVehicle(car, a3, a4, a5, a6, a7);

    car->m_nModelIndex = model;
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
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

template <unsigned int address>
CPlane* __fastcall CPlaneHooked(CPlane* plane, void*, int a2, char a3)
{
    return callMethodOriginalAndReturn<CPlane*, address>(plane, getRandomVariation(a2), a3);
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
    unsigned int random = 0;
    if (a1)
    {
        int replacePassenger = iniVeh.ReadInteger(std::to_string(a1->m_nModelIndex), "ReplacePassengers", 0);
        auto it = vehPassengers.find(a1->m_nModelIndex);
        if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
        {
            auto itGroup = vehPassengerGroups[currentOccupantsGroup].find(currentOccupantsModel);
            if (itGroup != vehPassengerGroups[currentOccupantsGroup].end())
            {
                random = CGeneral::GetRandomNumberInRange(0, (int)itGroup->second.size());
                passengerModelIndex = itGroup->second[random];
                goto ModelChosen;
            }
        }
        if (it != vehPassengers.end() && ((replacePassenger == 0 && CGeneral::GetRandomNumberInRange(0, 100) > 50) || replacePassenger == 1))
        {
            random = CGeneral::GetRandomNumberInRange(0, (int)it->second.size());
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
void __cdecl RegisterCoronaHooked(CCoronas* _this, unsigned int a2, unsigned __int8 a3, unsigned __int8 a4, unsigned __int8 a5, unsigned __int8 a6, CVector* a7, const CVector* a8,
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
                a7->y += it->second.first.y;
            if (it->second.first.z > -900.0)
                a7->z += it->second.first.z;
        }
    }

    if (lightsModel > 0)
    {
        auto it = LightColors.find(lightsModel);
        if (it != LightColors.end())
        {
            a3 = it->second.r;
            a4 = it->second.g;
            a5 = it->second.b;
            a6 = it->second.a;
        }
    }

    callOriginal<address>(_this, a2, a3, a4, a5, a6, a7, a8, a9, texture, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
}

void __cdecl RegisterCoronaHooked2(CCoronas* _this, unsigned int a2, unsigned __int8 a3, unsigned __int8 a4, unsigned __int8 a5, unsigned __int8 a6, CVector* a7, const CVector* a8,
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
                a7->y += it->second.first.y;
            if (it->second.first.z > -900.0)
                a7->z += it->second.first.z;
        }
    }

    if (lightsModel > 0)
    {
        auto it = LightColors2.find(lightsModel);
        if (it != LightColors2.end())
        {
            a3 = it->second.r;
            a4 = it->second.g;
            a5 = it->second.b;
            a6 = it->second.a;
        }
    }

    callOriginal<0x6ABA60>(_this, a2, a3, a4, a5, a6, a7, a8, a9, texture, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
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
                y += it->second.first.y;
            if (it->second.first.z > -900.0)
                z += it->second.first.z;
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

    if (*((unsigned short*)0x4250AC) == 416)
    {
        *((unsigned short*)0x4250AC) = car->m_nModelIndex;
        callOriginal<address>(car);
        *((unsigned short*)0x4250AC) = 416;
        return;
    }
    else
    {
        logModified(0x4250AC, "Modified method detected: CCarCtrl::PossiblyRemoveVehicle - 0x4250AC is " + std::to_string(*((unsigned short*)0x4250AC)));
        callOriginal<address>(car);
    }
}

template <unsigned int address>
CVehicle* __cdecl CreateCarForScriptHooked(int modelId, float posX, float posY, float posZ, char doMissionCleanup)
{
    return callOriginalAndReturn<CVehicle*, address>(getRandomVariation(modelId), posX, posY, posZ, doMissionCleanup);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////  Vehicle Special Features //////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template <unsigned int address>
void __fastcall ProcessControlHooked(CAutomobile* veh)
{
    int model = 0;
    unsigned short* modelAddress = NULL;

    if (getVariationOriginalModel(veh->m_nModelIndex) == 407)
    {
        model = 407;
        modelAddress = (unsigned short*)0x6B1F51;
    }
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 601)
    {
        model = 601;
        modelAddress = (unsigned short*)0x6B1F57;
    }
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 525) //Towtruck
    {
        model = 525;
        modelAddress = (unsigned short*)0x6B1FB5;
    }
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 531) //Tractor
    {
        model = 531;
        modelAddress = (unsigned short*)0x6B1FBB;
    }
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 432)
    {
        if (*((unsigned short*)0x6B1F7D) == 432 && *((unsigned short*)0x6B36D8) == 432)
        {
            *((unsigned short*)0x6B1F7D) = veh->m_nModelIndex;
            *((unsigned short*)0x6B36D8) = veh->m_nModelIndex;
            callMethodOriginal<address>(veh);
            *((unsigned short*)0x6B1F7D) = 432;
            *((unsigned short*)0x6B36D8) = 432;
            return;
        }
        else
        {
            logModified((unsigned int)0x6B1F7D, printToString("Modified method detected : CAutomobile::ProcessControl - 0x6B1F7D is %d", *(unsigned short*)0x6B1F7D));
            logModified((unsigned int)0x6B36D8, printToString("Modified method detected : CAutomobile::ProcessControl - 0x6B36D8 is %d", *(unsigned short*)0x6B36D8));
            callMethodOriginal<address>(veh);
        }
    }

    if (model > 0)
    {
        if (*modelAddress == model)
        {
            *modelAddress = veh->m_nModelIndex;
            callMethodOriginal<address>(veh);
            *modelAddress = (unsigned short)model;
        }
        else
        {
            logModified((unsigned int)modelAddress, printToString("Modified method detected : CAutomobile::ProcessControl - 0x%X is %d", modelAddress, *modelAddress));
            callMethodOriginal<address>(veh);
        }
    }
    else
        callMethodOriginal<address>(veh);
}

void __declspec(naked) enableSirenLights()
{
    __asm {
        mov eax, sirenModel
        lea edi, [eax - 0x197]
        jmp jmp6AB35A
    }
}

template <unsigned int address>
void __fastcall PreRenderHooked(CAutomobile* veh)
{
    BYTE sirenOriginal[5] = { *(BYTE*)0x6AB350, *(BYTE*)0x6AB351, *(BYTE*)0x6AB352, *(BYTE*)0x6AB353, *(BYTE*)0x6AB354 };
    unsigned short *modelAddress = NULL;
    unsigned short model = 0;

    auto sirenRestore = [sirenOriginal]()
    {
        ((BYTE*)0x6AB350)[0] = sirenOriginal[0];
        ((BYTE*)0x6AB350)[1] = sirenOriginal[1];
        ((BYTE*)0x6AB350)[2] = sirenOriginal[2];
        ((BYTE*)0x6AB350)[3] = sirenOriginal[3];
        ((BYTE*)0x6AB350)[4] = sirenOriginal[4];
    };

    if (veh == NULL)
        return;

    sirenModel = -1;
    lightsModel = 0;
    bool hasSiren = false;

    if (enableLights && (HasCarSiren<0>(veh) || getVariationOriginalModel(veh->m_nModelIndex) == 420 || getVariationOriginalModel(veh->m_nModelIndex) == 438))
    {
        sirenModel = getVariationOriginalModel(veh->m_nModelIndex);
        lightsModel = veh->m_nModelIndex;
        injector::MakeJMP(0x6AB350, enableSirenLights);
        hasSiren = true;
    }

    if (getVariationOriginalModel(veh->m_nModelIndex) == 407) { model = 407; modelAddress = (unsigned short*)0x6ACA59;} //Firetruck
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 601) { model = 601; modelAddress = (unsigned short*)0x6ACA53;} //SWAT Tank 
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 525) { model = 525; modelAddress = (unsigned short*)0x6AC509;} //Towtruck
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 531) { model = 531; modelAddress = (unsigned short*)0x6AC6DB;} //Tractor
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6ABC83) == 432 && *((unsigned short*)0x6ABD11) == 432 && *((unsigned short*)0x6ABFCC) == 432 &&
            *((unsigned short*)0x6AC029) == 432 && *((unsigned short*)0x6ACA4D))
        {
            *((unsigned short*)0x6ABC83) = veh->m_nModelIndex;
            *((unsigned short*)0x6ABD11) = veh->m_nModelIndex;
            *((unsigned short*)0x6ABFCC) = veh->m_nModelIndex;
            *((unsigned short*)0x6AC029) = veh->m_nModelIndex;
            *((unsigned short*)0x6ACA4D) = veh->m_nModelIndex;
            callMethodOriginal<address>(veh);
            *((unsigned short*)0x6ABC83) = 432;
            *((unsigned short*)0x6ABD11) = 432;
            *((unsigned short*)0x6ABFCC) = 432;
            *((unsigned short*)0x6AC029) = 432;
            *((unsigned short*)0x6ACA4D) = 432;
        }
        else
        {
            logModified((unsigned int)0x6ABC83, "Modified method detected : CAutomobile::PreRender - 0x6ABC83 is %d" + std::to_string(*((unsigned short*)0x6ABC83)));
            logModified((unsigned int)0x6ABD11, "Modified method detected : CAutomobile::PreRender - 0x6ABD11 is %d" + std::to_string(*((unsigned short*)0x6ABD11)));
            logModified((unsigned int)0x6ABFCC, "Modified method detected : CAutomobile::PreRender - 0x6ABFCC is %d" + std::to_string(*((unsigned short*)0x6ABFCC)));
            logModified((unsigned int)0x6AC029, "Modified method detected : CAutomobile::PreRender - 0x6AC029 is %d" + std::to_string(*((unsigned short*)0x6AC029)));
            logModified((unsigned int)0x6ACA4D, "Modified method detected : CAutomobile::PreRender - 0x6ACA4D is %d" + std::to_string(*((unsigned short*)0x6ACA4D)));
            callMethodOriginal<address>(veh);
        }
        if (hasSiren)
            sirenRestore();
        return;
    }
    

    if (model > 0)
    {
        if (*modelAddress == model)
        {
            *modelAddress = veh->m_nModelIndex;
            callMethodOriginal<address>(veh);
            *modelAddress = model;
        }
        else
        {
            logModified((unsigned int)modelAddress, printToString("Modified method detected : CAutomobile::PreRender - 0x%X is %d", modelAddress, *modelAddress));
            callMethodOriginal<address>(veh);
        }
    }
    else
        callMethodOriginal<address>(veh);

    if (hasSiren)
        sirenRestore();
}

template <unsigned int address>
char __fastcall GetTowBarPosHooked(CAutomobile* automobile, void*, CVector* outPos, char ignoreModelType, CVehicle* attachTo)
{
    if (getVariationOriginalModel(automobile->m_nModelIndex) == 525) //Towtruck
    {
        if (*((unsigned short*)0x6AF259) == 525)
        {
            *((unsigned short*)0x6AF259) = automobile->m_nModelIndex;
            char retVal = callMethodOriginalAndReturn<char, address>(automobile, outPos, ignoreModelType, attachTo);
            *((unsigned short*)0x6AF259) = 525;
            return retVal;
        }
        else
        {
            logModified((unsigned int)0x6AF259, "Modified method detected : CAutomobile::GetTowBarPos - 0x6AF259 is " + std::to_string(*((unsigned short*)0x6AF259)));
            return callMethodOriginalAndReturn<char, address>(automobile, outPos, ignoreModelType, attachTo);
        }
    }
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 531) //Tractor
    {
        if (*((unsigned short*)0x6AF264) == 531 && *((unsigned short*)0x6AF343) == 531)
        {
            *((unsigned short*)0x6AF264) = automobile->m_nModelIndex;
            *((unsigned short*)0x6AF343) = automobile->m_nModelIndex;
            char retVal = callMethodOriginalAndReturn<char, address>(automobile, outPos, ignoreModelType, attachTo);
            *((unsigned short*)0x6AF264) = 531;
            *((unsigned short*)0x6AF343) = 531;
            return retVal;
        }
        else
        {
            logModified((unsigned int)0x6AF264, "Modified method detected : CAutomobile::GetTowBarPos - 0x6AF264 is %d" + std::to_string(*((unsigned short*)0x6AF264)));
            logModified((unsigned int)0x6AF343, "Modified method detected : CAutomobile::GetTowBarPos - 0x6AF343 is %d" + std::to_string(*((unsigned short*)0x6AF343)));
            return callMethodOriginalAndReturn<char, address>(automobile, outPos, ignoreModelType, attachTo);
        }
    }


    return callMethodOriginalAndReturn<char, address>(automobile, outPos, ignoreModelType, attachTo);
}

template <unsigned int address>
char __fastcall SetTowLinkHooked(CAutomobile* automobile, void*, CVehicle* vehicle, char a3)
{
    if (vehicle != NULL)
    {
        if (getVariationOriginalModel(vehicle->m_nModelIndex) == 525) //Towtruck
        {
            if (*((unsigned short*)0x6B44B0) != 525)
            {
                logModified((unsigned int)0x6B44B0, "Modified method detected : CAutomobile::SetTowLink - 0x6B44B0 is " + std::to_string(*((unsigned short*)0x6B44B0)));
                return callMethodOriginalAndReturn<char, address>(automobile, vehicle, a3);
            }

            *((unsigned short*)0x6B44B0) = vehicle->m_nModelIndex;
            char retVal = callMethodOriginalAndReturn<char, address>(automobile, vehicle, a3);
            *((unsigned short*)0x6B44B0) = 525;
            return retVal;
        }
        else if (getVariationOriginalModel(vehicle->m_nModelIndex) == 531) //Tractor
        {
            if (*((unsigned short*)0x6B44E6) != 531)
            {
                logModified((unsigned int)0x6B44E6, "Modified method detected : CAutomobile::SetTowLink - 0x6B44E6 is " + std::to_string(*((unsigned short*)0x6B44E6)));
                return callMethodOriginalAndReturn<char, address>(automobile, vehicle, a3);
            }

            *((unsigned short*)0x6B44E6) = vehicle->m_nModelIndex;
            char retVal = callMethodOriginalAndReturn<char, address>(automobile, vehicle, a3);
            *((unsigned short*)0x6B44E6) = 531;
            return retVal;
        }
    }

    return callMethodOriginalAndReturn<char, address>(automobile, vehicle, a3);
}

template <unsigned int address>
char __fastcall GetTowHitchPosHooked(CTrailer* trailer, void*, CVector* point, char a3, CVehicle* a4)
{
    if (a4 != NULL)
    {
        if (getVariationOriginalModel(a4->m_nModelIndex) == 525) //Towtruck
        {
            if (*((unsigned short*)0x6CEED9) != 525)
            {
                logModified((unsigned int)0x6CEED9, "Modified method detected : CTrailer::GetTowHitchPos - 0x6CEED9 is " + std::to_string(*((unsigned short*)0x6CEED9)));
                return callMethodOriginalAndReturn<char, address>(trailer, point, a3, a4);
            }

            *((unsigned short*)0x6CEED9) = a4->m_nModelIndex;
            char retVal = callMethodOriginalAndReturn<char, address>(trailer, point, a3, a4);
            *((unsigned short*)0x6CEED9) = 525;
            return retVal;
        }
    }

    return callMethodOriginalAndReturn<char, address>(trailer, point, a3, a4);
}

template <unsigned int address>
void __fastcall UpdateTrailerLinkHooked(CVehicle* veh, void*, char a2, char a3)
{
    int model = 0;
    unsigned short* modelAddress = NULL;

    if (veh != NULL && veh->m_pTractor != NULL)
    {
        if (getVariationOriginalModel(veh->m_pTractor->m_nModelIndex) == 525) //Towtruck
        {
            model = 525;
            modelAddress = (unsigned short *)0x6DFDB8;
        }
        else if (getVariationOriginalModel(veh->m_pTractor->m_nModelIndex) == 531) //Tractor
        {
            model = 531;
            modelAddress = (unsigned short*)0x6DFDBE;
        }
    }

    if (model > 0)
    {
        if (*modelAddress == model)
        {
            *modelAddress = veh->m_pTractor->m_nModelIndex;
            callMethodOriginal<address>(veh, a2, a3);
            *modelAddress = (unsigned short)model;
        }
        else
        {
            logModified((unsigned int)modelAddress, printToString("Modified method detected : CVehicle::UpdateTrailerLink - 0x%X is %d", modelAddress, *modelAddress));
            callMethodOriginal<address>(veh, a2, a3);
        }
    }
    else
        callMethodOriginal<address>(veh, a2, a3);
}

template <unsigned int address>
void __fastcall UpdateTractorLinkHooked(CVehicle* veh, void*, bool a3, bool a4)
{
    int model = 0;
    unsigned short* modelAddress = NULL;
    
    if (veh != NULL)
    {
        if (getVariationOriginalModel(veh->m_nModelIndex) == 525) //Towtruck
        {
            model = 525;
            modelAddress = (unsigned short*)0x6E00D6;
        }
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 531) //Tractor
        {
            model = 531;
            modelAddress = (unsigned short*)0x6E00FC;
        }
    }

    if (model > 0)
    {
        if (*modelAddress == model)
        {
            *modelAddress = veh->m_nModelIndex;
            callMethodOriginal<address>(veh, a3, a4);
            *modelAddress = (unsigned short)model;
        }
        else
        {
            logModified((unsigned int)modelAddress, printToString("Modified method detected : CVehicle::UpdateTractorLink - 0x%X is %d", modelAddress, *modelAddress));
            callMethodOriginal<address>(veh, a3, a4);
        }
    }
    else
        callMethodOriginal<address>(veh, a3, a4);
}

template <unsigned int address>
char __fastcall SetUpWheelColModelHooked(CAutomobile* automobile, void*, CColModel* colModel)
{
    if (automobile && getVariationOriginalModel(automobile->m_nModelIndex) == 531) //Tractor
        return 0;

    return callMethodOriginalAndReturn<char, address>(automobile, colModel);
}


template <unsigned int address>
void __fastcall ProcessSuspensionHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6B029C) == 432 && *((unsigned short*)0x6AFB48) == 432)
        {
            *((unsigned short*)0x6B029C) = veh->m_nModelIndex;
            *((unsigned short*)0x6AFB48) = veh->m_nModelIndex;
            callMethodOriginal<address>(veh);
            *((unsigned short*)0x6B029C) = 432;
            *((unsigned short*)0x6AFB48) = 432;
            return;
        }
        else
        {
            logModified((unsigned int)0x6B029C, printToString("Modified method detected : CAutomobile::ProcessSuspension - 0x6B029C is %d", *((unsigned short*)0x6B029C)));
            logModified((unsigned int)0x6AFB48, printToString("Modified method detected : CAutomobile::ProcessSuspension - 0x6AFB48 is %d", *((unsigned short*)0x6AFB48)));
        }
    }

    callMethodOriginal<address>(veh);
}

template <unsigned int address>
void __fastcall VehicleDamageHooked(CAutomobile* veh, void*, float fDamageIntensity, __int16 tCollisionComponent, int Damager, RwV3d* vecCollisionCoors,  RwV3d* vecCollisionDirection, signed int a7)
{
    if (veh->m_pDamageEntity && getVariationOriginalModel(veh->m_pDamageEntity->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6A80C0) == 432 && *((unsigned short*)0x6A8384) == 432)
        {
            *((unsigned short*)0x6A80C0) = veh->m_pDamageEntity->m_nModelIndex;
            *((unsigned short*)0x6A8384) = veh->m_pDamageEntity->m_nModelIndex;
            callMethodOriginal<address>(veh, fDamageIntensity, tCollisionComponent, Damager, vecCollisionCoors, vecCollisionDirection, a7);
            *((unsigned short*)0x6A80C0) = 432;
            *((unsigned short*)0x6A8384) = 432;
            return;
        }
        else
        {
            logModified((unsigned int)0x6A80C0, printToString("Modified method detected : CAutomobile::VehicleDamage - 0x6A80C0 is %d", *((unsigned short*)0x6A80C0)));
            logModified((unsigned int)0x6A8384, printToString("Modified method detected : CAutomobile::VehicleDamage - 0x6A8384 is %d", *((unsigned short*)0x6A8384)));
        }
    }

    callMethodOriginal<address>(veh, fDamageIntensity, tCollisionComponent, Damager, vecCollisionCoors, vecCollisionDirection, a7);
}

template <unsigned int address>
void __fastcall SetupSuspensionLinesHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6A6606) == 432 && *((unsigned short*)0x6A6999) == 432)
        {
            *((unsigned short*)0x6A6606) = veh->m_nModelIndex;
            *((unsigned short*)0x6A6999) = veh->m_nModelIndex;
            callMethodOriginal<address>(veh);
            *((unsigned short*)0x6A6606) = 432;
            *((unsigned short*)0x6A6999) = 432;
            return;
        }
        else
        {
            logModified((unsigned int)0x6A6606, printToString("Modified method detected : CAutomobile::SetupSuspensionLines - 0x6A6606 is %d", *((unsigned short*)0x6A6606)));
            logModified((unsigned int)0x6A6999, printToString("Modified method detected : CAutomobile::SetupSuspensionLines - 0x6A6999 is %d", *((unsigned short*)0x6A6999)));
        }
    }

    callMethodOriginal<address>(veh);
}

template <unsigned int address>
void __fastcall DoBurstAndSoftGroundRatiosHooked(CAutomobile* a1)
{
    if (getVariationOriginalModel(a1->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6A4917) == 432)
        {
            *((unsigned short*)0x6A4917) = a1->m_nModelIndex;
            callMethodOriginal<address>(a1);
            *((unsigned short*)0x6A4917) = 432;
            return;
        }
        else
            logModified((unsigned int)0x6A4917, printToString("Modified method detected : CAutomobile::DoBurstAndSoftGroundRatios - 0x6A4917 is %d", *((unsigned short*)0x6A4917)));
    }

    callMethodOriginal<address>(a1);
}

template <unsigned int address>
char __fastcall BurstTyreHooked(CAutomobile* veh, void*, char componentId, char a3)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6A32BB) == 432)
        {
            *((unsigned short*)0x6A32BB) = veh->m_nModelIndex;
            char retVal = callMethodOriginalAndReturn<char, address>(veh, componentId, a3);
            *((unsigned short*)0x6A32BB) = 432;
            return retVal;
        }
        else
            logModified((unsigned int)0x6A32BB, printToString("Modified method detected : CAutomobile::BurstTyre - 0x6A32BB is %d", *((unsigned short*)0x6A32BB)));
    }

    return callMethodOriginalAndReturn<char, address>(veh, componentId, a3);
}

template <unsigned int address>
void __fastcall CAutomobileRenderHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6A2C2D) == 432 && *((unsigned short*)0x6A2EAD) == 432)
        {
            *((unsigned short*)0x6A2C2D) = veh->m_nModelIndex;
            *((unsigned short*)0x6A2EAD) = veh->m_nModelIndex;
            callMethodOriginal<address>(veh);
            *((unsigned short*)0x6A2C2D) = 432;
            *((unsigned short*)0x6A2EAD) = 432;
            return;
        }
        else
        {
            logModified((unsigned int)0x6A2C2D, printToString("Modified method detected : CAutomobile::Render - 0x6A2C2D is %d", *((unsigned short*)0x6A2C2D)));
            logModified((unsigned int)0x6A2EAD, printToString("Modified method detected : CAutomobile::Render - 0x6A2EAD is %d", *((unsigned short*)0x6A2EAD)));
        }
    }

    callMethodOriginal<address>(veh);
}

template <unsigned int address>
int __fastcall ProcessEntityCollisionHooked(CAutomobile* _this, void*, CVehicle* collEntity, CColPoint* colPoint)
{
    if (_this && getVariationOriginalModel(_this->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6ACEE9) == 432 && *((unsigned short*)0x6AD242) == 432)
        {
            *((unsigned short*)0x6ACEE9) = _this->m_nModelIndex;
            *((unsigned short*)0x6AD242) = _this->m_nModelIndex;
            int retVal = callMethodOriginalAndReturn<int, address>(_this, collEntity, colPoint);
            *((unsigned short*)0x6ACEE9) = 432;
            *((unsigned short*)0x6AD242) = 432;
            return retVal;
        }
        else
        {
            logModified((unsigned int)0x6ACEE9, printToString("Modified method detected : CAutomobile::ProcessEntityCollision - 0x6ACEE9 is %d", *((unsigned short*)0x6ACEE9)));
            logModified((unsigned int)0x6AD242, printToString("Modified method detected : CAutomobile::ProcessEntityCollision - 0x6AD242 is %d", *((unsigned short*)0x6AD242)));
        }
    }
    
    return callMethodOriginalAndReturn<int, address>(_this, collEntity, colPoint);
}

template <unsigned int address>
void __cdecl RegisterCarBlownUpByPlayerHooked(CVehicle* vehicle, int a2)
{
    if (vehicle != NULL)
    {
        auto model = vehicle->m_nModelIndex;
        vehicle->m_nModelIndex = (unsigned short)getVariationOriginalModel(vehicle->m_nModelIndex);
        callOriginal<address>(vehicle, a2);
        vehicle->m_nModelIndex = model;
    }
}

template <unsigned int address>
void __fastcall TankControlHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6AE9CB) == 432)
        {
            *((unsigned short*)0x6AE9CB) = veh->m_nModelIndex;
            callMethodOriginal<address>(veh);
            *((unsigned short*)0x6AE9CB) = 432;
            return;
        }
        else
            logModified((unsigned int)0x6AE9CB, printToString("Modified method detected : CAutomobile::TankControl - 0x6AE9CB is %d", *((unsigned short*)0x6AE9CB)));
    }

    callMethodOriginal<address>(veh);
}

template <unsigned int address>
void __fastcall DoSoftGroundResistanceHooked(CAutomobile* veh, void*, unsigned int *a3)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
    {
        if (*((unsigned short*)0x6A4BBA) == 432 && *((unsigned short*)0x6A4E0E) == 432)
        {
            *((unsigned short*)0x6A4BBA) = veh->m_nModelIndex;
            *((unsigned short*)0x6A4E0E) = veh->m_nModelIndex;
            callMethodOriginal<address>(veh, a3);
            *((unsigned short*)0x6A4BBA) = 432;
            *((unsigned short*)0x6A4E0E) = 432;
            return;
        }
        else
        {
            logModified((unsigned int)0x6A4BBA, printToString("Modified method detected : CAutomobile::DoSoftGroundResistance - 0x6A4BBA is %d", *((unsigned short*)0x6A4BBA)));
            logModified((unsigned int)0x6A4E0E, printToString("Modified method detected : CAutomobile::DoSoftGroundResistance - 0x6A4E0E is %d", *((unsigned short*)0x6A4E0E)));
        }
    }

    callMethodOriginal<address>(veh, a3);
}


void __declspec(naked) patch525462()
{
    __asm {
        push eax
        movsx eax, word ptr[edi + 0x22]
        push eax
        call getVariationOriginalModel
        cmp eax, 0x1BB
        pop eax
        jmp jmp52546A
    }
}

void __declspec(naked) patch431BEB()
{
    __asm {
        push ebx
        mov ebx, eax
        movsx eax, word ptr [esi+0x22]
        push eax
        call getVariationOriginalModel
        mov bx, ax
        mov eax, ebx
        pop ebx
        add esp, 4        
        jmp jmp431BF2
    }
}

void __declspec(naked) patch64467D()
{
    __asm {
        push eax
        movsx eax, word ptr[eax + 0x22]
        push eax
        call getVariationOriginalModel
        cmp eax, 0x213
        pop eax
        jmp jmp644683
    }
}

void __declspec(naked) patch51E5B8()
{
    __asm {
        push eax
        movsx eax, word ptr[esi + 0x22]
        push eax
        call getVariationOriginalModel
        cmp eax, 0x1B0
        pop eax
        jmp jmp51E5BE
    }
}

void __declspec(naked) patch6B4CE8()
{
    __asm {
        push eax
        movsx eax, word ptr[esi+0x22]
        push eax
        call getVariationOriginalModel
        mov cx, ax
        pop eax
        cmp cx, 0x21B
        jmp jmp6B4CF1
    }
}

void __declspec(naked) patchCoronas()
{
    __asm {
        push 0x0FF
        push ecx
        push edx
        push eax
        push esi
        push edi
        call RegisterCoronaHooked2
        jmp jmp6ABA65
    }
}

void installVehicleHooks()
{
    hookCall(0x43022A, ChooseModelHooked<0x43022A>, "ChooseModel"); //CCarCtrl::GenerateOneRandomCar
   //patch::RedirectJump(0x424E20, ChoosePoliceCarModelHooked);

    hookCall(0x42C320, ChoosePoliceCarModelHooked<0x42C320>, "ChoosePoliceCarModel"); //CCarCtrl::CreatePoliceChase
    hookCall(0x43020E, ChoosePoliceCarModelHooked<0x43020E>, "ChoosePoliceCarModel"); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x430283, ChoosePoliceCarModelHooked<0x430283>, "ChoosePoliceCarModel"); //CCarCtrl::GenerateOneRandomCar

/*****************************************************************************************************/

    hookCall(0x42BC26, AddPoliceCarOccupantsHooked<0x42BC26>, "AddPoliceCarOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x42C620, AddPoliceCarOccupantsHooked<0x42C620>, "AddPoliceCarOccupants"); //CCarCtrl::CreatePoliceChase
    hookCall(0x431EE5, AddPoliceCarOccupantsHooked<0x431EE5>, "AddPoliceCarOccupants"); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x499CBB, AddPoliceCarOccupantsHooked<0x499CBB>, "AddPoliceCarOccupants"); //CSetPiece::Update
    hookCall(0x499D6A, AddPoliceCarOccupantsHooked<0x499D6A>, "AddPoliceCarOccupants"); //CSetPiece::Update
    hookCall(0x49A5EB, AddPoliceCarOccupantsHooked<0x49A5EB>, "AddPoliceCarOccupants"); //CSetPiece::Update
    hookCall(0x49A85E, AddPoliceCarOccupantsHooked<0x49A85E>, "AddPoliceCarOccupants"); //CSetPiece::Update
    hookCall(0x49A9AF, AddPoliceCarOccupantsHooked<0x49A9AF>, "AddPoliceCarOccupants"); //CSetPiece::Update

/*****************************************************************************************************/
    
    hookCall(0x4216E5, CAutomobileHooked<0x4216E5, true>, "CAutomobile"); //CCarCtrl::GetNewVehicleDependingOnCarModel
    hookCall(0x42B909, CAutomobileHooked<0x42B909, true>, "CAutomobile"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x43A326, CAutomobileHooked<0x43A326, true>, "CAutomobile"); //CCheat::VehicleCheat
    hookCall(0x4480D0, CAutomobileHooked<0x4480D0, true>, "CAutomobile"); //CStoredCar::RestoreCar
    hookCall(0x45ABBF, CAutomobileHooked<0x45ABBF, true>, "CAutomobile"); //CRemote::GivePlayerRemoteControlledCar
    hookCall(0x462217, CAutomobileHooked<0x462217>, "CAutomobile");       //CRoadBlocks::CreateRoadBlockBetween2Points
    hookCall(0x484EE7, CAutomobileHooked<0x484EE7, true>, "CAutomobile"); //03C5  CREATE_RANDOM_CAR_FOR_CAR_PARK
    hookCall(0x4998F0, CAutomobileHooked<0x4998F0>, "CAutomobile");       //CSetPiece::TryToGenerateCopCar
    hookCall(0x5D2CE1, CAutomobileHooked<0x5D2CE1, true>, "CAutomobile"); //CPools::LoadVehiclePool
    hookCall(0x61354A, CAutomobileHooked<0x61354A>, "CAutomobile");       //CPopulation::CreateWaitingCoppers
    hookCall(0x6C41BB, CAutomobileHooked<0x6C41BB, true>, "CAutomobile"); //CHeli::CHeli
    hookCall(0x6C8D8B, CAutomobileHooked<0x6C8D8B, true>, "CAutomobile"); //Monster Truck?
    hookCall(0x6C8E4D, CAutomobileHooked<0x6C8E4D, true>, "CAutomobile"); //CPlane::CPlane
    hookCall(0x6CE39D, CAutomobileHooked<0x6CE39D, true>, "CAutomobile"); //CQuadBike::CQuadBike
    hookCall(0x6D03CB, CAutomobileHooked<0x6D03CB, true>, "CAutomobile"); //CTrailer::CTrailer
    hookCall(0x6F3942, CAutomobileHooked<0x6F3942, true>, "CAutomobile"); //CCarGenerator::DoInternalProcessing


    hookCall(0x6F3583, PickRandomCarHooked<0x6F3583>, "PickRandomCar"); //CCarGenerator::DoInternalProcessing
    hookCall(0x6F3EC1, DoInternalProcessingHooked<0x6F3EC1>, "DoInternalProcessing"); //CCarGenerator::Process 

    //Trains
    //patch::RedirectCall(0x4214DC, CTrainHooked); //CCarCtrl::GetNewVehicleDependingOnCarModel
    //patch::RedirectCall(0x5D2B15, CTrainHooked); //CPools::LoadVehiclePool
    hookCall(0x6F7634, CTrainHooked<0x6F7634>, "CTrain"); //CTrain::CreateMissionTrain 

    //Boats
    hookCall(0x42149E, CBoatHooked<0x42149E>, "CBoat"); //CCarCtrl::GetNewVehicleDependingOnCarModel
    hookCall(0x431FD0, CBoatHooked<0x431FD0>, "CBoat"); //CCarCtrl::CreateCarForScript
    hookCall(0x5D2ADC, CBoatHooked<0x5D2ADC>, "CBoat"); //CPools::LoadVehiclePool

    //Helis
    hookCall(0x6CD3C3, CHeliHooked<0x6CD3C3>, "CHeli"); //CPlane::DoPlaneGenerationAndRemoval
    hookCall(0x6C6590, CHeliHooked<0x6C6590>, "CHeli"); //CHeli::GenerateHeli
    hookCall(0x6C6568, CHeliHooked<0x6C6568>, "CHeli"); //CHeli::GenerateHeli
    hookCall(0x5D2C46, CHeliHooked<0x5D2C46>, "CHeli"); //CPools::LoadVehiclePool
    hookCall(0x6C7ACA, GenerateHeliHooked<0x6C7ACA>, "GenerateHeli"); //CHeli::UpdateHelis

    hookCall(0x6CD6D6, CPlaneHooked<0x6CD6D6>, "CPlane"); //CPlane::DoPlaneGenerationAndRemoval
    hookCall(0x42166F, CPlaneHooked<0x42166F>, "CPlane"); //CCarCtrl::GetNewVehicleDependingOnCarModel

    //Roadblocks
    hookCall(0x42CDDD, IsLawEnforcementVehicleHooked<0x42CDDD>, "IsLawEnforcementVehicle"); //CCarCtrl::RemoveDistantCars
    hookCall(0x42CE07, GenerateRoadBlockCopsForCarHooked<0x42CE07>, "GenerateRoadBlockCopsForCar"); //CCarCtrl::RemoveDistantCars
    hookCall(0x4613EB, GetColModelHooked<0x4613EB>, "GetColModel"); //CCarCtrl::RemoveDistantCars

    hookCall(0x42BBFB, AddAmbulanceOccupantsHooked<0x42BBFB>, "AddAmbulanceOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x42BC1A, AddFiretruckOccupantsHooked<0x42BC1A>, "AddFiretruckOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar

    hookCall(0x613A43, FindSpecificDriverModelForCar_ToUseHooked<0x613A43>, "FindSpecificDriverModelForCar_ToUse"); //CPopulation::AddPedInCar
    hookCall(0x6D1B0E, AddPedInCarHooked<0x6D1B0E>, "AddPedInCar"); //CVehicle::SetupPassenger 

    hookCall(0x431DE2, SetUpDriverAndPassengersForVehicleHooked<0x431DE2>, "SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x431DF9, SetUpDriverAndPassengersForVehicleHooked<0x431DF9>, "SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x431ED1, SetUpDriverAndPassengersForVehicleHooked<0x431ED1>, "SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar

    hookCall(0x6B11C2, IsLawEnforcementVehicleHooked<0x6B11C2>, "IsLawEnforcementVehicle"); //CAutomobile::CAutomobile

    hookCall(0x60C4E8, PossiblyRemoveVehicleHooked<0x60C4E8>, "PossiblyRemoveVehicle"); //CPlayerPed::KeepAreaAroundPlayerClear
    hookCall(0x42CD55, PossiblyRemoveVehicleHooked<0x42CD55>, "PossiblyRemoveVehicle"); //CCarCtrl::RemoveDistantCars

    if ((changeScriptedCars = iniVeh.ReadInteger("Settings", "ChangeScriptedCars", 0)) == 1)
        hookCall(0x467B01, CreateCarForScriptHooked<0x467B01>, "CreateCarForScript");

    if ((enableSpecialFeatures = iniVeh.ReadInteger("Settings", "EnableSpecialFeatures", 0)) == 1)
    {
        hookCall(0x871148, ProcessControlHooked<0x871148>, "ProcessControl", true);
        hookCall(0x6CED23, ProcessControlHooked<0x6CED23>, "ProcessControl");
        hookCall(0x871164, PreRenderHooked<0x871164>, "PreRender", true);
        hookCall(0x6CFADC, PreRenderHooked<0x6CFADC>, "PreRender");
        hookCall(0x871210, GetTowBarPosHooked<0x871210>, "GetTowBarPos", true);
        hookCall(0x871214, SetTowLinkHooked<0x871214>, "SetTowLink", true);
        hookCall(0x871D14, GetTowHitchPosHooked<0x871D14>, "GetTowHitchPos", true);

        hookCall(0x6B3271, UpdateTrailerLinkHooked<0x6B3271>, "UpdateTrailerLink");
        hookCall(0x6B329C, UpdateTrailerLinkHooked<0x6B329C>, "UpdateTrailerLink");
        hookCall(0x6B45C7, UpdateTrailerLinkHooked<0x6B45C7>, "UpdateTrailerLink");

        hookCall(0x6B3266, UpdateTractorLinkHooked<0x6B3266>, "UpdateTractorLink");
        hookCall(0x6B3291, UpdateTractorLinkHooked<0x6B3291>, "UpdateTractorLink");
        hookCall(0x6CFFAC, UpdateTractorLinkHooked<0x6CFFAC>, "UpdateTractorLink");

        hookCall(0x8711CC, SetUpWheelColModelHooked<0x8711CC>, "SetUpWheelColModel", true);
        hookCall(0x871B94, SetUpWheelColModelHooked<0x871B94>, "SetUpWheelColModel", true);
        hookCall(0x871CD4, SetUpWheelColModelHooked<0x871CD4>, "SetUpWheelColModel", true);


        if (*(unsigned int*)0x525462 == 0x22478B66 && *(BYTE*)0x525466 == 0x66)
            injector::MakeJMP(0x525462, patch525462);
        else
            logModified((unsigned int)0x525462, printToString("Modified function detected : sub_525252 - 0x525462 is %d", *(unsigned int*)0x525462));

        if (*(unsigned int*)0x431BEB == 0x22468B66 && *(BYTE*)0x431BEF == 0x83)
            injector::MakeJMP(0x431BEB, patch431BEB);
        else
            logModified((unsigned int)0x431BEB, printToString("Modified method detected : CCarCtrl::GenerateOneRandomCar - 0x431BEB is %d", *(unsigned int*)0x431BEB));

        if (*(unsigned int*)0x64467D == 0x22788166 && *(BYTE*)0x644681 == 0x13)
            injector::MakeJMP(0x64467D, patch64467D);
        else
            logModified((unsigned int)0x64467D, printToString("Modified method detected : CTaskSimpleCarDrive::ProcessPed - 0x64467D is %d", *(unsigned int*)0x64467D));

        if (*(unsigned int*)0x51E5B8 == 0x227E8166 && *(BYTE*)0x51E5BC == 0xB0)
            injector::MakeJMP(0x51E5B8, patch51E5B8);
        else
            logModified((unsigned int)0x51E5B8, printToString("Modified method detected : CCamera::TryToStartNewCamMode - 0x51E5B8 is %d", *(unsigned int*)0x51E5B8));

        if (*(unsigned int*)0x6B4CE8 == 0x224E8B66 && *(BYTE*)0x6B4CEC == 0x66)
            injector::MakeJMP(0x6B4CE8, patch6B4CE8);
        else
            logModified((unsigned int)0x6B4CE8, printToString("Modified method detected : CAutomobile::ProcessAI - 0x6B4CE8 is %d", *(unsigned int*)0x6B4CE8));
        
        hookCall(0x871238, ProcessSuspensionHooked<0x871238>, "ProcessSuspension", true);
        hookCall(0x871200, VehicleDamageHooked<0x871200>, "VehicleDamage", true);
        hookCall(0x8711E0, SetupSuspensionLinesHooked<0x8711E0>, "SetupSuspensionLines", true);
        hookCall(0x6B119E, SetupSuspensionLinesHooked<0x6B119E>, "SetupSuspensionLines");
        hookCall(0x8711F0, DoBurstAndSoftGroundRatiosHooked<0x8711F0>, "DoBurstAndSoftGroundRatios", true);
        hookCall(0x8711D0, BurstTyreHooked<0x8711D0>, "BurstTyre", true);
        hookCall(0x871168, CAutomobileRenderHooked<0x871168>, "CAutomobileRender", true);
        hookCall(0x871178, ProcessEntityCollisionHooked<0x871178>, "ProcessEntityCollision", true);


        hookCall(0x6B39E6, RegisterCarBlownUpByPlayerHooked<0x6B39E6>, "RegisterCarBlownUpByPlayer"); //CAutomobile::BlowUpCar
        hookCall(0x6B3DEA, RegisterCarBlownUpByPlayerHooked<0x6B3DEA>, "RegisterCarBlownUpByPlayer"); //CAutomobile::BlowUpCarCutSceneNoExtras
        hookCall(0x6E2D14, RegisterCarBlownUpByPlayerHooked<0x6E2D14>, "RegisterCarBlownUpByPlayer"); //CVehicle::~CVehicle

        hookCall(0x6B2028, TankControlHooked<0x6B2028>, "TankControl"); //CAutomobile::ProcessControl
        hookCall(0x6B51B8, DoSoftGroundResistanceHooked<0x6B51B8>, "DoSoftGroundResistance"); //CAutomobile::ProcessAI

    }

    if ((enableSiren = iniVeh.ReadInteger("Settings", "EnableSiren", 0)) == 1)
        hookCall(0x6D8492, HasCarSiren<0x6D8492>, "HasCarSiren"); //CVehicle::UsesSiren

    if (enableLights == 1 && enableSpecialFeatures == 1 && enableSiren == 1)
    {
        hookCall(0x6ABA60, RegisterCoronaHooked<0x6ABA60>, "RegisterCorona"); //CAutomobile::PreRender
        hookCall(0x6ABB35, RegisterCoronaHooked<0x6ABB35>, "RegisterCorona"); //CAutomobile::PreRender
        hookCall(0x6ABC69, RegisterCoronaHooked<0x6ABC69>, "RegisterCorona"); //CAutomobile::PreRender
        if (*(unsigned int *)0x6ABA56 == 0x0000FF68 && *(BYTE*)0x6ABA5A == 0)
            injector::MakeJMP(0x6ABA56, patchCoronas);
        else
            logModified((unsigned int)0x6ABA56, printToString("Modified method detected : CAutomobile::PreRender - 0x6ABA56 is %d", *(unsigned int*)0x6ABA56));

        hookCall(0x6AB80F, AddLightHooked<0x6AB80F>, "AddLight"); //CAutomobile::PreRender
        hookCall(0x6ABBA6, AddLightHooked<0x6ABBA6>, "AddLight"); //CAutomobile::PreRender
    }

    if ((disablePayAndSpray = iniVeh.ReadInteger("Settings", "DisablePayAndSpray", 0)) == 1)
        hookCall(0x44AC75, IsCarSprayableHooked<0x44AC75>, "IsCarSprayable"); //CGarage::Update

    if (iniVeh.ReadInteger("Settings", "EnableSideMissions", 0))
    {
        enableSideMissions = true;
        hookCall(0x48DA81, IsLawEnforcementVehicleHooked<0x48DA81>, "IsLawEnforcementVehicle");
        hookCall(0x469612, CollectParametersHooked<0x469612>, "CollectParameters");
    }
}
