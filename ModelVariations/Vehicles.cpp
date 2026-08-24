#include "Vehicles.hpp"
#include "DataReader.hpp"
#include "Helpers.hpp"
#include "Hooks.hpp"
#include "Log.hpp"
#include "Memory.hpp"
#include "SA.hpp"
#include "VariationData.hpp"

#include <plugin.h>
#include <CCarCtrl.h>
#include <CCarGenerator.h>
#include <CFont.h>
#include <CHeli.h>
#include <CModelInfo.h>
#include <CPlane.h>
#include <CSprite.h>
#include <CTheZones.h>
#include <CVector.h>
#include <CVehicle.h>
#include <CWorld.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>

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

// The continuation thunks execute the instruction bytes overwritten by a hook,
// then transfer control back to the game.  They are emitted as immutable code
// at compile time instead of being rebuilt whenever the naked hook executes.
#pragma section(".asm", execute, read)

namespace
{
    template <uint8_t nextInstrSize, uint32_t nextInstr, uint32_t nextInstr2, std::uintptr_t jmpAddress>
    struct AsmContinuation
    {
        static_assert(sizeof(std::uintptr_t) == sizeof(uint32_t),
            "Vehicle ASM hooks require a 32-bit build");
        static_assert(nextInstrSize <= sizeof(nextInstr) + sizeof(nextInstr2),
            "At most eight overwritten instruction bytes can be replayed");

        // push <absolute address>; ret is position-independent, so the entire
        // thunk can be constant-initialized without calculating an E9 rel32 at run time.
        using Code = std::array<uint8_t, nextInstrSize + 6>;

        static constexpr Code MakeCode() noexcept
        {
            Code bytes{};

            for (std::size_t i = 0; i < nextInstrSize; ++i)
            {
                const uint32_t source = i < sizeof(nextInstr) ? nextInstr : nextInstr2;
                bytes[i] = static_cast<uint8_t>(source >> ((i % sizeof(nextInstr)) * 8));
            }

            bytes[nextInstrSize] = 0x68; // push imm32
            bytes[nextInstrSize + 1] = static_cast<uint8_t>(jmpAddress);
            bytes[nextInstrSize + 2] = static_cast<uint8_t>(jmpAddress >> 8);
            bytes[nextInstrSize + 3] = static_cast<uint8_t>(jmpAddress >> 16);
            bytes[nextInstrSize + 4] = static_cast<uint8_t>(jmpAddress >> 24);
            bytes[nextInstrSize + 5] = 0xC3; // ret

            return bytes;
        }

        static const Code code;
    };

    template <uint8_t nextInstrSize, uint32_t nextInstr, uint32_t nextInstr2, std::uintptr_t jmpAddress>
    __declspec(allocate(".asm"))
    constinit const typename AsmContinuation<nextInstrSize, nextInstr, nextInstr2, jmpAddress>::Code
        AsmContinuation<nextInstrSize, nextInstr, nextInstr2, jmpAddress>::code =
            AsmContinuation<nextInstrSize, nextInstr, nextInstr2, jmpAddress>::MakeCode();
}

static const char* dataFileName = "ModelVariations_Vehicles.ini";

unsigned short roadblockModel = 0;
unsigned short roadblockDriver = 0;
unsigned short lightsModel = 0;
int currentOccupantsGroup = -1;
unsigned short currentOccupantsModel = 0;
bool tuneParkedCar = false;

int occupantModelIndex = -1;
int clumpLoadModel = -1;

std::map<CVehicle*, std::vector<CVehicle*>> spawnedTrailers;  //<veh, <trailers>>

std::uintptr_t x6ABCBE_Destination = 0;
std::uintptr_t x4306A1_Destination = 0;

struct vehVariationProperties {
    std::array<std::vector<unsigned short>, 6> wantedVariations;
    std::unordered_map<std::string, std::vector<unsigned short>> missionVariations;
    std::array<std::vector<unsigned short>, 6> groupWantedVariations;
    std::vector<unsigned short> currentVariations;
    bool hasVariations = false;
    bool changeOnlyWhenParked = false;
    bool useOnlyGroups = false;
    bool hasGroupWantedVariations = false;
    bool tuningFullBodykit = false;
    bool replaceDriver = false;
    bool replacePassengers = false;
    std::vector<unsigned short> drivers;
    std::vector<unsigned short> passengers;
    std::vector<unsigned short> driverGroups[9];
    std::vector<unsigned short> passengerGroups[9];
    std::vector<std::vector<unsigned short>> trailers[9];
    std::vector<vehTimeGroup> timeGroups;
    std::set<unsigned short> activeTimeGroups;
    std::optional<std::pair<CVector, float>> lightPositions;
    std::optional<RwRGBA> lightColors;
    std::optional<RwRGBA> lightColors2;
    std::optional<float> lightSizes;
    std::vector<unsigned short> tuningDriverIds;
    std::optional<BYTE> tuningChances;
    std::optional<BYTE> trailersSpawnChances;
    std::optional<short> trailersHealth;
    std::vector<unsigned short> trailersMatchExtras;
    std::vector<unsigned short> trailersMatchColors;
};

struct tVehVars {

    std::array<std::unique_ptr<vehVariationProperties>, 65536> vehById{};
    std::vector<unsigned short> populatedModels;

    std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>> occupantGroups;
    std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>> trailerZones;
    std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>> tuning;
    std::unordered_map<unsigned short, std::vector<unsigned short>>* currentTuning = nullptr;

    std::vector<std::pair<CVehicle*, std::array<int, 18>>> tuningStack;
    std::vector<CVehicle*> stack;
};

static tVehVars vehVars;

static vehVariationProperties* findVehProperties(unsigned short modelId) noexcept
{
    return vehVars.vehById[modelId].get();
}

static vehVariationProperties& getOrCreateVehProperties(unsigned short modelId)
{
    auto& properties = vehVars.vehById[modelId];
    if (!properties)
    {
        properties = std::make_unique<vehVariationProperties>();
        vehVars.populatedModels.push_back(modelId);
    }

    return *properties;
}


struct tVehOptions {
    bool changeCarGenerators = false;
    bool changeScriptedCars = false;
    bool disablePayAndSpray = false;
    bool enableLights = false;
    bool enableTrailerLights = false;
    bool enableSideMissions = false;
    bool enableSiren = false;
    bool enableSpecialFeatures = false;
    std::vector<unsigned short> carGenExclude;
    std::vector<unsigned short> inheritExclude;
};

static tVehOptions vehOptions;


float getDistanceFromVeh(CVehicle* vehicle, CEntity* target)
{
    if (vehicle == NULL || target == NULL)
        return 0.0f;

    return (vehicle->GetPosition() - target->GetPosition()).Magnitude();
}

bool isVehicleVisible(CVehicle* veh) 
{
    if (veh == NULL || veh->m_pRwObject == NULL)
        return false;

    CBaseModelInfo* modelInfo = CModelInfo::GetModelInfo(veh->m_nModelIndex);
    if (modelInfo == NULL)
        return false;

    CColModel* colModel = modelInfo->m_pColModel;
    if (colModel == NULL)
        return false;

    const CBox& box = colModel->m_boundBox;
    CMatrix* mat = veh->GetMatrix();
    if (mat == NULL)
        return false;

    const CVector camPos = *reinterpret_cast<CVector*>(0xB6F930);

    auto LocalToWorld = [&](const CVector& local) -> CVector {
        return CVector(
            mat->GetRight().x * local.x + mat->GetForward().x * local.y + mat->GetUp().x * local.z + mat->GetPosition().x,
            mat->GetRight().y * local.x + mat->GetForward().y * local.y + mat->GetUp().y * local.z + mat->GetPosition().y,
            mat->GetRight().z * local.x + mat->GetForward().z * local.y + mat->GetUp().z * local.z + mat->GetPosition().z
        );
        };

    // Use a height around the middle/top of the vehicle so ground/curbs block less often.
    const float z = (box.m_vecMin.z + box.m_vecMax.z) * 0.5f;

    CVector testPoints[6] = {
        LocalToWorld(CVector(box.m_vecMin.x, box.m_vecMin.y, z)), // rear-left
        LocalToWorld(CVector(box.m_vecMax.x, box.m_vecMin.y, z)), // rear-right
        LocalToWorld(CVector(box.m_vecMin.x, box.m_vecMax.y, z)), // front-left
        LocalToWorld(CVector(box.m_vecMax.x, box.m_vecMax.y, z)), // front-right
        LocalToWorld(CVector(box.m_vecMin.x, 0.0f,       z)),     // left middle
        LocalToWorld(CVector(box.m_vecMax.x, 0.0f,       z))      // right middle
    };

    for (int i = 0; i < 6; ++i) {
        const CVector& targetPos = testPoints[i];

        CColPoint hitPoint;
        CEntity* hitEntity = NULL;

        bool hitSomething = CWorld::ProcessLineOfSight(
            camPos,
            targetPos,
            hitPoint,
            hitEntity,
            true,   // buildings
            true,   // vehicles
            false,  // peds
            true,   // objects
            true,   // dummies
            true,   // doSeeThroughCheck
            true,   // doCameraIgnoreCheck
            false   // doShootThroughCheck
        );

        // If at least one point is not blocked, or the first hit is this vehicle,
        // we consider the vehicle visible.
        if (!hitSomething || hitEntity == veh)
            return true;
    }

    return false;
}

bool isAnotherVehicleBehind(CVehicle* veh, const std::vector<CVehicle*>& exceptions)
{
    auto polygonsOverlap = [](const std::vector<CVector2D>& a, const std::vector<CVector2D>& b)
    {
        auto separated = [](const std::vector<CVector2D>& p, const std::vector<CVector2D>& q)
        {
            for (unsigned int i = 0; i < p.size(); i++) {
                const CVector2D& p1 = p[i];
                const CVector2D& p2 = p[(i + 1) % p.size()];

                CVector2D axis = { -(p2.y - p1.y), p2.x - p1.x };

                auto dot = [&](const CVector2D& v) {
                    return v.x * axis.x + v.y * axis.y;
                };

                float minP = dot(p[0]), maxP = minP;
                float minQ = dot(q[0]), maxQ = minQ;

                for (const auto& v : p) {
                    float d = dot(v);
                    minP = std::min(minP, d);
                    maxP = std::max(maxP, d);
                }

                for (const auto& v : q) {
                    float d = dot(v);
                    minQ = std::min(minQ, d);
                    maxQ = std::max(maxQ, d);
                }

                if (maxP < minQ || maxQ < minP)
                    return true;
            }

            return false;
        };

        return !separated(a, b) && !separated(b, a);
    };

    auto* mInfo = CModelInfo::GetModelInfo(veh->m_nModelIndex);
    if (mInfo == NULL || mInfo->m_pColModel == NULL)
        return false;

    CVector vmin = mInfo->m_pColModel->m_boundBox.m_vecMin;
    CVector vmax = mInfo->m_pColModel->m_boundBox.m_vecMax;

    CVector bottom_left = veh->TransformFromObjectSpace({ vmin.x, vmin.y * 3.0f, 0.0f });
    CVector bottom_right = veh->TransformFromObjectSpace({ vmax.x, vmin.y * 3.0f, 0.0f });
    CVector top_right = veh->TransformFromObjectSpace({ vmax.x, vmin.y, 0.0f });
    CVector top_left = veh->TransformFromObjectSpace({ vmin.x, vmin.y, 0.0f });

    std::vector<CVector2D> polygon = {
        { top_left.x, top_left.y },
        { top_right.x, top_right.y },
        { bottom_right.x, bottom_right.y },
        { bottom_left.x, bottom_left.y }
    };

    for (const auto& i : CPools::ms_pVehiclePool)
    {
        if (std::abs(i->GetPosition().z - veh->GetPosition().z) > 25.0f)
            continue;

        bool exceptionFound = false;
        for (auto j : exceptions)
            if (j == i)
            {
                exceptionFound = true;
                break;
            }
        if (exceptionFound)
            continue;

        auto* mInfoTarget = CModelInfo::GetModelInfo(i->m_nModelIndex);
        if (i != veh && mInfoTarget != NULL && mInfoTarget->m_pColModel != NULL)
        {
            CVector vminTarget = mInfoTarget->m_pColModel->m_boundBox.m_vecMin;
            CVector vmaxTarget = mInfoTarget->m_pColModel->m_boundBox.m_vecMax;

            std::vector<CVector2D> targetPolygon = {
                convert3DVectorTo2D(i->TransformFromObjectSpace({ vminTarget.x, vmaxTarget.y, 0.0f })),
                convert3DVectorTo2D(i->TransformFromObjectSpace({ vmaxTarget.x, vmaxTarget.y, 0.0f })),
                convert3DVectorTo2D(i->TransformFromObjectSpace({ vmaxTarget.x, vminTarget.y, 0.0f })),
                convert3DVectorTo2D(i->TransformFromObjectSpace({ vminTarget.x, vminTarget.y, 0.0f }))
            };

            if (polygonsOverlap(polygon, targetPolygon))
                return true;
        }
    }

    return false;
}

int getTuningPartSlot(int model)
{
    const auto mInfo = CModelInfo::GetModelInfo(model);

    if (mInfo == NULL)
        return -1;

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

    return -1;
}

int getPedModelForCopType(int ctype) 
{
    switch (ctype) 
    {
        case COP_TYPE_CITYCOP:
            return CStreaming__GetDefaultCopModel();
        case COP_TYPE_LAPDM1:
            return MODEL_LAPDM1;
        case COP_TYPE_CSHER:
            return MODEL_CSHER;
        case COP_TYPE_ARMY:
            return MODEL_ARMY;
        case COP_TYPE_FBI:
            return MODEL_FBI;
        case COP_TYPE_SWAT1:
        case COP_TYPE_SWAT2:
            return MODEL_SWAT;
        default:
            return -1;
    }
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

    if (veh == NULL)
    {
        Log::Write("processTuning veh is NULL\n");
        return;
    }

    if (veh->m_nCreatedBy == eVehicleCreatedBy::MISSION_VEHICLE || vehVars.currentTuning == nullptr)
        return;

    auto it = vehVars.currentTuning->find(veh->m_nModelIndex);
    if (it != vehVars.currentTuning->end() && !it->second.empty())
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

        const auto* properties = findVehProperties(veh->m_nModelIndex);

        std::array<bool, 18> slotsSelected = {};
        for (unsigned int i = 0; i < 18; i++)
            if (properties && properties->tuningChances)
                slotsSelected[i] = (*properties->tuningChances > 0) && (rand<uint32_t>(0, 100) < *properties->tuningChances);
            else
                slotsSelected[i] = rand<uint32_t>(0, 3) == 0;

        if (properties && properties->tuningFullBodykit)
            if (slotsSelected[14] == true || slotsSelected[15] == true || slotsSelected[3] == true)
                slotsSelected[14] = slotsSelected[15] = slotsSelected[3] = true;

        std::array<int, 18> selectedParts;
        selectedParts.fill(-1);
        bool install = false;

        for (unsigned int slot = 0; slot < 18; slot++)
        {
            if (!slotsSelected[slot])
                continue;

            if (partsToInstall[slot].empty())
                continue;

            const uint32_t index = rand<uint32_t>(0, partsToInstall[slot].size());
            selectedParts[slot] = partsToInstall[slot][index];
            install = true;
        }

        if (install)
            vehVars.tuningStack.emplace_back(veh, selectedParts);
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
    if (veh == NULL)
        return;

    const auto* properties = findVehProperties(veh->m_nModelIndex);
    if ((properties && properties->useOnlyGroups) || rand<bool>())
    {
        std::vector<unsigned short> zoneGroups;

        if (currentZone)
            if (auto it = vehVars.occupantGroups.find(*reinterpret_cast<uint64_t*>(currentZone->m_szLabel)); it != vehVars.occupantGroups.end())
                if (auto it2 = it->second.find(veh->m_nModelIndex); it2 != it->second.end())
                    zoneGroups = it2->second;

        if (!zoneGroups.empty())
        {
            const CWanted* wanted = FindPlayerWanted(-1);
            const unsigned int wantedLevel = wanted ? (wanted->m_nWantedLevel - (wanted->m_nWantedLevel ? 1 : 0)) : 0;
            currentOccupantsModel = veh->m_nModelIndex;
            if (properties)
            {
                if (properties->hasGroupWantedVariations)
                    vectorfilterVector(zoneGroups, properties->groupWantedVariations[wantedLevel]);

                for (auto i : properties->activeTimeGroups)
                    vectorfilterVector(zoneGroups, properties->timeGroups[i].occupantGroups);
            }

            currentOccupantsGroup = vectorGetRandom(zoneGroups) - 1;
        }
    }
}

int getRandomVariation(const int modelid, bool parked = false)
{
    if (modelid < 400 || modelid >= 65536)
        return modelid;

    const auto* properties = findVehProperties(static_cast<unsigned short>(modelid));
    if (!properties || properties->currentVariations.empty())
        return modelid;

    if (!parked && properties->changeOnlyWhenParked)
        return modelid;

    const unsigned short variationModel = vectorGetRandom(properties->currentVariations);
    if (variationModel > 0 && variationModel != modelid)
    {
        if (auto loadState = loadModel(variationModel, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
        {
            Log::Write("Error loading vehicle model %d (%s) %s\n", variationModel, modelNames.contains(variationModel) ? modelNames[variationModel].c_str() : "", getLoadStateString(loadState));
            return modelid;
        }

        Log::WriteVerbose("Selected variation %u for vehicle model %u\n", variationModel, modelid);

        return variationModel;
    }
    return modelid;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void VehicleVariations::ClearData()
{
    for (auto modelId : vehVars.populatedModels)
        vehVars.vehById[modelId].reset();
    vehVars.populatedModels.clear();

    vehVars.occupantGroups.clear();
    vehVars.trailerZones.clear();
    vehVars.currentTuning = nullptr;
    vehVars.tuning.clear();

    vehVars.tuningStack.clear();
    vehVars.stack.clear();

    spawnedTrailers.clear();

    vehOptions = {};
}

void VehicleVariations::LoadData()
{
    DataReader dataFile(dataFileName);

    vehOptions.changeCarGenerators   = dataFile.ReadBoolean("Settings", "ChangeCarGenerators", false);
    vehOptions.changeScriptedCars    = dataFile.ReadBoolean("Settings", "ChangeScriptedCars", false);
    vehOptions.disablePayAndSpray    = dataFile.ReadBoolean("Settings", "DisablePayAndSpray", false);
    vehOptions.enableLights          = dataFile.ReadBoolean("Settings", "EnableLights", false);
    vehOptions.enableTrailerLights   = dataFile.ReadBoolean("Settings", "EnableTrailerBrakeLights", false);
    vehOptions.enableSideMissions    = dataFile.ReadBoolean("Settings", "EnableSideMissions", false);
    vehOptions.enableSiren           = dataFile.ReadBoolean("Settings", "EnableSiren", false);
    vehOptions.enableSpecialFeatures = dataFile.ReadBoolean("Settings", "EnableSpecialFeatures", false);
    vehOptions.carGenExclude         = dataFile.ReadLine("Settings", "ExcludeCarGeneratorModels", READ_VEHICLES);
    vehOptions.inheritExclude        = dataFile.ReadLine("Settings", "ExcludeModelsFromInheritance", READ_VEHICLES);

    Log::Write("\nReading vehicle data...\n");

    for (auto& iniData : dataFile.data)
    {
        std::string section(iniData.first);
        Log::Write("%s\n", section.c_str());
        int iModel = 0;
        if (section[0] >= '0' && section[0] <= '9')
             fromString<int>(section, iModel);
        else
            CModelInfo::GetModelInfo(section.data(), &iModel);

        if (iModel >= 400 && iModel < 65535)
        {
            unsigned short modelid = (unsigned short)iModel;
            auto& properties = getOrCreateVehProperties(modelid);
            std::map<CZone*, std::vector<unsigned short>> zones;

            if (dataFile.ReadBoolean(section, "ChangeOnlyParked", false))
                properties.changeOnlyWhenParked = true;

            for (auto& kvp : iniData.second)
            {
                if (auto it = presetAllZones.find(std::string(kvp.first)); it != presetAllZones.end())
                {
                    auto vec = dataFile.ReadLine(section, kvp.first, READ_VEHICLES);

                    if (!vec.empty())
                    {
                        properties.hasVariations = true;
                        if (it->second.empty()) //Global
                        {
                            for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
                            {
                                CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);
                                zones[zone] = vectorUnion(zones[zone], vec);
                            }
                        }
                        else for (auto zone : it->second)
                            zones[zone] = vectorUnion(zones[zone], vec);
                    }

                    //Occupant Groups
                    vec = dataFile.ReadLine(section, kvp.first, READ_OCCUPANT_GROUPS);

                    if (!vec.empty())
                    {
                        if (it->second.empty())
                            for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
                            {
                                CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);
                                uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                                vehVars.occupantGroups[zoneName][modelid] = vectorUnion(vehVars.occupantGroups[zoneName][modelid], vec);
                            }
                        else for (auto zone : it->second)
                        {
                            uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                            vehVars.occupantGroups[zoneName][modelid] = vectorUnion(vehVars.occupantGroups[zoneName][modelid], vec);
                        }
                    }

                    //Tuning
                    vec = dataFile.ReadLine(section, kvp.first, READ_TUNING);

                    if (!vec.empty())
                    {
                        if (it->second.empty()) //Global
                            for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
                            {
                                CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);
                                uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                                vehVars.tuning[zoneName][modelid] = vectorUnion(vehVars.tuning[zoneName][modelid], vec);
                            }
                        else for (auto zone : it->second)
                        {
                            uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                            vehVars.tuning[zoneName][modelid] = vectorUnion(vehVars.tuning[zoneName][modelid], vec);
                        }
                    }

                    //Trailers
                    vec = dataFile.ReadLine(section, kvp.first, READ_TRAILERS);

                    if (!vec.empty())
                    {
                        if (it->second.empty()) //Global
                            for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
                            {
                                CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);
                                uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                                vehVars.trailerZones[zoneName][modelid] = vectorUnion(vehVars.trailerZones[zoneName][modelid], vec);
                            }
                        else for (auto zone : it->second)
                        {
                            uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                            vehVars.trailerZones[zoneName][modelid] = vectorUnion(vehVars.trailerZones[zoneName][modelid], vec);
                        }
                    }
                }
                else if (kvp.first.size() >= 8 && kvp.first.starts_with("MISSION"))
                {
                    auto vec = dataFile.ReadLine(section, kvp.first, READ_VEHICLES);

                    if (!vec.empty())
                        properties.missionVariations.insert({ (kvp.first[7] == '_') ? std::string(kvp.first.substr(8)) : std::string(kvp.first), vec });
                }
            }

            bool mergeZones = dataFile.ReadBoolean(section, "MergeZonesWithAreas", false);

            for (auto& kvp : iniData.second)
            {
                if (kvp.first.size() > 1 && (kvp.first[1] < 'a' || kvp.first[1] > 'z'))
                {
                    uint64_t zoneName = 0;
                    copyString((char*)&zoneName, kvp.first.data(), std::min<std::size_t>(8, kvp.first.size()));

                    auto vec = dataFile.ReadLine(section, kvp.first, READ_VEHICLES);
                    if (!vec.empty())
                    {
                        properties.hasVariations = true;
                        auto zone = getZone(kvp.first);
                        if (zone != NULL)
                            zones[zone] = mergeZones ? vectorUnion(zones[zone], vec) : vec;
                    }

                    //Groups
                    vec = dataFile.ReadLine(section, kvp.first, READ_OCCUPANT_GROUPS);
                    if (!vec.empty())
                        vehVars.occupantGroups[zoneName][modelid] = mergeZones ? vectorUnion(vehVars.occupantGroups[zoneName][modelid], vec) : vec;

                    //Tuning
                    vec = dataFile.ReadLine(section, kvp.first, READ_TUNING);
                    if (!vec.empty())
                        vehVars.tuning[zoneName][modelid] = mergeZones ? vectorUnion(vehVars.tuning[zoneName][modelid], vec) : vec;

                    //Trailers
                    vec = dataFile.ReadLine(section, kvp.first, READ_TRAILERS);
                    if (!vec.empty())
                        vehVars.trailerZones[zoneName][modelid] = mergeZones ? vectorUnion(vehVars.trailerZones[zoneName][modelid], vec) : vec;
                }
            }

            for (auto& [zone, values] : zones)
                variations[zoneGetIndex(zone)][modelid] = variationSetsAdd(std::move(values));
                
            for (unsigned i = 0; i < 6; i++)
            {
                auto vec = dataFile.ReadLine(section, "Wanted" + std::to_string(i+1), READ_VEHICLES);
                if (vec.empty())
                    continue;
                properties.wantedVariations[i] = vec;
            }

            for (const auto& i : variations)
                if (auto it = i.second.find(modelid); it != i.second.end() && it->second < variationSets.size())
                    for (auto variation : variationSets[it->second])
                        if (variation > 0 && variation != modelid && !(vectorHasId(vehOptions.inheritExclude, variation)))
                            setOriginalModel(variation, modelid);


            const int tuningChance = dataFile.ReadInteger(section, "TuningChance", -1);
            if (tuningChance > -1 && !properties.tuningChances)
                properties.tuningChances = static_cast<BYTE>(std::min(tuningChance, 100));

            if (dataFile.ReadBoolean(section, "UseOnlyGroups", false))
                properties.useOnlyGroups = true;

            if (vehOptions.enableLights)
            {
                const float lightSize = dataFile.ReadFloat(section, "LightSize", -1.0);
                const float lightWidth = dataFile.ReadFloat(section, "LightWidth", -999.0);
                const float lightX = dataFile.ReadFloat(section, "LightX", 0.0);
                const float lightY = dataFile.ReadFloat(section, "LightY", 0.0);
                const float lightZ = dataFile.ReadFloat(section, "LightZ", 0.0);

                int r = dataFile.ReadInteger(section, "LightR", -1);
                int g = dataFile.ReadInteger(section, "LightG", -1);
                int b = dataFile.ReadInteger(section, "LightB", -1);
                int a = dataFile.ReadInteger(section, "LightA", -1);

                if (lightSize > 0.0 && !properties.lightSizes)
                    properties.lightSizes = lightSize;

                if (!properties.lightColors && (uint8_t)r == r && (uint8_t)g == g && (uint8_t)b == b && (uint8_t)a == a)
                {
                    RwRGBA colors = { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
                    properties.lightColors = colors;
                }

                if (!properties.lightPositions && (lightX != 0.0 || lightY != 0.0 || lightZ != 0.0 || lightWidth > -900.0))
                    properties.lightPositions = std::pair<CVector, float>{ { lightX, lightY, lightZ }, lightWidth };

                r = dataFile.ReadInteger(section, "LightR2", -1);
                g = dataFile.ReadInteger(section, "LightG2", -1);
                b = dataFile.ReadInteger(section, "LightB2", -1);
                a = dataFile.ReadInteger(section, "LightA2", -1);

                if (!properties.lightColors2 && (uint8_t)r == r && (uint8_t)g == g && (uint8_t)b == b && (uint8_t)a == a)
                {
                    RwRGBA colors = { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
                    properties.lightColors2 = colors;
                }
            }

            uint8_t trailersNum = 0;
            for (int j = 0; j < 9; j++)
            {
                auto vec = dataFile.ReadTrailerLine(section, "Trailers" + std::to_string(j + 1));
                if (vec.empty())
                    break;

                if (properties.trailers[j].empty())
                    properties.trailers[j] = vec;
                trailersNum++;                   
            }

            for (auto& zoneEntry : vehVars.trailerZones)
            {
                auto itModel = zoneEntry.second.find(modelid);
                if (itModel != zoneEntry.second.end())
                    checkNumGroups(itModel->second, trailersNum);
            }

            uint8_t numGroups = 0;
            for (int j = 0; j < 9; j++)
            {
                std::vector<unsigned short> vecDrivers = dataFile.ReadLine(section, "DriverGroup" + std::to_string(j + 1), READ_PEDS);
                if (!vecDrivers.empty())
                {
                    std::vector<unsigned short> vecPassengers = dataFile.ReadLine(section, "PassengerGroup" + std::to_string(j + 1), READ_PEDS);
                    if (!vecPassengers.empty())
                    {
                        if (properties.passengerGroups[j].empty())
                            properties.passengerGroups[j] = vecPassengers;
                        if (properties.driverGroups[j].empty())
                            properties.driverGroups[j] = vecDrivers;
                        numGroups++;
                        continue;
                    }
                }        
                break;
            }

            for (unsigned short j = 0; j < 6; j++)
            {
                std::vector<unsigned short> vec = dataFile.ReadLineUnique(section, "Wanted" + std::to_string(j + 1), READ_OCCUPANT_GROUPS);
                if (!vec.empty())
                {
                    checkNumGroups(vec, numGroups);
                    properties.groupWantedVariations[j] = vec;
                    properties.hasGroupWantedVariations = true;
                }
            }

            for (auto& zoneEntry : vehVars.occupantGroups) 
            {
                auto itModel = zoneEntry.second.find(modelid);
                if (itModel != zoneEntry.second.end())
                    checkNumGroups(itModel->second, numGroups);
            }

            for (unsigned int j = 0; j < 9; j++)
            {
                auto groupStart = dataFile.ReadInteger(section, "TimeGroup" + std::to_string(j + 1) + "Start", -1);
                if (groupStart > -1)
                {
                    auto groupEnd = dataFile.ReadInteger(section, "TimeGroup" + std::to_string(j + 1) + "End", -1);
                    if (groupEnd > -1)
                    {
                        auto vec = dataFile.ReadLine(section, "TimeGroup" + std::to_string(j + 1), READ_VEHICLES);
                        auto vec2 = dataFile.ReadLine(section, "TimeGroup" + std::to_string(j + 1), READ_OCCUPANT_GROUPS);
                        auto vec3 = dataFile.ReadLine(section, "TimeGroup" + std::to_string(j + 1), READ_TRAILERS);

                        if (!vec.empty() || !vec2.empty() || !vec3.empty())
                        {
                            properties.timeGroups.push_back(vehTimeGroup((unsigned short)groupStart, (unsigned short)groupEnd, vec2, vec3, vec));
                            continue;
                        }
                    }
                }
                break;
            }

            std::vector<unsigned short> vec = dataFile.ReadLine(section, "Drivers", READ_PEDS);
            if (!vec.empty() && properties.drivers.empty())
                properties.drivers = vec;

            vec = dataFile.ReadLine(section, "Passengers", READ_PEDS);
            if (!vec.empty() && properties.passengers.empty())
                properties.passengers = vec;

            vec = dataFile.ReadLine(section, "ParentModel", READ_VEHICLES);
            if (!vec.empty() && vec[0] >= 400)
                setOriginalModel(modelid, vec[0]);

            vec = dataFile.ReadLine(section, "TrailersMatchExtras", READ_NUMS);
            if (!vec.empty())
                properties.trailersMatchExtras = vec;

            vec = dataFile.ReadLine(section, "TrailersMatchColors", READ_NUMS);
            if (!vec.empty())
                properties.trailersMatchColors = vec;

            vec = dataFile.ReadLine(section, "TuningDriverIDs", READ_PEDS);
            if (!vec.empty())
                properties.tuningDriverIds = vec;

            const int trailersSpawnChance = dataFile.ReadInteger(section, "TrailersSpawnChance", -1);
            if (trailersSpawnChance > -1 && !properties.trailersSpawnChances)
                properties.trailersSpawnChances = static_cast<BYTE>(trailersSpawnChance > 100 ? 100 : trailersSpawnChance);

            const short trailersHealth = (short)dataFile.ReadInteger(section, "TrailersHealth", -1);
            if (trailersHealth > -1 && !properties.trailersHealth)
                properties.trailersHealth = trailersHealth;

            properties.tuningFullBodykit = dataFile.ReadBoolean(section, "TuningFullBodykit", false);
            properties.replaceDriver = dataFile.ReadBoolean(section, "ReplaceDriver", false);
            properties.replacePassengers = dataFile.ReadBoolean(section, "ReplacePassengers", false);
        }
    }

    std::sort(vehVars.populatedModels.begin(), vehVars.populatedModels.end());

    for (int i = 0; i < 65536; i++)
        if (getVariationOriginalModel(i) == 0)
            setOriginalModel(i, i);

    Log::Write("\n");
}

void VehicleVariations::Process()
{
    int variationsUpdateQueued = 0;

    static int lastGameTime = -1;
    int gameTime = (CClock__ms_nGameClockHours * 100 + CClock__ms_nGameClockMinutes);

    if (vehVars.populatedModels.empty())
        lastGameTime = -1;

    if (gameTime != lastGameTime)
    {
        lastGameTime = gameTime;
        for (auto modelId : vehVars.populatedModels)
            if (auto* properties = findVehProperties(modelId))
                for (auto it = properties->activeTimeGroups.begin(); it != properties->activeTimeGroups.end();)
                {
                    auto index = *it;

                    if (!isTimeInRange(gameTime, properties->timeGroups[index].start, properties->timeGroups[index].end))
                    {
                        it = properties->activeTimeGroups.erase(it);
                        variationsUpdateQueued = modelId;
                    }
                    else
                    {
                        ++it;
                    }
                }

        for (auto modelId : vehVars.populatedModels)
            if (auto* properties = findVehProperties(modelId))
                for (unsigned int i = 0; i < properties->timeGroups.size(); i++)
                {
                    if (isTimeInRange(gameTime, properties->timeGroups[i].start, properties->timeGroups[i].end))
                        if (properties->activeTimeGroups.insert((unsigned short)i).second == true)
                            variationsUpdateQueued = modelId;
                }
    }

    if (variationsUpdateQueued > 0)
    {
        std::string gameTimeString = msprintf("%02d:%02d", CClock__ms_nGameClockHours, CClock__ms_nGameClockMinutes);
        Log::Write("Updating vehicle variations due to model %d time groups. Game time: %s\n", variationsUpdateQueued, gameTimeString.c_str());
        UpdateVariations();
        VehicleVariations::LogCurrentVariations();
        Log::Write("\n");
        if (Log::Write("Active time groups\n"))
        {
            for (auto modelId : vehVars.populatedModels)
            {
                const auto* properties = findVehProperties(modelId);
                if (properties && !properties->activeTimeGroups.empty())
                {
                    Log::Write("%d: ", modelId);
                    for (auto j : properties->activeTimeGroups)
                        Log::Write("%u ", j + 1);
                    Log::Write("\n");
                }
            }
            Log::Write("\n\n");
        }
        variationsUpdateQueued = 0;
    }

    for (auto it = spawnedTrailers.begin(); it != spawnedTrailers.end(); )
    {
        CVehicle* veh = it->first;
        if (IsVehiclePointerValid(veh))
        {
            if ((CTimer::m_snTimeInMilliseconds - veh->m_nCreationTime) < 500)
            {
                bool trailerAttached = false;

                for (unsigned i = 0; i < it->second.size(); i++)
                    if (IsVehiclePointerValid(it->second[i]) && it->second[i]->m_pTractor == NULL)
                    {
                        if (i == 0)
                            trailerAttached |= it->second[i]->SetTowLink(veh, 1);
                        else if (IsVehiclePointerValid(it->second[i - 1]))
                            trailerAttached |= it->second[i]->SetTowLink(it->second[i - 1], 1);
                    }

                if (trailerAttached)
                    for (auto trailer : it->second)
                        if (IsVehiclePointerValid(trailer))
                            if (trailer->m_pTractor && (isAnotherVehicleBehind(veh, it->second) || isAnotherVehicleBehind(trailer, it->second) || CPhysical__TestCollision(trailer, false)))
                            {
                                for (auto& j : it->second)
                                    destroyVehicleAndOccupants(j);

                                it->second.clear();
                                break;
                            }
            }

            if ((CTimer::m_snTimeInMilliseconds - veh->m_nCreationTime) < 3900) //delete far detached trailers
            {
                bool deleteTrailers = false;
                bool trailersClose = false;

                for (auto trailer : it->second)
                    if (IsVehiclePointerValid(trailer))
                    {
                        if (trailer->m_pTractor == NULL)
                            deleteTrailers = true;

                        if ((veh->GetPosition() - trailer->GetPosition()).Magnitude() < 50.0)
                            trailersClose = true;
                    }

                if (deleteTrailers && !trailersClose)
                {
                    for (auto trailer : it->second)
                        if (IsVehiclePointerValid(trailer))
                            destroyVehicleAndOccupants(trailer);

                    it->second.clear();
                }
            }
            it++;
        }
        else
            it = spawnedTrailers.erase(it);
    }
    
    while (!vehVars.tuningStack.empty())
    {
        const auto it = vehVars.tuningStack.back();
        vehVars.tuningStack.pop_back();

        if (!IsVehiclePointerValid(it.first))
            continue;

        const auto* properties = findVehProperties(it.first->m_nModelIndex);
        if (!properties || properties->tuningDriverIds.empty() || vectorHasId(properties->tuningDriverIds, it.first->m_pDriver == NULL ? 0 : it.first->m_pDriver->m_nModelIndex))
            for (int selectedPart : it.second)
                if (selectedPart > -1)
                {
                    const unsigned short part = static_cast<unsigned short>(selectedPart);
                    if (part <= 20)
                        it.first->SetRemap(part);
                    else
                    {
                        CStreaming__RequestVehicleUpgrade(part, PRIORITY_REQUEST);
                        CStreaming__LoadAllRequestedModels(false);

                        auto partLoadState = CStreamingInfo__ms_pArrayBase[part].m_nLoadState;
                        
                        if (partLoadState != LOADSTATE_LOADED)
                            Log::Write("Error loading (%s) tuning part model %d (%s) for vehicle id %u\n", getLoadStateString(partLoadState), part, modelNames.contains(part) ? modelNames[part].c_str() : "", it.first->m_nModelIndex);
                        else
                        {
                            short otherUpgrade = CVehicleModelInfo__CLinkedUpgradeList__FindOtherUpgrade(CVehicleModelInfo__ms_linkedUpgrades, part);
                            unsigned char pairLoadState = otherUpgrade > -1 ? CStreamingInfo__ms_pArrayBase[otherUpgrade].m_nLoadState : LOADSTATE_NOT_LOADED;
                            if (otherUpgrade > -1 && pairLoadState != LOADSTATE_LOADED)
                            {
                                Log::Write("Error loading (%s) pair tuning part model %d (%s) for vehicle id %u\n", getLoadStateString(pairLoadState), otherUpgrade, modelNames.contains(otherUpgrade) ? modelNames[otherUpgrade].c_str() : "", it.first->m_nModelIndex);
                                continue;
                            }
                            it.first->AddVehicleUpgrade(part);
                            CStreaming__SetMissionDoesntRequireModel(part);
                            
                            if (otherUpgrade > -1)
                                CStreaming__SetMissionDoesntRequireModel(otherUpgrade);
                        }
                    }
                }
    }

    while (!vehVars.stack.empty())
    {
        CVehicle* veh = vehVars.stack.back();
        vehVars.stack.pop_back();

        if (!IsVehiclePointerValid(veh) || veh->m_nCreatedBy == eVehicleCreatedBy::MISSION_VEHICLE)
            continue;

        const auto* properties = findVehProperties(veh->m_nModelIndex);
        if (properties && !properties->currentVariations.empty() && properties->currentVariations[0] == 0)
            DestroyVehicleAndDriverAndPassengers(veh);
        else
        {
            if (properties && !properties->passengers.empty() && properties->passengers[0] == 0)
                for (int i = 0; i < 8; i++)
                {
                    CPed* passenger = veh->m_apPassengers[i];
                    if (passenger != NULL && passenger->m_nModelIndex > 0 && passenger->m_nCreatedBy != 2)
                    {
                        if (passenger->m_pIntelligence)
                            passenger->m_pIntelligence->FlushImmediately(false);
                        CTheScripts__RemoveThisPed(passenger);
                    }
                }

            bool spawnTrailer = rand<uint32_t>(0, 3) == 0;

            if (properties && properties->trailersSpawnChances)
                spawnTrailer = rand<uint32_t>(0, 100) < *properties->trailersSpawnChances;

            for (auto &i : spawnedTrailers)
                if (!i.second.empty() && i.second[0] == veh && veh->m_pTractor && isAnotherVehicleBehind(veh, i.second))
                {
                    for (auto& j : i.second)
                        destroyVehicleAndOccupants(j);

                    i.second.clear();
                    break;
                }
            
            if (IsVehiclePointerValid(veh) && veh->m_pDriver && veh->m_pDriver != FindPlayerPed() && spawnTrailer && !isAnotherVehicleBehind(veh, {}))
            {
                std::vector<unsigned short> zoneTrailers;
                if (currentZone)
                    if (auto it = vehVars.trailerZones.find(*reinterpret_cast<uint64_t*>(currentZone->m_szLabel)); it != vehVars.trailerZones.end())
                        if (auto it2 = it->second.find(veh->m_nModelIndex); it2 != it->second.end())
                            zoneTrailers = it2->second;

                if (properties)
                    for (auto i : properties->activeTimeGroups)
                        vectorfilterVector(zoneTrailers, properties->timeGroups[i].trailers);

                if (zoneTrailers.empty())
                    continue;
                
                auto trailerConfigSelected = vectorGetRandom(zoneTrailers) - 1;
                if (trailerConfigSelected < 0)
                    continue;
                if (!properties || properties->trailers[trailerConfigSelected].empty())
                    continue;

                CVehicle* previous = veh;
                CCarCtrl::SwitchVehicleToRealPhysics(veh);

                bool trailerMatchExtras = vectorHasId(properties->trailersMatchExtras, trailerConfigSelected + 1);
                bool trailerMatchColors = vectorHasId(properties->trailersMatchColors, trailerConfigSelected + 1);

                const auto& trailerConfigurations = properties->trailers[trailerConfigSelected];
                const std::vector<unsigned short> &trailersVec = trailerConfigurations[CGeneral::GetRandomNumberInRange(0, (int)trailerConfigurations.size())];
                CVehicle* firstTrailer = NULL;
                for (auto trailerModel : trailersVec)
                {
                    if (auto loadState = loadModel(trailerModel, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
                    {
                        Log::Write("Error loading vehicle model %d (%s) %s\n", trailerModel, modelNames.contains(trailerModel) ? modelNames[trailerModel].c_str() : "", getLoadStateString(loadState));
                        break;
                    }

                    if (trailersVec.size() == 1 && trailerMatchExtras)
                    {
                        CVehicleModelInfo::ms_compsToUse[0] = veh->m_anExtras[0];
                        //CVehicleModelInfo::ms_compsToUse[1] = veh->m_anExtras[1];
                    }

                    CVehicle* trailer = CCarCtrl::GetNewVehicleDependingOnCarModel(trailerModel, RANDOM_VEHICLE);
                    if (firstTrailer == NULL)
                        firstTrailer = trailer;

                    if (trailer && IsVehiclePointerValid(veh))
                    {
                        auto newPos = previous->GetPosition();
                        newPos.z = CWorld::FindGroundZForCoord(newPos.x, newPos.y) - 5.0f;

                        CWorld::Add(trailer);
                        //CTheScripts::ClearSpaceForMissionEntity(previous->GetPosition(), trailer);
                        spawnedTrailers[veh].push_back(trailer);
                        trailer->SetPosn(newPos);
                        if (previous == veh)
                            if (!trailer->SetTowLink(previous, 1))
                                Log::Write("SetTowLink() failed for vehicle %d and trailer %d.\n", veh->m_nModelIndex, trailer->m_nModelIndex);

                        previous = trailer;
                        if (properties->trailersHealth)
                            trailer->m_fHealth = static_cast<float>(*properties->trailersHealth);

                        if (trailerMatchColors)
                        {
                            trailer->m_nPrimaryColor = veh->m_nPrimaryColor;
                            trailer->m_nSecondaryColor = veh->m_nSecondaryColor;
                            trailer->m_nTertiaryColor = veh->m_nTertiaryColor;
                            trailer->m_nQuaternaryColor = veh->m_nQuaternaryColor;
                        }
                        if (firstTrailer && trailersVec.size() > 1 && trailerMatchExtras)
                        {
                            CVehicleModelInfo::ms_compsToUse[0] = firstTrailer->m_anExtras[0];
                            CVehicleModelInfo::ms_compsToUse[1] = firstTrailer->m_anExtras[1];
                        }
                    }
                }                
            }
        }
    }
}

void VehicleVariations::UpdateVariations()
{
    const CWanted* wanted = FindPlayerWanted(-1);
    vehVars.currentTuning = nullptr;

    for (auto modelId : vehVars.populatedModels)
        if (auto* properties = findVehProperties(modelId))
            properties->currentVariations.clear();

    auto currentZoneTuning = currentZone ? vehVars.tuning.find(*reinterpret_cast<uint64_t*>(currentZone->m_szLabel)) : vehVars.tuning.end();

    if (currentZoneTuning != vehVars.tuning.end())
        vehVars.currentTuning = &(currentZoneTuning->second);

    if (currentZoneVariations == variations.end())
        return;

    for (auto modelid : vehVars.populatedModels)
    {
        auto* properties = findVehProperties(modelid);
        if (!properties || !properties->hasVariations)
            continue;

        if (auto it = currentZoneVariations->second.find(modelid); it != currentZoneVariations->second.end() && it->second < variationSets.size())
            properties->currentVariations = variationSets[it->second];

        if (wanted)
        {
            const unsigned int wantedLevel = wanted->m_nWantedLevel - (wanted->m_nWantedLevel ? 1 : 0);
            if (!properties->wantedVariations[wantedLevel].empty() && !properties->currentVariations.empty())
                vectorfilterVector(properties->currentVariations, properties->wantedVariations[wantedLevel]);
        }

        for (auto i : properties->activeTimeGroups)
            vectorfilterVector(properties->currentVariations, properties->timeGroups[i].variations);

        if (!properties->missionVariations.empty())
        {
            if (!CTheScripts__IsPlayerOnAMission())
            {
                if (auto it2 = properties->missionVariations.find("MISSIONGAMEPLAY"); it2 != properties->missionVariations.end())
                    vectorfilterVector(properties->currentVariations, it2->second);
            }
            else if (auto it2 = properties->missionVariations.find(currentMission); it2 != properties->missionVariations.end())
                vectorfilterVector(properties->currentVariations, it2->second);
            else if (auto it3 = properties->missionVariations.find("MISSIONALL"); it3 != properties->missionVariations.end())
                vectorfilterVector(properties->currentVariations, it3->second);
        }
    }
}

void VehicleVariations::DrawDebugInfo(float fontSize, uint32_t debugOptions)
{
    auto* vehiclePool = CPools::ms_pVehiclePool;
    if (!vehiclePool)
        return;

    float fontSizew = RsGlobal.maximumHeight / 640.0f * fontSize;
    float fontSizeh = fontSizew * 2.2f;

    // Text style
    CFont::SetBackground(false, false);
    CFont::SetOrientation(ALIGN_CENTER);
    CFont::SetProportional(true);
    CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetScale(fontSizew, fontSizeh);
    CFont::SetEdge(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetColor(CRGBA(255, 255, 255, 255));

    for (int i = 0; i < vehiclePool->m_nSize; ++i)
    {
        CVehicle* veh = vehiclePool->GetAt(i);
        if (!IsVehiclePointerValid(veh) || !isVehicleVisible(veh) || veh->m_fHealth < 0.1)
            continue;

        // Position a little above the vehicle
        CVector pos = veh->GetPosition();

        RwV3d worldPos;
        worldPos.x = pos.x;
        worldPos.y = pos.y;
        worldPos.z = pos.z + 1.5f;

        RwV3d screenPos;
        float w, h;
        if (!CSprite::CalcScreenCoors(worldPos, &screenPos, &w, &h, true, true))
            continue;

        const float lineOffset = (RsGlobal.maximumHeight / 640.0f) * fontSize * 35.0f;
        float currentOffset = lineOffset;

        if (debugOptions & std::to_underlying(debugDrawVehStats::POINTER))
        {
            std::string line = msprintf("0x%08X", reinterpret_cast<std::uintptr_t>(veh));
            CFont::PrintString(screenPos.x, screenPos.y, line.c_str());
        }

        if (debugOptions & std::to_underlying(debugDrawVehStats::MODEL))
        {
            std::string line = msprintf("%u %s", veh->m_nModelIndex, modelNames.contains(veh->m_nModelIndex) ? modelNames[veh->m_nModelIndex].c_str() : "");
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }

        if (debugOptions & std::to_underlying(debugDrawVehStats::CREATED_BY))
        {
            std::string line;
            if (veh->m_nCreatedBy == 1)
                line = "RANDOM_VEHICLE";
            else if (veh->m_nCreatedBy == 2)
                line = "MISSION_VEHICLE";
            else if (veh->m_nCreatedBy == 3)
                line = "PARKED_VEHICLE";
            else if (veh->m_nCreatedBy == 4)
                line = "PERMANENT_VEHICLE";

            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }

        if (debugOptions & std::to_underlying(debugDrawVehStats::LOCKED) && !veh->CanPedOpenLocks(FindPlayerPed()))
        {
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, "Locked");
            currentOffset += lineOffset;
        }

        if (debugOptions & std::to_underlying(debugDrawVehStats::PROOFS))
        {
            std::string proofs = msprintf("%s%s%s%s%s%s", veh->bBulletProof ? " BP" : "",
                                                          veh->bFireProof ? " FP" : "",
                                                          veh->bCollisionProof ? " CP" : "",
                                                          veh->bMeleeProof ? " MP" : "",
                                                          veh->bExplosionProof ? " EP" : "",
                                                          veh->bInvulnerable ? " WP" : "");

            if (!proofs.empty())
            {
                std::string line = msprintf("Proofs:%s", proofs.c_str());
                CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
                currentOffset += lineOffset;
            }
        }

        if (debugOptions & std::to_underlying(debugDrawVehStats::HEALTH))
        {
            std::string line = msprintf("Health: %.0f", veh->m_fHealth);
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }

        if (auto parentModel = getVariationOriginalModel(veh->m_nModelIndex); parentModel != veh->m_nModelIndex && (debugOptions & std::to_underlying(debugDrawVehStats::PARENT_MODEL)))
        {
            std::string line = msprintf("Parent model : %d", parentModel);
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////  LOGGING   /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void VehicleVariations::LogCurrentVariations()
{
    if (!Log::Write("vehCurrentVariations"))
        return;

    bool hasCurrentVariations = false;
    for (auto modelId : vehVars.populatedModels)
        if (const auto* properties = findVehProperties(modelId); properties && !properties->currentVariations.empty())
        {
            hasCurrentVariations = true;
            break;
        }

    if (!hasCurrentVariations)
        Log::Write(" is empty\n");
    else
        Log::Write("\n");

    for (auto modelId : vehVars.populatedModels)
        if (const auto* properties = findVehProperties(modelId); properties && !properties->currentVariations.empty())
        {
            Log::Write("%d: ", modelId);
            for (auto variation : properties->currentVariations)
                Log::Write("%u ", variation);
            Log::Write("\n");
        }
}

void VehicleVariations::LogDataFile()
{
    if (!fileExists(dataFileName))
        Log::Write("\n%s not found!\n\n", dataFileName);
    else
    {
        if (Log::Write("%s\n", printFilenameWithBorder(dataFileName, '#').c_str()))
            Log::Write("%s\n", fileToString(dataFileName).c_str());
    }
}

void VehicleVariations::LogVariations()
{ 
    if (!Log::Write("Vehicle Variations:\n"))
        return;

    std::map<unsigned short, std::set<unsigned short>> variationsMap;
    for (const auto& it : variations)
        for (const auto& i : it.second)
        {
            auto mInfo = CModelInfo::GetModelInfo(i.first);
            if (!mInfo || mInfo->GetModelType() != MODEL_INFO_VEHICLE || i.second >= variationSets.size())
                continue;

            for (auto j : variationSets[i.second])
                variationsMap[i.first].insert(j);
        }
    
    for (auto& i : variationsMap)
    {
        Log::Write("%u: ", i.first);
        for (auto j : i.second)
            Log::Write("%u ", j);
        Log::Write("\n");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

__declspec(noinline) int __cdecl ChooseModelHooked(int* a1)
{
    const auto originalCall = captureCurrentOriginalCall();
    const int model = originalCall.callAndReturn<int>(a1);

    if (model < 400)
        return model;

    auto retVal = getRandomVariation((unsigned short)model);
    if (CStreamingInfo__ms_pArrayBase[retVal].m_nLoadState != LOADSTATE_LOADED)
    {
        Log::Write("ChooseModelHooked Error! Model %d is not loaded.\n", retVal);
        return -1;
    }

    return retVal;
}

__declspec(noinline) int __cdecl ChoosePoliceCarModelHooked(int a1)
{
    const auto originalCall = captureCurrentOriginalCall();
    const int model = originalCall.callAndReturn<int>(a1);

    if (model < 427 || model > 601)
        return model;

    auto retVal = getRandomVariation((unsigned short)model);
    if (CStreamingInfo__ms_pArrayBase[retVal].m_nLoadState != LOADSTATE_LOADED)
    {
        Log::Write("ChooseModelHooked Error! Model %d is not loaded.\n", retVal);
        return -1;
    }

    return retVal;
}

__declspec(noinline) void __cdecl AddPoliceCarOccupantsHooked(CVehicle* a2, char a3)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (a2 == NULL)
        return;

    processOccupantGroups(a2);

    const unsigned short model = a2->m_nModelIndex;
    a2->m_nModelIndex = (unsigned short)getVariationOriginalModel(a2->m_nModelIndex);

    originalCall.call(a2, a3);

    a2->m_nModelIndex = model;

    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
}

__declspec(noinline) CAutomobile* __fastcall CAutomobileHooked(CAutomobile* automobile, void*, int modelIndex, char usageType, char bSetupSuspensionLines)
{
    const auto originalCall = captureCurrentOriginalCall();
    return originalCall.callMethodAndReturn<CAutomobile*>(automobile, getRandomVariation(modelIndex), usageType, bSetupSuspensionLines);
}

__declspec(noinline) int __fastcall PickRandomCarHooked(CLoadedCarGroup* cargrp, void*, char a2, char a3) //for random parked cars
{
    const auto originalCall = captureCurrentOriginalCall();

    if (cargrp == NULL)
        return -1;

    int variation = getRandomVariation(originalCall.callMethodAndReturn<int>(cargrp, a2, a3), true);
    if (variation > 0 && CStreamingInfo__ms_pArrayBase[variation].m_nLoadState != LOADSTATE_LOADED)
    {
        Log::Write("PickRandomCarHooked Error! Model %d is not loaded.\n", variation);
        return -1;
    }

    return variation;
}

__declspec(noinline) void __fastcall DoInternalProcessingHooked(CCarGenerator* park) //for non-random parked cars
{
    const auto originalCall = captureCurrentOriginalCall();

    if (park == NULL)
        return;

    if (park->m_nModelId < 0) //We're tuning only random cars
    {
        tuneParkedCar = true;
        originalCall.callMethod(park);
        tuneParkedCar = false;
        return;
    }

    tuneParkedCar = false;

    if (vehOptions.changeCarGenerators)
    {
        auto originalModel = park->m_nModelId;
        if (!vectorHasId(vehOptions.carGenExclude, park->m_nModelId))
            park->m_nModelId = (short)getRandomVariation(park->m_nModelId, true);

        if (park->m_nModelId != originalModel && originalModel == 588)
        {
            static constexpr std::array<std::uintptr_t, 2> hotdogAddresses = { 0x6F3649, 0x6F3CAD };
            std::array<uint16_t, hotdogAddresses.size()> originalHotdogModels{};

            for (std::size_t i = 0; i < hotdogAddresses.size(); ++i)
            {
                const auto hdAddress = hotdogAddresses[i];
                if (!memoryMatchesOriginalExe(hdAddress, sizeof(uint16_t)))
                {
                    const auto value = injector::ReadMemory<uint16_t>(hdAddress, true);
                    Log::LogModifiedAddress(hdAddress, "Modified address detected: 0x%08X is %u\n", hdAddress, value);
                    originalCall.callMethod(park);
                    return;
                }

                originalHotdogModels[i] = injector::ReadMemory<uint16_t>(hdAddress, true);
            }

            loadModel(168, PRIORITY_REQUEST, true);
            for (auto hdAddress : hotdogAddresses)
                WriteMemory<uint16_t>(hdAddress, park->m_nModelId);

            originalCall.callMethod(park);

            for (std::size_t i = 0; i < hotdogAddresses.size(); ++i)
                WriteMemory<uint16_t>(hotdogAddresses[i], originalHotdogModels[i]);
        }
        else
            originalCall.callMethod(park);

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
            [[fallthrough]];
        default:
            originalCall.callMethod(park);
    }
}

__declspec(noinline) void* __fastcall CTrainHooked(void* train, void*, int modelIndex, int createdBy)
{
    const auto originalCall = captureCurrentOriginalCall();
    return originalCall.callMethodAndReturn<void*>(train, CTheScripts__IsPlayerOnAMission() ? modelIndex : getRandomVariation(modelIndex), createdBy);
}

__declspec(noinline) CVehicle* __fastcall CBoatHooked(void* boat, void*, int modelId, char a3)
{
    const auto originalCall = captureCurrentOriginalCall();
    return originalCall.callMethodAndReturn<CVehicle*>(boat, getRandomVariation(modelId), a3);
}

__declspec(noinline) CAutomobile* __fastcall CHeliHooked(CHeli* heli, void*, int a2, char usageType)
{
    const auto originalCall = captureCurrentOriginalCall();
    return originalCall.callMethodAndReturn<CAutomobile*>(heli, getRandomVariation(a2), usageType);
}

__declspec(noinline) CHeli* __cdecl GenerateHeliHooked(CPed* ped, char newsHeli)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (FindPlayerWanted(-1)->m_nWantedLevel < 4)
        return originalCall.callAndReturn<CHeli*>(ped, 0);

    if (CHeli::pHelis)
    {
        newsHeli = 1;
        if (CHeli::pHelis[0] && getVariationOriginalModel(CHeli::pHelis[0]->m_nModelIndex) == 488)
            newsHeli = 0;

        if (CHeli::pHelis[1] && getVariationOriginalModel(CHeli::pHelis[1]->m_nModelIndex) == 488)
            newsHeli = 0;

        unsigned short heliModel = newsHeli ? 488U : 497U;

        if (auto loadState = loadModel(heliModel, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
            Log::Write("Error loading vehicle model %d (%s) %s\n", heliModel, modelNames.contains(heliModel) ? modelNames[heliModel].c_str() : "", getLoadStateString(loadState));
    }

    return originalCall.callAndReturn<CHeli*>(ped, newsHeli);
}

__declspec(noinline) CPlane* __fastcall CPlaneHooked(CPlane* plane, void*, int a2, char a3)
{
    const auto originalCall = captureCurrentOriginalCall();
    return originalCall.callMethodAndReturn<CPlane*>(plane, getRandomVariation(a2), a3);
}

__declspec(noinline) bool __fastcall IsLawEnforcementVehicleHooked(CVehicle* veh)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (veh == NULL)
        return false;

    const unsigned short modelIndex = veh->m_nModelIndex;
    veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
    bool isLawEnforcement = originalCall.callMethodAndReturn<bool>(veh);
    veh->m_nModelIndex = modelIndex;

    return isLawEnforcement;
}

__declspec(noinline) char __cdecl GenerateRoadBlockCopsForCarHooked(CVehicle* a1, int pedsPositionsType, int type)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (a1 == NULL)
        return 0;

    processOccupantGroups(a1);

    roadblockModel = a1->m_nModelIndex;
    a1->m_nModelIndex = (unsigned short)getVariationOriginalModel(a1->m_nModelIndex);
    originalCall.call(a1, pedsPositionsType, type);
    if (roadblockModel >= 400)
        a1->m_nModelIndex = roadblockModel;
    roadblockModel = 0;
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;

    return 1;
}

__declspec(noinline) CColModel* __fastcall GetColModelHooked(CVehicle* entity)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (roadblockModel >= 400)
        entity->m_nModelIndex = roadblockModel;
    return originalCall.callMethodAndReturn<CColModel*>(entity);
}

__declspec(noinline) int __cdecl GetDefaultCopModelHooked()
{
    const auto originalCall = captureCurrentOriginalCall();

    if (roadblockDriver > 0)
        return roadblockDriver;

    auto retVal = originalCall.callAndReturn<int>();

    if (retVal == 0)
    {
        Log::Write("GetDefaultCopModel: Error! Returned model is 0. Trying to get model manually... ");
        int model = reinterpret_cast<int*>(0x8A5AA0)[CTheZones::m_CurrLevel];
        if (model > 0 && model < 65535)
        {
            Log::Write("OK. Using model %d\n", model);
            return model;
        }
        Log::Write("FAILED. Using MALE01\n");
        return 7;
    }

    return retVal;
}

__declspec(noinline) CCopPed* __fastcall CCopPedHooked(CCopPed* ped, void*, int copType)
{
    const auto originalCall = captureCurrentOriginalCall();

    static constexpr std::array<std::uintptr_t, 4> modelAddresses = {
        0x5DDE4F, 0x5DDD8F, 0x5DDDCF, 0x5DDE0F
    };

    for (const auto modelAddress : modelAddresses)
        if (!memoryMatchesOriginalExe(modelAddress, 5) && !forceEnableGlobal && !forceEnable.contains(modelAddress))
        {
            Log::LogModifiedAddress(modelAddress, "Modified address detected: 0x%08X is %u\n", modelAddress, *(uint16_t*)(modelAddress + 1));
            return originalCall.callMethodAndReturn<CCopPed*>(ped, copType);
        }

    unsigned int original283 = *(unsigned int*)0x5DDE50;
    unsigned int original285 = *(unsigned int*)0x5DDD90;
    unsigned int original286 = *(unsigned int*)0x5DDDD0;
    unsigned int original287 = *(unsigned int*)0x5DDE10;

    if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
    {
        const auto* properties = findVehProperties(currentOccupantsModel);
        if (properties && !properties->driverGroups[currentOccupantsGroup].empty())
        {
            auto driver = vectorGetRandom(properties->driverGroups[currentOccupantsGroup]);
            if (auto loadState = loadModel(driver, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
            {
                Log::Write("Error loading ped model %d (%s) %s\n", driver, modelNames.contains(driver) ? modelNames[driver].c_str() : "", getLoadStateString(loadState));
                roadblockDriver = 0;
                return originalCall.callMethodAndReturn<CCopPed*>(ped, copType);
            }

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

    auto retVal = originalCall.callMethodAndReturn<CCopPed*>(ped, copType);
    WriteMemory<unsigned int>(0x5DDE50, original283);
    WriteMemory<unsigned int>(0x5DDD90, original285);
    WriteMemory<unsigned int>(0x5DDDD0, original286);
    WriteMemory<unsigned int>(0x5DDE10, original287);
    roadblockDriver = 0;
    return retVal;
}

__declspec(noinline) CPed* __cdecl AddPedInCarHooked(CVehicle* veh, char driver, int a3, int a4, char a5, char a6)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (veh == NULL)
        return NULL;

    const auto* vehicleProperties = findVehProperties(veh->m_nModelIndex);

    if (driver)
    {
        const bool replaceDriver = vehicleProperties && vehicleProperties->replaceDriver ? true : rand<bool>();
        if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
        {
            const auto* properties = findVehProperties(currentOccupantsModel);
            if (properties && !properties->driverGroups[currentOccupantsGroup].empty())
                occupantModelIndex = vectorGetRandom(properties->driverGroups[currentOccupantsGroup]);
        }
        else if (vehicleProperties && !vehicleProperties->drivers.empty() && replaceDriver)
            occupantModelIndex = vectorGetRandom(vehicleProperties->drivers);
    }
    else
    {
        const bool replacePassenger = vehicleProperties && vehicleProperties->replacePassengers ? true : rand<bool>();
        if (currentOccupantsGroup > -1 && currentOccupantsGroup < 9 && currentOccupantsModel > 0)
        {
            const auto* properties = findVehProperties(currentOccupantsModel);
            if (properties && !properties->passengerGroups[currentOccupantsGroup].empty())
                occupantModelIndex = vectorGetRandom(properties->passengerGroups[currentOccupantsGroup]);
        }
        else if (vehicleProperties && !vehicleProperties->passengers.empty() && replacePassenger)
            occupantModelIndex = vectorGetRandom(vehicleProperties->passengers);
    }

    const auto model = veh->m_nModelIndex;
    veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
    CPed* ped = originalCall.callAndReturn<CPed*>(veh, driver, a3, a4, a5, a6);
    veh->m_nModelIndex = model;

    return ped;
}

__declspec(noinline) CPed* __cdecl AddPedHooked(unsigned int pedType, int modelIndex, CVector* posn, bool unknown)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (occupantModelIndex > 0)
    {
        if (auto loadState = loadModel(occupantModelIndex, PRIORITY_REQUEST, true); loadState == LOADSTATE_LOADED)
        {
            modelIndex = occupantModelIndex;
            if (pedType != PED_TYPE_MEDIC && pedType != PED_TYPE_COP)
            {
                CPedModelInfo* mInfo = (CPedModelInfo*)CModelInfo::GetModelInfo(occupantModelIndex);
                if (mInfo)
                    pedType = mInfo->m_nPedType;
            }
        }
        else
            Log::Write("Error loading ped model %d (%s) %s\n", occupantModelIndex, modelNames.contains((unsigned short)occupantModelIndex) ? modelNames[(unsigned short)occupantModelIndex].c_str() : "", getLoadStateString(loadState));

        CPed* ped = originalCall.callAndReturn<CPed*>(pedType, modelIndex, posn, unknown);
        occupantModelIndex = -1;
        return ped;
    }

    int model = modelIndex;

    if (pedType == PED_TYPE_COP)
        model = getPedModelForCopType(modelIndex);

    if (model > -1 && CStreamingInfo__ms_pArrayBase[model].m_nLoadState != LOADSTATE_LOADED)
    {
        Log::Write("Error! Ped model %d is not loaded. Loading now... ", model);
        if (loadModel(model, PRIORITY_REQUEST, true) == LOADSTATE_LOADED)
            Log::Write("OK\n");
        else
            Log::Write("FAILED\n");
    }

    return originalCall.callAndReturn<CPed*>(pedType, modelIndex, posn, unknown);
}

__declspec(noinline) void __cdecl SetUpDriverAndPassengersForVehicleHooked(CVehicle* car, int a3, int a4, char a5, char a6, int a7)
{
    const auto originalCall = captureCurrentOriginalCall();
    processOccupantGroups(car);
    originalCall.call(car, a3, a4, a5, a6, a7);
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
}

__declspec(noinline) void __cdecl AddAmbulanceOccupantsHooked(CVehicle* a1)
{
    const auto originalCall = captureCurrentOriginalCall();
    processOccupantGroups(a1);
    originalCall.call(a1);
    currentOccupantsGroup = -1;
    currentOccupantsModel = 0;
}

__declspec(noinline) void __cdecl PossiblyRemoveVehicleHooked(CVehicle* car)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (car == NULL)
        return;

    if (car->m_pRwObject == NULL)
    {
        Log::Write("PossiblyRemoveVehicleHooked Error! Vehicle 0x%X (%u) has NULL m_pRwObject. Returning.\n", car, car->m_nModelIndex);
        spawnedTrailers.erase(car);

        return;
    }

    std::vector<CVehicle*> trailersToCheck;    

    for (auto it = spawnedTrailers.begin(); it != spawnedTrailers.end(); )
    {
        if (!it->second.empty())
            for (auto trailer = it->second.back(); !IsVehiclePointerValid(trailer); trailer = it->second.back())
            {
                it->second.pop_back();
                if (it->second.empty())
                    break;
            }

        if (it->second.empty() || !IsVehiclePointerValid(it->first))
        {
            it = spawnedTrailers.erase(it);
            continue;
        }

        for (auto trailer : it->second)
            if (trailer == car && (((CTimer::m_snTimeInMilliseconds - trailer->m_nCreationTime) < 300) || (trailer->m_pTractor && (!trailer->m_pTractor->bFadeOut))))
                return;
          
        if (it->first == car)
            for (auto trailer : it->second)
                if (IsVehiclePointerValid(trailer) && trailer->m_pTractor)
                    trailersToCheck = it->second;
        
        it++;
    }

    originalCall.call(car);

    if (!trailersToCheck.empty())
        if (!IsVehiclePointerValid(car) || (IsVehiclePointerValid(car) && car->bFadeOut))
        {
            for (auto& trailer : trailersToCheck)
            {
                if (!IsVehiclePointerValid(trailer))
                    continue;

                if (trailer->m_pTractor)
                    trailer->bFadeOut = true;
                else if ((CTimer::m_snTimeInMilliseconds - trailer->m_nCreationTime) > 1500)
                    break;
            }
            spawnedTrailers.erase(car);
        }
}

__declspec(noinline) void* __fastcall SetDriverHooked(CVehicle* _this, void*, CPed* a2)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (_this == NULL)
        return NULL;

    unsigned short modelIndex = _this->m_nModelIndex;
    _this->m_nModelIndex = (unsigned short)getVariationOriginalModel(_this->m_nModelIndex);
    auto retVal = originalCall.callMethodAndReturn<void*>(_this, a2);
    _this->m_nModelIndex = modelIndex;

    return retVal;
}

__declspec(noinline) void __fastcall CAutomobile__PreRenderHooked(CAutomobile* veh)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (veh == NULL)
        return;

    unsigned short originalModel = veh->m_nModelIndex;
    lightsModel = veh->m_nModelIndex;


    originalCall.callMethod(veh);
    veh->m_nModelIndex = originalModel;
    lightsModel = 0;
}

__declspec(noinline) int __fastcall GetVehicleAppearanceHooked(CVehicle* veh)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (lightsModel > 0)
        veh->m_nModelIndex = lightsModel;

    lightsModel = 0;
    return originalCall.callMethodAndReturn<int>(veh);
}

__declspec(noinline) void* __fastcall CreateInstanceHooked(CVehicleModelInfo* _this)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (_this->m_pVehicleStruct == NULL)
    {
        int index = -1;
        auto mInfo = CModelInfo::GetModelInfoFromHashKey(_this->m_nKey, &index);

        if (mInfo == NULL || mInfo != _this)
        {
            Log::Write("Vehicle model lookup failed: this=0x%08X key=0x%08X mInfo=0x%08X index=%d\n", _this, _this->m_nKey, mInfo, index);
            return originalCall.callMethodAndReturn<void*>(_this);
        }

        auto &streamingInfo = CStreamingInfo__ms_pArrayBase[index];

        Log::Write("model=%d key=0x%08X state=%s flags=0x%02X img=%u cdPos=%u cdSize=%u next=%d prev=%d\n", index,
                                                                                                            _this->m_nKey,
                                                                                                            getLoadStateString(streamingInfo.m_nLoadState),
                                                                                                            streamingInfo.m_nFlags,
                                                                                                            streamingInfo.m_nImgId,
                                                                                                            streamingInfo.m_nCdPosn,
                                                                                                            streamingInfo.m_nCdSize,
                                                                                                            streamingInfo.m_nNextIndex,
                                                                                                            streamingInfo.m_nPrevIndex);

        Log::Write("Model %d has NULL vehicle struct (load state = %u). Trying to load model... ", index, streamingInfo.m_nLoadState);
        clumpLoadModel = index;
        CStreaming__RequestModel(index, PRIORITY_REQUEST);
        CStreaming__LoadAllRequestedModels(false);
        if (_this->m_pVehicleStruct != NULL)
            Log::Write("OK\n");
        else
        {
            Log::Write("\nFailed. Trying again as GAME_REQUIRED... ");
            clumpLoadModel = index;
            CStreaming__RequestModel(index, GAME_REQUIRED);
            CStreaming__LoadAllRequestedModels(false);
            if (_this->m_pVehicleStruct != NULL)
            {
                clumpLoadModel = -1;
                Log::Write("OK\n");
                streamingInfo.m_nFlags &= ~((uint8_t)GAME_REQUIRED);
                return originalCall.callMethodAndReturn<void*>(_this);
            }

            std::string errorString = 
            msprintf("Couldn't load model %d! The game will probably crash.\n"
                     "Load state: %s\n"
                     "Reference count: %u\n"
                     "Times used: %u\n"
                     "Vehicles: %u/%u\n"
                     "VehicleStructs: %u/%u\n"
                     "Streaming memory: %u/%u MB", index, getLoadStateString(streamingInfo.m_nLoadState), _this->m_nRefCount, _this->m_nTimesUsed,
                                                   CPools::ms_pVehiclePool->GetNoOfUsedSpaces(), CPools::ms_pVehiclePool->m_nSize, 
                                                   CVehicleModelInfo__CVehicleStructure__m_pInfoPool->GetNoOfUsedSpaces(),
                                                   CVehicleModelInfo__CVehicleStructure__m_pInfoPool->m_nSize,
                                                   CStreaming__ms_memoryUsed/1024/1024, CStreaming__ms_memoryAvailable/1024/1024);
            Log::Write("\n%s\n", errorString.c_str());
            MessageBox(NULL, errorString.c_str(), "Model Variations", MB_ICONERROR);
            return 0;
        }
        clumpLoadModel = -1;
    }

    return originalCall.callMethodAndReturn<void*>(_this);
}

__declspec(noinline) char __cdecl LoadClumpFileHooked(void* stream, int modelIndex)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (clumpLoadModel && clumpLoadModel == modelIndex)
        Log::Write("Called LoadClumpFileHooked with stream 0x%08X for model %d\n", stream, modelIndex);

    auto retVal = originalCall.callAndReturn<char>(stream, modelIndex);

    if (clumpLoadModel && clumpLoadModel == modelIndex)
    {
        Log::Write("LoadClumpFileHooked returned %s for model %d\n", retVal ? "true" : "false", modelIndex);
        clumpLoadModel = -1;
    }

    return retVal;
}

__declspec(noinline) CVehicle* __cdecl GetNewVehicleDependingOnCarModelHooked(int modelIndex, int createdBy)
{
    const auto originalCall = captureCurrentOriginalCall();
    CVehicle* veh = originalCall.callAndReturn<CVehicle*>(modelIndex, createdBy);
    if (veh && veh->m_pRwObject == NULL)
    {
        Log::Write("GetNewVehicleDependingOnCarModelHooked Error! Vehicle 0x%X (%u) has NULL m_pRwObject. Returning NULL.\n", veh, veh->m_nModelIndex);
        return NULL;
    }
    
    processTuning(veh);
    return veh;
}

__declspec(noinline) CPhysical* __fastcall CPhysicalHooked(CVehicle* _this)
{
    const auto originalCall = captureCurrentOriginalCall();
    CPhysical* retVal = originalCall.callMethodAndReturn<CPhysical*>(_this);
    vehVars.stack.push_back(_this);
    return retVal;
}

__declspec(noinline) void __fastcall AddAudioEventHooked(CAEVehicleAudioEntity* audio, void*, int audioEvent, float fVolume)
{
    const auto originalCall = captureCurrentOriginalCall();

    //https://github.com/JuniorDjjr/TruckTrailer
    if (audio)
    {
        CVehicle* vehicle = static_cast<CVehicle*>(audio->m_pEntity);
        if (vehicle && (CTimer::m_snTimeInMilliseconds - vehicle->m_nCreationTime) > 2000)
            originalCall.callMethod(audio, audioEvent, fVolume);
    }
}

__declspec(noinline) void __cdecl CWorld__RemoveHooked(CVehicle* entity)
{
    const auto originalCall = captureCurrentOriginalCall();
    if (entity)
    {
        auto it = spawnedTrailers.find(entity);
        if (it != spawnedTrailers.end())
        {
            for (auto trailer : it->second)
                if (IsVehiclePointerValid(trailer) && getDistanceFromVeh(entity, trailer) < 22.0f)
                    destroyVehicleAndOccupants(trailer);

            spawnedTrailers.erase(it);
        }
    }

    originalCall.call(entity);
}

__declspec(noinline) void __cdecl CWorld__AddHooked(CVehicle* a1)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (tuneParkedCar)
    {
        processTuning(a1);
        tuneParkedCar = false;
    }
    originalCall.call(a1);
}

__declspec(noinline) void* __cdecl FillFrameArrayHooked(void* clump, void* data)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (!isAddressValid(clump))
    {
        Log::Write("FillFrameArrayHooked Error! clump is invalid (0x%X).\n", clump);
        return NULL;
    }

    if (!isAddressValid(data))
    {
        Log::Write("FillFrameArrayHooked Error! clump is (0x%X) data is invalid (0x%X).\n", clump, data);
        return NULL;
    }

    return originalCall.callAndReturn<void*>(clump, data);
}

__declspec(noinline) void __fastcall SetupSuspensionLinesHooked(CVehicle* _this)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (_this == NULL)
    {
        Log::Write("SetupSuspensionLinesHooked Error! _this is NULL.\n");
        return;
    }

    if (_this->m_pRwObject == NULL)
    {
        Log::Write("CBike::SetupSuspensionLinesHooked Error! m_pRwObject for _this (0x%X) is NULL.\n", _this);
        return;
    }

    originalCall.callMethod(_this);
}

__declspec(noinline) void __fastcall UpdateClumpAlphaHooked(CVehicle* _this)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (!isAddressValid(_this))
    {
        Log::Write("UpdateClumpAlphaHooked Error! _this is invalid (0x%X).\n", _this);
        return;
    }

    if (!isAddressValid(_this->m_pRwObject))
    {
        Log::Write("UpdateClumpAlphaHooked Error! m_pRwObject for _this (0x%X) with modelIndex (%u) is invalid (0x%X).\n", _this, _this->m_nModelIndex, _this->m_pRwObject);
        return;
    }

    originalCall.callMethod(_this);
}

__declspec(noinline) void __cdecl SetClumpAlphaHooked(void* clump, int alpha)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (!isAddressValid(clump))
    {
        Log::Write("SetClumpAlphaHooked Error! clump is invalid (0x%X).\n", clump);
        return;
    }

    originalCall.call(clump, alpha);
}

//changeScriptedCars
__declspec(noinline) CVehicle* __cdecl CreateCarForScriptHooked(int modelId, float posX, float posY, float posZ, char doMissionCleanup)
{
    const auto originalCall = captureCurrentOriginalCall();
    return originalCall.callAndReturn<CVehicle*>(getRandomVariation(modelId), posX, posY, posZ, doMissionCleanup);
}

//enableSiren
__declspec(noinline) bool __fastcall UsesSirenHooked(CVehicle* veh)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (veh == NULL)
        return false;

    const unsigned short modelIndex = veh->m_nModelIndex;
    veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
    bool usesSiren = originalCall.callMethodAndReturn<bool>(veh);
    veh->m_nModelIndex = modelIndex;

    return usesSiren;
}

//enableLights
template <bool second = false>
__declspec(noinline) void __cdecl RegisterCoronaHooked(void* _this, CEntity* a2, unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha, CVector* coors, 
                                                       float size, float a9, void* texture, unsigned char a11, unsigned char a12, unsigned char a13, int a14, float a15, float a16, 
                                                       float a17, float a18, float a19, float a20, bool a21)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (!a2 || !coors)
        return originalCall.call(_this, a2, red, green, blue, alpha, coors, size, a9, texture, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);

    const auto* properties = findVehProperties(lightsModel);

    //size
    if (properties && properties->lightSizes)
        size = *properties->lightSizes;

    //position
    if (properties && properties->lightPositions)
    {
        const auto& position = *properties->lightPositions;
        if (position.second > -900.0)
            coors->x *= position.second;
        if (position.first.x != 0.0)
            coors->x += position.first.x;
        if (position.first.y != 0.0)
            coors->y += position.first.y;
        if (position.first.z != 0.0)
            coors->z += position.first.z;
    }

    //colors
    if (properties)
    {
        const auto& color = second ? properties->lightColors2 : properties->lightColors;
        if (color)
        {
            red = color->red;
            green = color->green;
            blue = color->blue;
            alpha = color->alpha;
        }
    }

    originalCall.call(_this, a2, red, green, blue, alpha, coors, size, a9, texture, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21);
}

__declspec(noinline) void __cdecl AddLightHooked(char type, float x, float y, float z, float dir_x, float dir_y, float dir_z, float radius, float r, float g, float b,
                                                 char fogType, char generateExtraShadows, int attachedTo)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (lightsModel > 0)
    {
        const auto* properties = findVehProperties(lightsModel);
        if (properties && properties->lightPositions)
        {
            const auto& position = *properties->lightPositions;
            if (position.second > -900.0f)
                x *= position.second;
            if (position.first.x != 0.0f)
                x += position.first.x;
            if (position.first.y != 0.0f)
                y += position.first.y;
            if (position.first.z != 0.0f)
                z += position.first.z;
        }

        if (properties && properties->lightSizes)
            radius = *properties->lightSizes;
    }

    originalCall.call(type, x, y, z, dir_x, dir_y, dir_z, radius, r, g, b, fogType, generateExtraShadows, attachedTo);
}

__declspec(noinline) void __fastcall AddDamagedVehicleParticlesHooked(CVehicle* veh)
{
    const auto originalCall = captureCurrentOriginalCall();
    originalCall.callMethod(veh);
    if (lightsModel > 0)
        veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
}

//enableTrailerLights
__declspec(noinline) void __fastcall DoVehicleLightsHooked(CAutomobile* _this, void*, void* m, int a3)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (_this == NULL || !CModelInfo::IsTrailerModel(_this->m_nModelIndex) || getVariationOriginalModel(_this->m_nModelIndex) == 610)
    {
        originalCall.callMethod(_this, m, a3);
        return;
    }

    CPed* driverOriginal = _this->m_pDriver;
    float brakeOriginal = _this->m_fBreakPedal;
    CVehicle* tractor = _this->m_pTractor;

    while (tractor && IsVehiclePointerValid(tractor->m_pTractor))
    {
        tractor = tractor->m_pTractor;
    }

    if (IsVehiclePointerValid(tractor) && tractor->m_fBreakPedal > 0.0f)
    {
        _this->bEngineOn = true;
        if (_this->m_fBreakPedal < 0.1)
            _this->m_fBreakPedal = 0.1f;
        _this->m_pDriver = FindPlayerPed();
    }

    originalCall.callMethod(_this, m, a3);
    _this->m_pDriver = driverOriginal;
    _this->m_fBreakPedal = brakeOriginal;
}

//disablePayAndSpray
__declspec(noinline) bool __cdecl IsCarSprayableHooked(CVehicle* veh)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (veh == NULL)
        return false;

    const unsigned short modelIndex = veh->m_nModelIndex;
    veh->m_nModelIndex = (unsigned short)getVariationOriginalModel(veh->m_nModelIndex);
    bool isCarSprayable = originalCall.callAndReturn<bool>(veh);
    veh->m_nModelIndex = modelIndex;

    return isCarSprayable;
}

//enableSideMissions
__declspec(noinline) CPed* __fastcall CPool__atHandleHooked(void* _this, void*, int h)
{
    const auto originalCall = captureCurrentOriginalCall();
    CPed* ped = originalCall.callMethodAndReturn<CPed*>(_this, h);
    if (IsPedPointerValid(ped) && IsVehiclePointerValid(ped->m_pVehicle))
    {
        auto originalModel = getVariationOriginalModel(ped->m_pVehicle->m_nModelIndex);
        if (ScriptParams[1] == originalModel)
            ScriptParams[1] = ped->m_pVehicle->m_nModelIndex;
    }
    return ped;
}

__declspec(noinline) CPed* __fastcall CPool__atHandleTaxiHooked(void* _this, void*, int h) //Unnecessarily complicated function to avoid incompatibility with FLA
{
    const auto originalCall = captureCurrentOriginalCall();
    static uint8_t taxiPed[sizeof(CPed)];
    static uint8_t taxiVeh[sizeof(CVehicle)];

    CPed* ped = originalCall.callMethodAndReturn<CPed*>(_this, h);
    if (IsPedPointerValid(ped) && IsVehiclePointerValid(ped->m_pVehicle) && ped->bInVehicle)
    {
        CPed* pTaxiPed = reinterpret_cast<CPed*>(&taxiPed);
        pTaxiPed->bInVehicle = ped->bInVehicle;

        constexpr auto pTaxiVeh = &taxiVeh;
        memcpy(&taxiPed[0x58C], &pTaxiVeh, 4);

        unsigned short originalModel = static_cast<uint16_t>(getVariationOriginalModel(ped->m_pVehicle->m_nModelIndex));
        memcpy(&taxiVeh[0x22], &originalModel, 2);

        return pTaxiPed;
    }
    return ped;
}


////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////  SPECIAL FEATURES  //////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

__declspec(noinline) char __fastcall SetUpWheelColModelHooked(CAutomobile* automobile, void*, CColModel* colModel)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (automobile == NULL)
        return 0;

    const auto originalModel = getVariationOriginalModel(automobile->m_nModelIndex);
    if (originalModel == 531 || originalModel == 532 || originalModel == 571) //Tractor || Combine Harvester || Kart
        return 0;

    return originalCall.callMethodAndReturn<char>(automobile, colModel);
}

__declspec(noinline) char __fastcall BurstTyreHooked(CAutomobile* veh, void*, char componentId, char a3)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (getVariationOriginalModel(veh->m_nModelIndex) == 432) //Rhino
        return 0;

    return originalCall.callMethodAndReturn<char>(veh, componentId, a3);
}

__declspec(noinline) void __cdecl RegisterCarBlownUpByPlayerHooked(CVehicle* vehicle, int a2)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (vehicle != NULL)
    {
        const auto model = vehicle->m_nModelIndex;
        vehicle->m_nModelIndex = (unsigned short)getVariationOriginalModel(vehicle->m_nModelIndex);
        originalCall.call(vehicle, a2);
        vehicle->m_nModelIndex = model;
    }
}

__declspec(noinline) void __fastcall ProcessControlInputsHooked(CPlane* _this, void*, unsigned char a2)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (_this == NULL)
        return;

    unsigned short modelIndex = _this->m_nModelIndex;
    _this->m_nModelIndex = (unsigned short)getVariationOriginalModel(_this->m_nModelIndex);
    originalCall.callMethod(_this, a2);
    _this->m_nModelIndex = modelIndex;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////  ASM HOOKS  /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void __declspec(naked) patch4306A1()
{
    __asm {
        call x4306A1_Destination
        test eax, eax
        jz pointerIsNull

        mov ecx, 0x4306A6
        jmp ecx

pointerIsNull:
        add esp, 8
        mov ecx, 0x431F30
        jmp ecx
    }
}


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
        push 0x6A1564
        ret
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
        push 0x588577
        ret
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
        push 0x6ABCC3
        ret
    }
}

void __declspec(naked) patch6D42FE()
{
    __asm {
        push ecx
        call getVariationOriginalModel
        lea  eax, [eax - 0x1A9]
        push 0x6D4304
        ret
    }
}

void __declspec(naked) patch6AC730()
{
    __asm {
        movsx ecx, word ptr [esi+0x22]
        mov eax, 0x403DA7
        mov eax, dword ptr [eax] //CModelInfo::ms_modelInfoPtrs
        mov eax, [eax + ecx*4]
        push 0x6AC735
        ret
    }
}

void __declspec(naked) patch6D474B()
{
    __asm {
        push edi
        call getVariationOriginalModel
        lea eax, [eax-0x1A9]
        push 0x6D4751
        ret
    }
}

void __declspec(naked) patch729B76()
{
    __asm {
        pushfd
        xor ebx, ebx
        movsx eax, word ptr [esi+0x22]
        push eax
        call getVariationOriginalModel
        cmp eax, 0x259
        jne isNotSWAT
        mov bx, word ptr [esi+0x22]
isNotSWAT:
        popfd
        push 0x729B7B
        ret
    }
}

void __declspec(naked) patch6DD218()
{
    __asm {
        push ecx
        push edx
        push eax
        mov edi, 0x1CC
        movsx eax, word ptr [esi+0x22]
        push eax
        call getVariationOriginalModel
        cmp eax, 0x1CC
        jne isNotSkimmer
        movsx edi, word ptr [esi+0x22]
isNotSkimmer:
        pop eax
        pop edx
        pop ecx
        push 0x6DD21D
        ret
    }
}

void __declspec(naked) patch6AC0E2()
{
    __asm {
        push eax
        movsx edi, word ptr [esi+0x22]
        push edi
        call getVariationOriginalModel
        cmp eax, 0x220
        je isFiretruckLS
        mov edi, 0x220
isFiretruckLS:
        pop eax
        push 0x6AC0E7
        ret
    }
}

void __declspec(naked) patch41F2A2()
{
    __asm {
        push eax
        movsx edi, word ptr[esi + 0x22]
        push edi
        call getVariationOriginalModel
        cmp eax, 0x20B
        je isCopBike
        mov edi, 0x20B
isCopBike:
        pop eax
        push 0x41F2A7
        ret
    }
}

decltype(&RegisterCoronaHooked<true>) RegisterCoronaHookedPointer = nullptr;
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
        push 0x6ABA65
        ret
    }
}

template <eRegs32 reg, std::uintptr_t jmpAddress, unsigned int model>
void __declspec(naked) cmpWordPtrRegModel()
{
    __asm {
        push eax
    }

    static unsigned int asmModel32 = model;
    static std::uintptr_t asmJmpAddress = jmpAddress;

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
        pop eax
        jmp asmJmpAddress
    }
}

template <eRegs16 reg, std::uintptr_t jmpAddress, unsigned int model, uint8_t nextInstrSize = 0, uint32_t nextInstr = 0x90909090, uint32_t nextInstr2 = 0x90909090>
void __declspec(naked) cmpReg16Model()
{
    __asm {
        push eax
    }

    static constinit const uint8_t* const jmpDest = AsmContinuation<nextInstrSize, nextInstr, nextInstr2, jmpAddress>::code.data();
    static unsigned int asmModel32 = model;
    static std::uintptr_t asmJmpAddress = jmpAddress;

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
        pop eax
    }

    if constexpr (nextInstrSize > 0) { __asm { jmp jmpDest } }

    __asm { jmp asmJmpAddress }
}

template <eRegs32 reg, std::uintptr_t jmpAddress, unsigned int model>
void __declspec(naked) cmpReg32Model()
{
    __asm {
        push eax
    }

    static unsigned int asmModel32 = model;
    static std::uintptr_t asmJmpAddress = jmpAddress;

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
        pop eax
        jmp asmJmpAddress
    }
}

template <eRegs16 target, eRegs32 source, std::uintptr_t jmpAddress, uint8_t nextInstrSize, uint32_t nextInstr, uint32_t nextInstr2 = 0x90909090>
void __declspec(naked) movReg16WordPtrReg()
{
    __asm {
        pushfd
        push eax
    }

    static constinit const uint8_t* const jmpDest = AsmContinuation<nextInstrSize, nextInstr, nextInstr2, jmpAddress>::code.data();
    static unsigned short asmModel16 = 0;

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
        pop eax
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
        push eax
    }

    static constinit const uint8_t* const jmpDest = AsmContinuation<nextInstrSize, nextInstr, nextInstr2, jmpAddress>::code.data();
    static unsigned int asmModel32 = 0;

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
        pop eax
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
    hookSharedCall<0x43022A, ChooseModelHooked>("CCarCtrl::ChooseModel"); //CCarCtrl::GenerateOneRandomCar

    hookSharedCall<0x42C320, ChoosePoliceCarModelHooked>("CCarCtrl::ChoosePoliceCarModel"); //CCarCtrl::CreatePoliceChase
    hookSharedCall<0x43020E, ChoosePoliceCarModelHooked>("CCarCtrl::ChoosePoliceCarModel"); //CCarCtrl::GenerateOneRandomCar
    hookSharedCall<0x430283, ChoosePoliceCarModelHooked>("CCarCtrl::ChoosePoliceCarModel"); //CCarCtrl::GenerateOneRandomCar

/*****************************************************************************************************/

    hookSharedCall<0x42BC26, AddPoliceCarOccupantsHooked>("CCarAI::AddPoliceCarOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookSharedCall<0x42C620, AddPoliceCarOccupantsHooked>("CCarAI::AddPoliceCarOccupants"); //CCarCtrl::CreatePoliceChase
    hookSharedCall<0x431EE5, AddPoliceCarOccupantsHooked>("CCarAI::AddPoliceCarOccupants"); //CCarCtrl::GenerateOneRandomCar
    hookSharedCall<0x499CBB, AddPoliceCarOccupantsHooked>("CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update
    hookSharedCall<0x499D6A, AddPoliceCarOccupantsHooked>("CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update
    hookSharedCall<0x49A5EB, AddPoliceCarOccupantsHooked>("CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update
    hookSharedCall<0x49A85E, AddPoliceCarOccupantsHooked>("CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update
    hookSharedCall<0x49A9AF, AddPoliceCarOccupantsHooked>("CCarAI::AddPoliceCarOccupants"); //CSetPiece::Update

/*****************************************************************************************************/
    
    hookSharedCall<0x42B909, CAutomobileHooked>("CAutomobile::CAutomobile"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookSharedCall<0x462217, CAutomobileHooked>("CAutomobile::CAutomobile"); //CRoadBlocks::CreateRoadBlockBetween2Points
    hookSharedCall<0x4998F0, CAutomobileHooked>("CAutomobile::CAutomobile"); //CSetPiece::TryToGenerateCopCar
    hookSharedCall<0x61354A, CAutomobileHooked>("CAutomobile::CAutomobile"); //CPopulation::CreateWaitingCoppers

    hookSharedCall<0x6F3583, PickRandomCarHooked>("CLoadedCarGroup::PickRandomCar"); //CCarGenerator::DoInternalProcessing
    hookSharedCall<0x6F3EC1, DoInternalProcessingHooked>("CCarGenerator::DoInternalProcessing"); //CCarGenerator::Process 
    hookASM(0x6F3B94, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6F3B9C, 4, 0x02133D66>, "CCarGenerator::DoInternalProcessing");

    //Trains
    hookSharedCall<0x6F7634, CTrainHooked>("CTrain::CTrain"); //CTrain::CreateMissionTrain 
    hookASM(0x64475D, 6, cmpWordPtrRegModel<REG_EAX, 0x644763, 0x23A>, "CTaskSimpleCarDrive::ProcessPed");
    hookASM(0x6F60D9, 6, cmpWordPtrRegModel<REG_ESI, 0x6F60DF, 0x23A>, "CTrain::CTrain");
    hookASM(0x6F6576, 6, cmpWordPtrRegModel<REG_EDI, 0x6F657C, 0x23A>, "CTrain::OpenDoor");
    hookASM(0x6F8E8A, 6, cmpWordPtrRegModel<REG_ESI, 0x6F8E90, 0x23A>, "CTrain::ProcessControl");

    //Boats
    hookSharedCall<0x42149E, CBoatHooked>("CBoat::CBoat"); //CCarCtrl::GetNewVehicleDependingOnCarModel
    hookSharedCall<0x431FD0, CBoatHooked>("CBoat::CBoat"); //CCarCtrl::CreateCarForScript
    hookSharedCall<0x5D2ADC, CBoatHooked>("CBoat::CBoat"); //CPools::LoadVehiclePool

    //Helis
    hookSharedCall<0x6CD3C3, CHeliHooked>("CHeli::CHeli"); //CPlane::DoPlaneGenerationAndRemoval
    hookSharedCall<0x6C6590, CHeliHooked>("CHeli::CHeli"); //CHeli::GenerateHeli
    hookSharedCall<0x6C6568, CHeliHooked>("CHeli::CHeli"); //CHeli::GenerateHeli
    hookSharedCall<0x5D2C46, CHeliHooked>("CHeli::CHeli"); //CPools::LoadVehiclePool
    hookSharedCall<0x6C7ACA, GenerateHeliHooked>("CHeli::GenerateHeli"); //CHeli::UpdateHelis

    hookSharedCall<0x6CD6D6, CPlaneHooked>("CPlane::CPlane"); //CPlane::DoPlaneGenerationAndRemoval
    hookSharedCall<0x42166F, CPlaneHooked>("CPlane::CPlane"); //CCarCtrl::GetNewVehicleDependingOnCarModel

    //Roadblocks
    hookSharedCall<0x42CDDD, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CCarCtrl::RemoveDistantCars
    hookSharedCall<0x42CE07, GenerateRoadBlockCopsForCarHooked>("CRoadBlocks::GenerateRoadBlockCopsForCar"); //CCarCtrl::RemoveDistantCars
    hookSharedCall<0x4613EB, GetColModelHooked>("CEntity::GetColModel"); //CRoadBlocks::GenerateRoadBlockCopsForCar
    hookSharedCall<0x5DDCA8, GetDefaultCopModelHooked>("CStreaming::GetDefaultCopModel"); //CCopPed::CCopPed
    hookSharedCall<0x46151A, CCopPedHooked>("CCopPed::CCopPed"); //CRoadBlocks::GenerateRoadBlockCopsForCar
    hookSharedCall<0x461541, CCopPedHooked>("CCopPed::CCopPed"); //CRoadBlocks::GenerateRoadBlockCopsForCar

    hookSharedCall<0x6D1A7A, AddPedInCarHooked>("CPopulation::AddPedInCar"); //CVehicle::SetUpDriver
    hookSharedCall<0x6D1B0E, AddPedInCarHooked>("CPopulation::AddPedInCar"); //CVehicle::SetupPassenger 
    hookSharedCall<0x6F6986, AddPedInCarHooked>("CPopulation::AddPedInCar"); //CTrain::RemoveRandomPassenger
    hookSharedCall<0x6F786F, AddPedInCarHooked>("CPopulation::AddPedInCar"); //CTrain::CreateMissionTrain
    hookSharedCall<0x613B7F, AddPedHooked>("CPopulation::AddPed"); //CPopulation::AddPedInCar
    hookSharedCall<0x431DE2, SetUpDriverAndPassengersForVehicleHooked>("CCarCtrl::SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar
    hookSharedCall<0x431DF9, SetUpDriverAndPassengersForVehicleHooked>("CCarCtrl::SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar
    hookSharedCall<0x431ED1, SetUpDriverAndPassengersForVehicleHooked>("CCarCtrl::SetUpDriverAndPassengersForVehicle"); //CCarCtrl::GenerateOneRandomCar
    hookSharedCall<0x42BBFB, AddAmbulanceOccupantsHooked>("CCarAI::AddAmbulanceOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar
    hookSharedCall<0x42BC1A, AddAmbulanceOccupantsHooked>("CCarAI::AddFiretruckOccupants"); //CCarCtrl::GenerateOneEmergencyServicesCar

    hookSharedCall<0x42DC19, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CCarCtrl::IsThisAnAppropriateNode
    hookSharedCall<0x42DD23, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CCarCtrl::IsThisAnAppropriateNode
    hookSharedCall<0x43DFCA, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CDarkel::RegisterCarBlownUpByPlayer
    hookSharedCall<0x478635, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //IS_EMERGENCY_SERVICES_VEHICLE
    hookSharedCall<0x479A28, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //IS_COP_VEHICLE_IN_AREA_3D_NO_SAVE
    hookSharedCall<0x4862B8, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CTheScripts::RemoveThisPed
    hookSharedCall<0x562D97, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CWanted::Update
    hookSharedCall<0x63E6BA, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CTaskComplexEnterCar::CreateSubTask
    hookSharedCall<0x6445FC, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CTaskSimpleCarDrive::ProcessPed
    hookSharedCall<0x647E48, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CTaskSimpleCarSetPedOut::ProcessPed
    hookSharedCall<0x64BD61, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CTaskSimpleCarSetPedInAsDriver::ProcessPed
    hookSharedCall<0x64C29F, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CTaskSimpleCarSetPedSlowDraggedOut::ProcessPed
    hookSharedCall<0x651145, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CCarEnterExit::IsVehicleStealable
    hookSharedCall<0x6B11C2, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //CAutomobile::CAutomobile

    hookSharedCall<0x60C4E8, PossiblyRemoveVehicleHooked>("CCarCtrl::PossiblyRemoveVehicle"); //CPlayerPed::KeepAreaAroundPlayerClear
    hookSharedCall<0x42CD55, PossiblyRemoveVehicleHooked>("CCarCtrl::PossiblyRemoveVehicle"); //CCarCtrl::RemoveDistantCars

    hookSharedCall<0x64BB57, SetDriverHooked>("CVehicle::SetDriver"); //CTaskSimpleCarSetPedInAsDriver::ProcessPed

    hookSharedCall<0x871164, CAutomobile__PreRenderHooked>("CAutomobile::PreRender", true);
    hookSharedCall<0x6CFADC, CAutomobile__PreRenderHooked>("CAutomobile::PreRender"); //CTrailer::PreRender

    hookSharedCall<0x6ABC93, GetVehicleAppearanceHooked>("CVehicle::GetVehicleAppearance"); //CAutomobile::PreRender
    x6ABCBE_Destination = injector::MakeJMP(0x6ABCBE, patch6ABCBE).as_int();

    hookSharedCall<0x85C5F4, CreateInstanceHooked>("CVehicleModelInfo::CreateInstance", true);
    hookSharedCall<0x40C80F, LoadClumpFileHooked>("CFileLoader::LoadClumpFile"); //CStreaming::ConvertBufferToObject

    hookSharedCall<0x4306A1, GetNewVehicleDependingOnCarModelHooked>("CCarCtrl::GetNewVehicleDependingOnCarModel"); ///CCarCtrl::GenerateOneRandomCar

    hookSharedCall<0x6D5F2F, CPhysicalHooked>("CPhysical::CPhysical"); //CVehicle::CVehicle

    hookSharedCall<0x6CFFBB, AddAudioEventHooked>("CAEVehicleAudioEntity::AddAudioEvent"); //CTrailer::SetTowLink
    hookSharedCall<0x6CEFCE, AddAudioEventHooked>("CAEVehicleAudioEntity::AddAudioEvent"); //CTrailer::BreakTowLink

    hookSharedCall<0x4251E6, CWorld__RemoveHooked>("CWorld::Remove"); //CCarCtrl::PossiblyRemoveVehicle
    hookSharedCall<0x425221, CWorld__RemoveHooked>("CWorld::Remove"); //CCarCtrl::PossiblyRemoveVehicle
    hookSharedCall<0x42541E, CWorld__RemoveHooked>("CWorld::Remove"); //CCarCtrl::PossiblyRemoveVehicle
    hookSharedCall<0x4323F9, CWorld__RemoveHooked>("CWorld::Remove"); //CCarCtrl::RemoveCarsIfThePoolGetsFull
    hookSharedCall<0x449729, CWorld__RemoveHooked>("CWorld::Remove"); //CGarage::RemoveCarsBlockingDoorNotInside
    hookSharedCall<0x4499F3, CWorld__RemoveHooked>("CWorld::Remove"); //CGarage::StoreAndRemoveCarsForThisHideOut
    hookSharedCall<0x449B43, CWorld__RemoveHooked>("CWorld::Remove"); //CGarage::StoreAndRemoveCarsForThisImpoundingGarage
    hookSharedCall<0x449CE0, CWorld__RemoveHooked>("CWorld::Remove"); //CGarage::TidyUpGarage
    hookSharedCall<0x449E2A, CWorld__RemoveHooked>("CWorld::Remove"); //CGarage::TidyUpGarageClose
    hookSharedCall<0x4610CC, CWorld__RemoveHooked>("CWorld::Remove"); //CRoadBlocks::ClearSpaceForRoadBlockObject
    hookSharedCall<0x467B3C, CWorld__RemoveHooked>("CWorld::Remove"); //DELETE_CAR
    hookSharedCall<0x4698E4, CWorld__RemoveHooked>("CWorld::Remove"); //DELETE_OBJECT
    hookSharedCall<0x486D3E, CWorld__RemoveHooked>("CWorld::Remove"); //CTheScripts::ClearSpaceForMissionEntity
    hookSharedCall<0x499D90, CWorld__RemoveHooked>("CWorld::Remove"); //CSetPiece::Update
    hookSharedCall<0x49A45A, CWorld__RemoveHooked>("CWorld::Remove"); //CSetPiece::Update
    hookSharedCall<0x5667B0, CWorld__RemoveHooked>("CWorld::Remove"); //CWorld::ClearCarsFromArea
    hookSharedCall<0x6A9CA4, CWorld__RemoveHooked>("CWorld::Remove"); //CAutomobile::Teleport
    hookSharedCall<0x6D22D7, CWorld__RemoveHooked>("CWorld::Remove"); //DestroyVehicleAndDriverAndPassengers

    //Tuning for parked cars
    hookSharedCall<0x6F3C8C, CWorld__AddHooked>("CWorld::Add"); //CCarGenerator::DoInternalProcessing

    if (enableNullGuards)
    {
        x4306A1_Destination = injector::GetBranchDestination(0x4306A1).as_int();
        if (isAddressValid(x4306A1_Destination))
            hookASM(0x4306A1, 0, patch4306A1, "CCarCtrl::GenerateOneRandomCar");

        hookSharedCall<0x6A078A, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CAutomobile::SetupModelNodes
        hookSharedCall<0x6A65B4, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CAutomobile::SetModelIndex
        hookSharedCall<0x6B0B92, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CAutomobile::CAutomobile
        hookSharedCall<0x6B597A, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CBike::SetupModelNodes
        hookSharedCall<0x6B8994, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CBike::SetModelIndex
        hookSharedCall<0x6BF50D, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CBike::CBike
        hookSharedCall<0x6F01BA, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CBoat::SetupModelNodes
        hookSharedCall<0x6F2A1D, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CBoat::CBoat
        hookSharedCall<0x6F5554, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CTrain::SetModelIndex
        hookSharedCall<0x6F60D1, FillFrameArrayHooked>("CClumpModelInfo::FillFrameArray"); //CTrain::CTrain

        hookSharedCall<0x6BF768, SetupSuspensionLinesHooked>("CBike::SetupSuspensionLines"); //CBike::CBike

        hookSharedCall<0x6B19F2, UpdateClumpAlphaHooked>("CVehicle::UpdateClumpAlpha"); //CAutomobile::ProcessControl
        hookSharedCall<0x6B92F5, UpdateClumpAlphaHooked>("CVehicle::UpdateClumpAlpha"); //CBike::ProcessControl
        hookSharedCall<0x6F185D, UpdateClumpAlphaHooked>("CVehicle::UpdateClumpAlpha"); //CBoat::ProcessControl

        hookSharedCall<0x6F3DF2, SetClumpAlphaHooked>("CVisibilityPlugins::SetClumpAlpha"); //CCarGenerator::DoInternalProcessing
    }

    if (vehOptions.changeScriptedCars)
        hookSharedCall<0x467B01, CreateCarForScriptHooked>("CCarCtrl::CreateCarForScript"); //00A5: CREATE_CAR

    if (vehOptions.enableSiren)
    {
        hookSharedCall<0x41DC74, UsesSirenHooked>("CVehicle::UsesSiren"); //CCarAI::UpdateCarAI
        hookSharedCall<0x41E05F, UsesSirenHooked>("CVehicle::UsesSiren"); //CCarAI::UpdateCarAI
        hookSharedCall<0x41E874, UsesSirenHooked>("CVehicle::UsesSiren"); //CCarAI::UpdateCarAI
        hookSharedCall<0x41F10F, UsesSirenHooked>("CVehicle::UsesSiren"); //CCarAI::UpdateCarAI
        hookSharedCall<0x462344, UsesSirenHooked>("CVehicle::UsesSiren"); //CRoadBlocks::CreateRoadBlockBetween2Points
        hookSharedCall<0x4F77DA, UsesSirenHooked>("CVehicle::UsesSiren"); //CAEVehicleAudioEntity::Initialise
        hookSharedCall<0x61369D, UsesSirenHooked>("CVehicle::UsesSiren"); //CPopulation::CreateWaitingCoppers
        hookSharedCall<0x6B2BCB, UsesSirenHooked>("CVehicle::UsesSiren"); //CAutomobile::ProcessControl
        hookSharedCall<0x6E0954, UsesSirenHooked>("CVehicle::UsesSiren"); //CVehicle::ProcessSirenAndHorn
    }

    if (vehOptions.enableLights)
    {
        SharedCallHookState* const registerCoronaState = hookSharedCall<0x6ABA60, RegisterCoronaHooked<false>>("CCoronas::RegisterCorona"); //CAutomobile::PreRender
        hookSharedCall<0x6ABB35, RegisterCoronaHooked<false>>("CCoronas::RegisterCorona"); //CAutomobile::PreRender
        hookSharedCall<0x6ABC69, RegisterCoronaHooked<false>>("CCoronas::RegisterCorona"); //CAutomobile::PreRender

        if (registerCoronaState)
            RegisterCoronaHookedPointer = createSharedCallThunk<&RegisterCoronaHooked<true>>(*registerCoronaState);

        if (RegisterCoronaHookedPointer && (memoryMatchesOriginalExe(0x6ABA56, 5) || forceEnableGlobal || forceEnable.contains(0x6ABA56)))
            injector::MakeJMP(0x6ABA56, patchCoronas);
        else if (RegisterCoronaHookedPointer)
            Log::LogModifiedAddress(0x6ABA56, "Modified method detected: CAutomobile::PreRender - 0x6ABA56 is %s\n", bytesToString(0x6ABA56, 5).c_str());

        hookSharedCall<0x6AB80F, AddLightHooked>("CPointLights::AddLight"); //CAutomobile::PreRender
        hookSharedCall<0x6ABBA6, AddLightHooked>("CPointLights::AddLight"); //CAutomobile::PreRender

        hookSharedCall<0x6AB34B, AddDamagedVehicleParticlesHooked>("CVehicle::AddDamagedVehicleParticles"); //CAutomobile::PreRender
    }

    if (vehOptions.enableTrailerLights)
        hookSharedCall<0x6ABCB9, DoVehicleLightsHooked>("CVehicle::DoVehicleLights"); //CAutomobile::PreRender

    if (vehOptions.disablePayAndSpray)
        hookSharedCall<0x44AC75, IsCarSprayableHooked>("CGarages::IsCarSprayable"); //CGarage::Update

    if (vehOptions.enableSideMissions)
    {
        hookSharedCall<0x48DA81, IsLawEnforcementVehicleHooked>("CVehicle::IsLawEnforcementVehicle"); //056C: IS_CHAR_IN_ANY_POLICE_VEHICLE
        hookSharedCall<0x469624, CPool__atHandleHooked>("CPool<CPed>::atHandle"); //00DD: IS_CHAR_IN_MODEL
        hookSharedCall<0x4912AD, CPool__atHandleTaxiHooked>("CPool<CPed>::atHandle"); //0602: IS_CHAR_IN_TAXI
    }	
	
    if (vehOptions.enableSpecialFeatures)
    {
        hookSharedCall<0x8711CC, SetUpWheelColModelHooked>("CAutomobile::SetUpWheelColModel", true);
        hookSharedCall<0x871B94, SetUpWheelColModelHooked>("CAutomobile::SetUpWheelColModel", true);
        hookSharedCall<0x871CD4, SetUpWheelColModelHooked>("CAutomobile::SetUpWheelColModel", true);
     
        hookASM(0x525462, 8, movReg16WordPtrReg<REG_AX, REG_EDI, 0x52546A, 4, 0x01BB3D66>, "CCam::Process_FollowCar_SA");
        hookASM(0x431BEB, 7, movReg16WordPtrReg<REG_AX, REG_ESI, 0x431BF2, 3, 0x9004C483>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x64467D, 6, cmpWordPtrRegModel<REG_EAX, 0x644683, 0x213>, "CTaskSimpleCarDrive::ProcessPed");
        hookASM(0x51E5B8, 6, cmpWordPtrRegModel<REG_ESI, 0x51E5BE, 0x1B0>, "CCamera::TryToStartNewCamMode");
        hookASM(0x6B4CE8, 9, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6B4CF1, 5, 0x1BF98166, 0x90909002>, "CAutomobile::ProcessAI");
        hookASM(0x5A0EAF, 6, cmpWordPtrRegModel<REG_EAX, 0x5A0EB5, 0x259>, "CObject::ObjectDamage");
        hookASM(0x4308A1, 6, cmpWordPtrRegModel<REG_ESI, 0x4308A7, 0x1A7>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x4F62E4, 6, cmpWordPtrRegModel<REG_EAX, 0x4F62EA, 0x1A7>, "CAEVehicleAudioEntity::GetSirenState");
        hookASM(0x4F9CBC, 6, cmpWordPtrRegModel<REG_ECX, 0x4F9CC2, 0x1A7>, "CAEVehicleAudioEntity::PlayHornOrSiren");
        hookASM(0x44AB2A, 6, cmpWordPtrRegModel<REG_EDI, 0x44AB30, 0x1A7>, "CGarage::Update");
        hookASM(0x52AE34, 6, cmpWordPtrRegModel<REG_EAX, 0x52AE3A, 0x1A7>, "CCamera::CamControl");
        hookASM(0x4FB26B, 8, movReg16WordPtrReg<REG_AX, REG_ECX, 0x4FB273, 4, 0x01BB3D66>, "CAEVehicleAudioEntity::ProcessMovingParts");
        hookASM(0x54742F, 9, movReg16WordPtrReg<REG_CX, REG_EDI, 0x547438, 5, 0x96F98166, 0x90909001>, "CPhysical::PositionAttachedEntity");
        hookASM(0x5A0052, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x5A005A, 4, 0x01963D66>, "CObject::SpecialEntityPreCollisionStuff");
        hookASM(0x5A21C9, 9, movReg16WordPtrReg<REG_CX, REG_EAX, 0x5A21D2, 5, 0x96F98166, 0x90909001>, "CObject::ProcessControl");
        hookASM(0x6A1480, 6, movReg16WordPtrReg<REG_CX, REG_EDI, 0x6A1486, 2, 0x9090F633>, "CAutomobile::UpdateMovingCollision");
        hookASM(0x6A173B, 8, movReg16WordPtrReg<REG_AX, REG_EDI, 0x6A1743, 4, 0x01E63D66>, "CAutomobile::UpdateMovingCollision");
        hookASM(0x6A1F69, 9, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6A1F72, 5, 0x96F98166, 0x90909001>, "CAutomobile::AddMovingCollisionSpeed");
        hookASM(0x6A2162, 8, movReg16WordPtrReg<REG_AX, REG_ECX, 0x6A216A, 4, 0x01963D66>, "CAutomobile::GetMovingCollisionOffset");
        hookASM(0x6C7F30, 6, cmpWordPtrRegModel<REG_ESI, 0x6C7F36, 0x196>, "CMonsterTruck::PreRender");
        hookASM(0x5470BF, 6, cmpWordPtrRegModel<REG_ECX, 0x5470C5, 0x212>, "CPhysical::PositionAttachedEntity");
        hookASM(0x54D70D, 6, cmpWordPtrRegModel<REG_EDI, 0x54D713, 0x212>, "CPhysical::AttachEntityToEntity");
        hookASM(0x5A0EBF, 6, cmpWordPtrRegModel<REG_EDI, 0x5A0EC5, 0x212>, "CObject::ObjectDamage");
        hookASM(0x6A1648, 6, cmpWordPtrRegModel<REG_EDI, 0x6A164E, 0x212>, "CAutomobile::UpdateMovingCollision");
        hookASM(0x6AD378, 6, cmpWordPtrRegModel<REG_ESI, 0x6AD37E, 0x212>, "CAutomobile::ProcessEntityCollision");
        hookASM(0x6E0FF8, 6, cmpWordPtrRegModel<REG_EDI, 0x6E0FFE, 0x212>, "CVehicle::DoHeadLightBeam");
        hookASM(0x43064C, 6, cmpReg32Model<REG_EDI, 0x430652, 0x1AF>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x64BCB3, 6, cmpWordPtrRegModel<REG_EAX, 0x64BCB9, 0x1AF>, "CTaskSimpleCarSetPedInAsDriver::ProcessPed");
        hookASM(0x430640, 6, cmpReg32Model<REG_EDI, 0x430646, 0x1B5>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x6A155C, 8, patch6A155C, "CAutomobile::UpdateMovingCollision");
        hookASM(0x502222, 6, cmpWordPtrRegModel<REG_EAX, 0x502228, 0x214>, "CAEVehicleAudioEntity::ProcessVehicle");
        hookASM(0x6AA515, 9, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6AA51E, 5, 0x14F98166, 0x90909002>, "CAutomobile::UpdateWheelMatrix");
        hookASM(0x6D1ABA, 6, movReg16WordPtrReg<REG_AX, REG_EDI, 0x6D1AC0, 2, 0x9090D232 >, "CVehicle::SetupPassenger");
        hookASM(0x6C926D, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C9275, 4, 0x02003D66>, "CPlane::ProcessControl");
        hookASM(0x6CA945, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6CA94D, 4, 0x02003D66>, "CPlane::PreRender");
        hookASM(0x6CACF0, 6, cmpWordPtrRegModel<REG_ESI, 0x6CACF6, 0x201>, "CPlane::OpenDoor");
        hookASM(0x6D67B7, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6D67BF, 4, 0x01963D66>, "CVehicle::SpecialEntityPreCollisionStuff");
        hookASM(0x6B0F47, 10, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B0F51, 6, 0x8B3805D9, 0x90900085>, "CAutomobile::CAutomobile");
        hookASM(0x6B0CF0, 6, cmpWordPtrRegModel<REG_ESI, 0x6B0CF6, 0x1B0>, "CAutomobile::CAutomobile");
        hookASM(0x6B0EE2, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B0EEA, 4, 0x020D3D66>, "CAutomobile::CAutomobile");
        hookASM(0x6B11D5, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B11DD, 4, 0xFFFE3D66>, "CAutomobile::CAutomobile");
        hookASM(0x6B0298, 6, cmpWordPtrRegModel<REG_ESI, 0x6B029E, 0x1B0>, "CAutomobile::ProcessSuspension");
        hookASM(0x6AFB44, 6, cmpWordPtrRegModel<REG_ESI, 0x6AFB4A, 0x1B0>, "CAutomobile::ProcessSuspension");
        hookASM(0x51D870, 6, cmpWordPtrRegModel<REG_EAX, 0x51D876, 0x1B0>, "sub_51D770");
        hookASM(0x527058, 6, cmpWordPtrRegModel<REG_EAX, 0x52705E, 0x208>, "CCam::Process");
        hookASM(0x58E09F, 6, cmpWordPtrRegModel<REG_EAX, 0x58E0A5, 0x208>, "CHud::DrawCrossHairs");
        hookASM(0x58E0B3, 6, cmpWordPtrRegModel<REG_EAX, 0x58E0B9, 0x1A9>, "CHud::DrawCrossHairs");
        hookASM(0x6A53BA, 5, cmpReg32Model<REG_EAX, 0x6A53BF, 0x208>, "CAutomobile::ProcessCarWheelPair");
        hookASM(0x6C8F10, 6, cmpReg32Model<REG_EDI, 0x6C8F16, 0x208>, "CPlane::CPlane");
        hookASM(0x6C9101, 6, cmpWordPtrRegModel<REG_ESI, 0x6C9107, 0x208>, "CPlane::CPlane");
        hookASM(0x6C968E, 6, cmpWordPtrRegModel<REG_ESI, 0x6C9694, 0x208>, "CPlane::PreRender");
        hookASM(0x6C9D7E, 9, movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6C9D87, 5, 0xFFFE2405, 0x909090FF>, "CPlane::PreRender");
        hookASM(0x6C9EE3, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C9EEB, 4, 0x02503D66>, "CPlane::PreRender");
        hookASM(0x6CC318, 9, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6CC321, 5, 0xD0F98166, 0x90909001>, "CPlane::ProcessFlyingCarStuff");
        hookASM(0x6D8EFE, 11, movReg16WordPtrReg<REG_BX, REG_ESI, 0x6D8F09, 7, 0x742484D9, 0x90000002>, "CVehicle::FlyingControl");
        hookASM(0x6D9C04, 6, cmpWordPtrRegModel<REG_ESI, 0x6D9C0A, 0x208>, "CVehicle::FlyingControl");
        hookASM(0x6E3457, 9, movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6E3460, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneWeaponFiringStatus");
        hookASM(0x6D4D5E, 9, movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6D4D67, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::FirePlaneGuns");
        hookASM(0x6D3F30, 9, movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D3F39, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneNumGuns");
        hookASM(0x6D4125, 9, movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D412E, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneGunsRateOfFire");
        hookASM(0x6D514F, 9, movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6D5158, 5, 0x0001A92D, 0x90909000>, "CVehicle::FireUnguidedMissile");
        hookASM(0x6D45D5, 9, movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D45DE, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneOrdnanceRateOfFire");
        hookASM(0x6D3E00, 9, movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D3E09, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::GetPlaneGunsAutoAimAngle");
        hookASM(0x501C73, 9, movsxReg32WordPtrReg<REG_EAX, REG_EDX, 0x501C7C, 5, 0xFFFDF905, 0x909090FF>, "CAEVehicleAudioEntity::ProcessAircraft");
        hookASM(0x4FF980, 9, movsxReg32WordPtrReg<REG_EAX, REG_EAX, 0x4FF989, 5, 0xFFFDF905, 0x909090FF>, "CAEVehicleAudioEntity::ProcessGenericJet");
        hookASM(0x524624, 8, movReg16WordPtrReg<REG_AX, REG_EDI, 0x52462C, 4, 0x01B93D66>, "CCam::Process_FollowCar_SA");
        hookASM(0x4F7814, 9, movsxReg32WordPtrReg<REG_EAX, REG_EDX, 0x4F781D, 5, 0xFFFE4005, 0x909090FF>, "CAEVehicleAudioEntity::Initialise");
        hookASM(0x4FB343, 9, movsxReg32WordPtrReg<REG_EAX, REG_EDX, 0x4FB34C, 5, 0xFFFE6A05, 0x909090FF>, "CAEVehicleAudioEntity::ProcessMovingParts");
        hookASM(0x426F94, 6, cmpWordPtrRegModel<REG_ESI, 0x426F9A, 0x21B>, "CCarCtrl::PickNextNodeToChaseCar");
        hookASM(0x427790, 6, cmpWordPtrRegModel<REG_ESI, 0x427796, 0x21B>, "CCarCtrl::PickNextNodeToFollowPath");
        hookASM(0x42DB2E, 6, cmpWordPtrRegModel<REG_EDI, 0x42DB34, 0x21B>, "CCarCtrl::IsThisAnAppropriateNode");
        hookASM(0x42FE50, 6, cmpWordPtrRegModel<REG_ESI, 0x42FE56, 0x21B>, "CCarCtrl::ReconsiderRoute");
        hookASM(0x42FF0B, 6, cmpWordPtrRegModel<REG_ESI, 0x42FF11, 0x21B>, "CCarCtrl::ReconsiderRoute");
        hookASM(0x435A81, 6, cmpWordPtrRegModel<REG_ESI, 0x435A87, 0x21B>, "CCarCtrl::SteerAICarWithPhysicsFollowPath_Racing");
        hookASM(0x4382A4, 6, cmpWordPtrRegModel<REG_ESI, 0x4382AA, 0x21B>, "CCarCtrl::SteerAICarWithPhysics");
        hookASM(0x5583B3, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x5583BB, 4, 0x01D93D66>, "CRope::Update");
        hookASM(0x5707FD, 8, movReg16WordPtrReg<REG_AX, REG_EBX, 0x570805, 4, 0x01CC3D66>, "CPlayerInfo::Process");
        hookASM(0x5869FF, 6, cmpWordPtrRegModel<REG_EAX, 0x586A05, 0x21B>, "CRadar::DrawRadarMap");
        hookASM(0x586B77, 6, cmpWordPtrRegModel<REG_EAX, 0x586B7D, 0x21B>, "CRadar::DrawMap");
        hookASM(0x587D66, 6, cmpWordPtrRegModel<REG_EAX, 0x587D6C, 0x21B>, "CRadar::SetupAirstripBlips");
        hookASM(0x588570, 7, patch588570, "CRadar::DrawBlips");
        hookASM(0x58A3D7, 6, cmpWordPtrRegModel<REG_EAX, 0x58A3DD, 0x21B>, "CHud::DrawRadar");
        hookASM(0x58A5A0, 6, cmpWordPtrRegModel<REG_EAX, 0x58A5A6, 0x21B>, "CHud::DrawRadar");
        hookASM(0x643BE5, 9, movReg16WordPtrReg<REG_CX, REG_EAX, 0x643BEE, 5, 0xCCF98166, 0x90909001>, "CTaskComplexEnterCar::CreateFirstSubTask");
        hookASM(0x6508ED, 9, movReg16WordPtrReg<REG_SI, REG_ESI, 0x6508F6, 5, 0xCCFE8166, 0x90909001>, "IsRoomForPedToLeaveCar");
        hookASM(0x6A8D4E, 6, cmpWordPtrRegModel<REG_ESI, 0x6A8D54, 0x21B>, "CAutomobile::ProcessBuoyancy");
        hookASM(0x6A8F18, 9, movReg16WordPtrReg<REG_CX, REG_ESI, 0X6A8F21, 5, 0xBFF98166, 0x90909001>, "CAutomobile::ProcessBuoyancy");
        hookASM(0x6AA72D, 6, cmpWordPtrRegModel<REG_ESI, 0x6AA733, 0x21B>, "CAutomobile::UpdateWheelMatrix");
        hookASM(0x6AFFEA, 6, cmpWordPtrRegModel<REG_ESI, 0x6AFFF0, 0x21B>, "CAutomobile::ProcessSuspension");
        hookASM(0x6B0017, 6, cmpWordPtrRegModel<REG_ESI, 0x6B001D, 0x21B>, "CAutomobile::ProcessSuspension");
        hookASM(0x6C8E54, 6, cmpWordPtrRegModel<REG_ESI, 0x6C8E5A, 0x21B>, "CPlane::CPlane");
        hookASM(0x6C934D, 6, cmpWordPtrRegModel<REG_ESI, 0x6C9353, 0x21B>, "CPlane::ProcessControl");
        hookASM(0x6C94FB, 6, cmpWordPtrRegModel<REG_ESI, 0x6C9501, 0x21B>, "CPlane::PreRender");
        hookASM(0x6C97E6, 6, cmpWordPtrRegModel<REG_ESI, 0x6C97EC, 0x21B>, "CPlane::PreRender");
        hookASM(0x6CA70E, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6CA716, 4, 0x34245CD9>, "CPlane::PreRender");
        hookASM(0x6CA750, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6CA758, 4, 0x021B3D66>, "CPlane::PreRender");
        hookASM(0x6CC4C4, 6, cmpWordPtrRegModel<REG_ESI, 0x6CC4CA, 0x21B>, "CPlane::VehicleDamage");
        hookASM(0x6D8E18, 9, movReg16WordPtrReg<REG_BX, REG_ESI, 0x6D8E21, 5, 0x1BFB8166, 0x90909002>, "CVehicle::FlyingControl");
        hookASM(0x6D9233, 6, cmpWordPtrRegModel<REG_ESI, 0x6D9239, 0x21B>, "CVehicle::FlyingControl");
        hookASM(0x70BF09, 6, movsxReg32WordPtrReg<REG_EBX, REG_EDI, 0x70BF0F, 2, 0x9090D8DD>, "CShadows::StoreShadowForVehicle");
        hookASM(0x501AB9, 9, movsxReg32WordPtrReg<REG_EAX, REG_EAX, 0x501AC2, 5, 0xFFFE4D05, 0x909090FF>, "CAEVehicleAudioEntity::ProcessSpecialVehicle");
        hookASM(0x6C41D9, 6, cmpReg32Model<REG_EDI, 0x6C41DF, 0x1A9>, "CHeli::CHeli");
        hookASM(0x6C50B3, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C50BB, 4, 0x01D13D66>, "CHeli::ProcessFlyingCarStuff");
        hookASM(0x6D4900, 9, movsxReg32WordPtrReg<REG_EAX, REG_ECX, 0x6D4909, 5, 0xFFFE5705, 0x909090FF>, "CVehicle::SelectPlaneWeapon");
        hookASM(0x6E1C17, 6, cmpWordPtrRegModel<REG_ESI, 0x6E1C1D, 0x1DD>, "CVehicle::DoVehicleLights");
        hookASM(0x6C4F66, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6C4F6E, 4, 0x01BF3D66>, "CHeli::ProcessFlyingCarStuff");
        hookASM(0x6C5605, 9, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6C560E, 5, 0xD5F98166, 0x90909001>, "CHeli::PreRender");
        hookASM(0x7408E3, 8, movReg16WordPtrReg<REG_AX, REG_EDI, 0x7408EB, 4, 0x01BF3D66>, "CWeapon::FireInstantHit");
        hookASM(0x6A8DE2, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6A8DEA, 4, 0x01BF3D66>, "CAutomobile::ProcessBuoyancy");
        hookASM(0x6F367E, 6, cmpReg32Model<REG_EBP, 0x6F3684, 0x1BF>, "CCarGenerator::DoInternalProcessing");
        hookASM(0x51D864, 6, cmpWordPtrRegModel<REG_EDX, 0x51D86A, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51D92B, 6, cmpWordPtrRegModel<REG_EDX, 0x51D931, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51DA60, 6, cmpWordPtrRegModel<REG_ECX, 0x51DA66, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51DCFC, 6, cmpWordPtrRegModel<REG_EAX, 0x51DD02, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51DE84, 6, cmpWordPtrRegModel<REG_EAX, 0x51DE8A, 0x1CC>, "CCamera::IsItTimeForNewcam");
        hookASM(0x51E5AC, 6, cmpWordPtrRegModel<REG_EAX, 0x51E5B2, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x51E773, 6, cmpWordPtrRegModel<REG_ECX, 0x51E779, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x51E937, 6, cmpWordPtrRegModel<REG_EAX, 0x51E93D, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x51EF39, 6, cmpWordPtrRegModel<REG_EDX, 0x51EF3F, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x51F15B, 6, cmpWordPtrRegModel<REG_ECX, 0x51F161, 0x1CC>, "CCamera::TryToStartNewCamMode");
        hookASM(0x55432A, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x554332, 4, 0x01B03D66>, "CRenderer::SetupEntityVisibility");
        hookASM(0x6C2D33, 8, movReg16WordPtrReg<REG_AX, REG_EDI, 0x6C2D3B, 4, 0x01A13D66>, "cBuoyancy::PreCalcSetup");
        hookASM(0x6C92EC, 6, cmpWordPtrRegModel<REG_ESI, 0x6C92F2, 0x1CC>, "CPlane::ProcessControl");
        hookASM(0x6CAA93, 6, cmpWordPtrRegModel<REG_ESI, 0x6CAA99, 0x1CC>, "CPlane::PreRender");
        hookASM(0x6D274C, 6, cmpWordPtrRegModel<REG_ESI, 0x6D2752, 0x1CC>, "CVehicle::ApplyBoatWaterResistance");
        hookASM(0x6DBF0A, 6, cmpWordPtrRegModel<REG_ESI, 0x6DBF10, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6DC00B, 6, cmpWordPtrRegModel<REG_ESI, 0x6DC011, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6DC21B, 6, cmpWordPtrRegModel<REG_ESI, 0x6DC221, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6DC621, 6, cmpWordPtrRegModel<REG_ESI, 0x6DC627, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6DCD63, 6, cmpWordPtrRegModel<REG_ESI, 0x6DCD69, 0x1CC>, "CVehicle::ProcessBoatControl");
        hookASM(0x6EDA0C, 6, cmpWordPtrRegModel<REG_ESI, 0x6EDA12, 0x1CC>, "CWaterLevel::RenderBoatWakes");
        hookASM(0x6F0234, 6, cmpWordPtrRegModel<REG_ESI, 0x6F023A, 0x1CC>, "CBoat::Render");
        hookASM(0x6F1A8A, 6, cmpWordPtrRegModel<REG_ESI, 0x6F1A90, 0x1CC>, "CBoat::ProcessControl");
        hookASM(0x6F1F5B, 6, cmpWordPtrRegModel<REG_ESI, 0x6F1F61, 0x1CC>, "CBoat::ProcessControl");
        hookASM(0x6F3672, 6, cmpReg32Model<REG_EBP, 0x6F3678, 0x1CC>, "CCarGenerator::DoInternalProcessing");
        hookASM(0x528294, 6, cmpWordPtrRegModel<REG_ECX, 0x52829A, 0x1CC>, "CCamera::CamControl");
        hookASM(0x6F368A, 6, cmpReg32Model<REG_EBP, 0x6F3690, 0x1A1>, "CCarGenerator::DoInternalProcessing");
        hookASM(0x5626D1, 6, cmpWordPtrRegModel<REG_ESI, 0x5626D7, 0x1F1>, "CWanted::WorkOutPolicePresence");
        hookASM(0x6C7172, 6, cmpWordPtrRegModel<REG_ESI, 0x6C7178, 0x1F1>, "CHeli::ProcessControl");
        hookASM(0x6C8F31, 6, cmpReg32Model<REG_EDI, 0x6C8F37, 0x1DC>, "CPlane::CPlane");
        hookASM(0x6C8F3D, 6, cmpReg32Model<REG_EDI, 0x6C8F43, 0x200>, "CPlane::CPlane");
        hookASM(0x6C8F49, 6, cmpReg32Model<REG_EDI, 0x6C8F4F, 0x207>, "CPlane::CPlane");
        hookASM(0x6C8F96, 6, cmpReg32Model<REG_EDI, 0x6C8F9C, 0x229>, "CPlane::CPlane");
        hookASM(0x6C8FCB, 6, cmpReg32Model<REG_EDI, 0x6C8FD1, 0x21B>, "CPlane::CPlane");
        hookASM(0x6C8FFA, 6, cmpReg32Model<REG_EDI, 0x6C9000, 0x201>, "CPlane::CPlane");
        hookASM(0x6D6A7B, 10, movsxReg32WordPtrReg<REG_ECX, REG_ESI, 0x6D6A85, 6, 0x04888688, 0x90900000>, "CVehicle::SetModelIndex");
        hookASM(0x429051, 6, cmpWordPtrRegModel<REG_ESI, 0x429057, 0x1AE>, "CCarCtrl::SteerAIBoatWithPhysicsAttackingPlayer");
        hookASM(0x48DA90, 6, cmpWordPtrRegModel<REG_EAX, 0x48DA96, 0x1AE>, "CRunningScript::ProcessCommands1300To1399");
        hookASM(0x512570, 6, cmpWordPtrRegModel<REG_ECX, 0x512576, 0x1AE>, "CCam::Process_WheelCam");
        hookASM(0x6F028D, 9, movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6F0296, 5, 0xFFFE5205, 0x909090FF>, "CBoat::Render");
        hookASM(0x6F1487, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6F148F, 4, 0x01AE3D66>, "CBoat::PreRender");
        hookASM(0x6F1801, 6, cmpWordPtrRegModel<REG_ESI, 0x6F1807, 0x1AE>, "CBoat::ProcessControl");
        hookASM(0x6F18AD, 6, cmpWordPtrRegModel<REG_ESI, 0x6F18B3, 0x1AE>, "CBoat::ProcessControl");
        hookASM(0x431C57, 6, cmpWordPtrRegModel<REG_ESI, 0x431C5D, 0x1C9>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x4F51F6, 6, cmpWordPtrRegModel<REG_EAX, 0x4F51FC, 0x1C9>, "CAEVehicleAudioEntity::GetVolumeForDummyIdle");
        hookASM(0x4F5316, 6, cmpWordPtrRegModel<REG_EAX, 0x4F531C, 0x1C9>, "CAEVehicleAudioEntity::GetFrequencyForDummyIdle");
        hookASM(0x4F5D35, 6, cmpWordPtrRegModel<REG_EDX, 0x4F5D3B, 0x1C9>, "CAEVehicleAudioEntity::GetVolForPlayerEngineSound");
        hookASM(0x4F8213, 6, cmpWordPtrRegModel<REG_EDX, 0x4F8219, 0x1C9>, "CAEVehicleAudioEntity::GetFreqForPlayerEngineSound");
        hookASM(0x4F8972, 6, cmpWordPtrRegModel<REG_ECX, 0x4F8978, 0x1C9>, "CAEVehicleAudioEntity::ProcessVehicleFlatTyre");
        hookASM(0x570F72, 6, cmpWordPtrRegModel<REG_ECX, 0x570F78, 0x1C9>, "CPlayerInfo::Process");
        hookASM(0x431A99, 6, cmpWordPtrRegModel<REG_ESI, 0x431A9F, 0x1E4>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x6F13A4, 6, cmpWordPtrRegModel<REG_ESI, 0x6F13AA, 0x1E4>, "CBoat::PreRender");
        hookASM(0x6F2B7E, 6, cmpWordPtrRegModel<REG_ESI, 0x6F2B84, 0x1E4>, "CBoat::CBoat");
        hookASM(0x6D03ED, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6D03F5, 4, 0x025E3D66>, "CTrailer::CTrailer");
        hookASM(0x6CFD6B, 8, movReg16WordPtrReg<REG_AX, REG_ECX, 0x6CFD73, 4, 0x025E3D66>, "CTrailer::GetTowBarPos");
        hookASM(0x6AF250, 7, movReg16WordPtrReg<REG_AX, REG_ECX, 0x6AF257, 3, 0x900CEC83>, "CAutomobile::GetTowBarPos");
        hookASM(0x6AF2B6, 8, movReg16WordPtrReg<REG_AX, REG_EDX, 0x6AF2BE, 4, 0x025E3D66>, "CAutomobile::GetTowBarPos");
        hookASM(0x6A845E, 6, cmpWordPtrRegModel<REG_ESI, 0x6A8464, 0x1A8>, "CAutomobile::VehicleDamage");
        hookASM(0x6B539C, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B53A4, 4, 0x01B93D66>, "CAutomobile::ProcessAI");
        hookASM(0x6A6128, 5, cmpReg16Model<REG_SI, 0x6A612D, 0x23B>, "CAutomobile::FindWheelWidth");
        hookASM(0x6A8052, 9, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6A805B, 5, 0xACF98166, 0x90909001>, "CAutomobile::VehicleDamage");
        hookASM(0x6A80BC, 6, cmpWordPtrRegModel<REG_EBP, 0x6A80C2, 0x1B0>, "CAutomobile::VehicleDamage");
        hookASM(0x6A8380, 6, cmpWordPtrRegModel<REG_EAX, 0x6A8386, 0x1B0>, "CAutomobile::VehicleDamage");
        hookASM(0x6E153D, 5, cmpReg16Model<REG_SI, 0x6E1542, 471>, "CVehicle::DoHeadLightReflectionSingle");
        hookASM(0x6DEC4A, 6, cmpWordPtrRegModel<REG_ESI, 0x6DEC50, 471>, "CVehicle::AddSingleWheelParticles");
        hookASM(0x6DEEA3, 6, cmpWordPtrRegModel<REG_ESI, 0x6DEEA9, 471>, "CVehicle::AddSingleWheelParticles");
        hookASM(0x6DF0E3, 6, cmpWordPtrRegModel<REG_ESI, 0x6DF0E9, 471>, "CVehicle::AddSingleWheelParticles");
        hookASM(0x6DF316, 6, cmpWordPtrRegModel<REG_ESI, 0x6DF31C, 471>, "CVehicle::AddSingleWheelParticles");
        hookASM(0x430778, 5, cmpReg16Model<REG_CX, 0x43077D, 446>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x43077F, 5, cmpReg16Model<REG_CX, 0x430784, 452>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x430786, 5, cmpReg16Model<REG_CX, 0x43078B, 493>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x431D89, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x431D91, 4, 0x01CF3D66>, "CCarCtrl::GenerateOneRandomCar");
        hookASM(0x6B6C86, 6, cmpWordPtrRegModel<REG_ESI, 0x6B6C8C, 0x1B0>, "CBike::DoBurstAndSoftGroundRatios");
        hookASM(0x6AF292, 6, cmpWordPtrRegModel<REG_EDX, 0x6AF298, 0x263>, "CAutomobile::GetTowBarPos");
        hookASM(0x6AF35E, 6, cmpWordPtrRegModel<REG_EAX, 0x6AF364, 0x262>, "CAutomobile::GetTowBarPos");
        hookASM(0x6CF055, 6, cmpWordPtrRegModel<REG_EDI, 0x6CF05B, 0x262>, "CTrailer::ScanForTowLink");
        hookASM(0x6CFC41, 6, cmpWordPtrRegModel<REG_ESI, 0x6CFC47, 0x262>, "CTrailer::PreRender");
        hookASM(0x6D42FE, 6, patch6D42FE, "CVehicle::GetPlaneGunsPosition");
        hookASM(0x6AC730, 1, patch6AC730, "CAutomobile::PreRender");
        hookASM(0x6D474B, 6, patch6D474B, "CVehicle::GetPlaneOrdnancePosition");
        hookASM(0x6DD218, 5, patch6DD218, "CVehicle::DoBoatSplashes");
        hookASM(0x6E1786, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6E178E, 4, 0x01B73D66>, "CVehicle::DoTailLightEffect");
        hookASM(0x6A6602, 6, cmpWordPtrRegModel<REG_EDI, 0x6A6608, 0x1B0>, "CAutomobile::SetupSuspensionLines");
        hookASM(0x6A6995, 6, cmpWordPtrRegModel<REG_EDI, 0x6A699B, 0x1B0>, "CAutomobile::SetupSuspensionLines");
        hookASM(0x6A6903, 6, cmpWordPtrRegModel<REG_EDI, 0x6A6909, 0x23B>, "CAutomobile::SetupSuspensionLines");
        hookASM(0x6A4913, 6, cmpWordPtrRegModel<REG_ESI, 0x6A4919, 0x1B0>, "CAutomobile::DoBurstAndSoftGroundRatios");
        hookASM(0x6A2C29, 6, cmpWordPtrRegModel<REG_ESI, 0x6A2C2F, 0x1B0>, "CAutomobile::Render");
        hookASM(0x6A2E98, 7, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6A2E9F, 3, 0x9008C483>, "CAutomobile::Render");
        hookASM(0x6B1F77, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B1F7F, 4, 0x01B03D66>, "CAutomobile::ProcessControl");
        hookASM(0x6B1F4B, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6B1F53, 4, 0x01973D66>, "CAutomobile::ProcessControl");
        hookASM(0x6B1E26, 9, movReg16WordPtrReg<REG_CX, REG_ESI, 0x6B1E2F, 5, 0xBFF98166, 0x90909001>, "CAutomobile::ProcessControl");
        hookASM(0x6B2BD4, 6, cmpWordPtrRegModel<REG_ESI, 0x6B2BDA, 0x1A7>, "CAutomobile::ProcessControl");
        hookASM(0x6B36D4, 6, cmpWordPtrRegModel<REG_ESI, 0x6B36DA, 0x1B0>, "CAutomobile::ProcessControl");
        hookASM(0x6B217D, 6, cmpWordPtrRegModel<REG_ESI, 0x6B2183, 0x1CC>, "CAutomobile::ProcessControl");
        hookASM(0x6B36C5, 6, cmpWordPtrRegModel<REG_ESI, 0x6B36CB, 0x214>, "CAutomobile::ProcessControl");
        hookASM(0x6B1E59, 5, cmpReg16Model<REG_CX, 0x6B1E5E, 0x21B>, "CAutomobile::ProcessControl");
        hookASM(0x6B284B, 6, cmpWordPtrRegModel<REG_ESI, 0x6B2851, 0x21B>, "CAutomobile::ProcessControl");
        hookASM(0x6B356A, 6, cmpWordPtrRegModel<REG_ESI, 0x6B3570, 0x21B>, "CAutomobile::ProcessControl");
        hookASM(0x6B44AA, 8, movReg16WordPtrReg<REG_AX, REG_EBX, 0x6B44B2, 4, 0x020D3D66>, "CAutomobile::SetTowLink");
        hookASM(0x6CEED5, 6, cmpWordPtrRegModel<REG_EAX, 0x6CEEDB, 0x20D>, "CTrailer::GetTowHitchPos");
        hookASM(0x6DFDB2, 8, movReg16WordPtrReg<REG_AX, REG_EBX, 0x6DFDBA, 4, 0x020D3D66>, "CVehicle::UpdateTrailerLink");
        hookASM(0x6E00D0, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6E00D8, 4, 0x020D3D66>, "CVehicle::UpdateTractorLink");
        hookASM(0x6ACEE1, 6, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6ACEE7, 2, 0x9090D233>, "CAutomobile::ProcessEntityCollision");
        hookASM(0x6AD23E, 6, cmpWordPtrRegModel<REG_ESI, 0x6AD244, 0x1B0>, "CAutomobile::ProcessEntityCollision");
        hookASM(0x6AE859, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6AE861, 4, 0x02343D66>, "CAutomobile::TankControl");
        hookASM(0x6A4BAA, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6A4BB2, 4, 0x01B93D66>, "CAutomobile::DoSoftGroundResistance");
        hookASM(0x6A4DFE, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6A4E06, 4, 0x01B93D66>, "CAutomobile::DoSoftGroundResistance");
        hookASM(0x4C8A15, 6, cmpReg32Model<REG_ESI, 0x4C8A1B, 0x197>, "CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors");
        hookASM(0x4C8A25, 6, cmpReg32Model<REG_ESI, 0x4C8A2B, 0x1A9>, "CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors");
        hookASM(0x4C8AD9, 6, cmpReg32Model<REG_ESI, 0x4C8ADF, 0x1AF>, "CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors");
        hookASM(0x4C8AD1, 6, cmpReg32Model<REG_ESI, 0x4C8AD7, 0x1B5>, "CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors");
        hookASM(0x4C8A1D, 6, cmpReg32Model<REG_ESI, 0x4C8A23, 0x1FC>, "CVehicleModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors");
        hookASM(0x6E1766, 6, cmpWordPtrRegModel<REG_ECX, 0x6E176C, 0x214>, "CVehicle::DoHeadLightReflection");
        hookASM(0x6B078E, 10, movReg16WordPtrReg<REG_DI, REG_ESI, 0x6B0798, 6, 0x8CA405D9, 0x90900085>, "CAutomobile::DoHeliDustEffect");
        hookASM(0x6E39B8, 6, cmpWordPtrRegModel<REG_ESI, 0x6E39BE, 0x208>, "CVehicle::ProcessWeapons");
        hookASM(0x4250A6, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x4250AE, 4, 0x01A03D66>, "CCarCtrl::PossiblyRemoveVehicle");
        hookASM(0x6AADE6, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6AADEE, 4, 0xFFFE3D66>, "CAutomobile::PreRender");
        //hookASM(0x6AB350, 10, movsxReg32WordPtrReg<REG_EAX, REG_ESI, 0x6AB35A, 6, 0xFE69B88D, 0x9090FFFF>, "CAutomobile::PreRender")
        hookASM(0x6ABC71, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6ABC79, 4, 0x01B93D66>, "CAutomobile::PreRender");
        hookASM(0x6ABC9D, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6ABCA5, 4, 0x02143D66>, "CAutomobile::PreRender");
        hookASM(0x6ABD0F, 7, cmpReg16Model<REG_AX, 0x6ABD16, 0x1B0, 3, 0x90C8BF0F>, "CAutomobile::PreRender");
        hookASM(0x6ABFC8, 6, cmpWordPtrRegModel<REG_ESI, 0x6ABFCE, 0x1B0>, "CAutomobile::PreRender");
        hookASM(0x6AC025, 6, cmpWordPtrRegModel<REG_ESI, 0x6AC02B, 0x1B0>, "CAutomobile::PreRender");
        hookASM(0x6AC297, 8, movReg16WordPtrReg<REG_AX, REG_ESI, 0x6AC29F, 4, 0x34245CD9>, "CAutomobile::PreRender");
        hookASM(0x6BD40F, 6, cmpWordPtrRegModel<REG_ESI, 0x6BD415, 0x20B>, "CBike::PreRender");
        hookASM(0x6D7E11, 6, cmpWordPtrRegModel<REG_ESI, 0x6D7E17, 0x20B>, "CVehicle::InflictDamage");
        hookASM(0x6AC0E2, 5, patch6AC0E2, "CAutomobile::PreRender");
        hookASM(0x41F2A2, 5, patch41F2A2, "CCarAI::UpdateCarAI");
        hookASM(0x6D199F, 9, movsxReg32WordPtrReg<REG_EAX, REG_EDI, 0x6D19A8, 5, 0x0001C93D, 0x90909000>, "CVehicle::RemoveDriver");

        if (isGameHOODLUM())
        {
            hookASM(0x40649C, 6, cmpWordPtrRegModel<REG_ESI, 0x6ACBCD, 0x1EF>, "CAutomobile::PreRender");
            hookASM(0x156A4D7, 6, cmpWordPtrRegModel<REG_ESI, 0x156A4DD, 0x21B>, "CCarCtrl::JoinCarWithRoadSystem");
            hookASM(0x407A15, 5, cmpReg16Model<REG_DX, 0x6D4453, 0x1DC>, "CVehicle::GetPlaneGunsPosition");
            hookASM(0x729B76, 5, patch729B76, "CAutomobile::FireTruckControl");
        }
        else
        {
            hookASM(0x6ACBC7, 6, cmpWordPtrRegModel<REG_ESI, 0x6ACBCD, 0x1EF>, "CAutomobile::PreRender");
            hookASM(0x42F8A7, 6, cmpWordPtrRegModel<REG_ESI, 0x42F8AD, 0x21B>, "CCarCtrl::JoinCarWithRoadSystem");
            hookASM(0x6D444E, 5, cmpReg16Model<REG_DX, 0x6D4453, 0x1DC>, "CVehicle::GetPlaneGunsPosition");
            hookASM(0x729B76, 5, patch729B76, "CAutomobile::FireTruckControl");
        }

        hookSharedCall<0x8711D0, BurstTyreHooked>("CAutomobile::BurstTyre", true);

        hookSharedCall<0x6B39E6, RegisterCarBlownUpByPlayerHooked>("CDarkel::RegisterCarBlownUpByPlayer"); //CAutomobile::BlowUpCar
        hookSharedCall<0x6B3DEA, RegisterCarBlownUpByPlayerHooked>("CDarkel::RegisterCarBlownUpByPlayer"); //CAutomobile::BlowUpCarCutSceneNoExtras
        hookSharedCall<0x6E2D14, RegisterCarBlownUpByPlayerHooked>("CDarkel::RegisterCarBlownUpByPlayer"); //CVehicle::~CVehicle

        hookSharedCall<0x8719A8, ProcessControlInputsHooked>("CPlane::ProcessControlInputs", true);
    }
}
