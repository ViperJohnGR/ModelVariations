#include "Vehicles.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LogUtil.hpp"
#include <plugin.h>

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

enum eRegs16
{
    REG_AX,
    REG_CX,
    REG_DX,
    REG_BX,
    REG_SP,
    REG_BP,
    REG_SI,
    REG_DI,
};

enum eRegs32
{
    REG_EAX,
    REG_ECX,
    REG_EDX,
    REG_EBX,
    REG_ESP,
    REG_EBP,
    REG_ESI,
    REG_EDI,
};

unsigned short roadblockModel = 0;
int sirenModel = -1;
unsigned short lightsModel = 0;
int currentOccupantsGroup = -1;
unsigned short currentOccupantsModel = 0;

int fireCmpModel = -1;

int passengerModelIndex = -1;
constexpr unsigned int jmp4C8AD7 = 0x4C8AD7;
constexpr unsigned int jmp4C8ADF = 0x4C8ADF;
constexpr unsigned int jmp613B7E = 0x613B7E;
constexpr unsigned int jmp6A1564 = 0x6A1564;
constexpr unsigned int jmp6AB35A = 0x6AB35A;
constexpr unsigned int jmp6ABA65 = 0x6ABA65;
constexpr unsigned int jmp6AC735 = 0x6AC735;
constexpr unsigned int jmp6B0CF6 = 0x6B0CF6;
constexpr unsigned int jmp729B7B = 0x729B7B;
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

static int __stdcall getVariationOriginalModel(const int modelIndex)
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

template <unsigned int address, typename... Args>
void changeModel(std::string funcName, unsigned short oldModel, int newModel, std::vector<unsigned short*> addresses, Args... args)
{
    if (newModel < 400 || newModel > 65535)
    {
        callMethodOriginal<address>(args...);
        return;
    }

    for (auto& i : addresses)
        if (*i != oldModel)
        {
            logModified((unsigned int)i, printToString("Modified method detected : %s - 0x%X is %u", funcName.c_str(), i, *i));
            return callMethodOriginal<address>(args...);
        }

    for (auto& i : addresses)
        *i = (unsigned short)newModel;
    callMethodOriginal<address>(args...);
    for (auto& i : addresses)
        *i = oldModel;
}

template <typename T, unsigned int address, typename... Args>
T changeModelAndReturn(std::string funcName, unsigned short oldModel, int newModel, std::vector<unsigned short*> addresses, Args... args)
{
    if (newModel < 400 || newModel > 65535)
    {
        return callMethodOriginalAndReturn<T, address>(args...);
    }

    for (auto& i : addresses)
        if (*i != oldModel)
        {
            logModified((unsigned int)i, printToString("Modified method detected : %s - 0x%X is %u", funcName.c_str(), i, *i));
            return callMethodOriginalAndReturn<T, address>(args...);
        }

    for (auto& i : addresses)
        *i = (unsigned short)newModel;
    T retValue = callMethodOriginalAndReturn<T, address>(args...);
    for (auto& i : addresses)
        *i = oldModel;
    return retValue;
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
    const CVehicle* vehicle = player->m_pVehicle;
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
    const int model = callOriginalAndReturn<int, address>(a1);
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
    const int model = callOriginalAndReturn<int, address>(a1);
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
            const CWanted* wanted = FindPlayerWanted(-1);
            const unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            const unsigned int i = CGeneral::GetRandomNumberInRange(0, (int)vehGroupWantedVariations[a2->m_nModelIndex][wantedLevel].size());
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

    const unsigned short model = a2->m_nModelIndex;
    a2->m_nModelIndex = (unsigned short)getVariationOriginalModel(a2->m_nModelIndex);

    callOriginal<address>(a2, a3);
    //CCarAI::AddPoliceCarOccupants(a2, a3);

    a2->m_nModelIndex = model;
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
}

template <unsigned int address>
CAutomobile* __fastcall CAutomobileHooked(CAutomobile* automobile, void*, int modelIndex, char usageType, char bSetupSuspensionLines)
{
    return callMethodOriginalAndReturn<CAutomobile*, address>(automobile, getRandomVariation(modelIndex), usageType, bSetupSuspensionLines);
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
        if (*(uint16_t*)0x6F3B9A == 531)
        {
            *(uint16_t*)0x6F3B9A = (uint16_t)variation;
            carGenModel = 531;
        }
        else
            logModified((unsigned int)0x6F3B9A, "Modified method detected : CCarGenerator::DoInternalProcessing - 0x6F3B9A is " + std::to_string(*(uint16_t*)0x6F3B9A));
    }
    else if (originalModel == 532) //Combine Harvester
    {
        if (*(uint16_t*)0x6F3BA0 == 532)
        {
            *(uint16_t*)0x6F3BA0 = (uint16_t)variation;
            carGenModel = 532;
        }
        else
            logModified((unsigned int)0x6F3BA0, "Modified method detected : CCarGenerator::DoInternalProcessing - 0x6F3BA0 is " + std::to_string(*(uint16_t*)0x6F3BA0));
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
                *(uint16_t*)0x6F3B9A = 531;
            else if (carGenModel == 532)
                *(uint16_t*)0x6F3BA0 = 532;

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
                if (*(uint16_t*)0x6F3B9A == 531)
                {
                    park->m_nModelId = (short)getRandomVariation(park->m_nModelId, true);
                    *(uint16_t*)0x6F3B9A = (uint16_t)park->m_nModelId;
                    callMethodOriginal<address>(park);
                    *(uint16_t*)0x6F3B9A = 531;
                    //park->m_nModelId = 531;
                    return;
                }
                else
                {
                    callMethodOriginal<address>(park);
                    logModified((unsigned int)0x6F3B9A, "Modified method detected : CCarGenerator::DoInternalProcessing - 0x6F3B9A is " + std::to_string(*(uint16_t*)0x6F3B9A));
                    return;
                }
            }
            else if (model == 532) //Combine Harvester
            {
                if (*(uint16_t*)0x6F3BA0 == 532)
                {
                    park->m_nModelId = (short)getRandomVariation(park->m_nModelId, true);
                    *(uint16_t*)0x6F3BA0 = (uint16_t)park->m_nModelId;
                    callMethodOriginal<address>(park);
                    *(uint16_t*)0x6F3BA0 = 532;
                    return;
                }
                else
                {
                    callMethodOriginal<address>(park);
                    logModified((unsigned int)0x6F3BA0, "Modified method detected : CCarGenerator::DoInternalProcessing - 0x6F3BA0 is " + std::to_string(*(uint16_t*)0x6F3BA0));
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

    if (isModelAmbulance(a1->m_nModelIndex) || isModelFiretruck(a1->m_nModelIndex) || IsLawEnforcementVehicleHooked<0>(a1) || 
        getVariationOriginalModel(a1->m_nModelIndex) == 431 || getVariationOriginalModel(a1->m_nModelIndex) == 437)
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

    if (getVariationOriginalModel(vehicle->m_nModelIndex) == 423 || isModelAmbulance(vehicle->m_nModelIndex) || isModelFiretruck(vehicle->m_nModelIndex) || 
        IsLawEnforcementVehicleHooked<0>(vehicle) || callMethodOriginalAndReturn<char, address>(vehicle))
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

    const int hplayer = CPools::GetPedRef(player);

    if ((int)(CTheScripts::ScriptParams[0].uParam) != hplayer)
        return;

    if (player->m_pVehicle)
    {
        const int originalModel = getVariationOriginalModel(player->m_pVehicle->m_nModelIndex);
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
            const CWanted* wanted = FindPlayerWanted(-1);
            const unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            const unsigned int i = CGeneral::GetRandomNumberInRange(0, (int)vehGroupWantedVariations[car->m_nModelIndex][wantedLevel].size());
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

    const int originalModel = getVariationOriginalModel(car->m_nModelIndex);
    if (originalModel != 407 && originalModel != 416)
    {
        callOriginal<address>(car);
        return;
    }

    if (*((uint16_t*)0x4250AC) == 416)
    {
        *((uint16_t*)0x4250AC) = car->m_nModelIndex;
        callOriginal<address>(car);
        *((uint16_t*)0x4250AC) = 416;
        return;
    }
    else
    {
        logModified(0x4250AC, "Modified method detected: CCarCtrl::PossiblyRemoveVehicle - 0x4250AC is " + std::to_string(*((uint16_t*)0x4250AC)));
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
    switch (getVariationOriginalModel(veh->m_nModelIndex))
    {
    case 406: //Dumper
        return changeModel<address>("CAutomobile::ProcessControl", 406, veh->m_nModelIndex, { (uint16_t*)0x6B1F9D }, veh);
    case 407: //Firetruck
        return changeModel<address>("CAutomobile::ProcessControl", 407, veh->m_nModelIndex, { (uint16_t*)0x6B1F51 }, veh);
    case 423: //Mr. Whoopie
        return changeModel<address>("CAutomobile::ProcessControl", 423, veh->m_nModelIndex, { (uint16_t*)0x6B2BD8 }, veh);
    case 432: //Rhino
        return changeModel<address>("CAutomobile::ProcessControl", 432, veh->m_nModelIndex, { (uint16_t*)0x6B1F7D, (uint16_t*)0x6B36D8 }, veh);
    case 443: //Packer
        return changeModel<address>("CAutomobile::ProcessControl", 443, veh->m_nModelIndex, { (uint16_t*)0x6B1F91 }, veh);
    case 486: //Dozer
        return changeModel<address>("CAutomobile::ProcessControl", 486, veh->m_nModelIndex, { (uint16_t*)0x6B1F97 }, veh);
    case 524: //Cement Truck
        return changeModel<address>("CAutomobile::ProcessControl", 524, veh->m_nModelIndex, { (uint16_t*)0x6B1FA3 }, veh);
    case 525: //Towtruck
        return changeModel<address>("CAutomobile::ProcessControl", 525, veh->m_nModelIndex, { (uint16_t*)0x6B1FB5 }, veh);
    case 530: //Forklift
        return changeModel<address>("CAutomobile::ProcessControl", 530, veh->m_nModelIndex, { (uint16_t*)0x6B1FAF }, veh);
    case 531: //Tractor
        return changeModel<address>("CAutomobile::ProcessControl", 531, veh->m_nModelIndex, { (uint16_t*)0x6B1FBB }, veh);
    case 532: //Combine Harverster
        return changeModel<address>("CAutomobile::ProcessControl", 532, veh->m_nModelIndex, { (uint16_t*)0x6B36C9 }, veh);
    case 601: //Swat Tank
        return changeModel<address>("CAutomobile::ProcessControl", 601, veh->m_nModelIndex, { (uint16_t*)0x6B1F57 }, veh);
    }

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
    const BYTE sirenOriginal[5] = { *(BYTE*)0x6AB350, *(BYTE*)0x6AB351, *(BYTE*)0x6AB352, *(BYTE*)0x6AB353, *(BYTE*)0x6AB354 };

    const auto sirenRestore = [sirenOriginal]()
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

    if (getVariationOriginalModel(veh->m_nModelIndex) == 407) //Firetruck
        changeModel<address>("CAutomobile::PreRender", 407, veh->m_nModelIndex, { (uint16_t*)0x6ACA59 }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        changeModel<address>("CAutomobile::PreRender", 432, veh->m_nModelIndex, { (uint16_t*)0x6ABC83, 
                                                                                  (uint16_t*)0x6ABD11, 
                                                                                  (uint16_t*)0x6ABFCC, 
                                                                                  (uint16_t*)0x6AC029, 
                                                                                  (uint16_t*)0x6ACA4D }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 434) //Hotknife
        changeModel<address>("CAutomobile::PreRender", 434, veh->m_nModelIndex, { (uint16_t*)0x6ACA43 }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 443) //Packer
        changeModel<address>("CAutomobile::PreRender", 443, veh->m_nModelIndex, { (uint16_t*)0x6AC4DB }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 486) //Dozer
        changeModel<address>("CAutomobile::PreRender", 486, veh->m_nModelIndex, { (uint16_t*)0x6AC40E }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 524) //Cement Truck
        changeModel<address>("CAutomobile::PreRender", 524, veh->m_nModelIndex, { (uint16_t*)0x6AC43D }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 525) //Towtruck
        changeModel<address>("CAutomobile::PreRender", 525, veh->m_nModelIndex, { (uint16_t*)0x6AC509 }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 530) //Forklift
        changeModel<address>("CAutomobile::PreRender", 530, veh->m_nModelIndex, { (uint16_t*)0x6AC71E }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 531) //Tractor
        changeModel<address>("CAutomobile::PreRender", 531, veh->m_nModelIndex, { (uint16_t*)0x6AC6DB }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 532) //Combine Harverster
        changeModel<address>("CAutomobile::PreRender", 532, veh->m_nModelIndex, { (uint16_t*)0x6ABCA3, (uint16_t*)0x6AC7AD }, veh);
    else if (getVariationOriginalModel(veh->m_nModelIndex) == 601) //SWAT Tank
        changeModel<address>("CAutomobile::PreRender", 601, veh->m_nModelIndex, { (uint16_t*)0x6ACA53 }, veh); 
    else
        callMethodOriginal<address>(veh);

    if (hasSiren)
        sirenRestore();
}

template <unsigned int address>
char __fastcall GetTowBarPosHooked(CAutomobile* automobile, void*, CVector* outPos, char ignoreModelType, CVehicle* attachTo)
{
    if (getVariationOriginalModel(automobile->m_nModelIndex) == 403) //Linerunner
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 403, automobile->m_nModelIndex, { (uint16_t*)0x6AF27A }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 485) //Baggage
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 485, automobile->m_nModelIndex, { (uint16_t*)0x6AF29C }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 514) //Tanker
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 514, automobile->m_nModelIndex, { (uint16_t*)0x6AF26E }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 515) //Roadtrain
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 515, automobile->m_nModelIndex, { (uint16_t*)0x6AF274 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 525) //Towtruck
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 525, automobile->m_nModelIndex, { (uint16_t*)0x6AF259 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 531) //Tractor
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 531, automobile->m_nModelIndex, { (uint16_t*)0x6AF264, (uint16_t*)0x6AF343 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 552) //Utility Van
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 552, automobile->m_nModelIndex, { (uint16_t*)0x6AF286 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 583) //Tug
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 583, automobile->m_nModelIndex, { (uint16_t*)0x6AF2A2 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 591) //Artic Trailer
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 591, automobile->m_nModelIndex, { (uint16_t*)0x6AF280 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 606) //Bag Box A
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 606, automobile->m_nModelIndex, { (uint16_t*)0x6AF2A8, (uint16_t*)0x6AF2BC }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 607) //Bag Box B
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 607, automobile->m_nModelIndex, { (uint16_t*)0x6AF2AE, (uint16_t*)0x6AF2C2 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 608) //Stairs
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 608, automobile->m_nModelIndex, { (uint16_t*)0x6AF2C8 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 610) //Farm Trailer
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 610, automobile->m_nModelIndex, { (uint16_t*)0x6AF362 }, automobile, outPos, ignoreModelType, attachTo);
    else if (getVariationOriginalModel(automobile->m_nModelIndex) == 611) //Utility Trailer
        return changeModelAndReturn<char, address>("CAutomobile::GetTowBarPos", 611, automobile->m_nModelIndex, { (uint16_t*)0x6AF296 }, automobile, outPos, ignoreModelType, attachTo);

    return callMethodOriginalAndReturn<char, address>(automobile, outPos, ignoreModelType, attachTo);
}

template <unsigned int address>
char __fastcall SetTowLinkHooked(CAutomobile* automobile, void*, CVehicle* vehicle, char a3)
{
    if (vehicle != NULL)
    {
        if (getVariationOriginalModel(vehicle->m_nModelIndex) == 525) //Towtruck
            return changeModelAndReturn<char, address>("CAutomobile::SetTowLink", 525, vehicle->m_nModelIndex, { (uint16_t*)0x6B44B0 }, automobile, vehicle, a3);
        else if (getVariationOriginalModel(vehicle->m_nModelIndex) == 531) //Tractor
            return changeModelAndReturn<char, address>("CAutomobile::SetTowLink", 531, vehicle->m_nModelIndex, { (uint16_t*)0x6B44E6 }, automobile, vehicle, a3);
    }

    return callMethodOriginalAndReturn<char, address>(automobile, vehicle, a3);
}

template <unsigned int address>
char __fastcall GetTowHitchPosHooked(CTrailer* trailer, void*, CVector* point, char a3, CVehicle* a4)
{
    if (a4 != NULL)
        if (getVariationOriginalModel(a4->m_nModelIndex) == 525) //Towtruck
            return changeModelAndReturn<char, address>("CTrailer::GetTowHitchPos", 525, a4->m_nModelIndex, { (uint16_t*)0x6CEED9 }, trailer, point, a3, a4);

    return callMethodOriginalAndReturn<char, address>(trailer, point, a3, a4);
}

template <unsigned int address>
void __fastcall UpdateTrailerLinkHooked(CVehicle* veh, void*, char a2, char a3)
{
    if (veh != NULL && veh->m_pTractor != NULL)
    {
        if (getVariationOriginalModel(veh->m_pTractor->m_nModelIndex) == 525) //Towtruck
            return changeModel<address>("CVehicle::UpdateTrailerLink", 525, veh->m_pTractor->m_nModelIndex, { (uint16_t*)0x6DFDB8 }, veh, a2, a3);
        else if (getVariationOriginalModel(veh->m_pTractor->m_nModelIndex) == 531) //Tractor
            return changeModel<address>("CVehicle::UpdateTrailerLink", 531, veh->m_pTractor->m_nModelIndex, { (uint16_t*)0x6DFDBE }, veh, a2, a3);
    }

    callMethodOriginal<address>(veh, a2, a3);
}

template <unsigned int address>
void __fastcall UpdateTractorLinkHooked(CVehicle* veh, void*, bool a3, bool a4)
{
    if (veh != NULL)
    {
        if (getVariationOriginalModel(veh->m_nModelIndex) == 525) //Towtruck
            return changeModel<address>("CVehicle::UpdateTractorLink", 525, veh->m_nModelIndex, { (uint16_t*)0x6E00D6 }, veh, a3, a4);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 531) //Tractor
            return changeModel<address>("CVehicle::UpdateTractorLink", 531, veh->m_nModelIndex, { (uint16_t*)0x6E00FC }, veh, a3, a4);
    }
    
    callMethodOriginal<address>(veh, a3, a4);
}

template <unsigned int address>
char __fastcall SetUpWheelColModelHooked(CAutomobile* automobile, void*, CColModel* colModel)
{
    if (automobile && (getVariationOriginalModel(automobile->m_nModelIndex) == 531 || getVariationOriginalModel(automobile->m_nModelIndex) == 532))
        return 0;

    return callMethodOriginalAndReturn<char, address>(automobile, colModel);
}

template <unsigned int address>
void __fastcall ProcessSuspensionHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::ProcessSuspension", 432, veh->m_nModelIndex, { (uint16_t*)0x6B029C, (uint16_t*)0x6AFB48 }, veh);

    callMethodOriginal<address>(veh);
}

template <unsigned int address>
void __fastcall VehicleDamageHooked(CAutomobile* veh, void*, float fDamageIntensity, __int16 tCollisionComponent, int Damager, RwV3d* vecCollisionCoors,  RwV3d* vecCollisionDirection, signed int a7)
{
    if (veh->m_pDamageEntity && getVariationOriginalModel(veh->m_pDamageEntity->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::VehicleDamage", 432, veh->m_pDamageEntity->m_nModelIndex, { (uint16_t*)0x6A80C0, (uint16_t*)0x6A8384 }, veh, fDamageIntensity, tCollisionComponent, Damager, vecCollisionCoors, vecCollisionDirection, a7);

    callMethodOriginal<address>(veh, fDamageIntensity, tCollisionComponent, Damager, vecCollisionCoors, vecCollisionDirection, a7);
}

template <unsigned int address>
void __fastcall SetupSuspensionLinesHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::SetupSuspensionLines", 432, veh->m_nModelIndex, { (uint16_t*)0x6A6606, (uint16_t*)0x6A6999 }, veh);

    callMethodOriginal<address>(veh);
}

template <unsigned int address>
void __fastcall DoBurstAndSoftGroundRatiosHooked(CAutomobile* a1)
{
    if (getVariationOriginalModel(a1->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::DoBurstAndSoftGroundRatios", 432, a1->m_nModelIndex, { (uint16_t*)0x6A4917 }, a1);

    callMethodOriginal<address>(a1);
}

template <unsigned int address>
char __fastcall BurstTyreHooked(CAutomobile* veh, void*, char componentId, char a3)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModelAndReturn<char, address>("CAutomobile::BurstTyre", 432, veh->m_nModelIndex, { (uint16_t*)0x6A32BB }, veh, componentId, a3);

    return callMethodOriginalAndReturn<char, address>(veh, componentId, a3);
}

template <unsigned int address>
void __fastcall CAutomobileRenderHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::Render", 432, veh->m_nModelIndex, { (uint16_t*)0x6A2C2D, (uint16_t*)0x6A2EAD }, veh);

    callMethodOriginal<address>(veh);
}

template <unsigned int address>
int __fastcall ProcessEntityCollisionHooked(CAutomobile* _this, void*, CVehicle* collEntity, CColPoint* colPoint)
{
    if (_this && getVariationOriginalModel(_this->m_nModelIndex) == 432) //Rhino
        return changeModelAndReturn<int, address>("CAutomobile::ProcessEntityCollision", 432, _this->m_nModelIndex, { (uint16_t*)0x6ACEE9, (uint16_t*)0x6AD242 }, _this, collEntity, colPoint);
    
    return callMethodOriginalAndReturn<int, address>(_this, collEntity, colPoint);
}

template <unsigned int address>
void __cdecl RegisterCarBlownUpByPlayerHooked(CVehicle* vehicle, int a2)
{
    if (vehicle != NULL)
    {
        const auto model = vehicle->m_nModelIndex;
        vehicle->m_nModelIndex = (unsigned short)getVariationOriginalModel(vehicle->m_nModelIndex);
        callOriginal<address>(vehicle, a2);
        vehicle->m_nModelIndex = model;
    }
}

template <unsigned int address>
void __fastcall TankControlHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::TankControl", 432, veh->m_nModelIndex, { (uint16_t*)0x6AE9CB }, veh);

    callMethodOriginal<address>(veh);
}

template <unsigned int address>
void __fastcall DoSoftGroundResistanceHooked(CAutomobile* veh, void*, unsigned int *a3)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::DoSoftGroundResistance", 432, veh->m_nModelIndex, { (uint16_t*)0x6A4BBA, (uint16_t*)0x6A4E0E }, veh, a3);

    callMethodOriginal<address>(veh, a3);
}

template <unsigned int address>
signed int __cdecl SetupEntityVisibilityHooked(CEntity* a1, float* a2)
{
    if (a1 != NULL && getVariationOriginalModel(a1->m_nModelIndex) == 437)
    {
        if (*(uint16_t*)0x554336 == 437)
        {
            *(uint16_t*)0x554336 = a1->m_nModelIndex;
            const signed int retValue = callOriginalAndReturn<signed int, address>(a1, a2);
            *(uint16_t*)0x554336 = 437;
            return retValue;
        }
        else
        {
            logModified((unsigned int)0x554336, printToString("Modified method detected : CRenderer::SetupEntityVisibility - 0x554336 is %u", *(uint16_t*)0x554336));
            return callOriginalAndReturn<signed int, address>(a1, a2);
        }
    }

    return callOriginalAndReturn<signed int, address>(a1, a2);
}

template <unsigned int address>
int __cdecl GetMaximumNumberOfPassengersFromNumberOfDoorsHooked(__int16 modelIndex)
{
    if (getVariationOriginalModel(modelIndex) == 437)
    {
        if (*(short*)0x4C8AD3 == 437)
        {
            *(short*)0x4C8AD3 = modelIndex;
            const signed int retValue = callOriginalAndReturn<int, address>(modelIndex);
            *(short*)0x4C8AD3 = 437;
            return retValue;
        }
        else
        {
            logModified((unsigned int)0x4C8AD3, printToString("Modified method detected : CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors - 0x4C8AD3 is %u", *(uint16_t*)0x4C8AD3));
            return callOriginalAndReturn<int, address>(modelIndex);
        }
    }
    else if (getVariationOriginalModel(modelIndex) == 431)
    {
        if (*(short*)0x4C8ADB == 431)
        {
            *(short*)0x4C8ADB = modelIndex;
            const signed int retValue = callOriginalAndReturn<int, address>(modelIndex);
            *(short*)0x4C8ADB = 431;
            return retValue;
        }
        else
        {
            logModified((unsigned int)0x4C8ADB, printToString("Modified method detected : CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors - 0x4C8ADB is %u", *(uint16_t*)0x4C8ADB));
            return callOriginalAndReturn<int, address>(modelIndex);
        }
    }

    return callOriginalAndReturn<int, address>(modelIndex);
}

template <unsigned int address>
void __fastcall DoHeadLightReflectionHooked(CVehicle* veh, void*, RwMatrixTag* matrix, char twin, char left, char right)
{
    if (veh != NULL && getVariationOriginalModel(veh->m_nModelIndex) == 532) //Combine Harvester
        return changeModel<address>("CVehicle::DoHeadLightReflection", 532, veh->m_nModelIndex, { (uint16_t*)0x6E176A }, veh, matrix, twin, left, right);

    callMethodOriginal<address>(veh, matrix, twin, left, right);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////  ASM HOOKS  /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void __declspec(naked) patch407293()
{
    __asm {
        pushfd
        push eax
        movsx eax, word ptr[esi + 0x22]
        push ecx
        push edx
        push eax
        call getVariationOriginalModel
        pop edx
        pop ecx
        cmp eax, 259h
        pop eax
        je isSWAT
        mov ebx, 259h
        popfd
        jmp jmp729B7B

isSWAT:
        movsx ebx, word ptr[esi + 0x22]
        popfd
        jmp jmp729B7B
    }
    
}

void __declspec(naked) patch6AC730()
{
    __asm {
        push ecx
        movsx eax, ax
        mov ecx, 4
        mul ecx       
        mov ecx, CModelInfo::ms_modelInfoPtrs
        add ecx, eax
        mov eax, [ecx]
        pop ecx
        jmp jmp6AC735
    }
}

void __declspec(naked) patch6A155C()
{
    __asm {
        push ecx
        push eax
        movsx eax, word ptr [edi+0x22]
        push edx
        push eax
        call getVariationOriginalModel
        pop edx
        mov cx, ax
        pop eax
        mov ax, cx
        pop ecx
        cmp ax, 0x20C
        je isCement
        cmp ax, 0x220
        mov ax, [edi+0x22]
        jmp jmp6A1564

isCement:
        mov ax, [edi+0x22]
        jmp jmp6A1564
    }
}

void __declspec(naked) patch6B0CF0()
{
    __asm {
        push eax
        movsx eax, word ptr [esi+0x22]
        push ecx
        push edx
        push eax
        call getVariationOriginalModel
        pop edx
        pop ecx
        cmp eax, 0x1B0
        pop eax
        jmp jmp6B0CF6
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

uint32_t asmNextinstr[4] = {};
uint16_t asmModel16 = 0;
uint32_t asmModel = 0;
uint32_t asmJmpAddress = 0;
uint32_t *jmpDest = NULL;

template <eRegs32 reg, unsigned int jmpAddress, unsigned int model>
void __declspec(naked) cmpWordPtrRegModel()
{
    __asm {
        pushad
    }

    asmModel = model;
    asmJmpAddress = jmpAddress;

    if constexpr (reg == REG_EAX) { __asm { movsx eax, word ptr[eax + 0x22] } }
    else if constexpr (reg == REG_ECX) { __asm { movsx eax, word ptr[ecx + 0x22] } }
    else if constexpr (reg == REG_EDX) { __asm { movsx eax, word ptr[edx + 0x22] } }
    else if constexpr (reg == REG_EBX) { __asm { movsx eax, word ptr[ebx + 0x22] } }
    else if constexpr (reg == REG_ESP) { __asm { movsx eax, word ptr[esp + 0x22] } }
    else if constexpr (reg == REG_EBP) { __asm { movsx eax, word ptr[ebp + 0x22] } }
    else if constexpr (reg == REG_ESI) { __asm { movsx eax, word ptr[esi + 0x22] } }
    else if constexpr (reg == REG_EDI) { __asm { movsx eax, word ptr[edi + 0x22] } }

    __asm {
        push eax
        call getVariationOriginalModel
        cmp eax, asmModel
        popad
        jmp asmJmpAddress
    }
}

template <eRegs32 reg, unsigned int jmpAddress, unsigned int model>
void __declspec(naked) cmpReg32Model()
{
    __asm {
        pushad
    }

    asmModel = model;
    asmJmpAddress = jmpAddress;

    if constexpr (reg == REG_EAX) { __asm { push eax } }
    else if constexpr (reg == REG_ECX) { __asm { push ecx } }
    else if constexpr (reg == REG_EDX) { __asm { push edx } }
    else if constexpr (reg == REG_EBX) { __asm { push ebx } }
    else if constexpr (reg == REG_ESP) { __asm { push esp } }
    else if constexpr (reg == REG_EBP) { __asm { push ebp } }
    else if constexpr (reg == REG_ESI) { __asm { push esi } }
    else if constexpr (reg == REG_EDI) { __asm { push edi } }

    __asm {
        call getVariationOriginalModel
        cmp eax, asmModel
        popad
        jmp asmJmpAddress
    }
}

template <eRegs16 target, eRegs32 source, unsigned int jmpAddress, uint8_t nextInstrSize, uint32_t nextInstr, uint32_t nextInstr2>
void __declspec(naked) movReg16WordPtrReg()
{
    __asm {
        pushfd
        pushad
    }

    asmNextinstr[0] = nextInstr;
    asmNextinstr[1] = nextInstr2;
    jmpDest = asmNextinstr;
    asmJmpAddress = jmpAddress;
    ((uint8_t*)asmNextinstr)[nextInstrSize] = 0xFF;
    ((uint8_t*)asmNextinstr)[nextInstrSize+1] = 0x25;
    *((uint32_t**)((uint8_t*)asmNextinstr+nextInstrSize+2)) = &asmJmpAddress;

    __asm {
        popad
    }

    if constexpr (source == REG_EAX) { __asm { movsx eax, word ptr[eax + 0x22]} }
    else if constexpr (source == REG_ECX) { __asm { movsx eax, word ptr[ecx + 0x22]} }
    else if constexpr (source == REG_EDX) { __asm { movsx eax, word ptr[edx + 0x22]} }
    else if constexpr (source == REG_EBX) { __asm { movsx eax, word ptr[ebx + 0x22]} }
    else if constexpr (source == REG_ESP) { __asm { movsx eax, word ptr[esp + 0x22]} }
    else if constexpr (source == REG_EBP) { __asm { movsx eax, word ptr[ebp + 0x22]} }
    else if constexpr (source == REG_ESI) { __asm { movsx eax, word ptr[esi + 0x22]} }
    else if constexpr (source == REG_EDI) { __asm { movsx eax, word ptr[edi + 0x22]} }

    __asm {
        pushad
        push eax
        call getVariationOriginalModel
        mov asmModel16, ax
        popad
        popfd
    }

    if constexpr (target == REG_AX) { __asm { mov ax, asmModel16 } }
    else if constexpr (target == REG_CX) { __asm { mov cx, asmModel16 } }
    else if constexpr (target == REG_DX) { __asm { mov dx, asmModel16 } }
    else if constexpr (target == REG_BX) { __asm { mov bx, asmModel16 } }
    else if constexpr (target == REG_SP) { __asm { mov sp, asmModel16 } }
    else if constexpr (target == REG_BP) { __asm { mov bp, asmModel16 } }
    else if constexpr (target == REG_SI) { __asm { mov si, asmModel16 } }
    else if constexpr (target == REG_DI) { __asm { mov di, asmModel16 } }

    __asm {
        jmp jmpDest
    }
}


void hookASM(bool notModified, unsigned int address, int nBytes, injector::memory_pointer_raw hookDest, std::string funcName)
{
    if (notModified)
        injector::MakeJMP(address, hookDest);
    else
    {
        if (funcName.find("::") != std::string::npos)
          logModified(address, printToString("Modified method detected: %s - 0x%X is %s", funcName.c_str(), address, bytesToString(address, nBytes).c_str()));
        else if (strncmp(funcName.c_str(), "sub_", 4) == 0)
            logModified(address, printToString("Modified function detected: %s - 0x%X is %s", funcName.c_str(), address, bytesToString(address, nBytes).c_str()));
        else
            logModified(address, printToString("Modified address detected: %s - 0x%X is %s", funcName.c_str(), address, bytesToString(address, nBytes).c_str()));
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
    
    hookCall(0x42B909, CAutomobileHooked<0x42B909>, "CAutomobile"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x462217, CAutomobileHooked<0x462217>, "CAutomobile"); //CRoadBlocks::CreateRoadBlockBetween2Points
    hookCall(0x4998F0, CAutomobileHooked<0x4998F0>, "CAutomobile"); //CSetPiece::TryToGenerateCopCar
    hookCall(0x61354A, CAutomobileHooked<0x61354A>, "CAutomobile"); //CPopulation::CreateWaitingCoppers

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
        hookCall(0x6C7059, ProcessControlHooked<0x6C7059>, "ProcessControl"); //CHeli::ProcessControl
        hookCall(0x6C82B4, ProcessControlHooked<0x6C82B4>, "ProcessControl"); //CMonsterTruck::ProcessControl
        hookCall(0x6C9313, ProcessControlHooked<0x6C9313>, "ProcessControl"); //CPlane::ProcessControl
        hookCall(0x6CE005, ProcessControlHooked<0x6CE005>, "ProcessControl"); //CQuadBike::ProcessControl
        hookCall(0x6CED23, ProcessControlHooked<0x6CED23>, "ProcessControl"); //CTrailer::ProcessControl
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
     
        hookASM(*(uint32_t*)0x525462 == 0x22478B66 && *(uint32_t*)0x525466 == 0x01BB3D66, 0x525462, 5, movReg16WordPtrReg<REG_AX, REG_EDI, 0x52546A, 4, 0x01BB3D66, 0x90909090>, "sub_525252");
        hookASM(*(uint32_t*)0x431BEB == 0x22468B66 && *(uint32_t*)0x431BEF == 0x6604C483, 0x431BEB, 5, movReg16WordPtrReg<REG_AX, REG_ESI, 0x431BF2, 3, 0x9004C483, 0x90909090>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(*(uint32_t*)0x64467D == 0x22788166 && *(uint16_t*)0x644681 == 0x0213, 0x64467D, 6, cmpWordPtrRegModel<REG_EAX, 0x644683, 0x213>, "CTaskSimpleCarDrive::ProcessPed");
        hookASM(*(uint32_t*)0x51E5B8 == 0x227E8166 && *(uint16_t*)0x51E5BC == 0x01B0, 0x51E5B8, 6, cmpWordPtrRegModel<REG_ESI, 0x51E5BE, 0x1B0>, "CCamera::TryToStartNewCamMode");
        hookASM(*(uint32_t*)0x6B4CE8 == 0x224E8B66 && *(uint32_t*)0x6B4CEC == 0x1BF98166, 0x6B4CE8, 5, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6B4CF1, 5, 0x1BF98166, 0x90909002>, "CAutomobile::ProcessAI");

        if (exeVersion != SA_EXE_COMPACT)
            hookASM(*(uint32_t*)0x407293 == 0x000259BB && *(BYTE*)0x407297 == 0x00, 0x407293, 5, patch407293, "CAutomobile::FireTruckControl");
        else
            hookASM(*(uint32_t*)0x729B76 == 0x000259BB && *(BYTE*)0x729B7A == 0x00, 0x729B76, 5, patch407293, "CAutomobile::FireTruckControl");

        hookASM(*(uint32_t*)0x5A0EAF == 0x22788166 && *(uint16_t*)0x5A0EB3 == 0x0259, 0x5A0EAF, 6, cmpWordPtrRegModel<REG_EAX, 0x5A0EB5, 0x259>, "CObject::ObjectDamage");
        hookASM(*(uint32_t*)0x4308A1 == 0x227E8166 && *(uint16_t*)0x4308A5 == 0x01A7, 0x4308A1, 6, cmpWordPtrRegModel<REG_ESI, 0x4308A7, 0x1A7>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(*(uint32_t*)0x4F62E4 == 0x22788166 && *(uint16_t*)0x4F62E8 == 0x01A7, 0x4F62E4, 6, cmpWordPtrRegModel<REG_EAX, 0x4F62EA, 0x1A7>, "CAEVehicleAudioEntity::GetSirenState");
        hookASM(*(uint32_t*)0x4F9CBC == 0x22798166 && *(uint16_t*)0x4F9CC0 == 0x01A7, 0x4F9CBC, 6, cmpWordPtrRegModel<REG_ECX, 0x4F9CC2, 0x1A7>, "CAEVehicleAudioEntity::PlayHornOrSiren");
        hookASM(*(uint32_t*)0x44AB2A == 0x227F8166 && *(uint16_t*)0x44AB2E == 0x01A7, 0x44AB2A, 6, cmpWordPtrRegModel<REG_EDI, 0x44AB30, 0x1A7>, "CGarage::Update");
        hookASM(*(uint32_t*)0x52AE34 == 0x22788166 && *(uint16_t*)0x52AE38 == 0x01A7, 0x52AE34, 6, cmpWordPtrRegModel<REG_EAX, 0x52AE3A, 0x1A7>, "0x52AE34");
        hookASM(*(uint32_t*)0x4FB26B == 0x22418B66 && *(uint32_t*)0x4FB26F == 0x01BB3D66, 0x4FB26B, 8, movReg16WordPtrReg<REG_AX, REG_ECX, 0x4FB273, 4, 0x01BB3D66, 0x90909090>, "CAEVehicleAudioEntity::ProcessMovingParts");
        hookASM(*(uint32_t*)0x54742F == 0x224F8B66 && *(uint32_t*)0x547433 == 0x96F98166, 0x54742F, 8, movReg16WordPtrReg<REG_CX, REG_EDI, 0x547438, 5, 0x96F98166, 0x90909001>, "CPhysical::PositionAttachedEntity");
        hookASM(*(uint32_t*)0x5A0052 == 0x22468B66 && *(uint32_t*)0x5A0056 == 0x01963D66, 0x5A0052, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x5A005A, 4, 0x01963D66, 0x90909090>, "CObject::SpecialEntityPreCollisionStuff");
        hookASM(*(uint32_t*)0x5A21C9 == 0x22488B66 && *(uint32_t*)0x5A21CD == 0x96F98166, 0x5A21C9, 8, movReg16WordPtrReg<REG_CX, REG_EAX, 0x5A21D2, 5, 0x96F98166, 0x90909001>, "CObject::ProcessControl");
        hookASM(*(uint32_t*)0x6A1480 == 0x224F8B66 && *(uint32_t*)0x6A1484 == 0x8166F633, 0x6A1480, 8, movReg16WordPtrReg<REG_CX, REG_EDI, 0x6A1486, 2, 0x9090F633, 0x90909090>, "CAutomobile::UpdateMovingCollision");
        hookASM(*(uint32_t*)0x6A173B == 0x22478B66 && *(uint32_t*)0x6A173F == 0x01E63D66, 0x6A173B, 8, movReg16WordPtrReg<REG_AX, REG_EDI, 0x6A1743, 4, 0x01E63D66, 0x90909090>, "CAutomobile::UpdateMovingCollision");
        hookASM(*(uint32_t*)0x6A1F69 == 0x224E8B66 && *(uint32_t*)0x6A1F6D == 0x96F98166, 0x6A1F69, 8, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6A1F72, 5, 0x96F98166, 0x90909001>, "CAutomobile::AddMovingCollisionSpeed");
        hookASM(*(uint32_t*)0x6A2162 == 0x22418B66 && *(uint32_t*)0x6A2166 == 0x01963D66, 0x6A2162, 8, movReg16WordPtrReg<REG_AX, REG_ECX, 0x6A216A, 4, 0x01963D66, 0x90909090>, "CAutomobile::GetMovingCollisionOffset");
        hookASM(*(uint32_t*)0x6C7F30 == 0x227E8166 && *(uint16_t*)0x6C7F34 == 0x0196, 0x6C7F30, 6, cmpWordPtrRegModel<REG_ESI, 0x6C7F36, 0x196>, "CMonsterTruck::PreRender");
        hookASM(*(uint32_t*)0x5470BF == 0x22798166 && *(uint16_t*)0x5470C3 == 0x0212, 0x5470BF, 6, cmpWordPtrRegModel<REG_ECX, 0x5470C5, 0x212>, "CPhysical::PositionAttachedEntity");
        hookASM(*(uint32_t*)0x54D70D == 0x227F8166 && *(uint16_t*)0x54D711 == 0x0212, 0x54D70D, 6, cmpWordPtrRegModel<REG_EDI, 0x54D713, 0x212>, "CPhysical::AttachEntityToEntity");
        hookASM(*(uint32_t*)0x5A0EBF == 0x227F8166 && *(uint16_t*)0x5A0EC3 == 0x0212, 0x5A0EBF, 6, cmpWordPtrRegModel<REG_EDI, 0x5A0EC5, 0x212>, "CObject::ObjectDamage");
        hookASM(*(uint32_t*)0x6A1648 == 0x227F8166 && *(uint16_t*)0x6A164C == 0x0212, 0x6A1648, 6, cmpWordPtrRegModel<REG_EDI, 0x6A164E, 0x212>, "CAutomobile::UpdateMovingCollision");
        hookASM(*(uint32_t*)0x6AD378 == 0x227E8166 && *(uint16_t*)0x6AD37C == 0x0212, 0x6AD378, 6, cmpWordPtrRegModel<REG_ESI, 0x6AD37E, 0x212>, "CAutomobile::ProcessEntityCollision");
        hookASM(*(uint32_t*)0x6E0FF8 == 0x227F8166 && *(uint16_t*)0x6E0FFC == 0x0212, 0x6E0FF8, 6, cmpWordPtrRegModel<REG_EDI, 0x6E0FFE, 0x212>, "CVehicle::DoHeadLightBeam");
        hookASM(*(uint32_t*)0x6AC730 == 0xA9B910A1 && *(BYTE*)0x6AC734 == 0x00, 0x6AC730, 5, patch6AC730, "CAutomobile::PreRender");
        hookASM(*(uint32_t*)0x43064C == 0x01AFFF81 && *(uint16_t*)0x430650 == 0x0000, 0x43064C, 6, cmpReg32Model<REG_EDI, 0x430652, 0x1AF>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(*(uint32_t*)0x64BCB3 == 0x22788166 && *(uint16_t*)0x64BCB7 == 0x01AF, 0x64BCB3, 6, cmpWordPtrRegModel<REG_EAX, 0x64BCB9, 0x1AF>, "CTaskSimpleCarSetPedInAsDriver::ProcessPed");
        hookASM(*(uint32_t*)0x430640 == 0x01B5FF81 && *(uint16_t*)0x430644 == 0x0000, 0x430640, 6, cmpReg32Model<REG_EDI, 0x430646, 0x1B5>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(*(uint32_t*)0x6A155C == 0x22478B66 && *(BYTE*)0x6A1560 == 0x66, 0x6A155C, 5, patch6A155C, "CAutomobile::UpdateMovingCollision");
        hookASM(*(uint32_t*)0x502222 == 0x22788166 && *(uint16_t*)0x502226 == 0x0214, 0x502222, 6, cmpWordPtrRegModel<REG_EAX, 0x502228, 0x214>, "CAEVehicleAudioEntity::ProcessVehicle");
        hookASM(*(uint32_t*)0x6AA515 == 0x224E8B66 && *(uint32_t*)0x6AA519 == 0x14F98166, 0x6AA515, 8, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6AA51E, 5, 0x14F98166, 0x90909002>, "CAutomobile::UpdateWheelMatrix");
        hookASM(*(uint32_t*)0x6D1ABA == 0x22478B66 && *(uint16_t*)0x6D1ABE == 0xD232, 0x6D1ABA, 6, movReg16WordPtrReg<REG_AX, REG_EDI, 0x6D1AC0, 2, 0x9090D232, 0x90909090 >, "CVehicle::SetupPassenger");
        hookASM(*(uint32_t*)0x6C8FFA == 0x0201FF81 && *(uint16_t*)0x6C8FFE == 0x0000, 0x6C8FFA, 6, cmpReg32Model<REG_EDI, 0x6C9000, 0x201>, "CPlane::CPlane");
        hookASM(*(uint32_t*)0x6C926D == 0x22468B66 && *(uint32_t*)0x6C9271 == 0x02003D66, 0x6C926D, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C9275, 4, 0x02003D66, 0x90909090>, "CPlane::ProcessControl");
        hookASM(*(uint32_t*)0x6CA945 == 0x22468B66 && *(uint32_t*)0x6CA949 == 0x02003D66, 0x6CA945, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6CA94D, 4, 0x02003D66, 0x90909090>, "CPlane::PreRender");
        hookASM(*(uint32_t*)0x6CACF0 == 0x227E8166 && *(uint16_t*)0x6CACF4 == 0x0201, 0x6CACF0, 6, cmpWordPtrRegModel<REG_ESI, 0x6CACF6, 0x201>, "CPlane::OpenDoor");
        hookASM(*(uint32_t*)0x6C8F3D == 0x0200FF81 && *(uint16_t*)0x6C8F41 == 0x0000, 0x6C8F3D, 6, cmpReg32Model<REG_EDI, 0x6C8F43, 0x200>, "CPlane::CPlane");
        hookASM(*(uint32_t*)0x6D67B7 == 0x22468B66 && *(uint32_t*)0x6D67BB == 0x01963D66, 0x6D67B7, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6D67BF, 4, 0x01963D66, 0x90909090>, "CVehicle::SpecialEntityPreCollisionStuff");
        hookASM(*(uint32_t*)0x6B0F47 == 0x22468B66 && *(uint32_t*)0x6B0F4B == 0x8B3805D9, 0x6B0F47, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B0F51, 6, 0x8B3805D9, 0x90900085>, "CAutomobile::CAutomobile");
        hookASM(*(uint32_t*)0x6B0CF0 == 0x227E8166 && *(uint16_t*)0x6B0CF4 == 0x01B0, 0x6B0CF0, 6, patch6B0CF0, "CAutomobile::CAutomobile");
        hookASM(*(uint32_t*)0x6B0EE2 == 0x22468B66 && *(uint32_t*)0x6B0EE6 == 0x020D3D66, 0x6B0EE2, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B0EEA, 4, 0x020D3D66, 0x90909090>, "CAutomobile::CAutomobile");

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

        hookCall(0x554937, SetupEntityVisibilityHooked<0x554937>, "SetupEntityVisibility"); //CRenderer::ScanSectorList

        hookCall(0x6D6A76, GetMaximumNumberOfPassengersFromNumberOfDoorsHooked<0x6D6A76>, "GetMaximumNumberOfPassengersFromNumberOfDoors"); //CVehicle::SetModelIndex

        hookCall(0x6E2730, DoHeadLightReflectionHooked<0x6E2730>, "DoHeadLightReflection"); //CVehicle::DoVehicleLights
    }

    if ((enableSiren = iniVeh.ReadInteger("Settings", "EnableSiren", 0)) == 1)
        hookCall(0x6D8492, HasCarSiren<0x6D8492>, "HasCarSiren"); //CVehicle::UsesSiren

    if (enableLights == 1 && enableSpecialFeatures == 1 && enableSiren == 1)
    {
        hookCall(0x6ABA60, RegisterCoronaHooked<0x6ABA60>, "RegisterCorona"); //CAutomobile::PreRender
        hookCall(0x6ABB35, RegisterCoronaHooked<0x6ABB35>, "RegisterCorona"); //CAutomobile::PreRender
        hookCall(0x6ABC69, RegisterCoronaHooked<0x6ABC69>, "RegisterCorona"); //CAutomobile::PreRender
        if (*(uint32_t *)0x6ABA56 == 0x0000FF68 && *(BYTE*)0x6ABA5A == 0)
            injector::MakeJMP(0x6ABA56, patchCoronas);
        else
            logModified((uint32_t)0x6ABA56, printToString("Modified method detected : CAutomobile::PreRender - 0x6ABA56 is %s", bytesToString(0x6ABA56, 5).c_str()));

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
