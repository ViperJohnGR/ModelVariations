#include "Vehicles.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"

#include <plugin.h>
#include <CCarGenerator.h>
#include <CHeli.h>
#include <CModelInfo.h>
#include <CPlane.h>
#include <CTheScripts.h>
#include <CTheZones.h>
#include <CTrailer.h>
#include <CVector.h>

#include <array>
#include <set>
#include <stack>

#include <psapi.h>

using namespace plugin;

struct rgba
{
    BYTE r;
    BYTE g;
    BYTE b;
    BYTE a;
};

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

static const char* dataFileName = "ModelVariations_Vehicles.ini";
static DataReader dataFile(dataFileName);

unsigned short roadblockModel = 0;
int sirenModel = -1;
unsigned short lightsModel = 0;
int currentOccupantsGroup = -1;
unsigned short currentOccupantsModel = 0;
bool tuneParkedCar = false;

int passengerModelIndex = -1;
constexpr std::uintptr_t jmp588577 = 0x588577;
constexpr std::uintptr_t jmp613B7E = 0x613B7E;
constexpr std::uintptr_t jmp6A1564 = 0x6A1564;
constexpr std::uintptr_t jmp6AB35A = 0x6AB35A;
constexpr std::uintptr_t jmp6ABA65 = 0x6ABA65;
int carGenModel = -1;

uint32_t asmNextInstr[4] = {};
uint16_t asmModel16 = 0;
uint32_t asmModel32 = 0;
std::uintptr_t asmJmpAddress = 0;
uint32_t* jmpDest = NULL;

std::array<std::vector<unsigned short>, 16> vehVariations[212];
std::array<std::vector<unsigned short>, 6> vehWantedVariations[212];

std::unordered_map<unsigned short, std::array<std::vector<unsigned short>, 16>> vehGroups;
std::unordered_map<unsigned short, std::array<std::vector<unsigned short>, 16>> vehTuning;
std::unordered_map<unsigned short, std::array<std::vector<unsigned short>, 6>> vehGroupWantedVariations;
std::unordered_map<unsigned short, unsigned short> vehOriginalModels;
std::unordered_map<unsigned short, std::vector<unsigned short>> vehDrivers;
std::unordered_map<unsigned short, std::vector<unsigned short>> vehPassengers;
std::unordered_map<unsigned short, std::vector<unsigned short>> vehDriverGroups[9];
std::unordered_map<unsigned short, std::vector<unsigned short>> vehPassengerGroups[9];
std::unordered_map<unsigned short, BYTE> modelNumGroups;
std::unordered_map<unsigned short, std::pair<CVector, float>> LightPositions;
std::unordered_map<unsigned short, rgba> LightColors;
std::unordered_map<unsigned short, rgba> LightColors2;
std::map<unsigned short, std::vector<unsigned short>> vehCurrentTuning;
std::unordered_map<unsigned short, std::string> vehModels;
std::unordered_map<unsigned short, BYTE> tuningRarities;

std::vector<unsigned short> vehCurrentVariations[212];

std::set<unsigned short> parkedCars;
std::set<unsigned short> vehHasVariations;
std::set<unsigned short> vehMergeZones;
std::set<unsigned short> vehUseOnlyGroups;

std::stack<std::pair<CVehicle*, std::array<std::vector<unsigned short>, 18>>> tuningStack;
std::stack<CVehicle*> vehStack;

//INI Options
bool changeCarGenerators = false;
bool changeScriptedCars = false;
bool disablePayAndSpray = false;
bool enableLights = false;
bool enableSideMissions = false;
bool enableAllSideMissions = false;
bool enableSiren = false;
bool enableSpecialFeatures = false;
bool loadAllVehicles = false;
std::vector<unsigned short> vehCarGenExclude;
std::vector<unsigned short> vehInheritExclude;


int getTuningPartSlot(int model)
{
    const auto mInfo = CModelInfo::GetModelInfo(model);

    if (mInfo != NULL)
    {
        if ((mInfo->m_nFlags & 0x100) != 0)
            switch ((mInfo->m_nFlags >> 10) & 0x1F)
            {
                case 1:  return 11;
                case 2:  return 12;
                case 12: return 14;
                case 13: return 15;
                case 19: return 13;
                case 20:
                case 21:
                case 22: return 16;
            }
        else
            switch ((mInfo->m_nFlags >> 10) & 0x1F)
            {
                case 0:  return 0;
                case 1:
                case 2:  return 1;
                case 6:  return 2;
                case 8:
                case 9:  return 3;
                case 10: return 4;
                case 11: return 5;
                case 12: return 6;
                case 14: return 7;
                case 15: return 8;
                case 16: return 9;
                case 17: return 10;
            }
    }

    return -1;
}

void processTuning(CVehicle* veh)
{
    /* TUNING PART SLOTS
       0 - hood vents
       1 - hood scoops
       2 - spoilers
       3 - side skirts
       4 - front bullbars
       5 - rear bullbars
       6 - lights
       7 - roof
       8 - nitrous
       9 - hydralics
       10 - stereo
       11
       12 - wheels
       13 - exhaust
       14 - front bumper
       15 - rear bumper
       16 - misc
    */

    if (veh->m_nCreatedBy != eVehicleCreatedBy::MISSION_VEHICLE)
    {
        auto it = vehCurrentTuning.find(veh->m_nModelIndex);
        if (it != vehCurrentTuning.end() && !it->second.empty())
        {
            std::array<std::vector<unsigned short>, 18> partsToInstall;
            for (auto& part : it->second)
                if (part < static_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(veh->m_nModelIndex))->GetNumRemaps())
                    partsToInstall[17].push_back(part);
                else
                {
                    unsigned modSlot = (unsigned)getTuningPartSlot(part);
                    if (modSlot < 17)
                        partsToInstall[modSlot].push_back(part);
                }


            auto tuningRarity = tuningRarities.find(veh->m_nModelIndex);

            std::array<bool, 18> slotsToInstall = {};
            for (unsigned int i = 0; i < 18; i++)
                if (tuningRarity != tuningRarities.end())
                    slotsToInstall[i] = (tuningRarity->second == 0) ? false : ((rand<uint32_t>(0, tuningRarity->second) == 0) ? true : false);
                else
                    slotsToInstall[i] = (rand<uint32_t>(0, 3) == 0 ? true : false);

            std::string section;
            auto it2 = vehModels.find(veh->m_nModelIndex);
            if (it2 != vehModels.end())
                section = it2->second;
            else
                section = std::to_string(veh->m_nModelIndex);

            if (dataFile.ReadBoolean(section, "TuningFullBodykit", false))
                if (slotsToInstall[14] == true || slotsToInstall[15] == true || slotsToInstall[3] == true)
                    slotsToInstall[14] = slotsToInstall[15] = slotsToInstall[3] = true;

            for (unsigned int i = 0; i < 18; i++)
                if (slotsToInstall[i] == false)
                    partsToInstall[i].clear();


            //if (logfile.is_open())
                //logfile << "Installing part " << it->second[0] << " modSlot " << modSlot << std::endl;
            tuningStack.push({ veh, partsToInstall });
        }
    }
}

void checkNumGroups(std::vector<unsigned short>& vec, uint8_t numGroups)
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

void processOccupantGroups(const CVehicle* veh)
{
    if (vehUseOnlyGroups.find(veh->m_nModelIndex) != vehUseOnlyGroups.end() || rand<bool>())
    {
        auto it = modelNumGroups.find(veh->m_nModelIndex);
        if (it != modelNumGroups.end())
        {
            const CWanted* wanted = FindPlayerWanted(-1);
            const unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            currentOccupantsModel = veh->m_nModelIndex;

            std::string section;
            auto it2 = vehModels.find(veh->m_nModelIndex);
            if (it2 != vehModels.end())
                section = it2->second;
            else
                section = std::to_string(veh->m_nModelIndex);

            std::vector<unsigned short> zoneGroups = dataFile.ReadLine(section, currentZone, READ_GROUPS);
            checkNumGroups(zoneGroups, it->second);
            if (vehGroups.find(veh->m_nModelIndex) != vehGroups.end())
            {
                if (vehMergeZones.find(veh->m_nModelIndex) != vehMergeZones.end())
                    zoneGroups = vectorUnion(zoneGroups, vehGroups[veh->m_nModelIndex][currentTown]);
                else if (zoneGroups.empty())
                    zoneGroups = vehGroups[veh->m_nModelIndex][currentTown];
            }

            if (zoneGroups.empty() && !vehGroupWantedVariations[veh->m_nModelIndex][wantedLevel].empty())
                currentOccupantsGroup = vectorGetRandom(vehGroupWantedVariations[veh->m_nModelIndex][wantedLevel]) - 1;
            else
            {
                if (!vehGroupWantedVariations[veh->m_nModelIndex][wantedLevel].empty())
                    vectorfilterVector(zoneGroups, vehGroupWantedVariations[veh->m_nModelIndex][wantedLevel]);
                if (!zoneGroups.empty())
                    currentOccupantsGroup = vectorGetRandom(zoneGroups) - 1;
                else
                    currentOccupantsGroup = rand<int32_t>(0, it->second);
            }
        }
    }
}

bool hasModelSideMission(int model)
{
    switch (model)
    {
        case 407: //Fire Truck
        case 416: //Ambulance
        case 425: //Hunter
        case 537: //Freight
        case 538: //Brown Streak
        case 570: //Streak Car
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

    const unsigned short variationModel = vectorGetRandom(vehCurrentVariations[modelid - 400]);
    if (variationModel > 0)
    {
        loadModels({ variationModel }, PRIORITY_REQUEST, true);
        return variationModel;
    }
    return modelid;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void VehicleVariations::AddToStack(CVehicle* veh)
{
    vehStack.push(veh);
}

void VehicleVariations::ClearData()
{
    for (int i = 0; i < 212; i++)
        for (unsigned short j = 0; j < 16; j++)
        {
            vehVariations[i][j].clear();

            if (j < 6)
                vehWantedVariations[i][j].clear();
        }

    //maps
    vehGroups.clear();
    vehOriginalModels.clear();
    vehDrivers.clear();
    vehPassengers.clear();

    for (int i = 0; i < 9; i++)
    {
        vehDriverGroups[i].clear();
        vehPassengerGroups[i].clear();
    }

    modelNumGroups.clear();
    LightPositions.clear();
    LightColors.clear();
    LightColors2.clear();
    vehGroupWantedVariations.clear();
    vehTuning.clear();
    vehCurrentTuning.clear();
    vehModels.clear();
    tuningRarities.clear();

    //sets
    parkedCars.clear();
    vehUseOnlyGroups.clear();
    vehMergeZones.clear();
    vehHasVariations.clear();

    //stacks
    while (!tuningStack.empty()) tuningStack.pop();
    while (!vehStack.empty()) vehStack.pop();

    //vectors
    for (int i = 0; i < 212; i++)
        vehCurrentVariations[i].clear();

    vehCarGenExclude.clear();
    vehInheritExclude.clear();

    dataFile.data.clear();
}

void VehicleVariations::LoadData()
{
    dataFile.SetIniPath(dataFileName);

    changeCarGenerators   = dataFile.ReadBoolean("Settings", "ChangeCarGenerators", false);
    changeScriptedCars    = dataFile.ReadBoolean("Settings", "ChangeScriptedCars", false);
    disablePayAndSpray    = dataFile.ReadBoolean("Settings", "DisablePayAndSpray", false);
    enableLights          = dataFile.ReadBoolean("Settings", "EnableLights", false);
    enableSideMissions    = dataFile.ReadBoolean("Settings", "EnableSideMissions", false);
    enableAllSideMissions = dataFile.ReadBoolean("Settings", "EnableSideMissionsForAllScripts", false);
    enableSiren           = dataFile.ReadBoolean("Settings", "EnableSiren", false);
    enableSpecialFeatures = dataFile.ReadBoolean("Settings", "EnableSpecialFeatures", false);
    loadAllVehicles       = dataFile.ReadBoolean("Settings", "LoadAllVehicles", false);
    vehCarGenExclude      = dataFile.ReadLine("Settings", "ExcludeCarGeneratorModels", READ_VEHICLES);
    vehInheritExclude     = dataFile.ReadLine("Settings", "ExcludeModelsFromInheritance", READ_VEHICLES);

    for (int i = 0; i < CTheZones::TotalNumberOfInfoZones; i++) //for every zone name
        for (auto& j : dataFile.data)
        {
            std::string section = j.first;

            int modelid = 0;
            if (j.first[0] >= '0' && j.first[0] <= '9')
                modelid = std::stoi(j.first);
            else
                CModelInfo::GetModelInfo(section.data(), &modelid);

            if (modelid > 0)
            {
                char zoneLabel[9] = {};
                memcpy(&zoneLabel[0], *(char**)(0x572BB6 + 1) + i * 0x20, 8);
                for (auto& k : dataFile.ReadLine(j.first, zoneLabel, READ_VEHICLES)) //for every variation 'k' of veh id 'j' in zone 'i'
                    if (modelid != k && !(vectorHasId(vehInheritExclude, k)))
                        vehOriginalModels.insert({ k, (unsigned short)modelid });
            }
        }

    Log::Write("\nReading vehicle data...\n");

    for (auto& inidata : dataFile.data)
    {
        Log::Write("%s\n", inidata.first.c_str());

        std::string section = inidata.first;
        int modelid = 0;
        if (section[0] >= '0' && section[0] <= '9')
            modelid = std::stoi(section);
        else
        {
            CModelInfo::GetModelInfo(section.data(), &modelid);
            if (modelid > 400)
                vehModels.insert({ (unsigned short)modelid, section });
        }

        unsigned short i = (unsigned short)modelid;
        if (i >= 400)
        {
            if (i < 612)
            {
                vehHasVariations.insert(i - 400U);

                if (dataFile.ReadBoolean(section, "ChangeOnlyParked", false))
                    parkedCars.insert(i);

                vehVariations[i - 400][0] = dataFile.ReadLine(section, "Countryside", READ_VEHICLES);
                vehVariations[i - 400][1] = dataFile.ReadLine(section, "LosSantos", READ_VEHICLES);
                vehVariations[i - 400][2] = dataFile.ReadLine(section, "SanFierro", READ_VEHICLES);
                vehVariations[i - 400][3] = dataFile.ReadLine(section, "LasVenturas", READ_VEHICLES);
                vehVariations[i - 400][4] = dataFile.ReadLine(section, "Global", READ_VEHICLES);
                vehVariations[i - 400][5] = dataFile.ReadLine(section, "Desert", READ_VEHICLES);

                vehVariations[i - 400][6] = vectorUnion(dataFile.ReadLine(section, "TierraRobada", READ_VEHICLES), vehVariations[i - 400][5]);
                vehVariations[i - 400][7] = vectorUnion(dataFile.ReadLine(section, "BoneCounty", READ_VEHICLES), vehVariations[i - 400][5]);
                vehVariations[i - 400][8] = vectorUnion(dataFile.ReadLine(section, "RedCounty", READ_VEHICLES), vehVariations[i - 400][0]);
                vehVariations[i - 400][9] = vectorUnion(dataFile.ReadLine(section, "Blueberry", READ_VEHICLES), vehVariations[i - 400][8]);
                vehVariations[i - 400][10] = vectorUnion(dataFile.ReadLine(section, "Montgomery", READ_VEHICLES), vehVariations[i - 400][8]);
                vehVariations[i - 400][11] = vectorUnion(dataFile.ReadLine(section, "Dillimore", READ_VEHICLES), vehVariations[i - 400][8]);
                vehVariations[i - 400][12] = vectorUnion(dataFile.ReadLine(section, "PalominoCreek", READ_VEHICLES), vehVariations[i - 400][8]);
                vehVariations[i - 400][13] = vectorUnion(dataFile.ReadLine(section, "FlintCounty", READ_VEHICLES), vehVariations[i - 400][0]);
                vehVariations[i - 400][14] = vectorUnion(dataFile.ReadLine(section, "Whetstone", READ_VEHICLES), vehVariations[i - 400][0]);
                vehVariations[i - 400][15] = vectorUnion(dataFile.ReadLine(section, "AngelPine", READ_VEHICLES), vehVariations[i - 400][14]);

                vehWantedVariations[i - 400][0] = dataFile.ReadLine(section, "Wanted1", READ_VEHICLES);
                vehWantedVariations[i - 400][1] = dataFile.ReadLine(section, "Wanted2", READ_VEHICLES);
                vehWantedVariations[i - 400][2] = dataFile.ReadLine(section, "Wanted3", READ_VEHICLES);
                vehWantedVariations[i - 400][3] = dataFile.ReadLine(section, "Wanted4", READ_VEHICLES);
                vehWantedVariations[i - 400][4] = dataFile.ReadLine(section, "Wanted5", READ_VEHICLES);
                vehWantedVariations[i - 400][5] = dataFile.ReadLine(section, "Wanted6", READ_VEHICLES);


                for (auto &j : vehVariations[i - 400])
                    for (auto& k : j)
                        if (k != i && !(vectorHasId(vehInheritExclude, k)))
                            vehOriginalModels.insert({ k, i });
            }

            
            vehGroups[i][0] = dataFile.ReadLine(section, "Countryside", READ_GROUPS);
            vehGroups[i][1] = dataFile.ReadLine(section, "LosSantos", READ_GROUPS);
            vehGroups[i][2] = dataFile.ReadLine(section, "SanFierro", READ_GROUPS);
            vehGroups[i][3] = dataFile.ReadLine(section, "LasVenturas", READ_GROUPS);
            vehGroups[i][4] = dataFile.ReadLine(section, "Global", READ_GROUPS);
            vehGroups[i][5] = dataFile.ReadLine(section, "Desert", READ_GROUPS);

            vehGroups[i][6] = vectorUnion(dataFile.ReadLine(section, "TierraRobada", READ_GROUPS), vehGroups[i][5]);
            vehGroups[i][7] = vectorUnion(dataFile.ReadLine(section, "BoneCounty", READ_GROUPS), vehGroups[i][5]);
            vehGroups[i][8] = vectorUnion(dataFile.ReadLine(section, "RedCounty", READ_GROUPS), vehGroups[i][0]);
            vehGroups[i][9] = vectorUnion(dataFile.ReadLine(section, "Blueberry", READ_GROUPS), vehGroups[i][8]);
            vehGroups[i][10] = vectorUnion(dataFile.ReadLine(section, "Montgomery", READ_GROUPS), vehGroups[i][8]);
            vehGroups[i][11] = vectorUnion(dataFile.ReadLine(section, "Dillimore", READ_GROUPS), vehGroups[i][8]);
            vehGroups[i][12] = vectorUnion(dataFile.ReadLine(section, "PalominoCreek", READ_GROUPS), vehGroups[i][8]);
            vehGroups[i][13] = vectorUnion(dataFile.ReadLine(section, "FlintCounty", READ_GROUPS), vehGroups[i][0]);
            vehGroups[i][14] = vectorUnion(dataFile.ReadLine(section, "Whetstone", READ_GROUPS), vehGroups[i][0]);
            vehGroups[i][15] = vectorUnion(dataFile.ReadLine(section, "AngelPine", READ_GROUPS), vehGroups[i][14]);

            //Veh Tuning
            vehTuning[i][0] = dataFile.ReadLine(section, "Countryside", READ_TUNING);
            vehTuning[i][1] = dataFile.ReadLine(section, "LosSantos", READ_TUNING);
            vehTuning[i][2] = dataFile.ReadLine(section, "SanFierro", READ_TUNING);
            vehTuning[i][3] = dataFile.ReadLine(section, "LasVenturas", READ_TUNING);
            vehTuning[i][4] = dataFile.ReadLine(section, "Global", READ_TUNING);
            vehTuning[i][5] = dataFile.ReadLine(section, "Desert", READ_TUNING);

            vehTuning[i][6] = vectorUnion(dataFile.ReadLine(section, "TierraRobada", READ_TUNING), vehTuning[i][5]);
            vehTuning[i][7] = vectorUnion(dataFile.ReadLine(section, "BoneCounty", READ_TUNING), vehTuning[i][5]);
            vehTuning[i][8] = vectorUnion(dataFile.ReadLine(section, "RedCounty", READ_TUNING), vehTuning[i][0]);
            vehTuning[i][9] = vectorUnion(dataFile.ReadLine(section, "Blueberry", READ_TUNING), vehTuning[i][8]);
            vehTuning[i][10] = vectorUnion(dataFile.ReadLine(section, "Montgomery", READ_TUNING), vehTuning[i][8]);
            vehTuning[i][11] = vectorUnion(dataFile.ReadLine(section, "Dillimore", READ_TUNING), vehTuning[i][8]);
            vehTuning[i][12] = vectorUnion(dataFile.ReadLine(section, "PalominoCreek", READ_TUNING), vehTuning[i][8]);
            vehTuning[i][13] = vectorUnion(dataFile.ReadLine(section, "FlintCounty", READ_TUNING), vehTuning[i][0]);
            vehTuning[i][14] = vectorUnion(dataFile.ReadLine(section, "Whetstone", READ_TUNING), vehTuning[i][0]);
            vehTuning[i][15] = vectorUnion(dataFile.ReadLine(section, "AngelPine", READ_TUNING), vehTuning[i][14]);

            const int tuningRarity = dataFile.ReadInteger(section, "TuningRarity", -1);
            if (tuningRarity > -1)
                tuningRarities.insert({ i, (BYTE)tuningRarity });

            if (dataFile.ReadBoolean(section, "UseOnlyGroups", false))
                vehUseOnlyGroups.insert(i);

            if (enableLights)
            {
                const float lightWidth = dataFile.ReadFloat(section, "LightWidth", -999.0);
                const float lightX = dataFile.ReadFloat(section, "LightX", 0.0);
                const float lightY = dataFile.ReadFloat(section, "LightY", 0.0);
                const float lightZ = dataFile.ReadFloat(section, "LightZ", 0.0);

                int r = dataFile.ReadInteger(section, "LightR", -1);
                int g = dataFile.ReadInteger(section, "LightG", -1);
                int b = dataFile.ReadInteger(section, "LightB", -1);
                int a = dataFile.ReadInteger(section, "LightA", -1);

                if ((uint8_t)r == r && (uint8_t)g == g && (uint8_t)b == b && (uint8_t)a == a)
                {
                    rgba colors = { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
                    LightColors.insert({ i, colors });
                }

                if (lightX != 0.0 || lightY != 0.0 || lightZ != 0.0 || lightWidth > -900.0)
                    LightPositions.insert({ i, {{ lightX, lightY, lightZ }, lightWidth} });

                r = dataFile.ReadInteger(section, "LightR2", -1);
                g = dataFile.ReadInteger(section, "LightG2", -1);
                b = dataFile.ReadInteger(section, "LightB2", -1);
                a = dataFile.ReadInteger(section, "LightA2", -1);

                if ((uint8_t)r == r && (uint8_t)g == g && (uint8_t)b == b && (uint8_t)a == a)
                {
                    rgba colors = { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
                    LightColors2.insert({ i, colors });
                }
            }

            if (dataFile.ReadBoolean(section, "MergeZonesWithCities", false))
                vehMergeZones.insert(i);

            uint8_t numGroups = 0;
            for (int j = 0; j < 9; j++)
            {
                std::vector<unsigned short> vecDrivers = dataFile.ReadLine(section, "DriverGroup" + std::to_string(j + 1), READ_PEDS);
                if (!vecDrivers.empty())
                {
                    std::vector<unsigned short> vecPassengers = dataFile.ReadLine(section, "PassengerGroup" + std::to_string(j + 1), READ_PEDS);
                    if (!vecPassengers.empty())
                    {
                        vehPassengerGroups[j].insert({ i, vecPassengers });
                        vehDriverGroups[j].insert({ i, vecDrivers });
                        numGroups++;
                    }
                }             
            }

            if (numGroups > 0)
                modelNumGroups[i] = numGroups;

            for (unsigned short j = 0; j < 6; j++)
            {
                std::vector<unsigned short> vec = dataFile.ReadLineUnique(section, "Wanted" + std::to_string(j + 1), READ_GROUPS);
                if (!vec.empty())
                {
                    checkNumGroups(vec, numGroups);
                    vehGroupWantedVariations[i][j] = vec;
                }
            }

            if (vehGroups.find(i) != vehGroups.end())
                for (auto &j : vehGroups[i])
                    checkNumGroups(j, numGroups);


            std::vector<unsigned short> vec = dataFile.ReadLine(section, "Drivers", READ_PEDS);
            if (!vec.empty())
                vehDrivers.insert({ i, vec });

            vec = dataFile.ReadLine(section, "Passengers", READ_PEDS);
            if (!vec.empty())
                vehPassengers.insert({ i, vec });

            vec = dataFile.ReadLine(section, "ParentModel", READ_VEHICLES);
            if (!vec.empty() && vec[0] >= 400)
                vehOriginalModels[i] = vec[0];
        }
    }

    Log::Write("\n");
}

void VehicleVariations::Process()
{
    while (!tuningStack.empty())
    {
        auto it = tuningStack.top();
        tuningStack.pop();

        if (IsVehiclePointerValid(it.first))
            for (auto& slot : it.second)
                if (!slot.empty())
                {
                    const uint32_t i = rand<uint32_t>(0, slot.size());

                    if (slot[i] <= 20)
                    {
                        it.first->SetRemap(slot[i]);
                    }
                    else
                    {
                        CStreaming::RequestVehicleUpgrade(slot[i], 2);
                        CStreaming::LoadAllRequestedModels(false);

                        it.first->AddVehicleUpgrade(slot[i]);
                        CStreaming::SetMissionDoesntRequireModel(slot[i]);
                        short otherUpgrade = reinterpret_cast<short(__thiscall*)(uint32_t, uint16_t)>(0x4C74D0)(0xB4E6D8, slot[i]);
                        if (otherUpgrade > -1)
                            CStreaming::SetMissionDoesntRequireModel(otherUpgrade);
                    }
                }
    }

    while (!vehStack.empty())
    {
        CVehicle* veh = vehStack.top();
        vehStack.pop();

        if (!IsVehiclePointerValid(veh))
            continue;

        if (veh->m_nModelIndex >= 400 && veh->m_nModelIndex < 612 && !vehCurrentVariations[veh->m_nModelIndex - 400].empty() &&
            vehCurrentVariations[veh->m_nModelIndex - 400][0] == 0 && veh->m_nCreatedBy != eVehicleCreatedBy::MISSION_VEHICLE)
        {
            veh->m_nVehicleFlags.bFadeOut = 1;
        }
        else
        {
            auto it = vehPassengers.find(veh->m_nModelIndex);
            if (it != vehPassengers.end() && it->second[0] == 0)
                for (int i = 0; i < 8; i++)
                    if (veh->m_apPassengers[i] != NULL)
                    {
                        CPed* passenger = veh->m_apPassengers[i];
                        if (passenger->m_pIntelligence)
                            passenger->m_pIntelligence->FlushImmediately(false);
                        CTheScripts::RemoveThisPed(passenger);
                    }
        }
    }
}

void VehicleVariations::UpdateVariations()
{
    for (auto& i : vehTuning)
    {
        vehCurrentTuning[i.first] = vectorUnion(i.second[4], i.second[currentTown]);

        std::string section;
        auto it = vehModels.find(i.first);
        if (it != vehModels.end())
            section = it->second;
        else
            section = std::to_string(i.first);

        std::vector<unsigned short> vec = dataFile.ReadLine(section, currentZone, READ_TUNING);
        if (!vec.empty())
        {
            if (vehMergeZones.find(i.first) != vehMergeZones.end())
                vehCurrentTuning[i.first] = vectorUnion(vehCurrentTuning[i.first], vec);
            else
                vehCurrentTuning[i.first] = vec;
        }
    }

    for (auto& modelid : vehHasVariations)
    {
        vehCurrentVariations[modelid] = vectorUnion(vehVariations[modelid][4], vehVariations[modelid][currentTown]);

        std::string section;
        auto it = vehModels.find(modelid + 400U);
        if (it != vehModels.end())
            section = it->second;
        else
            section = std::to_string(modelid + 400);

        std::vector<unsigned short> vec = dataFile.ReadLine(section, currentZone, READ_VEHICLES);
        if (!vec.empty())
        {
            if (vehMergeZones.find((unsigned short)(modelid + 400)) != vehMergeZones.end())
                vehCurrentVariations[modelid] = vectorUnion(vehCurrentVariations[modelid], vec);
            else
                vehCurrentVariations[modelid] = vec;
        }

        const CWanted* wanted = FindPlayerWanted(-1);
        if (wanted)
        {
            const unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            if (!vehWantedVariations[modelid][wantedLevel].empty() && !vehCurrentVariations[modelid].empty())
                vectorfilterVector(vehCurrentVariations[modelid], vehWantedVariations[modelid][wantedLevel]);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////  LOGGING   /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void VehicleVariations::LogCurrentVariations()
{
    Log::Write("vehCurrentVariations\n");
    for (int i = 0; i < 212; i++)
        if (!vehCurrentVariations[i].empty())
        {
            Log::Write("%d: ", i+400);
            for (auto j : vehCurrentVariations[i])
                Log::Write("%u ", j);
            Log::Write("\n");
        }
}

void VehicleVariations::LogDataFile()
{
    if (GetFileAttributes(dataFileName) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
        Log::Write("\n%s not found!\n\n", dataFileName);
    else
        Log::Write("##################################\n"
                   "## ModelVariations_Vehicles.ini ##\n"
                   "##################################\n%s\n", fileToString(dataFileName).c_str());
}

void VehicleVariations::LogVariations()
{
    Log::Write("Vehicle Variations:\n");
    for (unsigned int i = 0; i < 212; i++)
        for (unsigned int j = 0; j < 16; j++)
            if (!vehVariations[i][j].empty())
            {
                Log::Write("%d: ", i+400);
                for (unsigned int k = 0; k < 16; k++)
                    if (!vehVariations[i][k].empty())
                    {
                        Log::Write("(%u) ", k);
                        for (const auto& l : vehVariations[i][k])
                            Log::Write("%u ", l);
                    }

                Log::Write("\n");
                break;
            }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template <std::uintptr_t address, typename... Args>
void changeModel(const char* funcName, unsigned short oldModel, int newModel, std::vector<std::uintptr_t> addresses, Args... args)
{
    if (newModel < 400 || newModel > 65535)
    {
        callMethodOriginal<address>(args...);
        return;
    }

    for (auto& i : addresses)
        if (*(uint16_t*)i != oldModel && forceEnable == false)
        {
            Log::LogModifiedAddress(i, "Modified method detected : %s - 0x%08X is %u\n", funcName, i, *(uint16_t*)i);
            return callMethodOriginal<address>(args...);
        }

    for (auto& i : addresses)
        WriteMemory<uint16_t>(i, newModel);
    callMethodOriginal<address>(args...);
    for (auto& i : addresses)
        WriteMemory<uint16_t>(i, oldModel);
}

template <typename T, std::uintptr_t address, typename... Args>
T changeModelAndReturn(const char* funcName, unsigned short oldModel, int newModel, std::vector<std::uintptr_t> addresses, Args... args)
{
    if (newModel < 400 || newModel > 65535)
    {
        return callMethodOriginalAndReturn<T, address>(args...);
    }

    for (auto& i : addresses)
        if (*(uint16_t*)i != oldModel && forceEnable == false)
        {
            Log::LogModifiedAddress(i, "Modified method detected : %s - 0x%08X is %u\n", funcName, i, *(uint16_t*)i);
            return callMethodOriginalAndReturn<T, address>(args...);
        }

    for (auto& i : addresses)
        WriteMemory<uint16_t>(i, newModel);
    T retValue = callMethodOriginalAndReturn<T, address>(args...);
    for (auto& i : addresses)
        WriteMemory<uint16_t>(i, oldModel);
    return retValue;
}

template <std::uintptr_t address>
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

template <std::uintptr_t address>
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

template <std::uintptr_t address>
void __cdecl AddPoliceCarOccupantsHooked(CVehicle* a2, char a3)
{
    if (a2 == NULL)
        return;

    processOccupantGroups(a2);

    const unsigned short model = a2->m_nModelIndex;
    a2->m_nModelIndex = (unsigned short)getVariationOriginalModel(a2->m_nModelIndex);

    callOriginal<address>(a2, a3);
    //CCarAI::AddPoliceCarOccupants(a2, a3);

    a2->m_nModelIndex = model;
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
}

template <std::uintptr_t address>
CAutomobile* __fastcall CAutomobileHooked(CAutomobile* automobile, void*, int modelIndex, char usageType, char bSetupSuspensionLines)
{
    return callMethodOriginalAndReturn<CAutomobile*, address>(automobile, getRandomVariation(modelIndex), usageType, bSetupSuspensionLines);
}

template <std::uintptr_t address>
signed int __fastcall PickRandomCarHooked(CLoadedCarGroup* cargrp, void*, char a2, char a3) //for random parked cars
{
    if (cargrp == NULL)
        return -1;

    const int originalModel = callMethodOriginalAndReturn<signed int, address>(cargrp, a2, a3);
    int variation = getRandomVariation(originalModel, true);

    if (originalModel == 531) //Tractor
    {
        if (*(uint16_t*)0x6F3B9A == 531 || forceEnable)
        {
            WriteMemory<uint16_t>(0x6F3B9A, variation);
            carGenModel = 531;
        }
        else
            Log::LogModifiedAddress(0x6F3B9A, "Modified method detected: CCarGenerator::DoInternalProcessing - 0x6F3B9A is %u\n", *(uint16_t*)0x6F3B9A);
    }
    else if (originalModel == 532) //Combine Harvester
    {
        if (*(uint16_t*)0x6F3BA0 == 532 || forceEnable)
        {
            WriteMemory<uint16_t>(0x6F3BA0, variation);
            carGenModel = 532;
        }
        else
            Log::LogModifiedAddress(0x6F3BA0, "Modified method detected: CCarGenerator::DoInternalProcessing - 0x6F3BA0 is %u\n", *(uint16_t*)0x6F3BA0);
    }

    return variation;
}

template <std::uintptr_t address>
void __fastcall DoInternalProcessingHooked(CCarGenerator* park) //for non-random parked cars
{
    if (park != NULL)
    {
        if (park->m_nModelId < 0)
        {
            callMethodOriginal<address>(park);
            if (carGenModel == 531)
                WriteMemory<uint16_t>(0x6F3B9A, 531);
            else if (carGenModel == 532)
                WriteMemory<uint16_t>(0x6F3BA0, 532);

            carGenModel = -1;
            tuneParkedCar = true;
            return;
        }

        const short model = park->m_nModelId;
        if (changeCarGenerators)
        {
            if (!vehCarGenExclude.empty())
                if (std::find(vehCarGenExclude.begin(), vehCarGenExclude.end(), model) != vehCarGenExclude.end())
                {
                    callMethodOriginal<address>(park);
                    return;
                }
            
            if (model == 531) //Tractor
            {
                if (*(uint16_t*)0x6F3B9A == 531 || forceEnable)
                {
                    park->m_nModelId = (short)getRandomVariation(park->m_nModelId, true);
                    WriteMemory<uint16_t>(0x6F3B9A, park->m_nModelId);
                    callMethodOriginal<address>(park);
                    WriteMemory<uint16_t>(0x6F3B9A, 531);
                    //park->m_nModelId = 531;
                    return;
                }
                else
                {
                    callMethodOriginal<address>(park);
                    Log::LogModifiedAddress(0x6F3B9A, "Modified method detected: CCarGenerator::DoInternalProcessing - 0x6F3B9A is %u\n", *(uint16_t*)0x6F3B9A);
                    return;
                }
            }
            else if (model == 532) //Combine Harvester
            {
                if (*(uint16_t*)0x6F3BA0 == 532 || forceEnable)
                {
                    park->m_nModelId = (short)getRandomVariation(park->m_nModelId, true);
                    WriteMemory<uint16_t>(0x6F3BA0, park->m_nModelId);
                    callMethodOriginal<address>(park);
                    WriteMemory<uint16_t>(0x6F3BA0, 532);
                    return;
                }
                else
                {
                    callMethodOriginal<address>(park);
                    Log::LogModifiedAddress(0x6F3BA0, "Modified method detected: CCarGenerator::DoInternalProcessing - 0x6F3BA0 is %u\n", *(uint16_t*)0x6F3BA0);
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
            case 497: //Police Maverick
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

template <std::uintptr_t address>
void* __fastcall CTrainHooked(void* train, void*, int modelIndex, int createdBy)
{
    return callMethodOriginalAndReturn<void*, address>(train, getRandomVariation(modelIndex), createdBy);
}

template <std::uintptr_t address>
CVehicle* __fastcall CBoatHooked(void* boat, void*, int modelId, char a3)
{
    return callMethodOriginalAndReturn<CVehicle*, address>(boat, getRandomVariation(modelId), a3);
}

template <std::uintptr_t address>
CAutomobile* __fastcall CHeliHooked(CHeli* heli, void*, int a2, char usageType)
{
    return callMethodOriginalAndReturn<CAutomobile*, address>(heli, getRandomVariation(a2), usageType);
}

template <std::uintptr_t address>
char __fastcall IsLawEnforcementVehicleHooked(CVehicle* vehicle)
{
    if (vehicle == NULL)
        return 0;
    const unsigned short modelIndex = vehicle->m_nModelIndex;

    vehicle->m_nModelIndex = (unsigned short)getVariationOriginalModel(vehicle->m_nModelIndex);

    char isLawEnforcement = 0;
    if constexpr (address == 0)
        isLawEnforcement = vehicle->IsLawEnforcementVehicle();
    else
        isLawEnforcement = callMethodOriginalAndReturn<char, address>(vehicle);

    vehicle->m_nModelIndex = modelIndex;
    return isLawEnforcement;
}

template <std::uintptr_t address>
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

template <std::uintptr_t address>
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

template <std::uintptr_t address>
void __cdecl AddAmbulanceOccupantsHooked(CVehicle* pVehicle)
{
    if (pVehicle == NULL)
        return;
    
    const auto model = pVehicle->m_nModelIndex;

    if (isModelAmbulance(pVehicle->m_nModelIndex))
        pVehicle->m_nModelIndex = 416;
    callOriginal<address>(pVehicle);
    pVehicle->m_nModelIndex = model;
}

template <std::uintptr_t address>
void __cdecl AddFiretruckOccupantsHooked(CVehicle* pVehicle)
{
    if (pVehicle == NULL)
        return;

    const auto model = pVehicle->m_nModelIndex;

    if (isModelFiretruck(pVehicle->m_nModelIndex))
        pVehicle->m_nModelIndex = 407;
    callOriginal<address>(pVehicle);
    pVehicle->m_nModelIndex = model;
}

template <std::uintptr_t address>
DWORD __cdecl FindSpecificDriverModelForCar_ToUseHooked(int carModel)
{
    if (carModel < 400)
        return callOriginalAndReturn<DWORD, address>(getVariationOriginalModel(carModel));

    auto it = vehDrivers.find((unsigned short)carModel);
    
    std::string section;
    auto it2 = vehModels.find((unsigned short)carModel);
    if (it2 != vehModels.end())
        section = it2->second;
    else
        section = std::to_string(carModel);

    const auto replaceDriver = dataFile.ReadBoolean(section, "ReplaceDriver", false);
    if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
    {
        auto itGroup = vehDriverGroups[currentOccupantsGroup].find(currentOccupantsModel);
        if (itGroup != vehDriverGroups[currentOccupantsGroup].end())
        {
            auto model = vectorGetRandom(itGroup->second);
            loadModels({ model }, PRIORITY_REQUEST, true);
            return model;
        }
    }
    if (it != vehDrivers.end() && ((!replaceDriver && rand<bool>()) || replaceDriver))
    {
        auto model = vectorGetRandom(it->second);
        loadModels({ model }, PRIORITY_REQUEST, true);
        return model;
    }

    return callOriginalAndReturn<DWORD, address>(getVariationOriginalModel(carModel));
}

template <std::uintptr_t address>
void __fastcall CollectParametersHooked(CRunningScript* script, void*, unsigned __int16 a2)
{
    callMethodOriginal<address>(script, a2);
    if (enableAllSideMissions == 0)
    {
        if (!hasModelSideMission(ScriptParams[1]))
            return;

        if (strcmp(script->m_szName, "r3") != 0 && strcmp(script->m_szName, "ambulan") != 0 && strcmp(script->m_szName, "firetru") != 0 &&
            strcmp(script->m_szName, "freight") != 0 && strcmp(script->m_szName, "trains") != 0 && strcmp(script->m_szName, "trainsl") != 0 &&
            strcmp(script->m_szName, "copcar") != 0)
            return;
    }

    CPlayerPed* player = FindPlayerPed();  
    if (player == NULL)
        return;

    const int hplayer = CPools::GetPedRef(player);

    if (ScriptParams[0] != hplayer)
        return;

    if (player->m_pVehicle)
    {
        const int originalModel = getVariationOriginalModel(player->m_pVehicle->m_nModelIndex);
        if (ScriptParams[1] == originalModel)
            ScriptParams[1] = player->m_pVehicle->m_nModelIndex;
    }
}

template <std::uintptr_t address>
char __cdecl GenerateRoadBlockCopsForCarHooked(CVehicle* a1, int pedsPositionsType, int type)
{
    if (a1 == NULL)
        return 0;

    processOccupantGroups(a1);

    roadblockModel = a1->m_nModelIndex;
    a1->m_nModelIndex = (unsigned short)getVariationOriginalModel(a1->m_nModelIndex);
    callOriginal<address>(a1, pedsPositionsType, type);
    if (roadblockModel >= 400)
        a1->m_nModelIndex = roadblockModel;
    roadblockModel = 0;
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;

    return 1;
}

template <std::uintptr_t address>
CColModel* __fastcall GetColModelHooked(CVehicle* entity)
{
    if (roadblockModel >= 400)
        entity->m_nModelIndex = roadblockModel;
    return callMethodOriginalAndReturn<CColModel*, address>(entity);
}

template <std::uintptr_t address>
CCopPed* __fastcall CCopPedHooked(CCopPed* ped, void*, int copType)
{
    if (!memcmp(0x5DDD90, "1D 01 00 00"))
    {
        Log::LogModifiedAddress(0x5DDD90, "Modified address detected: 0x5DDD90 is %u\n", *(uint16_t*)0x5DDD90);
        return callMethodOriginalAndReturn<CCopPed*, address>(ped, copType);
    }

    unsigned int originalModel = *(unsigned int*)0x5DDD90;

    if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
    {
        auto itGroup = vehDriverGroups[currentOccupantsGroup].find(currentOccupantsModel);
        if (itGroup != vehDriverGroups[currentOccupantsGroup].end())
        {
            *(unsigned int*)0x5DDD90 = vectorGetRandom(itGroup->second);
            copType = 3;
        }
    }

    auto retVal = callMethodOriginalAndReturn<CCopPed*, address>(ped, copType);
    *(unsigned int*)0x5DDD90 = originalModel;
    return retVal;
}

template <std::uintptr_t address>
void __cdecl SetUpDriverAndPassengersForVehicleHooked(CVehicle* car, int a3, int a4, char a5, char a6, int a7)
{
    if (car == NULL)
        return;

    processOccupantGroups(car);

    //laemt1 -> dsher
    loadModels(274, 288, GAME_REQUIRED, true);

    const auto model = car->m_nModelIndex;

    car->m_nModelIndex = (unsigned short)getVariationOriginalModel(car->m_nModelIndex);

    callOriginal<address>(car, a3, a4, a5, a6, a7);
    //CCarCtrl::SetUpDriverAndPassengersForVehicle(car, a3, a4, a5, a6, a7);

    car->m_nModelIndex = model;
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
}

template <std::uintptr_t address>
CHeli* __cdecl GenerateHeliHooked(CPed* ped, char newsHeli)
{
    if (FindPlayerWanted(-1)->m_nWantedLevel < 4)
        return callOriginalAndReturn<CHeli*, address>(ped, 0);
        //return CHeli::GenerateHeli(ped, 0);

    if (CHeli::pHelis)
    {
        loadModels({ 488, 497 }, GAME_REQUIRED, true);
        newsHeli = 1;
        if (CHeli::pHelis[0] && (CHeli::pHelis[0]->m_nModelIndex == 488 || getVariationOriginalModel(CHeli::pHelis[0]->m_nModelIndex) == 488))
            newsHeli = 0;

        if (CHeli::pHelis[1] && (CHeli::pHelis[1]->m_nModelIndex == 488 || getVariationOriginalModel(CHeli::pHelis[1]->m_nModelIndex) == 488))
            newsHeli = 0;
    }

    return callOriginalAndReturn<CHeli*, address>(ped, newsHeli);
    //return CHeli::GenerateHeli(ped, newsHeli);
}

template <std::uintptr_t address>
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

template <std::uintptr_t address>
CPed* __cdecl AddPedInCarHooked(CVehicle* a1, char driver, int a3, signed int a4, int a5, char a6)
{
    if (a1)
    {
        std::string section;
        auto it2 = vehModels.find(a1->m_nModelIndex);
        if (it2 != vehModels.end())
            section = it2->second;
        else
            section = std::to_string(a1->m_nModelIndex);

        const auto replacePassenger = dataFile.ReadBoolean(section, "ReplacePassengers", false);
        auto it = vehPassengers.find(a1->m_nModelIndex);
        if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
        {
            auto itGroup = vehPassengerGroups[currentOccupantsGroup].find(currentOccupantsModel);
            if (itGroup != vehPassengerGroups[currentOccupantsGroup].end())
                passengerModelIndex = vectorGetRandom(itGroup->second);
        }
        else if (it != vehPassengers.end() && ((!replacePassenger && rand<bool>()) || replacePassenger))
            passengerModelIndex = vectorGetRandom(it->second);
    }

    if (passengerModelIndex > 0)
    {
        uint8_t originalData[5] = { *(uint8_t*)0x613B78, *(uint8_t*)0x613B79, *(uint8_t*)0x613B7A, *(uint8_t*)0x613B7B, *(uint8_t*)0x613B7C };
        loadModels({ passengerModelIndex }, PRIORITY_REQUEST, true);
        injector::MakeJMP(0x613B78, patchPassengerModel);
        CPed* ped = callOriginalAndReturn<CPed*, address>(a1, driver, a3, a4, a5, a6);
        injector::WriteMemoryRaw(0x613B78, originalData, 5, true);
        passengerModelIndex = -1;
        return ped;
    }

    return callOriginalAndReturn<CPed*, address>(a1, driver, a3, a4, a5, a6);
    //return CPopulation::AddPedInCar(a1, a2, a3, a4, a5, a6);
}

template <std::uintptr_t address>
void __cdecl RegisterCoronaHooked(void* _this, unsigned int a2, unsigned __int8 a3, unsigned __int8 a4, unsigned __int8 a5, unsigned __int8 a6, CVector* a7, const CVector* a8,
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
            if (it->second.first.x != 0.0)
                a7->x += it->second.first.x;
            if (it->second.first.y != 0.0)
                a7->y += it->second.first.y;
            if (it->second.first.z != 0.0)
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

void __cdecl RegisterCoronaHooked2(void* _this, unsigned int a2, unsigned __int8 a3, unsigned __int8 a4, unsigned __int8 a5, unsigned __int8 a6, CVector* a7, const CVector* a8,
                                   float a9, void* texture, unsigned __int8 a11, unsigned __int8 a12, unsigned __int8 a13, int a14, float a15, float a16, float a17, float a18,
                                   float a19, float a20, bool a21)
{
    if (a7 && lightsModel > 0)
    {
        auto it = LightPositions.find(lightsModel);
        if (it != LightPositions.end())
        {
            if (it->second.second > -900.0f)
                a7->x *= it->second.second;
            if (it->second.first.x != 0.0f)
                a7->x += it->second.first.x;
            if (it->second.first.y != 0.0f)
                a7->y += it->second.first.y;
            if (it->second.first.z != 0.0f)
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

template <std::uintptr_t address>
void __cdecl AddLightHooked(char type, float x, float y, float z, float dir_x, float dir_y, float dir_z, float radius, float r, float g, float b, 
                            char fogType, char generateExtraShadows, int attachedTo)
{
    if (lightsModel > 0)
    {
        auto it = LightPositions.find(lightsModel);
        if (it != LightPositions.end())
        {
            if (it->second.second > -900.0f)
                x *= it->second.second;
            if (it->second.first.x != 0.0f)
                x += it->second.first.x;
            if (it->second.first.y != 0.0f)
                y += it->second.first.y;
            if (it->second.first.z != 0.0f)
                z += it->second.first.z;
        }
    }

    callOriginal<address>(type, x, y, z, dir_x, dir_y, dir_z, radius, r, g, b, fogType, generateExtraShadows, attachedTo);
}

template <std::uintptr_t address>
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

    if (*(uint16_t*)0x4250AC == 416 || forceEnable)
    {
        WriteMemory<uint16_t>(0x4250AC, car->m_nModelIndex);
        callOriginal<address>(car);
        WriteMemory<uint16_t>(0x4250AC, 416);
        return;
    }
    else
    {
        Log::LogModifiedAddress(0x4250AC, "Modified method detected: CCarCtrl::PossiblyRemoveVehicle - 0x4250AC is %u\n", *(uint16_t*)0x4250AC);
        callOriginal<address>(car);
    }
}

template <std::uintptr_t address>
void* __fastcall SetDriverHooked(CVehicle* _this, void*, CPed* a2)
{
    if (_this == NULL)
        return NULL;

    unsigned short modelIndex = _this->m_nModelIndex;
    _this->m_nModelIndex = (unsigned short)getVariationOriginalModel(_this->m_nModelIndex);
    auto retVal = callMethodOriginalAndReturn<void*, address>(_this, a2);
    _this->m_nModelIndex = modelIndex;

    return retVal;
}

template <std::uintptr_t address>
CVehicle* __cdecl CreateCarForScriptHooked(int modelId, float posX, float posY, float posZ, char doMissionCleanup)
{
    return callOriginalAndReturn<CVehicle*, address>(getRandomVariation(modelId), posX, posY, posZ, doMissionCleanup);
}

template <std::uintptr_t address>
void* __cdecl GetNewVehicleDependingOnCarModelHooked(int modelIndex, int createdBy)
{
    CVehicle *veh = reinterpret_cast<CVehicle*>(callOriginalAndReturn<void*, address>(modelIndex, createdBy));
    processTuning(veh);
    return veh;
}

////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////  SPECIAL FEATURES  //////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

template <std::uintptr_t address>
void __fastcall ProcessControlHooked(CAutomobile* veh)
{
    switch (getVariationOriginalModel(veh->m_nModelIndex))
    {
        case 406: //Dumper
            return changeModel<address>("CAutomobile::ProcessControl", 406, veh->m_nModelIndex, { 0x6B1F9D }, veh);
        case 407: //Firetruck
            return changeModel<address>("CAutomobile::ProcessControl", 407, veh->m_nModelIndex, { 0x6B1F51 }, veh);
        case 417: //Leviathan
            return changeModel<address>("CAutomobile::ProcessControl", 417, veh->m_nModelIndex, { 0x6B1E34 }, veh);
        case 423: //Mr. Whoopie
            return changeModel<address>("CAutomobile::ProcessControl", 423, veh->m_nModelIndex, { 0x6B2BD8 }, veh);
        case 432: //Rhino
            return changeModel<address>("CAutomobile::ProcessControl", 432, veh->m_nModelIndex, { 0x6B1F7D, 0x6B36D8 }, veh);
        case 443: //Packer
            return changeModel<address>("CAutomobile::ProcessControl", 443, veh->m_nModelIndex, { 0x6B1F91 }, veh);
        case 447: //Sea Sparrow
            return changeModel<address>("CAutomobile::ProcessControl", 447, veh->m_nModelIndex, { 0x6B1E2D }, veh);
        case 460: //Skimmer
            return changeModel<address>("CAutomobile::ProcessControl", 460, veh->m_nModelIndex, { 0x6B2181 }, veh);
        case 486: //Dozer
            return changeModel<address>("CAutomobile::ProcessControl", 486, veh->m_nModelIndex, { 0x6B1F97 }, veh);
        case 524: //Cement Truck
            return changeModel<address>("CAutomobile::ProcessControl", 524, veh->m_nModelIndex, { 0x6B1FA3 }, veh);
        case 525: //Towtruck
            return changeModel<address>("CAutomobile::ProcessControl", 525, veh->m_nModelIndex, { 0x6B1FB5 }, veh);
        case 530: //Forklift
            return changeModel<address>("CAutomobile::ProcessControl", 530, veh->m_nModelIndex, { 0x6B1FAF }, veh);
        case 531: //Tractor
            return changeModel<address>("CAutomobile::ProcessControl", 531, veh->m_nModelIndex, { 0x6B1FBB }, veh);
        case 532: //Combine Harverster
            return changeModel<address>("CAutomobile::ProcessControl", 532, veh->m_nModelIndex, { 0x6B36C9 }, veh);
        case 539: //Vortex
            return changeModel<address>("CAutomobile::ProcessControl", 539, veh->m_nModelIndex, { 0x6B1E5C, 0x6B284F, 0x6B356E }, veh);
        case 601: //Swat Tank
            return changeModel<address>("CAutomobile::ProcessControl", 601, veh->m_nModelIndex, { 0x6B1F57 }, veh);
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

template <std::uintptr_t address>
void __fastcall CAutomobile__PreRenderHooked(CAutomobile* veh)
{
    if (veh == NULL)
        return;

    uint8_t sirenLightsOriginal[5] = { *(uint8_t*)0x6AB350, *(uint8_t*)0x6AB351, *(uint8_t*)0x6AB352, *(uint8_t*)0x6AB353, *(uint8_t*)0x6AB354 };

    sirenModel = -1;
    lightsModel = 0;
    bool hasSirenLights = false;

    if (!LoadedModules::IsModLoaded(MOD_WLE))
        if (enableLights && (HasCarSiren<0>(veh) || getVariationOriginalModel(veh->m_nModelIndex) == 420 || getVariationOriginalModel(veh->m_nModelIndex) == 438))
        {
            sirenModel = getVariationOriginalModel(veh->m_nModelIndex);
            lightsModel = veh->m_nModelIndex;
            injector::MakeJMP(0x6AB350, enableSirenLights);
            hasSirenLights = true;
        }

    if (enableSpecialFeatures)
    {
        if (getVariationOriginalModel(veh->m_nModelIndex) == 407) //Firetruck
            changeModel<address>("CAutomobile::PreRender", 407, veh->m_nModelIndex, { 0x6ACA59 }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 424) //BF Injection
            changeModel<address>("CAutomobile::PreRender", 424, veh->m_nModelIndex, { 0x6AC2A1 }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
            changeModel<address>("CAutomobile::PreRender", 432, veh->m_nModelIndex, { 0x6ABC83,
                                                                                      0x6ABD11,
                                                                                      0x6ABFCC,
                                                                                      0x6AC029,
                                                                                      0x6ACA4D }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 434) //Hotknife
            changeModel<address>("CAutomobile::PreRender", 434, veh->m_nModelIndex, { 0x6ACA43 }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 443) //Packer
            changeModel<address>("CAutomobile::PreRender", 443, veh->m_nModelIndex, { 0x6AC4DB }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 477) //ZR-350
            changeModel<address>("CAutomobile::PreRender", 477, veh->m_nModelIndex, { 0x6ACA8F }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 486) //Dozer
            changeModel<address>("CAutomobile::PreRender", 486, veh->m_nModelIndex, { 0x6AC40E }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 495) //Sandking
            changeModel<address>("CAutomobile::PreRender", 495, veh->m_nModelIndex, { (GetGameVersion() != GAME_10US_COMPACT) ? 0x4064A0U : 0x6ACBCBU }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 524) //Cement Truck
            changeModel<address>("CAutomobile::PreRender", 524, veh->m_nModelIndex, { 0x6AC43D }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 525) //Towtruck
            changeModel<address>("CAutomobile::PreRender", 525, veh->m_nModelIndex, { 0x6AC509 }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 530) //Forklift
            changeModel<address>("CAutomobile::PreRender", 530, veh->m_nModelIndex, { 0x6AC71E }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 531) //Tractor
            changeModel<address>("CAutomobile::PreRender", 531, veh->m_nModelIndex, { 0x6AC6DB }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 532) //Combine Harverster
            changeModel<address>("CAutomobile::PreRender", 532, veh->m_nModelIndex, { 0x6ABCA3, 0x6AC7AD }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 539) //Vortex
            changeModel<address>("CAutomobile::PreRender", 539, veh->m_nModelIndex, { 0x6AAE27 }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 568) //Bandito
            changeModel<address>("CAutomobile::PreRender", 568, veh->m_nModelIndex, { 0x6ACA39 }, veh);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 601) //SWAT Tank
            changeModel<address>("CAutomobile::PreRender", 601, veh->m_nModelIndex, { 0x6ACA53 }, veh);
        else
            callMethodOriginal<address>(veh);
    }
    else
        callMethodOriginal<address>(veh);

    if (hasSirenLights)
        injector::WriteMemoryRaw(0x6AB350, sirenLightsOriginal, 5, true);
}

template <std::uintptr_t address>
char __fastcall SetTowLinkHooked(CAutomobile* automobile, void*, CVehicle* vehicle, char a3)
{
    if (vehicle != NULL)
    {
        if (getVariationOriginalModel(vehicle->m_nModelIndex) == 525) //Towtruck
            return changeModelAndReturn<char, address>("CAutomobile::SetTowLink", 525, vehicle->m_nModelIndex, { 0x6B44B0 }, automobile, vehicle, a3);
        else if (getVariationOriginalModel(vehicle->m_nModelIndex) == 531) //Tractor
            return changeModelAndReturn<char, address>("CAutomobile::SetTowLink", 531, vehicle->m_nModelIndex, { 0x6B44E6 }, automobile, vehicle, a3);
    }

    return callMethodOriginalAndReturn<char, address>(automobile, vehicle, a3);
}

template <std::uintptr_t address>
char __fastcall GetTowHitchPosHooked(CTrailer* trailer, void*, CVector* point, char a3, CVehicle* a4)
{
    if (a4 != NULL)
        if (getVariationOriginalModel(a4->m_nModelIndex) == 525) //Towtruck
            return changeModelAndReturn<char, address>("CTrailer::GetTowHitchPos", 525, a4->m_nModelIndex, { 0x6CEED9 }, trailer, point, a3, a4);

    return callMethodOriginalAndReturn<char, address>(trailer, point, a3, a4);
}

template <std::uintptr_t address>
void __fastcall UpdateTrailerLinkHooked(CVehicle* veh, void*, char a2, char a3)
{
    if (veh != NULL && veh->m_pTractor != NULL)
    {
        if (getVariationOriginalModel(veh->m_pTractor->m_nModelIndex) == 525) //Towtruck
            return changeModel<address>("CVehicle::UpdateTrailerLink", 525, veh->m_pTractor->m_nModelIndex, { 0x6DFDB8 }, veh, a2, a3);
        else if (getVariationOriginalModel(veh->m_pTractor->m_nModelIndex) == 531) //Tractor
            return changeModel<address>("CVehicle::UpdateTrailerLink", 531, veh->m_pTractor->m_nModelIndex, { 0x6DFDBE }, veh, a2, a3);
    }

    callMethodOriginal<address>(veh, a2, a3);
}

template <std::uintptr_t address>
void __fastcall UpdateTractorLinkHooked(CVehicle* veh, void*, bool a3, bool a4)
{
    if (veh != NULL)
    {
        if (getVariationOriginalModel(veh->m_nModelIndex) == 525) //Towtruck
            return changeModel<address>("CVehicle::UpdateTractorLink", 525, veh->m_nModelIndex, { 0x6E00D6 }, veh, a3, a4);
        else if (getVariationOriginalModel(veh->m_nModelIndex) == 531) //Tractor
            return changeModel<address>("CVehicle::UpdateTractorLink", 531, veh->m_nModelIndex, { 0x6E00FC }, veh, a3, a4);
    }
    
    callMethodOriginal<address>(veh, a3, a4);
}

template <std::uintptr_t address>
char __fastcall SetUpWheelColModelHooked(CAutomobile* automobile, void*, CColModel* colModel)
{
    if (automobile && (getVariationOriginalModel(automobile->m_nModelIndex) == 531 || getVariationOriginalModel(automobile->m_nModelIndex) == 532 || getVariationOriginalModel(automobile->m_nModelIndex) == 571))
        return 0;

    return callMethodOriginalAndReturn<char, address>(automobile, colModel);
}

template <std::uintptr_t address>
void __fastcall VehicleDamageHooked(CAutomobile* veh, void*, float fDamageIntensity, __int16 tCollisionComponent, int Damager, RwV3d* vecCollisionCoors,  RwV3d* vecCollisionDirection, signed int a7)
{
    if (veh->m_pDamageEntity && getVariationOriginalModel(veh->m_pDamageEntity->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::VehicleDamage", 432, veh->m_pDamageEntity->m_nModelIndex, { 0x6A80C0, 0x6A8384 }, veh, fDamageIntensity, tCollisionComponent, Damager, vecCollisionCoors, vecCollisionDirection, a7);

    callMethodOriginal<address>(veh, fDamageIntensity, tCollisionComponent, Damager, vecCollisionCoors, vecCollisionDirection, a7);
}

template <std::uintptr_t address>
void __fastcall SetupSuspensionLinesHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::SetupSuspensionLines", 432, veh->m_nModelIndex, { 0x6A6606, 0x6A6999 }, veh);
    if (getVariationOriginalModel(veh->m_nModelIndex) == 571) //Kart
        return changeModel<address>("CAutomobile::SetupSuspensionLines", 571, veh->m_nModelIndex, { 0x6A6907 }, veh);

    callMethodOriginal<address>(veh);
}

template <std::uintptr_t address>
void __fastcall DoBurstAndSoftGroundRatiosHooked(CAutomobile* a1)
{
    if (getVariationOriginalModel(a1->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::DoBurstAndSoftGroundRatios", 432, a1->m_nModelIndex, { 0x6A4917 }, a1);

    callMethodOriginal<address>(a1);
}

template <std::uintptr_t address>
char __fastcall BurstTyreHooked(CAutomobile* veh, void*, char componentId, char a3)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModelAndReturn<char, address>("CAutomobile::BurstTyre", 432, veh->m_nModelIndex, { 0x6A32BB }, veh, componentId, a3);

    return callMethodOriginalAndReturn<char, address>(veh, componentId, a3);
}

template <std::uintptr_t address>
void __fastcall CAutomobile__RenderHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::Render", 432, veh->m_nModelIndex, { 0x6A2C2D, 0x6A2EAD }, veh);

    callMethodOriginal<address>(veh);
}

template <std::uintptr_t address>
int __fastcall ProcessEntityCollisionHooked(CAutomobile* _this, void*, CVehicle* collEntity, CColPoint* colPoint)
{
    if (_this && getVariationOriginalModel(_this->m_nModelIndex) == 432) //Rhino
        return changeModelAndReturn<int, address>("CAutomobile::ProcessEntityCollision", 432, _this->m_nModelIndex, { 0x6ACEE9, 0x6AD242 }, _this, collEntity, colPoint);
    
    return callMethodOriginalAndReturn<int, address>(_this, collEntity, colPoint);
}

template <std::uintptr_t address>
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

template <std::uintptr_t address>
void __fastcall TankControlHooked(CAutomobile* veh)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::TankControl", 432, veh->m_nModelIndex, { 0x6AE9CB }, veh);

    callMethodOriginal<address>(veh);
}

template <std::uintptr_t address>
void __fastcall DoSoftGroundResistanceHooked(CAutomobile* veh, void*, unsigned int *a3)
{
    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return changeModel<address>("CAutomobile::DoSoftGroundResistance", 432, veh->m_nModelIndex, { 0x6A4BBA, 0x6A4E0E }, veh, a3);

    callMethodOriginal<address>(veh, a3);
}

template <std::uintptr_t address>
int __cdecl GetMaximumNumberOfPassengersFromNumberOfDoorsHooked(__int16 modelIndex)
{
    auto changeModelAtAddress = [modelIndex](std::uintptr_t modelAddress, short oldModel)
    {
        if (*(short*)modelAddress == oldModel || forceEnable)
        {
            WriteMemory<short>(modelAddress, modelIndex);
            const signed int retValue = callOriginalAndReturn<int, address>(modelIndex);
            WriteMemory<short>(modelAddress, oldModel);
            return retValue;
        }
        else
        {
            Log::LogModifiedAddress(modelAddress, "Modified method detected: CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors - 0x%X is %u\n", modelAddress, *(uint16_t*)modelAddress);
            return callOriginalAndReturn<int, address>(modelIndex);
        }
    };

    switch (getVariationOriginalModel(modelIndex))
    {
        case 407: //Firetruck
            return changeModelAtAddress(0x4C8A17, 407);
        case 425: //Hunter
            return changeModelAtAddress(0x4C8A27, 425);
        case 431: //Bus
            return changeModelAtAddress(0x4C8ADB, 431);
        case 437: //Coach
            return changeModelAtAddress(0x4C8AD3, 437);
    }

    return callOriginalAndReturn<int, address>(modelIndex);
}

template <std::uintptr_t address>
void __fastcall DoHeadLightReflectionHooked(CVehicle* veh, void*, RwMatrixTag* matrix, char twin, char left, char right)
{
    if (veh != NULL && getVariationOriginalModel(veh->m_nModelIndex) == 532) //Combine Harvester
        return changeModel<address>("CVehicle::DoHeadLightReflection", 532, veh->m_nModelIndex, { 0x6E176A }, veh, matrix, twin, left, right);

    callMethodOriginal<address>(veh, matrix, twin, left, right);
}

template <std::uintptr_t address>
void __fastcall ProcessControlInputsHooked(CPlane* _this, void*, unsigned __int8 a2)
{
    if (_this == NULL)
        return;

    unsigned short modelIndex = _this->m_nModelIndex;
    _this->m_nModelIndex = (unsigned short)getVariationOriginalModel(_this->m_nModelIndex);
    callMethodOriginal<address>(_this, a2);
    _this->m_nModelIndex = modelIndex;
}

template <std::uintptr_t address>
void __fastcall DoHeliDustEffectHooked(CAutomobile* _this, void*, float a3, float a4)
{
    if (_this == NULL)
        return;

    if (getVariationOriginalModel(_this->m_nModelIndex) == 520)
        return changeModel<address>("CAutomobile::DoHeliDustEffect", 520, _this->m_nModelIndex, { 0x6B0845 }, _this, a3, a4);

    callMethodOriginal<address>(_this, a3, a4);
}

template <std::uintptr_t address>
void __fastcall ProcessWeaponsHooked(CEntity* _this)
{
    if (_this == NULL)
        return;

    if (getVariationOriginalModel(_this->m_nModelIndex) == 520)
        return changeModel<address>("CVehicle::ProcessWeapons", 520, _this->m_nModelIndex, { 0x6E39BC }, _this);

    callMethodOriginal<address>(_this);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////  ASM HOOKS  /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void __declspec(naked) patch6A155C()
{
    __asm {
        push ecx
        push edx
        movsx eax, word ptr[edi + 0x22]
        push eax
        call getVariationOriginalModel
        pop edx
        pop ecx        
        cmp ax, 0x20C
        je isCement
        cmp ax, 0x220

    isCement:
        mov ax, word ptr [edi + 0x22]
        jmp jmp6A1564
    }
}

void __declspec(naked) patch588570()
{
    __asm {
        add esp, 8
        push eax
        push ecx
        push edx
        movsx ecx, word ptr [eax+0x22]
        push ecx
        call getVariationOriginalModel
        cmp ax, bx
        pop edx
        pop ecx
        pop eax
        jmp jmp588577
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

template <eRegs32 reg, std::uintptr_t jmpAddress, unsigned int model>
void __declspec(naked) cmpWordPtrRegModel()
{
    __asm {
        pushad
    }

    asmModel32 = model;
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
        cmp eax, asmModel32
        popad
        jmp asmJmpAddress
    }
}

template <eRegs16 reg, std::uintptr_t jmpAddress, unsigned int model>
void __declspec(naked) cmpReg16Model()
{
    __asm {
        pushad
    }

    asmModel32 = model;
    asmJmpAddress = jmpAddress;

    if constexpr (reg == REG_AX) { __asm { movsx eax, ax } }
    else if constexpr (reg == REG_CX) { __asm { movsx eax, cx } }
    else if constexpr (reg == REG_DX) { __asm { movsx eax, dx } }
    else if constexpr (reg == REG_BX) { __asm { movsx eax, bx } }
    else if constexpr (reg == REG_SP) { __asm { movsx eax, sp } }
    else if constexpr (reg == REG_BP) { __asm { movsx eax, bp } }
    else if constexpr (reg == REG_SI) { __asm { movsx eax, si } }
    else if constexpr (reg == REG_DI) { __asm { movsx eax, di } }

    __asm {
        push eax
        call getVariationOriginalModel
        cmp eax, asmModel32
        popad
        jmp asmJmpAddress
    }
}

template <eRegs32 reg, std::uintptr_t jmpAddress, unsigned int model>
void __declspec(naked) cmpReg32Model()
{
    __asm {
        pushad
    }

    asmModel32 = model;
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
        cmp eax, asmModel32
        popad
        jmp asmJmpAddress
    }
}

template <eRegs16 target, eRegs32 source, std::uintptr_t jmpAddress, uint8_t nextInstrSize, uint32_t nextInstr, uint32_t nextInstr2 = 0x90909090>
void __declspec(naked) movReg16WordPtrReg()
{
    __asm {
        pushfd
        pushad
    }

    asmNextInstr[0] = nextInstr;
    asmNextInstr[1] = nextInstr2;
    jmpDest = asmNextInstr;
    asmJmpAddress = jmpAddress;
    ((uint8_t*)asmNextInstr)[nextInstrSize] = 0xFF;
    ((uint8_t*)asmNextInstr)[nextInstrSize + 1] = 0x25;
    *((uint32_t**)((uint8_t*)asmNextInstr + nextInstrSize + 2)) = &asmJmpAddress;

    __asm {
        popad
        pushad
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

template <eRegs32 target, eRegs32 source, std::uintptr_t jmpAddress, uint8_t nextInstrSize, uint32_t nextInstr, uint32_t nextInstr2 = 0x90909090>
void __declspec(naked) movsxReg32WordPtrReg()
{
    __asm {
        pushfd
        pushad
    }

    asmNextInstr[0] = nextInstr;
    asmNextInstr[1] = nextInstr2;
    jmpDest = asmNextInstr;
    asmJmpAddress = jmpAddress;
    ((uint8_t*)asmNextInstr)[nextInstrSize] = 0xFF;
    ((uint8_t*)asmNextInstr)[nextInstrSize + 1] = 0x25;
    *((uint32_t**)((uint8_t*)asmNextInstr + nextInstrSize + 2)) = &asmJmpAddress;

    __asm {
        popad
        pushad
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
        push eax
        call getVariationOriginalModel
        mov asmModel32, eax
        popad
        popfd
    }

    if constexpr (target == REG_EAX) { __asm { mov eax, asmModel32 } }
    else if constexpr (target == REG_ECX) { __asm { mov ecx, asmModel32 } }
    else if constexpr (target == REG_EDX) { __asm { mov edx, asmModel32 } }
    else if constexpr (target == REG_EBX) { __asm { mov ebx, asmModel32 } }
    else if constexpr (target == REG_ESP) { __asm { mov esp, asmModel32 } }
    else if constexpr (target == REG_EBP) { __asm { mov ebp, asmModel32 } }
    else if constexpr (target == REG_ESI) { __asm { mov esi, asmModel32 } }
    else if constexpr (target == REG_EDI) { __asm { mov edi, asmModel32 } }

    __asm {
        jmp jmpDest
    }
}

void hookASM(std::uintptr_t address, std::string_view originalData, injector::memory_pointer_raw hookDest, std::string_view funcName)
{
    int numBytes = (int)originalData.size()/3+1;
    
    if (!memcmp(address, originalData.data()) && forceEnable == false)
    {
        std::string moduleName;

        std::stringstream ss;
        const unsigned char* c = reinterpret_cast<unsigned char*>(address);

        for (int j = 0; j < numBytes; j++)
            ss << std::setfill('0') << std::setw(2) << std::uppercase << std::hex << static_cast<unsigned int>(c[j]) << " ";

        std::string bytes = ss.str();
        moduleName = LoadedModules::GetModuleAtAddress(injector::GetBranchDestination(address).as_int()).first;

        if (funcName.find("::") != std::string::npos)
            Log::LogModifiedAddress(address, "Modified method detected: %s - 0x%08X is %s %s\n", funcName.data(), address, bytes.c_str(), getFilenameFromPath(moduleName).c_str());
        else
            Log::LogModifiedAddress(address, "Modified function detected: %s - 0x%08X is %s %s\n", funcName.data(), address, bytes.c_str(), getFilenameFromPath(moduleName).c_str());
           
        return;
    }

    injector::MakeJMP(address, hookDest);
}

void VehicleVariations::InstallHooks()
{
    Events::initScriptsEvent.after += []
    {
        if (loadAllVehicles)
            loadModels(400, 611, KEEP_IN_MEMORY, false);
    };

    hookCall(0x43022A, ChooseModelHooked<0x43022A>, "CCarCtrl::ChooseModel"); //CCarCtrl::GenerateOneRandomCar

    hookCall(0x42C320, ChoosePoliceCarModelHooked<0x42C320>, "CCarCtrl::ChoosePoliceCarModel"); //CCarCtrl::CreatePoliceChase
    hookCall(0x43020E, ChoosePoliceCarModelHooked<0x43020E>, "CCarCtrl::ChoosePoliceCarModel"); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x430283, ChoosePoliceCarModelHooked<0x430283>, "CCarCtrl::ChoosePoliceCarModel"); //CCarCtrl::GenerateOneRandomCar

/*****************************************************************************************************/

    hookCall(0x42BC26, AddPoliceCarOccupantsHooked<0x42BC26>, "CCarAI::AddPoliceCarOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x42C620, AddPoliceCarOccupantsHooked<0x42C620>, "CCarAI::AddPoliceCarOccupants"); //CCarCtrl::CreatePoliceChase
    hookCall(0x431EE5, AddPoliceCarOccupantsHooked<0x431EE5>, "CCarAI::AddPoliceCarOccupants"); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x499CBB, AddPoliceCarOccupantsHooked<0x499CBB>, "CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update
    hookCall(0x499D6A, AddPoliceCarOccupantsHooked<0x499D6A>, "CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update
    hookCall(0x49A5EB, AddPoliceCarOccupantsHooked<0x49A5EB>, "CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update
    hookCall(0x49A85E, AddPoliceCarOccupantsHooked<0x49A85E>, "CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update
    hookCall(0x49A9AF, AddPoliceCarOccupantsHooked<0x49A9AF>, "CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update

/*****************************************************************************************************/
    
    hookCall(0x42B909, CAutomobileHooked<0x42B909>, "CAutomobile::CAutomobile"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x462217, CAutomobileHooked<0x462217>, "CAutomobile::CAutomobile"); //CRoadBlocks::CreateRoadBlockBetween2Points
    hookCall(0x4998F0, CAutomobileHooked<0x4998F0>, "CAutomobile::CAutomobile"); //CSetPiece::TryToGenerateCopCar
    hookCall(0x61354A, CAutomobileHooked<0x61354A>, "CAutomobile::CAutomobile"); //CPopulation::CreateWaitingCoppers

    hookCall(0x6F3583, PickRandomCarHooked<0x6F3583>, "CLoadedCarGroup::PickRandomCar"); //CCarGenerator::DoInternalProcessing
    hookCall(0x6F3EC1, DoInternalProcessingHooked<0x6F3EC1>, "CCarGenerator::DoInternalProcessing"); //CCarGenerator::Process 

    //Trains
    //patch::RedirectCall(0x4214DC, CTrainHooked); //CCarCtrl::GetNewVehicleDependingOnCarModel
    //patch::RedirectCall(0x5D2B15, CTrainHooked); //CPools::LoadVehiclePool
    hookCall(0x6F7634, CTrainHooked<0x6F7634>, "CTrain::CTrain"); //CTrain::CreateMissionTrain 

    hookASM(0x64475D, "66 81 78 22 3A 02", cmpWordPtrRegModel<REG_EAX, 0x644763, 0x23A>, "CTaskSimpleCarDrive::ProcessPed");
    hookASM(0x6F60D9, "66 81 7E 22 3A 02", cmpWordPtrRegModel<REG_ESI, 0x6F60DF, 0x23A>, "CTrain::CTrain");
    hookASM(0x6F6576, "66 81 7F 22 3A 02", cmpWordPtrRegModel<REG_EDI, 0x6F657C, 0x23A>, "CTrain::OpenDoor");
    hookASM(0x6F8E8A, "66 81 7E 22 3A 02", cmpWordPtrRegModel<REG_ESI, 0x6F8E90, 0x23A>, "CTrain::ProcessControl");
    hookASM(0x613A68, "0F BF 47 22 05",    movsxReg32WordPtrReg<REG_EAX, REG_EDI, 0x613A71, 5, 0xFFFE6905, 0x909090FF>, "CPopulation::AddPedInCar");

    //Boats
    hookCall(0x42149E, CBoatHooked<0x42149E>, "CBoat::CBoat"); //CCarCtrl::GetNewVehicleDependingOnCarModel
    hookCall(0x431FD0, CBoatHooked<0x431FD0>, "CBoat::CBoat"); //CCarCtrl::CreateCarForScript
    hookCall(0x5D2ADC, CBoatHooked<0x5D2ADC>, "CBoat::CBoat"); //CPools::LoadVehiclePool

    //Helis
    hookCall(0x6CD3C3, CHeliHooked<0x6CD3C3>, "CHeli::CHeli"); //CPlane::DoPlaneGenerationAndRemoval
    hookCall(0x6C6590, CHeliHooked<0x6C6590>, "CHeli::CHeli"); //CHeli::GenerateHeli
    hookCall(0x6C6568, CHeliHooked<0x6C6568>, "CHeli::CHeli"); //CHeli::GenerateHeli
    hookCall(0x5D2C46, CHeliHooked<0x5D2C46>, "CHeli::CHeli"); //CPools::LoadVehiclePool
    hookCall(0x6C7ACA, GenerateHeliHooked<0x6C7ACA>, "CHeli::GenerateHeli"); //CHeli::UpdateHelis

    hookCall(0x6CD6D6, CPlaneHooked<0x6CD6D6>, "CPlane::CPlane"); //CPlane::DoPlaneGenerationAndRemoval
    hookCall(0x42166F, CPlaneHooked<0x42166F>, "CPlane::CPlane"); //CCarCtrl::GetNewVehicleDependingOnCarModel

    //Roadblocks
    hookCall(0x42CDDD, IsLawEnforcementVehicleHooked<0x42CDDD>, "CVehicle::IsLawEnforcementVehicle"); //CCarCtrl::RemoveDistantCars
    hookCall(0x42CE07, GenerateRoadBlockCopsForCarHooked<0x42CE07>, "CRoadBlocks::GenerateRoadBlockCopsForCar"); //CCarCtrl::RemoveDistantCars
    hookCall(0x4613EB, GetColModelHooked<0x4613EB>, "CEntity::GetColModel"); //CCarCtrl::RemoveDistantCars
    hookCall(0x46151A, CCopPedHooked<0x46151A>, "CCopPed::CCopPed"); //CRoadBlocks::GenerateRoadBlockCopsForCar
    hookCall(0x461541, CCopPedHooked<0x461541>, "CCopPed::CCopPed"); //CRoadBlocks::GenerateRoadBlockCopsForCar

    hookCall(0x42BBFB, AddAmbulanceOccupantsHooked<0x42BBFB>, "CCarAI::AddAmbulanceOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookCall(0x42BC1A, AddFiretruckOccupantsHooked<0x42BC1A>, "CCarAI::AddFiretruckOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar

    hookCall(0x613A43, FindSpecificDriverModelForCar_ToUseHooked<0x613A43>, "CPopulation::FindSpecificDriverModelForCar_ToUse"); //CPopulation::AddPedInCar
    hookCall(0x6D1B0E, AddPedInCarHooked<0x6D1B0E>, "CPopulation::AddPedInCar"); //CVehicle::SetupPassenger 
    hookCall(0x6F6986, AddPedInCarHooked<0x6F6986>, "CPopulation::AddPedInCar"); //CTrain::RemoveRandomPassenger

    hookCall(0x431DE2, SetUpDriverAndPassengersForVehicleHooked<0x431DE2>, "CCarCtrl::SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x431DF9, SetUpDriverAndPassengersForVehicleHooked<0x431DF9>, "CCarCtrl::SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar
    hookCall(0x431ED1, SetUpDriverAndPassengersForVehicleHooked<0x431ED1>, "CCarCtrl::SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar

    hookCall(0x6B11C2, IsLawEnforcementVehicleHooked<0x6B11C2>, "CVehicle::IsLawEnforcementVehicle"); //CAutomobile::CAutomobile

    hookCall(0x60C4E8, PossiblyRemoveVehicleHooked<0x60C4E8>, "CCarCtrl::PossiblyRemoveVehicle"); //CPlayerPed::KeepAreaAroundPlayerClear
    hookCall(0x42CD55, PossiblyRemoveVehicleHooked<0x42CD55>, "CCarCtrl::PossiblyRemoveVehicle"); //CCarCtrl::RemoveDistantCars

    hookCall(0x64BB57, SetDriverHooked<0x64BB57>, "CVehicle::SetDriver"); //CTaskSimpleCarSetPedInAsDriver::ProcessPed

    hookCall(0x871164, CAutomobile__PreRenderHooked<0x871164>, "CAutomobile::PreRender", true);
    hookCall(0x6CFADC, CAutomobile__PreRenderHooked<0x6CFADC>, "CAutomobile::PreRender"); //CTrailer::PreRender

    if (changeScriptedCars)
        hookCall(0x467B01, CreateCarForScriptHooked<0x467B01>, "CCarCtrl::CreateCarForScript");
    
    if (enableSpecialFeatures)
    {
        hookCall(0x871148, ProcessControlHooked<0x871148>, "CAutomobile::ProcessControl", true);
        hookCall(0x6C7059, ProcessControlHooked<0x6C7059>, "CAutomobile::ProcessControl"); //CHeli::ProcessControl
        hookCall(0x6C82B4, ProcessControlHooked<0x6C82B4>, "CAutomobile::ProcessControl"); //CMonsterTruck::ProcessControl
        hookCall(0x6C9313, ProcessControlHooked<0x6C9313>, "CAutomobile::ProcessControl"); //CPlane::ProcessControl
        hookCall(0x6CE005, ProcessControlHooked<0x6CE005>, "CAutomobile::ProcessControl"); //CQuadBike::ProcessControl
        hookCall(0x6CED23, ProcessControlHooked<0x6CED23>, "CAutomobile::ProcessControl"); //CTrailer::ProcessControl
        hookCall(0x871214, SetTowLinkHooked<0x871214>, "CAutomobile::SetTowLink", true);
        hookCall(0x871D14, GetTowHitchPosHooked<0x871D14>, "CTrailer::GetTowHitchPos", true);

        hookCall(0x6B3271, UpdateTrailerLinkHooked<0x6B3271>, "CVehicle::UpdateTrailerLink");
        hookCall(0x6B329C, UpdateTrailerLinkHooked<0x6B329C>, "CVehicle::UpdateTrailerLink");
        hookCall(0x6B45C7, UpdateTrailerLinkHooked<0x6B45C7>, "CVehicle::UpdateTrailerLink");

        hookCall(0x6B3266, UpdateTractorLinkHooked<0x6B3266>, "CVehicle::UpdateTractorLink");
        hookCall(0x6B3291, UpdateTractorLinkHooked<0x6B3291>, "CVehicle::UpdateTractorLink");
        hookCall(0x6CFFAC, UpdateTractorLinkHooked<0x6CFFAC>, "CVehicle::UpdateTractorLink");

        hookCall(0x8711CC, SetUpWheelColModelHooked<0x8711CC>, "CAutomobile::SetUpWheelColModel", true);
        hookCall(0x871B94, SetUpWheelColModelHooked<0x871B94>, "CAutomobile::SetUpWheelColModel", true);
        hookCall(0x871CD4, SetUpWheelColModelHooked<0x871CD4>, "CAutomobile::SetUpWheelColModel", true);
     
        hookASM(0x525462, "66 8B 47 22 66 3D BB 01",          movReg16WordPtrReg<REG_AX, REG_EDI, 0x52546A, 4, 0x01BB3D66>, "CCam::Process_FollowCar_SA");
        hookASM(0x431BEB, "66 8B 46 22 83 C4 04",             movReg16WordPtrReg<REG_AX, REG_ESI, 0x431BF2, 3, 0x9004C483>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x64467D, "66 81 78 22 13 02",                cmpWordPtrRegModel<REG_EAX, 0x644683, 0x213>, "CTaskSimpleCarDrive::ProcessPed");
        hookASM(0x51E5B8, "66 81 7E 22 B0 01",                cmpWordPtrRegModel<REG_ESI, 0x51E5BE, 0x1B0>, "CCamera::TryToStartNewCamMode");
        hookASM(0x6B4CE8, "66 8B 4E 22 66 81 F9 1B 02",       movReg16WordPtrReg<REG_CX, REG_ESI, 0x6B4CF1, 5, 0x1BF98166, 0x90909002>, "CAutomobile::ProcessAI");
        hookASM(0x5A0EAF, "66 81 78 22 59 02",                cmpWordPtrRegModel<REG_EAX, 0x5A0EB5, 0x259>, "CObject::ObjectDamage");
        hookASM(0x4308A1, "66 81 7E 22 A7 01",                cmpWordPtrRegModel<REG_ESI, 0x4308A7, 0x1A7>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x4F62E4, "66 81 78 22 A7 01",                cmpWordPtrRegModel<REG_EAX, 0x4F62EA, 0x1A7>, "CAEVehicleAudioEntity::GetSirenState");
        hookASM(0x4F9CBC, "66 81 79 22 A7 01",                cmpWordPtrRegModel<REG_ECX, 0x4F9CC2, 0x1A7>, "CAEVehicleAudioEntity::PlayHornOrSiren");
        hookASM(0x44AB2A, "66 81 7F 22 A7 01",                cmpWordPtrRegModel<REG_EDI, 0x44AB30, 0x1A7>, "CGarage::Update");
        hookASM(0x52AE34, "66 81 78 22 A7 01",                cmpWordPtrRegModel<REG_EAX, 0x52AE3A, 0x1A7>, "CCamera::CamControl");
        hookASM(0x4FB26B, "66 8B 41 22 66 3D BB 01",          movReg16WordPtrReg<REG_AX, REG_ECX, 0x4FB273, 4, 0x01BB3D66>, "CAEVehicleAudioEntity::ProcessMovingParts");
        hookASM(0x54742F, "66 8B 4F 22 66 81 F9 96 01",       movReg16WordPtrReg<REG_CX, REG_EDI, 0x547438, 5, 0x96F98166, 0x90909001>, "CPhysical::PositionAttachedEntity");
        hookASM(0x5A0052, "66 8B 46 22 66 3D 96 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x5A005A, 4, 0x01963D66>, "CObject::SpecialEntityPreCollisionStuff");
        hookASM(0x5A21C9, "66 8B 48 22 66 81 F9 96 01",       movReg16WordPtrReg<REG_CX, REG_EAX, 0x5A21D2, 5, 0x96F98166, 0x90909001>, "CObject::ProcessControl");
        hookASM(0x6A1480, "66 8B 4F 22 33 F6",                movReg16WordPtrReg<REG_CX, REG_EDI, 0x6A1486, 2, 0x9090F633>, "CAutomobile::UpdateMovingCollision");
        hookASM(0x6A173B, "66 8B 47 22 66 3D E6 01",          movReg16WordPtrReg<REG_AX, REG_EDI, 0x6A1743, 4, 0x01E63D66>, "CAutomobile::UpdateMovingCollision");
        hookASM(0x6A1F69, "66 8B 4E 22 66 81 F9 96 01",       movReg16WordPtrReg<REG_CX, REG_ESI, 0x6A1F72, 5, 0x96F98166, 0x90909001>, "CAutomobile::AddMovingCollisionSpeed");
        hookASM(0x6A2162, "66 8B 41 22 66 3D 96 01",          movReg16WordPtrReg<REG_AX, REG_ECX, 0x6A216A, 4, 0x01963D66>, "CAutomobile::GetMovingCollisionOffset");
        hookASM(0x6C7F30, "66 81 7E 22 96 01",                cmpWordPtrRegModel<REG_ESI, 0x6C7F36, 0x196>, "CMonsterTruck::PreRender");
        hookASM(0x5470BF, "66 81 79 22 12 02",                cmpWordPtrRegModel<REG_ECX, 0x5470C5, 0x212>, "CPhysical::PositionAttachedEntity");
        hookASM(0x54D70D, "66 81 7F 22 12 02",                cmpWordPtrRegModel<REG_EDI, 0x54D713, 0x212>, "CPhysical::AttachEntityToEntity");
        hookASM(0x5A0EBF, "66 81 7F 22 12 02",                cmpWordPtrRegModel<REG_EDI, 0x5A0EC5, 0x212>, "CObject::ObjectDamage");
        hookASM(0x6A1648, "66 81 7F 22 12 02",                cmpWordPtrRegModel<REG_EDI, 0x6A164E, 0x212>, "CAutomobile::UpdateMovingCollision");
        hookASM(0x6AD378, "66 81 7E 22 12 02",                cmpWordPtrRegModel<REG_ESI, 0x6AD37E, 0x212>, "CAutomobile::ProcessEntityCollision");
        hookASM(0x6E0FF8, "66 81 7F 22 12 02",                cmpWordPtrRegModel<REG_EDI, 0x6E0FFE, 0x212>, "CVehicle::DoHeadLightBeam");
        hookASM(0x43064C, "81 FF AF 01 00 00",                cmpReg32Model<REG_EDI, 0x430652, 0x1AF>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x64BCB3, "66 81 78 22 AF 01",                cmpWordPtrRegModel<REG_EAX, 0x64BCB9, 0x1AF>, "CTaskSimpleCarSetPedInAsDriver::ProcessPed");
        hookASM(0x430640, "81 FF B5 01 00 00",                cmpReg32Model<REG_EDI, 0x430646, 0x1B5>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x6A155C, "66 8B 47 22 66 3D 0C 02",          patch6A155C, "CAutomobile::UpdateMovingCollision");
        hookASM(0x502222, "66 81 78 22 14 02",                cmpWordPtrRegModel<REG_EAX, 0x502228, 0x214>, "CAEVehicleAudioEntity::ProcessVehicle");
        hookASM(0x6AA515, "66 8B 4E 22 66 81 F9 14 02",       movReg16WordPtrReg<REG_CX, REG_ESI, 0x6AA51E, 5, 0x14F98166, 0x90909002>, "CAutomobile::UpdateWheelMatrix");
        hookASM(0x6D1ABA, "66 8B 47 22 32 D2",                movReg16WordPtrReg<REG_AX, REG_EDI, 0x6D1AC0, 2, 0x9090D232 >, "CVehicle::SetupPassenger");
        hookASM(0x6C926D, "66 8B 46 22 66 3D 00 02",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C9275, 4, 0x02003D66>, "CPlane::ProcessControl");
        hookASM(0x6CA945, "66 8B 46 22 66 3D 00 02",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6CA94D, 4, 0x02003D66>, "CPlane::PreRender");
        hookASM(0x6CACF0, "66 81 7E 22 01 02",                cmpWordPtrRegModel<REG_ESI, 0x6CACF6, 0x201>, "CPlane::OpenDoor");
        hookASM(0x6D67B7, "66 8B 46 22 66 3D 96 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6D67BF, 4, 0x01963D66>, "CVehicle::SpecialEntityPreCollisionStuff");
        hookASM(0x6B0F47, "66 8B 46 22 D9 05 38 8B 85 00",    movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B0F51, 6, 0x8B3805D9, 0x90900085>, "CAutomobile::CAutomobile");
        hookASM(0x6B0CF0, "66 81 7E 22 B0 01",                cmpWordPtrRegModel<REG_ESI, 0x6B0CF6, 0x1B0>, "CAutomobile::CAutomobile");
        hookASM(0x6B0EE2, "66 8B 46 22 66 3D 0D 02",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B0EEA, 4, 0x020D3D66>, "CAutomobile::CAutomobile");
        hookASM(0x6B11D5, "66 8B 46 22 66 3D FE FF",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B11DD, 4, 0xFFFE3D66>, "CAutomobile::CAutomobile");
        hookASM(0x6B0298, "66 81 7E 22 B0 01",                cmpWordPtrRegModel<REG_ESI, 0x6B029E, 0x1B0>, "CAutomobile::ProcessSuspension");
        hookASM(0x6AFB44, "66 81 7E 22 B0 01",                cmpWordPtrRegModel<REG_ESI, 0x6AFB4A, 0x1B0>, "CAutomobile::ProcessSuspension");
        hookASM(0x51D870, "66 81 78 22 B0 01",                cmpWordPtrRegModel<REG_EAX, 0x51D876, 0x1B0>, "sub_51D770");
        hookASM(0x527058, "66 81 78 22 08 02",                cmpWordPtrRegModel<REG_EAX, 0x52705E, 0x208>, "CCam::Process");
        hookASM(0x58E09F, "66 81 78 22 08 02",                cmpWordPtrRegModel<REG_EAX, 0x58E0A5, 0x208>, "CHud::DrawCrossHairs");
        hookASM(0x58E0B3, "66 81 78 22 A9 01",                cmpWordPtrRegModel<REG_EAX, 0x58E0B9, 0x1A9>, "CHud::DrawCrossHairs");
        hookASM(0x6A53BA, "3D 08 02 00 00",                   cmpReg32Model<REG_EAX, 0x6A53BF, 0x208>, "CAutomobile::ProcessCarWheelPair");
        hookASM(0x6C8F10, "81 FF 08 02 00 00",                cmpReg32Model<REG_EDI, 0x6C8F16, 0x208>, "CPlane::CPlane");
        hookASM(0x6C9101, "66 81 7E 22 08 02",                cmpWordPtrRegModel<REG_ESI, 0x6C9107, 0x208>, "CPlane::CPlane");
        hookASM(0x6C968E, "66 81 7E 22 08 02",                cmpWordPtrRegModel<REG_ESI, 0x6C9694, 0x208>, "CPlane::PreRender");
        hookASM(0x6C9D7E, "0F BF 46 22 05 24 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6C9D87, 5, 0xFFFE2405, 0x909090FF>, "CPlane::PreRender");
        hookASM(0x6C9EE3, "66 8B 46 22 66 3D 50 02",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C9EEB, 4, 0x02503D66>, "CPlane::PreRender");
        hookASM(0x6CC318, "66 8B 4E 22 66 81 F9 D0 01",       movReg16WordPtrReg<REG_CX, REG_ESI, 0x6CC321, 5, 0xD0F98166, 0x90909001>, "CPlane::ProcessFlyingCarStuff");
        hookASM(0x6D8EFE, "66 8B 5E 22 D9 84 24 74 02 00 00", movReg16WordPtrReg<REG_BX, REG_ESI, 0x6D8F09, 7, 0x742484D9, 0x90000002>, "CVehicle::FlyingControl");
        hookASM(0x6D9C04, "66 81 7E 22 08 02",                cmpWordPtrRegModel<REG_ESI, 0x6D9C0A, 0x208>, "CVehicle::FlyingControl");
        hookASM(0x6E3457, "0F BF 46 22 05 57 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6E3460, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneWeaponFiringStatus");
        hookASM(0x6D4D5E, "0F BF 46 22 05 57 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6D4D67, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::FirePlaneGuns");
        hookASM(0x6D3F30, "0F BF 41 22 05 57 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D3F39, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneNumGuns");
        hookASM(0x6D4125, "0F BF 41 22 05 57 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D412E, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneGunsRateOfFire");
        hookASM(0x6D514F, "0F BF 46 22 2D A9 01 00 00",       movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6D5158, 5, 0x0001A92D, 0x90909000>, "CVehicle::FireUnguidedMissile");
        hookASM(0x6D45D5, "0F BF 41 22 05 57 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D45DE, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneOrdnanceRateOfFire");
        hookASM(0x6D3E00, "0F BF 41 22 05 57 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D3E09, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneGunsAutoAimAngle");
        hookASM(0x501C73, "0F BF 42 22 05 F9 FD FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_EDX, 0x501C7C, 5, 0xFFFDF905, 0x909090FF>, "CAEVehicleAudioEntity::ProcessAircraft");
        hookASM(0x4FF980, "0F BF 40 22 05 F9 FD FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_EAX, 0x4FF989, 5, 0xFFFDF905, 0x909090FF>, "CAEVehicleAudioEntity::ProcessGenericJet");
        hookASM(0x524624, "66 8B 47 22 66 3D B9 01",          movReg16WordPtrReg<REG_AX, REG_EDI, 0x52462C, 4, 0x01B93D66>, "CCam::Process_FollowCar_SA");
        hookASM(0x4F7814, "0F BF 42 22 05 40 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_EDX, 0x4F781D, 5, 0xFFFE4005, 0x909090FF>, "CAEVehicleAudioEntity::Initialise");
        hookASM(0x4FB343, "0F BF 42 22 05 6A FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_EDX, 0x4FB34C, 5, 0xFFFE6A05, 0x909090FF>, "CAEVehicleAudioEntity::ProcessMovingParts");
        hookASM(0x426F94, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x426F9A, 0x21B>, "CCarCtrl::PickNextNodeToChaseCar");
        hookASM(0x427790, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x427796, 0x21B>, "CCarCtrl::PickNextNodeToFollowPath");
        hookASM(0x42DB2E, "66 81 7F 22 1B 02",                cmpWordPtrRegModel<REG_EDI, 0x42DB34, 0x21B>, "CCarCtrl::IsThisAnAppropriateNode");
        
        if (GetGameVersion() == GAME_10US_COMPACT)
            hookASM(0x42F8A7, "66 81 7E 22 1B 02",            cmpWordPtrRegModel<REG_ESI, 0x42F8AD, 0x21B>, "CCarCtrl::JoinCarWithRoadSystem");
        else
            hookASM(0x156A4D7, "66 81 7E 22 1B 02",           cmpWordPtrRegModel<REG_ESI, 0x156A4DD, 0x21B>, "CCarCtrl::JoinCarWithRoadSystem");

        hookASM(0x42FE50, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x42FE56, 0x21B>, "CCarCtrl::ReconsiderRoute");
        hookASM(0x42FF0B, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x42FF11, 0x21B>, "CCarCtrl::ReconsiderRoute");
        hookASM(0x435A81, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x435A87, 0x21B>, "CCarCtrl::SteerAICarWithPhysicsFollowPath_Racing");
        hookASM(0x4382A4, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x4382AA, 0x21B>, "CCarCtrl::SteerAICarWithPhysics");
        hookASM(0x5583B3, "66 8B 46 22 66 3D D9 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x5583BB, 4, 0x01D93D66>, "CRope::Update");
        hookASM(0x5707FD, "66 8B 43 22 66 3D CC 01",          movReg16WordPtrReg<REG_AX, REG_EBX, 0x570805, 4, 0x01CC3D66>, "CPlayerInfo::Process");
        hookASM(0x5869FF, "66 81 78 22 1B 02",                cmpWordPtrRegModel<REG_EAX, 0x586A05, 0x21B>, "CRadar::DrawRadarMap");
        hookASM(0x586B77, "66 81 78 22 1B 02",                cmpWordPtrRegModel<REG_EAX, 0x586B7D, 0x21B>, "CRadar::DrawMap");
        hookASM(0x587D66, "66 81 78 22 1B 02",                cmpWordPtrRegModel<REG_EAX, 0x587D6C, 0x21B>, "CRadar::SetupAirstripBlips");
        hookASM(0x588570, "83 C4 08 66 39 58 22",             patch588570, "CRadar::DrawBlips");
        hookASM(0x58A3D7, "66 81 78 22 1B 02",                cmpWordPtrRegModel<REG_EAX, 0x58A3DD, 0x21B>, "CHud::DrawRadar");
        hookASM(0x58A5A0, "66 81 78 22 1B 02",                cmpWordPtrRegModel<REG_EAX, 0x58A5A6, 0x21B>, "CHud::DrawRadar");
        hookASM(0x643BE5, "66 8B 48 22 66 81 F9 CC 01",       movReg16WordPtrReg<REG_CX, REG_EAX, 0x643BEE, 5, 0xCCF98166, 0x90909001>, "CTaskComplexEnterCar::CreateFirstSubTask");
        hookASM(0x6508ED, "66 8B 76 22 66 81 FE CC 01",       movReg16WordPtrReg<REG_SI, REG_ESI, 0x6508F6, 5, 0xCCFE8166, 0x90909001>, "IsRoomForPedToLeaveCar");
        hookASM(0x6A8D4E, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6A8D54, 0x21B>, "CAutomobile::ProcessBuoyancy");
        hookASM(0x6A8F18, "66 8B 4E 22 66 81 F9 BF 01",       movReg16WordPtrReg<REG_CX, REG_ESI, 0X6A8F21, 5, 0xBFF98166, 0x90909001>, "CAutomobile::ProcessBuoyancy");
        hookASM(0x6AA72D, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6AA733, 0x21B>, "CAutomobile::UpdateWheelMatrix");
        hookASM(0x6AFFEA, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6AFFF0, 0x21B>, "CAutomobile::ProcessSuspension");
        hookASM(0x6B0017, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6B001D, 0x21B>, "CAutomobile::ProcessSuspension");
        hookASM(0x6C8E54, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6C8E5A, 0x21B>, "CPlane::CPlane");
        hookASM(0x6C934D, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6C9353, 0x21B>, "CPlane::ProcessControl");
        hookASM(0x6C94FB, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6C9501, 0x21B>, "CPlane::PreRender");
        hookASM(0x6C97E6, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6C97EC, 0x21B>, "CPlane::PreRender");
        hookASM(0x6CA70E, "66 8B 46 22 D9 5C 24 34",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6CA716, 4, 0x34245CD9>, "CPlane::PreRender");
        hookASM(0x6CA750, "66 8B 46 22 66 3D 1B 02",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6CA758, 4, 0x021B3D66>, "CPlane::PreRender");
        hookASM(0x6CC4C4, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6CC4CA, 0x21B>, "CPlane::VehicleDamage");
        hookASM(0x6D8E18, "66 8B 5E 22 66 81 FB 1B 02",       movReg16WordPtrReg<REG_BX, REG_ESI, 0x6D8E21, 5, 0x1BFB8166, 0x90909002>, "CVehicle::FlyingControl");
        hookASM(0x6D9233, "66 81 7E 22 1B 02",                cmpWordPtrRegModel<REG_ESI, 0x6D9239, 0x21B>, "CVehicle::FlyingControl");
        hookASM(0x70BF09, "0F BF 5F 22 DD D8",                movsxReg32WordPtrReg<REG_EBX, REG_EDI, 0x70BF0F, 2, 0x9090D8DD>, "CShadows::StoreShadowForVehicle");
        hookASM(0x501AB9, "0F BF 40 22 05 4D FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_EAX, 0x501AC2, 5, 0xFFFE4D05, 0x909090FF>, "CAEVehicleAudioEntity::ProcessSpecialVehicle");
        hookASM(0x6C41D9, "81 FF A9 01 00 00",                cmpReg32Model<REG_EDI, 0x6C41DF, 0x1A9>, "CHeli::CHeli");
        hookASM(0x6C50B3, "66 8B 46 22 66 3D D1 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C50BB, 4, 0x01D13D66>, "CHeli::ProcessFlyingCarStuff");
        hookASM(0x6D4900, "0F BF 41 22 05 57 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D4909, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::SelectPlaneWeapon");
        hookASM(0x6E1C17, "66 81 7E 22 DD 01",                cmpWordPtrRegModel<REG_ESI, 0x6E1C1D, 0x1DD>, "CVehicle::DoVehicleLights");
        hookASM(0x6C4F66, "66 8B 46 22 66 3D BF 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C4F6E, 4, 0x01BF3D66>, "CHeli::ProcessFlyingCarStuff");
        hookASM(0x6C5605, "66 8B 4E 22 66 81 F9 D5 01",       movReg16WordPtrReg<REG_CX, REG_ESI, 0x6C560E, 5, 0xD5F98166, 0x90909001>, "CHeli::PreRender");
        hookASM(0x7408E3, "66 8B 47 22 66 3D BF 01",          movReg16WordPtrReg<REG_AX, REG_EDI, 0x7408EB, 4, 0x01D53D66>, "CWeapon::FireInstantHit");
        hookASM(0x6A8DE2, "66 8B 46 22 66 3D BF 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6A8DEA, 4, 0x01BF3D66>, "CAutomobile::ProcessBuoyancy");
        hookASM(0x6F367E, "81 FD BF 01 00 00",                cmpReg32Model<REG_EBP, 0x6F3684, 0x1BF>, "CCarGenerator::DoInternalProcessing");
        hookASM(0x51D864, "66 81 7A 22 CC 01",                cmpWordPtrRegModel<REG_EDX, 0x51D86A, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51D92B, "66 81 7A 22 CC 01",                cmpWordPtrRegModel<REG_EDX, 0x51D931, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51DA60, "66 81 79 22 CC 01",                cmpWordPtrRegModel<REG_ECX, 0x51DA66, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51DCFC, "66 81 78 22 CC 01",                cmpWordPtrRegModel<REG_EAX, 0x51DD02, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51DE84, "66 81 78 22 CC 01",                cmpWordPtrRegModel<REG_EAX, 0x51DE8A, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51E5AC, "66 81 78 22 CC 01",                cmpWordPtrRegModel<REG_EAX, 0x51E5B2, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x51E773, "66 81 79 22 CC 01",                cmpWordPtrRegModel<REG_ECX, 0x51E779, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x51E937, "66 81 78 22 CC 01",                cmpWordPtrRegModel<REG_EAX, 0x51E93D, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x51EF39, "66 81 7A 22 CC 01",                cmpWordPtrRegModel<REG_EDX, 0x51EF3F, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x51F15B, "66 81 79 22 CC 01",                cmpWordPtrRegModel<REG_ECX, 0x51F161, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x55432A, "66 8B 46 22 66 3D B0 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x554332, 4, 0x01B03D66>, "CRenderer::SetupEntityVisibility");
        hookASM(0x6C2D33, "66 8B 47 22 66 3D A1 01",          movReg16WordPtrReg<REG_AX, REG_EDI, 0x6C2D3B, 4, 0x01A13D66>, "cBuoyancy::PreCalcSetup");
        hookASM(0x6C92EC, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6C92F2, 0x1CC>, "CPlane::ProcessControl");
        hookASM(0x6CAA93, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6CAA99, 0x1CC>, "CPlane::PreRender");
        hookASM(0x6D274C, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6D2752, 0x1CC>, "CVehicle::ApplyBoatWaterResistance");
        hookASM(0x6DBF0A, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6DBF10, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6DC00B, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6DC011, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6DC21B, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6DC221, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6DC621, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6DC627, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6DCD63, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6DCD69, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6EDA0C, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6EDA12, 0x1CC>, "CWaterLevel::RenderBoatWakes");
        hookASM(0x6F0234, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6F023A, 0x1CC>, "CBoat::Render");
        hookASM(0x6F1A8A, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6F1A90, 0x1CC>, "CBoat::ProcessControl");
        hookASM(0x6F1F5B, "66 81 7E 22 CC 01",                cmpWordPtrRegModel<REG_ESI, 0x6F1F61, 0x1CC>, "CBoat::ProcessControl");
        hookASM(0x6F3672, "81 FD CC 01 00 00",                cmpReg32Model<REG_EBP, 0x6F3678, 0x1CC>, "CCarGenerator::DoInternalProcessing");
        hookASM(0x528294, "66 81 79 22 CC 01",                cmpWordPtrRegModel<REG_ECX, 0x52829A, 0x1CC>, "CCamera::CamControl");
        hookASM(0x6F368A, "81 FD A1 01 00 00",                cmpReg32Model<REG_EBP, 0x6F3690, 0x1A1>, "CCarGenerator::DoInternalProcessing");
        hookASM(0x5626D1, "66 81 7E 22 F1 01",                cmpWordPtrRegModel<REG_ESI, 0x5626D7, 0x1F1>, "CWanted::WorkOutPolicePresence");
        hookASM(0x6C7172, "66 81 7E 22 F1 01",                cmpWordPtrRegModel<REG_ESI, 0x6C7178, 0x1F1>, "CHeli::ProcessControl");
        hookASM(0x6C8F31, "81 FF DC 01 00 00",                cmpReg32Model<REG_EDI, 0x6C8F37, 0x1DC>, "CPlane::CPlane");
        hookASM(0x6C8F3D, "81 FF 00 02 00 00",                cmpReg32Model<REG_EDI, 0x6C8F43, 0x200>, "CPlane::CPlane");
        hookASM(0x6C8F49, "81 FF 07 02 00 00",                cmpReg32Model<REG_EDI, 0x6C8F4F, 0x207>, "CPlane::CPlane");
        hookASM(0x6C8F96, "81 FF 29 02 00 00",                cmpReg32Model<REG_EDI, 0x6C8F9C, 0x229>, "CPlane::CPlane");
        hookASM(0x6C8FCB, "81 FF 1B 02 00 00",                cmpReg32Model<REG_EDI, 0x6C8FD1, 0x21B>, "CPlane::CPlane");
        hookASM(0x6C8FFA, "81 FF 01 02 00 00",                cmpReg32Model<REG_EDI, 0x6C9000, 0x201>, "CPlane::CPlane");

        hookASM((GetGameVersion() != GAME_10US_COMPACT) ? 0x407A15 : 0x6D444EU, "66 81 FA DC 01", cmpReg16Model<REG_DX, 0x6D4453, 0x1DC>, "CVehicle::GetPlaneGunsPosition");

        hookASM(0x6D6A7B, "0F BF 4E 22 88 86 88 04 00 00",    movsxReg32WordPtrReg<REG_ECX, REG_ESI, 0x6D6A85, 6, 0x04888688, 0x90900000>, "CVehicle::SetModelIndex");
        hookASM(0x429051, "66 81 7E 22 AE 01",                cmpWordPtrRegModel<REG_ESI, 0x429057, 0x1AE>, "CCarCtrl::SteerAIBoatWithPhysicsAttackingPlayer");
        hookASM(0x48DA90, "66 81 78 22 AE 01",                cmpWordPtrRegModel<REG_EAX, 0x48DA96, 0x1AE>, "CRunningScript::ProcessCommands1300To1399");
        hookASM(0x512570, "66 81 79 22 AE 01",                cmpWordPtrRegModel<REG_ECX, 0x512576, 0x1AE>, "CCam::Process_WheelCam");
        hookASM(0x6F028D, "0F BF 46 22 05 52 FE FF FF",       movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6F0296, 5, 0xFFFE5205, 0x909090FF>, "CBoat::Render");
        hookASM(0x6F1487, "66 8B 46 22 66 3D AE 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6F148F, 4, 0x01AE3D66>, "CBoat::PreRender");
        hookASM(0x6F1801, "66 81 7E 22 AE 01",                cmpWordPtrRegModel<REG_ESI, 0x6F1807, 0x1AE>, "CBoat::ProcessControl");
        hookASM(0x6F18AD, "66 81 7E 22 AE 01",                cmpWordPtrRegModel<REG_ESI, 0x6F18B3, 0x1AE>, "CBoat::ProcessControl");
        hookASM(0x431C57, "66 81 7E 22 C9 01",                cmpWordPtrRegModel<REG_ESI, 0x431C5D, 0x1C9>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x4F51F6, "66 81 78 22 C9 01",                cmpWordPtrRegModel<REG_EAX, 0x4F51FC, 0x1C9>, "CAEVehicleAudioEntity::GetVolumeForDummyIdle");
        hookASM(0x4F5316, "66 81 78 22 C9 01",                cmpWordPtrRegModel<REG_EAX, 0x4F531C, 0x1C9>, "CAEVehicleAudioEntity::GetFrequencyForDummyIdle");
        hookASM(0x4F5D35, "66 81 7A 22 C9 01",                cmpWordPtrRegModel<REG_EDX, 0x4F5D3B, 0x1C9>, "CAEVehicleAudioEntity::GetVolForPlayerEngineSound");
        hookASM(0x4F8213, "66 81 7A 22 C9 01",                cmpWordPtrRegModel<REG_EDX, 0x4F8219, 0x1C9>, "CAEVehicleAudioEntity::GetFreqForPlayerEngineSound");
        hookASM(0x4F8972, "66 81 79 22 C9 01",                cmpWordPtrRegModel<REG_ECX, 0x4F8978, 0x1C9>, "CAEVehicleAudioEntity::ProcessVehicleFlatTyre");
        hookASM(0x570F72, "66 81 79 22 C9 01",                cmpWordPtrRegModel<REG_ECX, 0x570F78, 0x1C9>, "CPlayerInfo::Process");
        hookASM(0x6D19A3, "3D C9 01 00 00",                   cmpReg32Model<REG_EAX, 0x6D19A8, 0x1C9>, "CVehicle::RemoveDriver");
        hookASM(0x431A99, "66 81 7E 22 E4 01",                cmpWordPtrRegModel<REG_ESI, 0x431A9F, 0x1E4>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x6F13A4, "66 81 7E 22 E4 01",                cmpWordPtrRegModel<REG_ESI, 0x6F13AA, 0x1E4>, "CBoat::PreRender");
        hookASM(0x6F2B7E, "66 81 7E 22 E4 01",                cmpWordPtrRegModel<REG_ESI, 0x6F2B84, 0x1E4>, "CBoat::CBoat");
        hookASM(0x6D03ED, "66 8B 46 22 66 3D 5E 02",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6D03F5, 4, 0x025E3D66>, "CTrailer::CTrailer");
        hookASM(0x6CFD6B, "66 8B 41 22 66 3D 5E 02",          movReg16WordPtrReg<REG_AX, REG_ECX, 0x6CFD73, 4, 0x025E3D66>, "CTrailer::GetTowBarPos");
        hookASM(0x6AF250, "66 8B 41 22 83 EC 0C",             movReg16WordPtrReg<REG_AX, REG_ECX, 0x6AF257, 3, 0x900CEC83>, "CAutomobile::GetTowBarPos");
        hookASM(0x6AF2B6, "66 8B 42 22 66 3D 5E 02",          movReg16WordPtrReg<REG_AX, REG_EDX, 0x6AF2BE, 4, 0x025E3D66>, "CAutomobile::GetTowBarPos");
        hookASM(0x6A845E, "66 81 7E 22 A8 01",                cmpWordPtrRegModel<REG_ESI, 0x6A8464, 0x1A8>, "CAutomobile::VehicleDamage");
        hookASM(0x6B539C, "66 8B 46 22 66 3D B9 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B53A4, 4, 0x01B93D66>, "CAutomobile::ProcessAI");
        hookASM(0x6A6128, "66 81 FE 3B 02",                   cmpReg16Model<REG_SI, 0x6A612D, 0x23B>, "CAutomobile::FindWheelWidth");

        
        MakeInline<0x6D42FE, 6>("CVehicle::GetPlaneGunsPosition", "8D 81 57 FE FF FF", [](injector::reg_pack& regs)
        {
            regs.eax = (uint32_t)getVariationOriginalModel((int)regs.ecx) - 0x1A9;
        });

        MakeInline<0x6AC730>("CAutomobile::PreRender", "A1 10 B9 A9 00", [](injector::reg_pack& regs)
        {
            regs.eax = reinterpret_cast<uint32_t>(CModelInfo::GetModelInfo((int)regs.eax & 0xFFFF));
        });

        MakeInline<0x6D46E5, 11>("CVehicle::GetPlaneOrdnancePosition", "0F BF 79 22 8B 04 BD C8 B0 A9 00", [](injector::reg_pack& regs)
        {
            int modelIndex = reinterpret_cast<CEntity*>(regs.ecx)->m_nModelIndex;

            regs.eax = reinterpret_cast<uint32_t>(CModelInfo::GetModelInfo(modelIndex));
            regs.edi = (uint32_t)getVariationOriginalModel(modelIndex);
        });

        MakeInline<0x729B76U>("CAutomobile::FireTruckControl", (GetGameVersion() != GAME_10US_COMPACT) ? "E9 18 D7 CD FF" : "BB 59 02 00 00", [](injector::reg_pack& regs)
        {
            if (getVariationOriginalModel(reinterpret_cast<CEntity*>(regs.esi)->m_nModelIndex) == 601)
                regs.ebx = reinterpret_cast<CEntity*>(regs.esi)->m_nModelIndex;
        });

        MakeInline<0x6DD218>("CVehicle::DoBoatSplashes", "BF CC 01 00 00", [](injector::reg_pack& regs)
        {
            if (getVariationOriginalModel(reinterpret_cast<CEntity*>(regs.esi)->m_nModelIndex) == 460)
                regs.edi = reinterpret_cast<CEntity*>(regs.esi)->m_nModelIndex;
        });

        MakeInline<0x6CD78B>("CPlane::DoPlaneGenerationAndRemoval", "B8 08 02 00 00", [](injector::reg_pack& regs)
        {
            if (getVariationOriginalModel(CPlane::GenPlane_ModelIndex) == 520)
                regs.eax = (uint32_t)CPlane::GenPlane_ModelIndex;
        });


        hookCall(0x871200, VehicleDamageHooked<0x871200>, "CAutomobile::VehicleDamage", true);
        hookCall(0x8711E0, SetupSuspensionLinesHooked<0x8711E0>, "CAutomobile::SetupSuspensionLines", true);
        hookCall(0x6B119E, SetupSuspensionLinesHooked<0x6B119E>, "CAutomobile::SetupSuspensionLines");
        hookCall(0x8711F0, DoBurstAndSoftGroundRatiosHooked<0x8711F0>, "CAutomobile::DoBurstAndSoftGroundRatios", true);
        hookCall(0x8711D0, BurstTyreHooked<0x8711D0>, "CAutomobile::BurstTyre", true);
        hookCall(0x871168, CAutomobile__RenderHooked<0x871168>, "CAutomobile::Render", true);
        hookCall(0x871178, ProcessEntityCollisionHooked<0x871178>, "CAutomobile::ProcessEntityCollision", true);


        hookCall(0x6B39E6, RegisterCarBlownUpByPlayerHooked<0x6B39E6>, "CDarkel::RegisterCarBlownUpByPlayer"); //CAutomobile::BlowUpCar
        hookCall(0x6B3DEA, RegisterCarBlownUpByPlayerHooked<0x6B3DEA>, "CDarkel::RegisterCarBlownUpByPlayer"); //CAutomobile::BlowUpCarCutSceneNoExtras
        hookCall(0x6E2D14, RegisterCarBlownUpByPlayerHooked<0x6E2D14>, "CDarkel::RegisterCarBlownUpByPlayer"); //CVehicle::~CVehicle

        hookCall(0x6B2028, TankControlHooked<0x6B2028>, "CAutomobile::TankControl"); //CAutomobile::ProcessControl
        hookCall(0x6B51B8, DoSoftGroundResistanceHooked<0x6B51B8>, "CAutomobile::DoSoftGroundResistance"); //CAutomobile::ProcessAI

        hookCall(0x6D6A76, GetMaximumNumberOfPassengersFromNumberOfDoorsHooked<0x6D6A76>, "CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors"); //CVehicle::SetModelIndex

        hookCall(0x6E2730, DoHeadLightReflectionHooked<0x6E2730>, "CVehicle::DoHeadLightReflection"); //CVehicle::DoVehicleLights

        hookCall(0x8719A8, ProcessControlInputsHooked<0x8719A8>, "CPlane::ProcessControlInputs", true);

        hookCall(0x6C5600, DoHeliDustEffectHooked<0x6C5600>, "CAutomobile::DoHeliDustEffect"); //CHeli::PreRender
        hookCall(0x6C9FB7, DoHeliDustEffectHooked<0x6C9FB7>, "CAutomobile::DoHeliDustEffect"); //CPlane::PreRender

        hookCall(0x6C7917, ProcessWeaponsHooked<0x6C7917>, "CVehicle::ProcessWeapons"); //CHeli::ProcessControl
        hookCall(0x6C9348, ProcessWeaponsHooked<0x6C9348>, "CVehicle::ProcessWeapons"); //CPlane::ProcessControl
    }

    if (enableSiren)
        hookCall(0x6D8492, HasCarSiren<0x6D8492>, "HasCarSiren"); //CVehicle::UsesSiren

    if (enableLights)
    {
        hookCall(0x6ABA60, RegisterCoronaHooked<0x6ABA60>, "CCoronas::RegisterCorona"); //CAutomobile::PreRender
        hookCall(0x6ABB35, RegisterCoronaHooked<0x6ABB35>, "CCoronas::RegisterCorona"); //CAutomobile::PreRender
        hookCall(0x6ABC69, RegisterCoronaHooked<0x6ABC69>, "CCoronas::RegisterCorona"); //CAutomobile::PreRender
        if (memcmp(0x6ABA56, "68 FF 00 00 00") || forceEnable)
            injector::MakeJMP(0x6ABA56, patchCoronas);
        else
            Log::LogModifiedAddress(0x6ABA56, "Modified method detected: CAutomobile::PreRender - 0x6ABA56 is %s\n", bytesToString(0x6ABA56, 5).c_str());

        hookCall(0x6AB80F, AddLightHooked<0x6AB80F>, "CPointLights::AddLight"); //CAutomobile::PreRender
        hookCall(0x6ABBA6, AddLightHooked<0x6ABBA6>, "CPointLights::AddLight"); //CAutomobile::PreRender
    }

    if (disablePayAndSpray)
        hookCall(0x44AC75, IsCarSprayableHooked<0x44AC75>, "CGarages::IsCarSprayable"); //CGarage::Update

    if (enableSideMissions)
    {
        hookCall(0x48DA81, IsLawEnforcementVehicleHooked<0x48DA81>, "CVehicle::IsLawEnforcementVehicle");
        hookCall(0x469612, CollectParametersHooked<0x469612>, "CRunningScript::CollectParameters"); //00DD: IS_CHAR_IN_MODEL
        hookASM(0x4912CC, "66 8B 40 22 66 3D A4 01", movReg16WordPtrReg<REG_AX, REG_EAX, 0x4912D4, 4, 0x01A43D66>, "CRunningScript::ProcessCommands1500To1599"); //0602: IS_CHAR_IN_TAXI
    }

    hookCall(0x4306A1, GetNewVehicleDependingOnCarModelHooked<0x4306A1>, "CCarCtrl::GetNewVehicleDependingOnCarModel"); ///CCarCtrl::GenerateOneRandomCar

    //Tuning for parked cars
    MakeInline<0x6F3B88, 6>("CCarGenerator::DoInternalProcessing", "88 96 2A 04 00 00", [](injector::reg_pack& regs)
    {
        *reinterpret_cast<uint8_t*>(regs.esi + 0x42A) = (uint8_t)(regs.edx & 0xFF);
        if (tuneParkedCar)
        {
            processTuning(reinterpret_cast<CVehicle*>(regs.esi));
            tuneParkedCar = false;
        }
    });

    DWORD oldProtect;
    if (VirtualProtect(asmNextInstr, 16, PAGE_EXECUTE_READWRITE, &oldProtect) == 0)
        Log::Write("VirtualProtect failed: %s\n", GetLastError());
}
