#include "Vehicles.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "SA.hpp"

#include <plugin.h>
#include <CCarCtrl.h>
#include <CCarGenerator.h>
#include <CHeli.h>
#include <CModelInfo.h>
#include <CPlane.h>
#include <CTheScripts.h>
#include <CVector.h>
#include <CWorld.h>

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
unsigned short roadblockDriver = 0;
unsigned short lightsModel = 0;
int currentOccupantsGroup = -1;
unsigned short currentOccupantsModel = 0;
bool policeOccupants = false;
bool tuneParkedCar = false;

int occupantModelIndex = -1;

std::vector<std::pair<CVehicle*, CVehicle*>> spawnedTrailers;

std::uintptr_t x6ABCBE_Destination = 0;

uint32_t asmNextInstr[4] = {};
uint16_t asmModel16 = 0;
uint32_t asmModel32 = 0;
std::uintptr_t asmJmpAddress = 0;
uint32_t* jmpDest = asmNextInstr;

struct tVehVars {
    std::array<std::vector<unsigned short>, 16> variations[212];
    std::array<std::vector<unsigned short>, 6> wantedVariations[212];
    std::array<std::unordered_map<uint64_t, std::vector<unsigned short>>, 212> zoneVariations;

    std::unordered_map<unsigned short, std::array<std::vector<unsigned short>, 16>> occupantGroups;
    std::unordered_map<unsigned short, std::array<std::vector<unsigned short>, 16>> tuning;
    std::unordered_map<unsigned short, std::array<std::vector<unsigned short>, 6>> groupWantedVariations;
    std::unordered_map<unsigned short, unsigned short> originalModels;
    std::unordered_map<unsigned short, std::vector<unsigned short>> drivers;
    std::unordered_map<unsigned short, std::vector<unsigned short>> passengers;
    std::unordered_map<unsigned short, std::vector<unsigned short>> driverGroups[9];
    std::unordered_map<unsigned short, std::vector<unsigned short>> passengerGroups[9];
    std::unordered_map<unsigned short, BYTE> modelNumGroups;
    std::unordered_map<unsigned short, std::pair<CVector, float>> lightPositions;
    std::unordered_map<unsigned short, rgba> lightColors;
    std::unordered_map<unsigned short, rgba> lightColors2;
    std::map<unsigned short, std::vector<unsigned short>> currentTuning;
    std::unordered_map<unsigned short, std::string> vehModels;
    std::unordered_map<unsigned short, BYTE> tuningChances;
    std::unordered_map<unsigned short, std::vector<unsigned short>> trailers;
    std::unordered_map<unsigned short, BYTE> trailersSpawnChances;
    std::unordered_map<unsigned short, unsigned short> trailersHealth;

    std::vector<unsigned short> currentVariations[212];
    std::vector<unsigned short> mergeZones;
    std::vector<unsigned short> parkedCars;
    std::vector<unsigned short> trailersMatchColors;
    std::vector<unsigned short> useOnlyGroups;

    std::set<unsigned short> vehHasVariations;
    std::set<unsigned short> vehHasTuning;

    std::stack<std::pair<CVehicle*, std::array<std::vector<unsigned short>, 18>>> tuningStack;
    std::stack<CVehicle*> stack;
};

std::unique_ptr<tVehVars> vehVars(new tVehVars);


struct tVehOptions {
    bool changeCarGenerators = false;
    bool changeScriptedCars = false;
    bool disablePayAndSpray = false;
    bool enableLights = false;
    bool enableSideMissions = false;
    bool enableSiren = false;
    bool enableSpecialFeatures = false;
    std::vector<unsigned short> carGenExclude;
    std::vector<unsigned short> inheritExclude;
};

std::unique_ptr<tVehOptions> vehOptions(new tVehOptions);


bool isAnotherVehicleBehind(CVehicle* veh)
{
    auto isPointInPolygon = [](const std::vector<CVector2D>& polygon, const CVector2D& point)
    {
        //https://www.geeksforgeeks.org/point-in-polygon-in-cpp/
        unsigned int n = polygon.size();
        bool inside = false;

        for (unsigned int i = 0; i < n; i++) {
            CVector2D p1 = polygon[i];
            CVector2D p2 = polygon[(i + 1) % n];

            bool yCheck = (p1.y > point.y) != (p2.y > point.y);

            double xIntersect = (p2.x - p1.x) * (point.y - p1.y) / (p2.y - p1.y) + (double)p1.x;

            if (yCheck && point.x < xIntersect)
                inside = !inside;
        }

        return inside;
    };

    auto* mInfo = CModelInfo::GetModelInfo(veh->m_nModelIndex);
    if (mInfo == NULL)
        return false;
    
    CVector vmin = mInfo->m_pColModel->m_boundBox.m_vecMin;
    CVector vmax = mInfo->m_pColModel->m_boundBox.m_vecMax;

    CVector bottom_left = veh->TransformFromObjectSpace({ vmin.x, vmin.y * 3.0f, 0.0f });
    CVector bottom_right = veh->TransformFromObjectSpace({ -vmin.x, vmin.y * 3.0f, 0.0f });
    CVector top_right = veh->TransformFromObjectSpace({ vmax.x, vmin.y, 0.0f });
    CVector top_left = veh->TransformFromObjectSpace({ vmin.x, vmin.y, 0.0f });

    std::vector<CVector2D> polygon = { { top_left.x, top_left.y }, { top_right.x, top_right.y }, { bottom_right.x, bottom_right.y }, { bottom_left.x, bottom_left.y } };

    for (const auto& i : CPools::ms_pVehiclePool)
    {
        if (std::abs(i->GetPosition().z - veh->GetPosition().z) > 25.0f)
            continue;

        auto* mInfoTarget = CModelInfo::GetModelInfo(i->m_nModelIndex);
        if (i != veh && mInfoTarget != NULL)
        {
            CVector vminTarget = mInfoTarget->m_pColModel->m_boundBox.m_vecMin;
            CVector vmaxTarget = mInfoTarget->m_pColModel->m_boundBox.m_vecMax;

            CVector2D top_rightTarget = convert3DVectorTo2D(i->TransformFromObjectSpace({ vmaxTarget.x, vminTarget.y, 0.0f }));
            CVector2D top_leftTarget = convert3DVectorTo2D(i->TransformFromObjectSpace({ vminTarget.x, vminTarget.y, 0.0f }));
            CVector2D bottom_leftTarget = convert3DVectorTo2D(i->TransformFromObjectSpace({ vminTarget.x, vminTarget.y * 2.0f, 0.0f }));
            CVector2D bottom_rightTarget = convert3DVectorTo2D(i->TransformFromObjectSpace({ -vminTarget.x, vminTarget.y * 2.0f, 0.0f }));

            CVector2D top_centerTarget = convert3DVectorTo2D(i->TransformFromObjectSpace({ 0.0, vminTarget.y, 0.0f }));

            if (isPointInPolygon(polygon, top_rightTarget) || isPointInPolygon(polygon, top_leftTarget) || isPointInPolygon(polygon, top_centerTarget) ||
                isPointInPolygon(polygon, bottom_leftTarget) || isPointInPolygon(polygon, bottom_rightTarget))
                return true;
        }
    }
    

    return false;
}

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
        auto it = vehVars->currentTuning.find(veh->m_nModelIndex);
        if (it != vehVars->currentTuning.end() && !it->second.empty())
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


            auto tuningChance = vehVars->tuningChances.find(veh->m_nModelIndex);

            std::array<bool, 18> slotsToInstall = {};
            for (unsigned int i = 0; i < 18; i++)
                if (tuningChance != vehVars->tuningChances.end())
                    slotsToInstall[i] = (tuningChance->second == 0) ? false : ((rand<uint32_t>(0, 100) < tuningChance->second) ? true : false);
                else
                    slotsToInstall[i] = (rand<uint32_t>(0, 3) == 0 ? true : false);

            std::string section;
            if (auto it2 = vehVars->vehModels.find(veh->m_nModelIndex); it2 != vehVars->vehModels.end())
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
            vehVars->tuningStack.push({ veh, partsToInstall });
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
    if (vectorHasId(vehVars->useOnlyGroups, veh->m_nModelIndex) || rand<bool>())
    {
        if (auto modelNumGroups = vehVars->modelNumGroups.find(veh->m_nModelIndex); modelNumGroups != vehVars->modelNumGroups.end())
        {
            const CWanted* wanted = FindPlayerWanted(-1);
            const unsigned int wantedLevel = wanted->m_nWantedLevel - (wanted->m_nWantedLevel ? 1 : 0);
            currentOccupantsModel = veh->m_nModelIndex;

            std::string section;
            if (auto it = vehVars->vehModels.find(veh->m_nModelIndex); it != vehVars->vehModels.end())
                section = it->second;
            else
                section = std::to_string(veh->m_nModelIndex);

            std::vector<unsigned short> zoneGroups = dataFile.ReadLine(section, currentZone, READ_GROUPS);
            checkNumGroups(zoneGroups, modelNumGroups->second);

            if (auto it = vehVars->occupantGroups.find(veh->m_nModelIndex); it != vehVars->occupantGroups.end())
            {
                if (vectorHasId(vehVars->mergeZones, veh->m_nModelIndex))
                    zoneGroups = vectorUnion(zoneGroups, it->second[currentTown]);
                else if (zoneGroups.empty())
                    zoneGroups = it->second[currentTown];
            }

            if (zoneGroups.empty() && !vehVars->groupWantedVariations[veh->m_nModelIndex][wantedLevel].empty())
                currentOccupantsGroup = vectorGetRandom(vehVars->groupWantedVariations[veh->m_nModelIndex][wantedLevel]) - 1;
            else
            {
                if (!vehVars->groupWantedVariations[veh->m_nModelIndex][wantedLevel].empty())
                    vectorfilterVector(zoneGroups, vehVars->groupWantedVariations[veh->m_nModelIndex][wantedLevel]);
                if (!zoneGroups.empty())
                    currentOccupantsGroup = vectorGetRandom(zoneGroups) - 1;
                else
                    currentOccupantsGroup = rand<int32_t>(0, modelNumGroups->second);
            }
        }
    }
}

static int __stdcall getVariationOriginalModel(const int modelIndex)
{
    if (modelIndex < 400)
        return modelIndex;

    auto it = vehVars->originalModels.find((unsigned short)modelIndex);
    if (it != vehVars->originalModels.end())
        return it->second;

    return modelIndex;
}

int getRandomVariation(const int modelid, bool parked = false)
{
    if (modelid < 400 || modelid > 611)
        return modelid;
    if (vehVars->currentVariations[modelid - 400].empty())
        return modelid;

    if (parked == false)
    {
        if (vectorHasId(vehVars->parkedCars, modelid))
            return modelid;
    }

    const unsigned short variationModel = vectorGetRandom(vehVars->currentVariations[modelid - 400]);
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

void VehicleVariations::ClearData()
{
    vehVars.reset(new tVehVars);
    vehOptions.reset(new tVehOptions);

    dataFile.data.clear();
}

void VehicleVariations::LoadData()
{
    dataFile.Load(dataFileName);

    vehOptions->changeCarGenerators   = dataFile.ReadBoolean("Settings", "ChangeCarGenerators", false);
    vehOptions->changeScriptedCars    = dataFile.ReadBoolean("Settings", "ChangeScriptedCars", false);
    vehOptions->disablePayAndSpray    = dataFile.ReadBoolean("Settings", "DisablePayAndSpray", false);
    vehOptions->enableLights          = dataFile.ReadBoolean("Settings", "EnableLights", false);
    vehOptions->enableSideMissions    = dataFile.ReadBoolean("Settings", "EnableSideMissions", false);
    vehOptions->enableSiren           = dataFile.ReadBoolean("Settings", "EnableSiren", false);
    vehOptions->enableSpecialFeatures = dataFile.ReadBoolean("Settings", "EnableSpecialFeatures", false);
    vehOptions->carGenExclude         = dataFile.ReadLine("Settings", "ExcludeCarGeneratorModels", READ_VEHICLES);
    vehOptions->inheritExclude        = dataFile.ReadLine("Settings", "ExcludeModelsFromInheritance", READ_VEHICLES);

    Log::Write("\nReading vehicle data...\n");

    for (auto& iniData : dataFile.data)
    {
        Log::Write("%s\n", iniData.first.c_str());

        std::string section = iniData.first;
        int modelid = 0;
        if (section[0] >= '0' && section[0] <= '9')
            modelid = std::stoi(section);
        else
        {
            CModelInfo::GetModelInfo(section.data(), &modelid);
            if (modelid >= 400)
                vehVars->vehModels.insert({ (unsigned short)modelid, section });
        }

        unsigned short i = (unsigned short)modelid;
        if (i >= 400)
        {
            if (i < 612)
            {
                vehVars->vehHasVariations.insert(i - 400U);

                if (dataFile.ReadBoolean(section, "ChangeOnlyParked", false))
                    vehVars->parkedCars.push_back(i);

                for (unsigned j = 0; j < 16; j++)
                    if (j < 6)
                        vehVars->variations[i - 400][j] = dataFile.ReadLine(section, areas[j].first, READ_VEHICLES);
                    else
                        vehVars->variations[i - 400][j] = vectorUnion(dataFile.ReadLine(section, areas[j].first, READ_VEHICLES), vehVars->variations[i - 400][areas[j].second]);

                vehVars->wantedVariations[i - 400][0] = dataFile.ReadLine(section, "Wanted1", READ_VEHICLES);
                vehVars->wantedVariations[i - 400][1] = dataFile.ReadLine(section, "Wanted2", READ_VEHICLES);
                vehVars->wantedVariations[i - 400][2] = dataFile.ReadLine(section, "Wanted3", READ_VEHICLES);
                vehVars->wantedVariations[i - 400][3] = dataFile.ReadLine(section, "Wanted4", READ_VEHICLES);
                vehVars->wantedVariations[i - 400][4] = dataFile.ReadLine(section, "Wanted5", READ_VEHICLES);
                vehVars->wantedVariations[i - 400][5] = dataFile.ReadLine(section, "Wanted6", READ_VEHICLES);


                for (auto &j : vehVars->variations[i - 400])
                    for (auto& k : j)
                        if (k > 0 && k != i && !(vectorHasId(vehOptions->inheritExclude, k)))
                            vehVars->originalModels.insert({ k, i });

                for (const auto& keyValue : iniData.second)
                    if (zoneNames.contains(keyValue.first))
                    {
                        std::vector<unsigned short> vec = dataFile.ReadLine(section, keyValue.first, READ_VEHICLES);
                        if (!vec.empty())
                        {
                            char tmp[8] = {};
                            strncpy(tmp, keyValue.first.c_str(), 7);
                            vehVars->zoneVariations[i - 400U][*reinterpret_cast<uint64_t*>(tmp)] = vec;
                        }

                        if (!dataFile.ReadLine(section, keyValue.first, READ_TUNING).empty())
                            vehVars->vehHasTuning.insert(i);

                        for (auto variation : vec)
                            if (variation > 0 && variation != i && !(vectorHasId(vehOptions->inheritExclude, variation)))
                                vehVars->originalModels[variation] = i;
                    }
            }

            //Groups
            for (unsigned j = 0; j < 16; j++)
            {
                std::vector<unsigned short> vec = dataFile.ReadLine(section, areas[j].first, READ_GROUPS);

                if (!vec.empty())
                {
                    if (j < 6)
                        vehVars->occupantGroups[i][j] = vec;
                    else
                        vehVars->occupantGroups[i][j] = vectorUnion(vec, vehVars->occupantGroups[i][areas[j].second]);
                }
            }

            //Tuning
            for (unsigned j = 0; j < 16; j++)
            {
                std::vector<unsigned short> vec = dataFile.ReadLine(section, areas[j].first, READ_TUNING);

                if (!vec.empty())
                {
                    vehVars->vehHasTuning.insert(i);
                    if (j < 6)
                        vehVars->tuning[i][j] = vec;
                    else
                        vehVars->tuning[i][j] = vectorUnion(vec, vehVars->tuning[i][areas[j].second]);
                }
            }


            const int tuningChance = dataFile.ReadInteger(section, "TuningChance", -1);
            if (tuningChance > -1)
                vehVars->tuningChances.insert({ i, (BYTE)tuningChance });

            if (dataFile.ReadBoolean(section, "UseOnlyGroups", false))
                vehVars->useOnlyGroups.push_back(i);

            if (vehOptions->enableLights)
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
                    vehVars->lightColors.insert({ i, colors });
                }

                if (lightX != 0.0 || lightY != 0.0 || lightZ != 0.0 || lightWidth > -900.0)
                    vehVars->lightPositions.insert({ i, {{ lightX, lightY, lightZ }, lightWidth} });

                r = dataFile.ReadInteger(section, "LightR2", -1);
                g = dataFile.ReadInteger(section, "LightG2", -1);
                b = dataFile.ReadInteger(section, "LightB2", -1);
                a = dataFile.ReadInteger(section, "LightA2", -1);

                if ((uint8_t)r == r && (uint8_t)g == g && (uint8_t)b == b && (uint8_t)a == a)
                {
                    rgba colors = { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
                    vehVars->lightColors2.insert({ i, colors });
                }
            }

            if (dataFile.ReadBoolean(section, "MergeZonesWithCities", false))
                vehVars->mergeZones.push_back(i);

            uint8_t numGroups = 0;
            for (int j = 0; j < 9; j++)
            {
                std::vector<unsigned short> vecDrivers = dataFile.ReadLine(section, "DriverGroup" + std::to_string(j + 1), READ_PEDS);
                if (!vecDrivers.empty())
                {
                    std::vector<unsigned short> vecPassengers = dataFile.ReadLine(section, "PassengerGroup" + std::to_string(j + 1), READ_PEDS);
                    if (!vecPassengers.empty())
                    {
                        vehVars->passengerGroups[j].insert({ i, vecPassengers });
                        vehVars->driverGroups[j].insert({ i, vecDrivers });
                        numGroups++;
                    }
                }             
            }

            if (numGroups > 0)
                vehVars->modelNumGroups[i] = numGroups;

            for (unsigned short j = 0; j < 6; j++)
            {
                std::vector<unsigned short> vec = dataFile.ReadLineUnique(section, "Wanted" + std::to_string(j + 1), READ_GROUPS);
                if (!vec.empty())
                {
                    checkNumGroups(vec, numGroups);
                    vehVars->groupWantedVariations[i][j] = vec;
                }
            }

            if (auto it = vehVars->occupantGroups.find(i); it!= vehVars->occupantGroups.end())
                for (auto &j : it->second)
                    checkNumGroups(j, numGroups);


            std::vector<unsigned short> vec = dataFile.ReadLine(section, "Drivers", READ_PEDS);
            if (!vec.empty())
                vehVars->drivers.insert({ i, vec });

            vec = dataFile.ReadLine(section, "Passengers", READ_PEDS);
            if (!vec.empty())
                vehVars->passengers.insert({ i, vec });

            vec = dataFile.ReadLine(section, "ParentModel", READ_VEHICLES);
            if (!vec.empty() && vec[0] >= 400)
                vehVars->originalModels[i] = vec[0];

            vec = dataFile.ReadLine(section, "Trailers", READ_VEHICLES);
            if (!vec.empty())
                vehVars->trailers.insert({ i, vec });

            if (dataFile.ReadBoolean(section, "TrailersMatchColors", false))
                vehVars->trailersMatchColors.push_back(i);

            const int trailersSpawnChance = dataFile.ReadInteger(section, "TrailersSpawnChance", -1);
            if (trailersSpawnChance > -1)
                vehVars->trailersSpawnChances.insert({ i, (BYTE)(trailersSpawnChance > 100 ? 100 : trailersSpawnChance) });

            const unsigned short trailersHealth = (unsigned short)dataFile.ReadInteger(section, "TrailersHealth", 2000);
            if (trailersHealth < 2000)
                vehVars->trailersHealth.insert({ i, trailersHealth });
        }
    }

    std::sort(vehVars->mergeZones.begin(), vehVars->mergeZones.end());
    std::sort(vehVars->parkedCars.begin(), vehVars->parkedCars.end());
    std::sort(vehVars->trailersMatchColors.begin(), vehVars->trailersMatchColors.end());
    std::sort(vehVars->useOnlyGroups.begin(), vehVars->useOnlyGroups.end());

    Log::Write("\n");
}

void VehicleVariations::Process()
{
    for (auto it = spawnedTrailers.begin(); it != spawnedTrailers.end(); )
    {
        if (IsVehiclePointerValid(it->first))
        {
            if ((CTimer::m_snTimeInMilliseconds - it->first->m_nCreationTime) < 500)
            {
                if (IsVehiclePointerValid(it->second) && it->first->m_pTractor == NULL)
                    it->first->SetTowLink(it->second, 1);
            }
            else if ((CTimer::m_snTimeInMilliseconds - it->first->m_nCreationTime) < 3900)
            {
                if (it->first->m_pTractor == NULL && !it->first->GetIsOnScreen())
                    it->first->m_nVehicleFlags.bFadeOut = 1;
            }
            it++;
        }
        else
            it = spawnedTrailers.erase(it);
    }

    while (!vehVars->tuningStack.empty())
    {
        const auto it = vehVars->tuningStack.top();
        vehVars->tuningStack.pop();

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
                        CStreaming::RequestVehicleUpgrade(slot[i], PRIORITY_REQUEST);
                        CStreaming::LoadAllRequestedModels(false);

                        it.first->AddVehicleUpgrade(slot[i]);
                        CStreaming::SetMissionDoesntRequireModel(slot[i]);
                        short otherUpgrade = CVehicleModelInfo__CLinkedUpgradeList__FindOtherUpgrade(CVehicleModelInfo__ms_linkedUpgrades, slot[i]);
                        if (otherUpgrade > -1)
                            CStreaming::SetMissionDoesntRequireModel(otherUpgrade);
                    }
                }
    }

    while (!vehVars->stack.empty())
    {
        CVehicle* veh = vehVars->stack.top();
        vehVars->stack.pop();

        if (!IsVehiclePointerValid(veh) || veh->m_nCreatedBy == eVehicleCreatedBy::MISSION_VEHICLE)
            continue;

        if (veh->m_nModelIndex >= 400 && veh->m_nModelIndex < 612 && !vehVars->currentVariations[veh->m_nModelIndex - 400].empty() && vehVars->currentVariations[veh->m_nModelIndex - 400][0] == 0)
        {
            veh->m_nVehicleFlags.bFadeOut = 1;
        }
        else
        {
            if (auto it = vehVars->passengers.find(veh->m_nModelIndex); it != vehVars->passengers.end() && it->second[0] == 0)
                for (int i = 0; i < 8; i++)
                {
                    CPed* passenger = veh->m_apPassengers[i];
                    if (passenger != NULL && passenger->m_nModelIndex > 0 && passenger->m_nCreatedBy != 2)
                    {
                        if (passenger->m_pIntelligence)
                            passenger->m_pIntelligence->FlushImmediately(false);
                        CTheScripts::RemoveThisPed(passenger);
                    }
                }

            const auto trailersSpawnChance = vehVars->trailersSpawnChances.find(veh->m_nModelIndex);
            bool spawnTrailer = (rand<uint32_t>(0, 3) == 0 ? true : false);

            if (trailersSpawnChance != vehVars->trailersSpawnChances.end())
                spawnTrailer = (trailersSpawnChance->second == 0) ? false : ((rand<uint32_t>(0, 100) < trailersSpawnChance->second) ? true : false);

            for (auto& i : spawnedTrailers)
            {
                if (veh == i.first && veh->m_pTractor && isAnotherVehicleBehind(veh))
                {
                    veh->SetPosn({});
                    veh->m_nVehicleFlags.bFadeOut = 1;
                    break;
                }
            }

            if (veh->m_pDriver && veh->m_pDriver != FindPlayerPed() && spawnTrailer && vehVars->trailers.contains(veh->m_nModelIndex))
            {
                unsigned short randTrailer = vectorGetRandom(vehVars->trailers[veh->m_nModelIndex]);
                loadModels({randTrailer}, PRIORITY_REQUEST, true);
                CVehicle* trailer = CCarCtrl::GetNewVehicleDependingOnCarModel(randTrailer, RANDOM_VEHICLE);
                if (trailer && IsVehiclePointerValid(veh))
                {
                    CWorld::Add(trailer);
                    CTheScripts::ClearSpaceForMissionEntity(veh->GetPosition(), trailer);
                    trailer->SetTowLink(veh, 1);
                    CCarCtrl::SwitchVehicleToRealPhysics(veh);
                    if (vehVars->trailersHealth.contains(veh->m_nModelIndex))
                        trailer->m_fHealth = (float)vehVars->trailersHealth[veh->m_nModelIndex];

                    if (vectorHasId(vehVars->trailersMatchColors, veh->m_nModelIndex))
                    {
                        trailer->m_nPrimaryColor = veh->m_nPrimaryColor;
                        trailer->m_nSecondaryColor = veh->m_nSecondaryColor;
                        trailer->m_nTertiaryColor = veh->m_nTertiaryColor;
                        trailer->m_nQuaternaryColor = veh->m_nQuaternaryColor;
                    }
                    spawnedTrailers.push_back({ trailer, veh });
                }
            }            
        }
    }
}

void VehicleVariations::UpdateVariations()
{
    for (auto& i : vehVars->vehHasTuning)
    {
        std::vector<unsigned short> currentAreaTuning;

        if (auto it = vehVars->tuning.find(i); it != vehVars->tuning.end())
            currentAreaTuning = vectorUnion(it->second[4], it->second[currentTown]);

        if (!currentAreaTuning.empty() || vehVars->currentTuning.contains(i))
            vehVars->currentTuning[i] = currentAreaTuning;

        std::string section;
        if (auto it = vehVars->vehModels.find(i); it != vehVars->vehModels.end())
            section = it->second;
        else
            section = std::to_string(i);

        std::vector<unsigned short> vec = dataFile.ReadLine(section, currentZone, READ_TUNING);
        if (!vec.empty())
        {
            if (vectorHasId(vehVars->mergeZones, i))
                vehVars->currentTuning[i] = vectorUnion(vehVars->currentTuning[i], vec);
            else
                vehVars->currentTuning[i] = vec;
        }
    }

    for (auto& modelid : vehVars->vehHasVariations)
    {
        vehVars->currentVariations[modelid] = vectorUnion(vehVars->variations[modelid][4], vehVars->variations[modelid][currentTown]);

        const auto it = vehVars->zoneVariations[modelid].find(*reinterpret_cast<uint64_t*>(currentZone));
        if (it != vehVars->zoneVariations[modelid].end())
        {
            if (!it->second.empty())
            {
                if (vectorHasId(vehVars->mergeZones, modelid + 400))
                    vehVars->currentVariations[modelid] = vectorUnion(vehVars->currentVariations[modelid], it->second);
                else
                    vehVars->currentVariations[modelid] = it->second;
            }
        }

        const CWanted* wanted = FindPlayerWanted(-1);
        if (wanted)
        {
            const unsigned int wantedLevel = wanted->m_nWantedLevel - (wanted->m_nWantedLevel ? 1 : 0);
            if (!vehVars->wantedVariations[modelid][wantedLevel].empty() && !vehVars->currentVariations[modelid].empty())
                vectorfilterVector(vehVars->currentVariations[modelid], vehVars->wantedVariations[modelid][wantedLevel]);
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
        if (!vehVars->currentVariations[i].empty())
        {
            Log::Write("%d: ", i+400);
            for (auto j : vehVars->currentVariations[i])
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
            if (!vehVars->variations[i][j].empty())
            {
                Log::Write("%d: ", i+400);
                for (unsigned int k = 0; k < 16; k++)
                    if (!vehVars->variations[i][k].empty())
                    {
                        Log::Write("(%u) ", k);
                        for (const auto& l : vehVars->variations[i][k])
                            Log::Write("%u ", l);
                    }

                Log::Write("\n");
                break;
            }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template <std::uintptr_t address, bool isMethod = true, typename... Args>
void changeModel(const char* funcName, unsigned short oldModel, int newModel, std::vector<std::uintptr_t> addresses, Args... args)
{
    if (newModel < 400 || newModel > 65535)
        return isMethod ? callMethodOriginal<address>(args...) : callOriginal<address>(args...);

    for (auto& i : addresses)
        if (*(uint16_t*)i != oldModel && forceEnableGlobal == false && !forceEnable.contains(address))
        {
            Log::LogModifiedAddress(i, "Modified method detected: %s - 0x%08X is %u\n", funcName, i, *(uint16_t*)i);
            return isMethod ? callMethodOriginal<address>(args...) : callOriginal<address>(args...);
        }

    for (auto& i : addresses)
        WriteMemory<uint16_t>(i, newModel);

    isMethod ? callMethodOriginal<address>(args...) : callOriginal<address>(args...);

    for (auto& i : addresses)
        WriteMemory<uint16_t>(i, oldModel);
}

template <typename T, std::uintptr_t address, bool isMethod = true, typename... Args>
T changeModelAndReturn(const char* funcName, unsigned short oldModel, int newModel, std::vector<std::uintptr_t> addresses, Args... args)
{
    if (newModel < 400 || newModel > 65535)
        return isMethod ? callMethodOriginalAndReturn<T, address>(args...) : callOriginalAndReturn<T, address>(args...);

    for (auto& i : addresses)
        if (*(uint16_t*)i != oldModel && forceEnableGlobal == false && !forceEnable.contains(address))
        {
            Log::LogModifiedAddress(i, "Modified method detected: %s - 0x%08X is %u\n", funcName, i, *(uint16_t*)i);
            return isMethod ? callMethodOriginalAndReturn<T, address>(args...) : callOriginalAndReturn<T, address>(args...);
        }

    for (auto& i : addresses)
        WriteMemory<uint16_t>(i, newModel);
    
    T retValue = isMethod ? callMethodOriginalAndReturn<T, address>(args...) : callOriginalAndReturn<T, address>(args...);

    for (auto& i : addresses)
        WriteMemory<uint16_t>(i, oldModel);

    return retValue;
}

template <std::uintptr_t address>
int __cdecl ChooseModelHooked(int* a1)
{
    const int model = callOriginalAndReturn<int, address>(a1);

    if (model < 400 || model > 611)
        return model;

    return getRandomVariation((unsigned short)model);
}

template <std::uintptr_t address>
int __cdecl ChoosePoliceCarModelHooked(int a1)
{
    const int model = callOriginalAndReturn<int, address>(a1);

    if (model < 427 || model > 601)
        return model;

    return getRandomVariation((unsigned short)model);
}

template <std::uintptr_t address>
void __cdecl AddPoliceCarOccupantsHooked(CVehicle* a2, char a3)
{
    assert(a2 != NULL);

    processOccupantGroups(a2);
    policeOccupants = true;

    const unsigned short model = a2->m_nModelIndex;
    a2->m_nModelIndex = (unsigned short)getVariationOriginalModel(a2->m_nModelIndex);

    callOriginal<address>(a2, a3);

    a2->m_nModelIndex = model;

    policeOccupants = false;
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
}

template <std::uintptr_t address>
CAutomobile* __fastcall CAutomobileHooked(CAutomobile* automobile, void*, int modelIndex, char usageType, char bSetupSuspensionLines)
{
    return callMethodOriginalAndReturn<CAutomobile*, address>(automobile, getRandomVariation(modelIndex), usageType, bSetupSuspensionLines);
}

template <std::uintptr_t address>
int __fastcall PickRandomCarHooked(CLoadedCarGroup* cargrp, void*, char a2, char a3) //for random parked cars
{
    return (cargrp == NULL) ? -1 : getRandomVariation(callMethodOriginalAndReturn<int, address>(cargrp, a2, a3), true);
}

template <std::uintptr_t address>
void __fastcall DoInternalProcessingHooked(CCarGenerator* park) //for non-random parked cars
{
    if (park != NULL)
    {
        if (park->m_nModelId < 0)
        {
            callMethodOriginal<address>(park);
            tuneParkedCar = true;
            return;
        }

        tuneParkedCar = false;

        if (vehOptions->changeCarGenerators)
        {
            if (vectorHasId(vehOptions->carGenExclude, park->m_nModelId))
            {
                callMethodOriginal<address>(park);
                return;
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
            default:
                callMethodOriginal<address>(park);
        }
    }
}

template <std::uintptr_t address>
void* __fastcall CTrainHooked(void* train, void*, int modelIndex, int createdBy)
{
    return callMethodOriginalAndReturn<void*, address>(train, CTheScripts::IsPlayerOnAMission() ? modelIndex : getRandomVariation(modelIndex), createdBy);
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
char __fastcall IsLawEnforcementVehicleHooked(CVehicle* veh)
{
    assert(veh != NULL);

    const unsigned short modelIndex = veh->m_nModelIndex;
    veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
    char isLawEnforcement = callMethodOriginalAndReturn<char, address>(veh);
    veh->m_nModelIndex = modelIndex;

    return isLawEnforcement;
}

template <std::uintptr_t address>
bool __fastcall UsesSirenHooked(CVehicle* veh)
{
    assert(veh != NULL);

    const unsigned short modelIndex = veh->m_nModelIndex;
    veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
    bool usesSiren = callMethodOriginalAndReturn<bool, address>(veh);
    veh->m_nModelIndex = modelIndex;

    return usesSiren;
}

template <std::uintptr_t address>
bool __cdecl IsCarSprayableHooked(CVehicle* veh)
{
    assert(veh != NULL);

    const unsigned short modelIndex = veh->m_nModelIndex;
    veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
    bool isCarSprayable = callOriginalAndReturn<bool, address>(veh);
    veh->m_nModelIndex = modelIndex;

    return isCarSprayable;
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
int __cdecl GetDefaultCopModelHooked()
{
    if (roadblockDriver > 0)
        return roadblockDriver;

    return callOriginalAndReturn<int, address>();
}

template <std::uintptr_t address>
CCopPed* __fastcall CCopPedHooked(CCopPed* ped, void*, int copType)
{
    std::pair<std::uintptr_t, std::string> originalData[4] = { {0x5DDE4F, "68 1B 01 00 00"}, 
                                                               {0x5DDD8F, "68 1D 01 00 00"}, 
                                                               {0x5DDDCF, "68 1E 01 00 00"}, 
                                                               {0x5DDE0F, "68 1F 01 00 00"} };

    for (const auto &i : originalData)
        if (!memcmp(i.first, i.second.c_str()) && !forceEnableGlobal && !forceEnable.contains(i.first))
        {
            Log::LogModifiedAddress(i.first, "Modified address detected: 0x%08X is %u\n", i.first, *(uint16_t*)(i.first+1));
            return callMethodOriginalAndReturn<CCopPed*, address>(ped, copType);
        }

    unsigned int original283 = *(unsigned int*)0x5DDE50;
    unsigned int original285 = *(unsigned int*)0x5DDD90;
    unsigned int original286 = *(unsigned int*)0x5DDDD0;
    unsigned int original287 = *(unsigned int*)0x5DDE10;

    if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
    {
        if (auto it = vehVars->driverGroups[currentOccupantsGroup].find(currentOccupantsModel); it != vehVars->driverGroups[currentOccupantsGroup].end())
        {
            auto driver = vectorGetRandom(it->second);
            loadModels({ driver }, PRIORITY_REQUEST, true);

            switch (getVariationOriginalModel(currentOccupantsModel))
            {
                case 427:
                case 601:
                    copType = COP_TYPE_SWAT1;
                    WriteMemory<unsigned int>(0x5DDD90, driver);
                    break;
                case 433:
                case 470:
                    copType = COP_TYPE_ARMY;
                    WriteMemory<unsigned int>(0x5DDE10, driver);
                    break;
                case 490:
                    copType = COP_TYPE_FBI;
                    WriteMemory<unsigned int>(0x5DDDD0, driver);
                    break;
                case 596:
                case 597:
                case 598:
                    copType = COP_TYPE_CITYCOP;
                    roadblockDriver = driver;
                    break;
                case 599:
                    copType = COP_TYPE_CSHER;
                    WriteMemory<unsigned int>(0x5DDE50, driver);
            }
            if (copType == COP_TYPE_CITYCOP)
            {
                if (driver == 283)
                    copType = COP_TYPE_CSHER;
                else if (driver == 285)
                    copType = COP_TYPE_SWAT1;
                else if (driver == 286)
                    copType = COP_TYPE_FBI;
                else if (driver == 287)
                    copType = COP_TYPE_ARMY;
            }

        }
    }

    auto retVal = callMethodOriginalAndReturn<CCopPed*, address>(ped, copType);
    WriteMemory<unsigned int>(0x5DDE50, original283);
    WriteMemory<unsigned int>(0x5DDD90, original285);
    WriteMemory<unsigned int>(0x5DDDD0, original286);
    WriteMemory<unsigned int>(0x5DDE10, original287);
    roadblockDriver = 0;
    return retVal;
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
        if (CHeli::pHelis[0] && getVariationOriginalModel(CHeli::pHelis[0]->m_nModelIndex) == 488)
            newsHeli = 0;

        if (CHeli::pHelis[1] && getVariationOriginalModel(CHeli::pHelis[1]->m_nModelIndex) == 488)
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

template <std::uintptr_t address>
CPed* __cdecl AddPedInCarHooked(CVehicle* veh, char driver, int a3, int a4, char a5, char a6)
{
    assert(veh != NULL);

    //laemt1 -> dsher
    loadModels(274, 288, GAME_REQUIRED, true);
    if (!policeOccupants && currentOccupantsGroup == -1)
        processOccupantGroups(veh);

    std::string section;
    if (auto it = vehVars->vehModels.find(veh->m_nModelIndex); it != vehVars->vehModels.end())
        section = it->second;
    else
        section = std::to_string(veh->m_nModelIndex);

    if (driver)
    {
        const bool replaceDriver = dataFile.ReadBoolean(section, "ReplaceDriver", false) ? true : rand<bool>();
        if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
        {
            if (auto it = vehVars->driverGroups[currentOccupantsGroup].find(currentOccupantsModel); it != vehVars->driverGroups[currentOccupantsGroup].end())
                occupantModelIndex = vectorGetRandom(it->second);
        }
        else if (auto it = vehVars->drivers.find(veh->m_nModelIndex); it != vehVars->drivers.end() && replaceDriver)
            occupantModelIndex = vectorGetRandom(it->second);
    }
    else
    {
        const bool replacePassenger = dataFile.ReadBoolean(section, "ReplacePassengers", false) ? true : rand<bool>();
        if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
        {
            if (auto it = vehVars->passengerGroups[currentOccupantsGroup].find(currentOccupantsModel); it != vehVars->passengerGroups[currentOccupantsGroup].end())
                occupantModelIndex = vectorGetRandom(it->second);
        }
        else if (auto it = vehVars->passengers.find(veh->m_nModelIndex); it != vehVars->passengers.end() && replacePassenger)
            occupantModelIndex = vectorGetRandom(it->second);
    }

    const auto model = veh->m_nModelIndex;
    veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
    CPed* ped = callOriginalAndReturn<CPed*, address>(veh, driver, a3, a4, a5, a6);
    veh->m_nModelIndex = model;

    if (!policeOccupants)
    {
        currentOccupantsGroup = -1;
        currentOccupantsModel = 0;
    }

    return ped;
}

template <std::uintptr_t address>
CPed* __cdecl AddPedHooked(int pedType, int modelIndex, CVector* posn, bool unknown)
{
    if (occupantModelIndex > 0)
    {
        modelIndex = occupantModelIndex;
        loadModels({ occupantModelIndex }, PRIORITY_REQUEST, true);
        CPed* ped = callOriginalAndReturn<CPed*, address>(pedType, modelIndex, posn, unknown);
        occupantModelIndex = -1;
        return ped;
    }

    return callOriginalAndReturn<CPed*, address>(pedType, modelIndex, posn, unknown);
}

template <std::uintptr_t address, bool second = false>
void __cdecl RegisterCoronaHooked(void* _this, CEntity* a2, unsigned char a3, unsigned char a4, unsigned char a5, unsigned char a6, CVector* a7, const CVector* a8,
                                  float a9, void* texture, unsigned char a11, unsigned char a12, unsigned char a13, int a14, float a15, float a16, float a17, float a18,
                                  float a19, float a20, bool a21)
{
    if (a7 && a2)
    {
        const auto it = vehVars->lightPositions.find(lightsModel);
        if (it != vehVars->lightPositions.end())
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

    if (a2)
    {
        auto &lightsMap = (second ? vehVars->lightColors2 : vehVars->lightColors);

        const auto it = lightsMap.find(lightsModel);
        if (it != lightsMap.end())
        {
            a3 = it->second.r;
            a4 = it->second.g;
            a5 = it->second.b;
            a6 = it->second.a;
        }
    }

    callOriginal<address>(_this, a2, a3, a4, a5, a6, a7, a8, a9, texture, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
}

template <std::uintptr_t address>
void __cdecl AddLightHooked(char type, float x, float y, float z, float dir_x, float dir_y, float dir_z, float radius, float r, float g, float b, 
                            char fogType, char generateExtraShadows, int attachedTo)
{
    if (lightsModel > 0)
    {
        const auto it = vehVars->lightPositions.find(lightsModel);
        if (it != vehVars->lightPositions.end())
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

    CVehicle *trailerToCheck = NULL;

    for (auto it = spawnedTrailers.begin(); it != spawnedTrailers.end(); )
    {
        if (!IsVehiclePointerValid(it->first))
        {
            it = spawnedTrailers.erase(it);
            continue;
        }

        if (it->first == car && it->first->m_pTractor && !it->first->m_pTractor->m_nVehicleFlags.bFadeOut)
            return;

        if (it->second == car && it->first->m_pTractor == it->second)
            trailerToCheck = it->first;

        it++;
    }

    switch (getVariationOriginalModel(car->m_nModelIndex))
    {
        case 407: //Firetruck
            return changeModel<address, false>("CCarCtrl::PossiblyRemoveVehicle", 407, car->m_nModelIndex, { 0x4250B2 }, car);
        case 416: //Ambulance
            return changeModel<address, false>("CCarCtrl::PossiblyRemoveVehicle", 416, car->m_nModelIndex, { 0x4250AC }, car);
    }

    callOriginal<address>(car);

    if (trailerToCheck != NULL && !IsVehiclePointerValid(car))
    {
        trailerToCheck->SetPosn({});
        trailerToCheck->m_nVehicleFlags.bFadeOut = 1;
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
void __fastcall CAutomobile__PreRenderHooked(CAutomobile* veh)
{
    assert(veh != NULL);

    unsigned short originalModel = veh->m_nModelIndex;
    lightsModel = veh->m_nModelIndex;

    if (vehOptions->enableSpecialFeatures)
    {
        constexpr const char* funcName = "CAutomobile::PreRender";

        switch (getVariationOriginalModel(originalModel))
        {
            case 407: return changeModel<address>(funcName, 407, originalModel, { 0x6ACA59 }, veh); // Firetruck
            case 424: return changeModel<address>(funcName, 424, originalModel, { 0x6AC2A1 }, veh); // BF Injection
            case 432: return changeModel<address>(funcName, 432, originalModel, { 0x6ABC83, 0x6ABD11, 0x6ABFCC, 0x6AC029, 0x6ACA4D }, veh); // Rhino
            case 434: return changeModel<address>(funcName, 434, originalModel, { 0x6ACA43 }, veh); // Hotknife
            case 443: return changeModel<address>(funcName, 443, originalModel, { 0x6AC4DB }, veh); // Packer
            case 471: return changeModel<address>(funcName, 471, originalModel, { 0x6ABCA9 }, veh); // Quad
            case 477: return changeModel<address>(funcName, 477, originalModel, { 0x6ACA8F }, veh); // ZR-350
            case 486: return changeModel<address>(funcName, 486, originalModel, { 0x6AC40E }, veh); // Dozer
            case 495: return changeModel<address>(funcName, 495, originalModel, { (GetGameVersion() != GAME_10US_COMPACT) ? 0x4064A0U : 0x6ACBCBU }, veh); // Sandking
            case 524: return changeModel<address>(funcName, 524, originalModel, { 0x6AC43D }, veh); // Cement Truck
            case 525: return changeModel<address>(funcName, 525, originalModel, { 0x6AC509 }, veh); // Towtruck
            case 530: return changeModel<address>(funcName, 530, originalModel, { 0x6AC71E }, veh); // Forklift
            case 531: return changeModel<address>(funcName, 531, originalModel, { 0x6AC6DB }, veh); // Tractor
            case 532: return changeModel<address>(funcName, 532, originalModel, { 0x6ABCA3, 0x6AC7AD }, veh); // Combine Harvester
            case 539: return changeModel<address>(funcName, 539, originalModel, { 0x6AAE27 }, veh); // Vortex
            case 544: return changeModel<address>(funcName, 544, originalModel, { 0x6AC0E3 }, veh); // Firetruck LS
            case 568: return changeModel<address>(funcName, 568, originalModel, { 0x6ACA39 }, veh); // Bandito
            case 601: return changeModel<address>(funcName, 601, originalModel, { 0x6ACA53 }, veh); // SWAT Tank
        }
    }

    callMethodOriginal<address>(veh);
    veh->m_nModelIndex = originalModel;
    lightsModel = 0;
}

template <std::uintptr_t address>
int __fastcall GetVehicleAppearanceHooked(CVehicle* veh)
{
    if (lightsModel > 0)
        veh->m_nModelIndex = lightsModel;

    lightsModel = 0;
    return callMethodOriginalAndReturn<int, address>(veh);
}

template <std::uintptr_t address>
CVehicle* __cdecl CreateCarForScriptHooked(int modelId, float posX, float posY, float posZ, char doMissionCleanup)
{
    return callOriginalAndReturn<CVehicle*, address>(getRandomVariation(modelId), posX, posY, posZ, doMissionCleanup);
}

template <std::uintptr_t address>
CVehicle* __cdecl GetNewVehicleDependingOnCarModelHooked(int modelIndex, int createdBy)
{
    CVehicle *veh = callOriginalAndReturn<CVehicle*, address>(modelIndex, createdBy);
    processTuning(veh);
    return veh;
}

template <std::uintptr_t address>
int __fastcall CreateInstanceHooked(CVehicleModelInfo* _this)
{
    if (_this->m_pVehicleStruct == NULL)
    {
        int index = 0;
        CModelInfo::GetModelInfoFromHashKey(_this->m_nKey, &index);
        Log::Write("Model %d has NULL vehicle struct. Trying to load model... ", index);
        loadModels({ index }, PRIORITY_REQUEST, true);
        if (_this->m_pVehicleStruct != NULL)
            Log::Write("OK\n");
        else
        {
            char errorString[256] = {};
            snprintf(errorString, 255, "Couldn't load model %d! The game will crash.\nLoad state: %u\nReference count: %u\nTimes used: %u\n", index, CStreaming__ms_aInfoForModel[index].m_nLoadState, _this->m_nRefCount, _this->m_nTimesUsed);
            Log::Write("\n%s\n", errorString);
            MessageBox(NULL, errorString, "Model Variations", MB_ICONERROR);
        }
    }

    return callMethodOriginalAndReturn<int, address>(_this);
}

template <std::uintptr_t address>
CPhysical* __fastcall CPhysicalHooked(CVehicle* _this)
{
    CPhysical* retVal = callMethodOriginalAndReturn<CPhysical*, address>(_this);
    vehVars->stack.push(_this);
    return retVal;
}


////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////  SPECIAL FEATURES  //////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

template <std::uintptr_t address>
void __fastcall ProcessControlHooked(CAutomobile* veh)
{
    assert(veh != NULL);

    constexpr const char* funcName = "CAutomobile::ProcessControl";
    const auto modelIndex = veh->m_nModelIndex;

    switch (getVariationOriginalModel(veh->m_nModelIndex))
    {
        case 406: return changeModel<address>(funcName, 406, modelIndex, { 0x6B1F9D }, veh); //Dumper
        case 407: return changeModel<address>(funcName, 407, modelIndex, { 0x6B1F51 }, veh); //Firetruck
        case 417: return changeModel<address>(funcName, 417, modelIndex, { 0x6B1E34 }, veh); //Leviathan
        case 423: return changeModel<address>(funcName, 423, modelIndex, { 0x6B2BD8 }, veh); //Mr. Whoopie
        case 432: return changeModel<address>(funcName, 432, modelIndex, { 0x6B1F7D, 0x6B36D8 }, veh); //Rhino
        case 443: return changeModel<address>(funcName, 443, modelIndex, { 0x6B1F91 }, veh); //Packer
        case 447: return changeModel<address>(funcName, 447, modelIndex, { 0x6B1E2D }, veh); //Sea Sparrow
        case 460: return changeModel<address>(funcName, 460, modelIndex, { 0x6B2181 }, veh); //Skimmer
        case 486: return changeModel<address>(funcName, 486, modelIndex, { 0x6B1F97 }, veh); //Dozer
        case 524: return changeModel<address>(funcName, 524, modelIndex, { 0x6B1FA3 }, veh); //Cement Truck
        case 525: return changeModel<address>(funcName, 525, modelIndex, { 0x6B1FB5 }, veh); //Towtruck
        case 530: return changeModel<address>(funcName, 530, modelIndex, { 0x6B1FAF }, veh); //Forklift
        case 531: return changeModel<address>(funcName, 531, modelIndex, { 0x6B1FBB }, veh); //Tractor
        case 532: return changeModel<address>(funcName, 532, modelIndex, { 0x6B36C9 }, veh); //Combine Harvester
        case 539: return changeModel<address>(funcName, 539, modelIndex, { 0x6B1E5C, 0x6B284F, 0x6B356E }, veh); //Vortex
        case 601: return changeModel<address>(funcName, 601, modelIndex, { 0x6B1F57 }, veh); //Swat Tank
    }

    callMethodOriginal<address>(veh);
}

template <std::uintptr_t address>
void __fastcall AddDamagedVehicleParticlesHooked(CVehicle* veh)
{
    callMethodOriginal<address>(veh);
    if (lightsModel > 0)
        veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
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
char __fastcall GetTowHitchPosHooked(void* trailer, void*, CVector* point, char a3, CVehicle* a4)
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
    if (automobile == NULL)
        return 0;

    const auto originalModel = getVariationOriginalModel(automobile->m_nModelIndex);
    if (originalModel == 531 || originalModel == 532 || originalModel == 571)
        return 0;

    return callMethodOriginalAndReturn<char, address>(automobile, colModel);
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
        return 0;

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
int __cdecl GetMaximumNumberOfPassengersFromNumberOfDoorsHooked(int modelIndex)
{
    switch (getVariationOriginalModel(modelIndex))
    {
        case 407: //Firetruck
            return changeModelAndReturn<int, address, false>("CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors", 407, modelIndex, { 0x4C8A17 }, modelIndex);
        case 425: //Hunter
            return changeModelAndReturn<int, address, false>("CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors", 425, modelIndex, { 0x4C8A27 }, modelIndex);
        case 431: //Bus
            return changeModelAndReturn<int, address, false>("CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors", 431, modelIndex, { 0x4C8ADB }, modelIndex);
        case 437: //Coach
            return changeModelAndReturn<int, address, false>("CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors", 437, modelIndex, { 0x4C8AD3 }, modelIndex);
        case 508: //Journey
            return changeModelAndReturn<int, address, false>("CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors", 508, modelIndex, { 0x4C8A1F }, modelIndex);
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
void __fastcall ProcessControlInputsHooked(CPlane* _this, void*, unsigned char a2)
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

template <std::uintptr_t address>
CPed* __fastcall CPool__atHandleHooked(void* _this, void*, signed int h)
{
    CPed* ped = callMethodOriginalAndReturn<CPed*, address>(_this, h);
    if (IsPedPointerValid(ped) && IsVehiclePointerValid(ped->m_pVehicle))
    {
        auto originalModel = getVariationOriginalModel(ped->m_pVehicle->m_nModelIndex);
        if (ScriptParams[1] == originalModel)
            ScriptParams[1] = ped->m_pVehicle->m_nModelIndex;
    }
    return ped;
}

template <std::uintptr_t address>
CPed* __fastcall CPool__atHandleTaxiHooked(void* _this, void*, signed int h) //Unnecessarily complicated function to avoid incompatibility with FLA
{
    static uint8_t taxiPed[sizeof(CPed)];
    static uint8_t taxiVeh[sizeof(CVehicle)];

    CPed* ped = callMethodOriginalAndReturn<CPed*, address>(_this, h);
    if (IsPedPointerValid(ped) && IsVehiclePointerValid(ped->m_pVehicle) && ped->m_nPedFlags.bInVehicle)
    {
        CPed* pTaxiPed = reinterpret_cast<CPed*>(&taxiPed);
        pTaxiPed->m_nPedFlags.bInVehicle = ped->m_nPedFlags.bInVehicle;

        constexpr auto pTaxiVeh = &taxiVeh;
        memcpy(&taxiPed[0x58C], &pTaxiVeh, 4);

        unsigned short originalModel = static_cast<uint16_t>(getVariationOriginalModel(ped->m_pVehicle->m_nModelIndex));
        memcpy(&taxiVeh[0x22], &originalModel, 2);

        return pTaxiPed;
    }
    return ped;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////  ASM HOOKS  /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void __declspec(naked) patch6A155C()
{
    __asm {
        movsx eax, word ptr[edi + 0x22]
        push eax
        call getVariationOriginalModel     
        cmp ax, 0x20C
        je isCement
        cmp ax, 0x220

    isCement:
        mov ax, word ptr [edi + 0x22]
        mov asmJmpAddress, 0x6A1564
        jmp asmJmpAddress
    }
}

void __declspec(naked) patch588570()
{
    __asm {
        add esp, 8
        push eax
        movsx ecx, word ptr [eax+0x22]
        push ecx
        call getVariationOriginalModel
        cmp ax, bx
        pop eax
        mov asmJmpAddress, 0x588577
        jmp asmJmpAddress
    }
}

void __declspec(naked) patch6ABCBE()
{
    __asm {
        mov ax, lightsModel
        test ax, ax
        jz skipModelChange
        mov word ptr [esi+0x22], ax
        mov lightsModel, 0

skipModelChange:
        mov eax, x6ABCBE_Destination
        test eax, eax
        jz jmpOriginal
        jmp x6ABCBE_Destination

jmpOriginal:
        xor edi, edi
        push edi
        push 0xFFFFFFFF
        mov asmJmpAddress, 0x6ABCC3
        jmp asmJmpAddress
    }
}

auto RegisterCoronaHookedPointer = RegisterCoronaHooked<0x6ABA60, true>;
void __declspec(naked) patchCoronas()
{
    __asm {
        push 0x0FF
        push ecx
        push edx
        push eax
        push esi
        push edi
        call RegisterCoronaHookedPointer
        mov asmJmpAddress, 0x6ABA65
        jmp asmJmpAddress
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
    reinterpret_cast<uint8_t*>(asmNextInstr)[nextInstrSize] = 0xE9;
    *(uint32_t*)((uint8_t*)asmNextInstr + nextInstrSize + 1) = jmpAddress - (uintptr_t)((uint8_t*)asmNextInstr + nextInstrSize + 5);

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
    reinterpret_cast<uint8_t*>(asmNextInstr)[nextInstrSize] = 0xE9;
    *(uint32_t*)((uint8_t*)asmNextInstr + nextInstrSize + 1) = jmpAddress - (uintptr_t)((uint8_t*)asmNextInstr + nextInstrSize + 5);

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

void VehicleVariations::InstallHooks()
{
    if (isFLASpecialFeaturesEnabled == 1 && vehOptions->enableSpecialFeatures)
    {
        MessageBox(NULL, "Both Model Variations and FLA special features are enabled. Disable one of them.", "Model Variations", MB_ICONWARNING);
        vehOptions->enableSpecialFeatures = false;
    }

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
    hookASM(0x6F3B94, "66 8B 46 22 66 3D 13 02", movReg16WordPtrReg<REG_AX, REG_ESI, 0x6F3B9C, 4, 0x02133D66>, "CCarGenerator::DoInternalProcessing");

    //Trains
    //patch::RedirectCall(0x4214DC, CTrainHooked); //CCarCtrl::GetNewVehicleDependingOnCarModel
    //patch::RedirectCall(0x5D2B15, CTrainHooked); //CPools::LoadVehiclePool
    hookCall(0x6F7634, CTrainHooked<0x6F7634>, "CTrain::CTrain"); //CTrain::CreateMissionTrain 

    hookASM(0x64475D, "66 81 78 22 3A 02", cmpWordPtrRegModel<REG_EAX, 0x644763, 0x23A>, "CTaskSimpleCarDrive::ProcessPed");
    hookASM(0x6F60D9, "66 81 7E 22 3A 02", cmpWordPtrRegModel<REG_ESI, 0x6F60DF, 0x23A>, "CTrain::CTrain");
    hookASM(0x6F6576, "66 81 7F 22 3A 02", cmpWordPtrRegModel<REG_EDI, 0x6F657C, 0x23A>, "CTrain::OpenDoor");
    hookASM(0x6F8E8A, "66 81 7E 22 3A 02", cmpWordPtrRegModel<REG_ESI, 0x6F8E90, 0x23A>, "CTrain::ProcessControl");

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
    hookCall(0x4613EB, GetColModelHooked<0x4613EB>, "CEntity::GetColModel"); //CRoadBlocks::GenerateRoadBlockCopsForCar
    hookCall(0x5DDCA8, GetDefaultCopModelHooked<0x5DDCA8>, "CStreaming::GetDefaultCopModel"); //CCopPed::CCopPed
    hookCall(0x46151A, CCopPedHooked<0x46151A>, "CCopPed::CCopPed"); //CRoadBlocks::GenerateRoadBlockCopsForCar
    hookCall(0x461541, CCopPedHooked<0x461541>, "CCopPed::CCopPed"); //CRoadBlocks::GenerateRoadBlockCopsForCar

    hookCall(0x6D1A7A, AddPedInCarHooked<0x6D1A7A>, "CPopulation::AddPedInCar"); //CVehicle::SetUpDriver
    hookCall(0x6D1B0E, AddPedInCarHooked<0x6D1B0E>, "CPopulation::AddPedInCar"); //CVehicle::SetupPassenger 
    hookCall(0x6F6986, AddPedInCarHooked<0x6F6986>, "CPopulation::AddPedInCar"); //CTrain::RemoveRandomPassenger
    hookCall(0x6F786F, AddPedInCarHooked<0x6F786F>, "CPopulation::AddPedInCar"); //CTrain::CreateMissionTrain
    hookCall(0x613B7F, AddPedHooked<0x613B7F>, "CPopulation::AddPed"); //CPopulation::AddPedInCar

    hookCall(0x6B11C2, IsLawEnforcementVehicleHooked<0x6B11C2>, "CVehicle::IsLawEnforcementVehicle"); //CAutomobile::CAutomobile

    hookCall(0x60C4E8, PossiblyRemoveVehicleHooked<0x60C4E8>, "CCarCtrl::PossiblyRemoveVehicle"); //CPlayerPed::KeepAreaAroundPlayerClear
    hookCall(0x42CD55, PossiblyRemoveVehicleHooked<0x42CD55>, "CCarCtrl::PossiblyRemoveVehicle"); //CCarCtrl::RemoveDistantCars

    hookCall(0x64BB57, SetDriverHooked<0x64BB57>, "CVehicle::SetDriver"); //CTaskSimpleCarSetPedInAsDriver::ProcessPed

    hookCall(0x871164, CAutomobile__PreRenderHooked<0x871164>, "CAutomobile::PreRender", true);
    hookCall(0x6CFADC, CAutomobile__PreRenderHooked<0x6CFADC>, "CAutomobile::PreRender"); //CTrailer::PreRender

    hookCall(0x6ABC93, GetVehicleAppearanceHooked<0x6ABC93>, "CVehicle::GetVehicleAppearance"); //CAutomobile::PreRender
    x6ABCBE_Destination = injector::MakeJMP(0x6ABCBE, patch6ABCBE).as_int();

    if (vehOptions->changeScriptedCars)
        hookCall(0x467B01, CreateCarForScriptHooked<0x467B01>, "CCarCtrl::CreateCarForScript"); //00A5: CREATE_CAR
    
    if (vehOptions->enableSpecialFeatures)
    {
        hookCall(0x871148, ProcessControlHooked<0x871148>, "CAutomobile::ProcessControl", true);
        hookCall(0x6C7059, ProcessControlHooked<0x6C7059>, "CAutomobile::ProcessControl"); //CHeli::ProcessControl
        hookCall(0x6C82B4, ProcessControlHooked<0x6C82B4>, "CAutomobile::ProcessControl"); //CMonsterTruck::ProcessControl
        hookCall(0x6C9313, ProcessControlHooked<0x6C9313>, "CAutomobile::ProcessControl"); //CPlane::ProcessControl
        hookCall(0x6CE005, ProcessControlHooked<0x6CE005>, "CAutomobile::ProcessControl"); //CQuadBike::ProcessControl
        hookCall(0x6CED23, ProcessControlHooked<0x6CED23>, "CAutomobile::ProcessControl"); //CTrailer::ProcessControl
        hookCall(0x871214, SetTowLinkHooked<0x871214>, "CAutomobile::SetTowLink", true);
        hookCall(0x871D14, GetTowHitchPosHooked<0x871D14>, "CTrailer::GetTowHitchPos", true);

        hookCall(0x6B3271, UpdateTrailerLinkHooked<0x6B3271>, "CVehicle::UpdateTrailerLink"); //CAutomobile::ProcessControl
        hookCall(0x6B329C, UpdateTrailerLinkHooked<0x6B329C>, "CVehicle::UpdateTrailerLink"); //CAutomobile::ProcessControl
        hookCall(0x6B45C7, UpdateTrailerLinkHooked<0x6B45C7>, "CVehicle::UpdateTrailerLink"); //CAutomobile::SetTowLink

        hookCall(0x6B3266, UpdateTractorLinkHooked<0x6B3266>, "CVehicle::UpdateTractorLink"); //CAutomobile::ProcessControl
        hookCall(0x6B3291, UpdateTractorLinkHooked<0x6B3291>, "CVehicle::UpdateTractorLink"); //CAutomobile::ProcessControl
        hookCall(0x6CFFAC, UpdateTractorLinkHooked<0x6CFFAC>, "CVehicle::UpdateTractorLink"); //CTrailer::SetTowLink

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
        hookASM(0x6A8052, "66 8B 4E 22 66 81 F9 AC 01",       movReg16WordPtrReg<REG_CX, REG_ESI, 0x6A805B, 5, 0xACF98166, 0x90909001>, "CAutomobile::VehicleDamage");
        hookASM(0x6A80BC, "66 81 7D 22 B0 01",                cmpWordPtrRegModel<REG_EBP, 0x6A80C2, 0x1B0>, "CAutomobile::VehicleDamage");
        hookASM(0x6A8380, "66 81 78 22 B0 01",                cmpWordPtrRegModel<REG_EAX, 0x6A8386, 0x1B0>, "CAutomobile::VehicleDamage");
        hookASM(0x6E153D, "66 81 FE D7 01",                   cmpReg16Model<REG_SI, 0x6E1542, 471>, "CVehicle::DoHeadLightReflectionSingle");
        hookASM(0x6DEC4A, "66 81 7E 22 D7 01",                cmpWordPtrRegModel<REG_ESI, 0x6DEC50, 471>, "CVehicle::AddSingleWheelParticles");
        hookASM(0x6DEEA3, "66 81 7E 22 D7 01",                cmpWordPtrRegModel<REG_ESI, 0x6DEEA9, 471>, "CVehicle::AddSingleWheelParticles");
        hookASM(0x6DF0E3, "66 81 7E 22 D7 01",                cmpWordPtrRegModel<REG_ESI, 0x6DF0E9, 471>, "CVehicle::AddSingleWheelParticles");
        hookASM(0x6DF316, "66 81 7E 22 D7 01",                cmpWordPtrRegModel<REG_ESI, 0x6DF31C, 471>, "CVehicle::AddSingleWheelParticles");
        hookASM(0x430778, "66 81 F9 BE 01",                   cmpReg16Model<REG_CX, 0x43077D, 446>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x43077F, "66 81 F9 C4 01",                   cmpReg16Model<REG_CX, 0x430784, 452>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x430786, "66 81 F9 ED 01",                   cmpReg16Model<REG_CX, 0x43078B, 493>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x431D89, "66 8B 46 22 66 3D CF 01",          movReg16WordPtrReg<REG_AX, REG_ESI, 0x431D91, 4, 0x01CF3D66>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x6B6C86, "66 81 7E 22 B0 01",                cmpWordPtrRegModel<REG_ESI, 0x6B6C8C, 0x1B0>, "CBike::DoBurstAndSoftGroundRatios");
        hookASM(0x6AF292, "66 81 7A 22 63 02",                cmpWordPtrRegModel<REG_EDX, 0x6AF298, 0x263>, "CAutomobile::GetTowBarPos");
        hookASM(0x6AF35E, "66 81 78 22 62 02",                cmpWordPtrRegModel<REG_EAX, 0x6AF364, 0x262>, "CAutomobile::GetTowBarPos");
        hookASM(0x6CF055, "66 81 7F 22 62 02",                cmpWordPtrRegModel<REG_EDI, 0x6CF05B, 0x262>, "CTrailer::ScanForTowLink");
        hookASM(0x6CFC41, "66 81 7E 22 62 02",                cmpWordPtrRegModel<REG_ESI, 0x6CFC47, 0x262>, "CTrailer::PreRender");
        
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

    if (vehOptions->enableSiren)
    {
        hookCall(0x41DC74, UsesSirenHooked<0x41DC74>, "CVehicle::UsesSiren"); //CCarAI::UpdateCarAI
        hookCall(0x41E05F, UsesSirenHooked<0x41E05F>, "CVehicle::UsesSiren"); //CCarAI::UpdateCarAI
        hookCall(0x41E874, UsesSirenHooked<0x41E874>, "CVehicle::UsesSiren"); //CCarAI::UpdateCarAI
        hookCall(0x41F10F, UsesSirenHooked<0x41F10F>, "CVehicle::UsesSiren"); //CCarAI::UpdateCarAI
        hookCall(0x462344, UsesSirenHooked<0x462344>, "CVehicle::UsesSiren"); //CRoadBlocks::CreateRoadBlockBetween2Points
        hookCall(0x4F77DA, UsesSirenHooked<0x4F77DA>, "CVehicle::UsesSiren"); //CAEVehicleAudioEntity::Initialise
        hookCall(0x61369D, UsesSirenHooked<0x61369D>, "CVehicle::UsesSiren"); //CPopulation::CreateWaitingCoppers
        hookCall(0x6B2BCB, UsesSirenHooked<0x6B2BCB>, "CVehicle::UsesSiren"); //CAutomobile::ProcessControl
        hookCall(0x6E0954, UsesSirenHooked<0x6E0954>, "CVehicle::UsesSiren"); //CVehicle::ProcessSirenAndHorn
    }

    if (vehOptions->enableLights)
    {
        hookCall(0x6ABA60, RegisterCoronaHooked<0x6ABA60>, "CCoronas::RegisterCorona"); //CAutomobile::PreRender
        hookCall(0x6ABB35, RegisterCoronaHooked<0x6ABB35>, "CCoronas::RegisterCorona"); //CAutomobile::PreRender
        hookCall(0x6ABC69, RegisterCoronaHooked<0x6ABC69>, "CCoronas::RegisterCorona"); //CAutomobile::PreRender
        if (memcmp(0x6ABA56, "68 FF 00 00 00") || forceEnableGlobal || forceEnable.contains(0x6ABA56))
            injector::MakeJMP(0x6ABA56, patchCoronas);
        else
            Log::LogModifiedAddress(0x6ABA56, "Modified method detected: CAutomobile::PreRender - 0x6ABA56 is %s\n", bytesToString(0x6ABA56, 5).c_str());

        hookCall(0x6AB80F, AddLightHooked<0x6AB80F>, "CPointLights::AddLight"); //CAutomobile::PreRender
        hookCall(0x6ABBA6, AddLightHooked<0x6ABBA6>, "CPointLights::AddLight"); //CAutomobile::PreRender

        hookCall(0x6AB34B, AddDamagedVehicleParticlesHooked<0x6AB34B>, "CVehicle::AddDamagedVehicleParticles"); //CAutomobile::PreRender
    }

    if (vehOptions->disablePayAndSpray)
        hookCall(0x44AC75, IsCarSprayableHooked<0x44AC75>, "CGarages::IsCarSprayable"); //CGarage::Update

    if (vehOptions->enableSideMissions)
    {
        hookCall(0x48DA81, IsLawEnforcementVehicleHooked<0x48DA81>, "CVehicle::IsLawEnforcementVehicle"); //056C: IS_CHAR_IN_ANY_POLICE_VEHICLE
        hookCall(0x469624, CPool__atHandleHooked<0x469624>, "CPool<CPed>::atHandle"); //00DD: IS_CHAR_IN_MODEL
        hookCall(0x4912AD, CPool__atHandleTaxiHooked<0x4912AD>, "CPool<CPed>::atHandle"); //0602: IS_CHAR_IN_TAXI
    }

    hookCall(0x85C5F4, CreateInstanceHooked<0x85C5F4>, "CVehicleModelInfo::CreateInstance", true);

    hookCall(0x4306A1, GetNewVehicleDependingOnCarModelHooked<0x4306A1>, "CCarCtrl::GetNewVehicleDependingOnCarModel"); ///CCarCtrl::GenerateOneRandomCar

    hookCall(0x6D5F2F, CPhysicalHooked<0x6D5F2F>, "CPhysical::CPhysical"); //CVehicle::CVehicle

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
