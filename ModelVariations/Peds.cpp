#include "Peds.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"

#include <plugin.h>
#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CPed.h>
#include <CTheScripts.h>

#include <array>
#include <stack>

constexpr int MAX_PED_ID = 300;

static const char* dataFileName = "ModelVariations_Peds.ini";
static DataReader dataFile(dataFileName);
int16_t destroyedModelCounters[20000];

struct tPedVars {
    std::array<std::vector<unsigned short>, 16> variations[MAX_PED_ID];
    std::array<std::vector<unsigned short>, 6> wantedVariations[MAX_PED_ID];

    std::map<unsigned short, int> pedTimeSinceSpawn;
    std::unordered_map<unsigned short, std::vector<unsigned short>> originalModels;
    std::unordered_map<unsigned short, std::string> pedModels;
    std::unordered_map<unsigned short, std::vector<unsigned short>> voices;

    std::set<unsigned short> drugDealers;
    std::set<unsigned short> dontInheritBehaviourModels;
    std::set<unsigned short> mergeZones;
    std::set<unsigned short> pedHasVariations;
    std::set<unsigned short> useParentVoice;

    std::stack<CPed*> stack;

    std::vector<unsigned short> currentVariations[MAX_PED_ID];
};

std::unique_ptr<tPedVars> pedVars(new tPedVars);


struct tPedOptions {
    bool recursiveVariations = true;
    bool enableCloneRemover = false;
    bool cloneRemoverDisableOnMission = true;
    bool cloneRemoverVehicleOccupants = false;
    int cloneRemoverSpawnDelay = 3;
    std::vector<unsigned short> cloneRemoverIncludeVariations;
    std::vector<unsigned short> cloneRemoverExclusions;
};

std::unique_ptr<tPedOptions> pedOptions(new tPedOptions);

int dealersFrames = 0;
unsigned short modelIndex = 0;


bool isValidPedId(int id)
{
    if (id <= 0 || id >= MAX_PED_ID)
        return false;
    if (id >= 190 && id <= 195)
        return false;

    return true;
}

bool pedDelaySpawn(unsigned short model, bool includeParentModels)
{
    if (!includeParentModels)
    {
        if (pedVars->pedTimeSinceSpawn.contains(model))
            return true;
    }
    else
    {
        auto it = pedVars->originalModels.find(model);
        if (it != pedVars->originalModels.end())
            for (auto& i : it->second)
                if (pedVars->pedTimeSinceSpawn.contains(i))
                    return true;
    }
    return false;
}

bool compareOriginalModels(unsigned short model1, unsigned short model2, bool includeVariations = false)
{
    if (model1 == model2)
        return true;

    if (includeVariations)
    {
        auto it1 = pedVars->originalModels.find(model1);
        auto it2 = pedVars->originalModels.find(model2);
        if (it1 != pedVars->originalModels.end() && it2 != pedVars->originalModels.end())
            return std::find_first_of(it1->second.begin(), it1->second.end(), it2->second.begin(), it2->second.end()) != it1->second.end();
        else
        {
            unsigned short model = 0;
            std::vector<unsigned short>* vec = NULL;
            if (it1 != pedVars->originalModels.end())
            {
                model = model2;
                vec = &it1->second;
            }
            else if (it2 != pedVars->originalModels.end())
            {
                model = model1;
                vec = &it2->second;
            }
            else
                return false;

            if (std::find(vec->begin(), vec->end(), model) != vec->end())
                return true;
        }
    }

    return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PedVariations::AddToStack(CPed* ped)
{
    pedVars->stack.push(ped);
}

void PedVariations::ClearData()
{
    pedVars.reset(new tPedVars);
    pedOptions.reset(new tPedOptions);

    dataFile.data.clear();
}

void PedVariations::LoadData()
{
    dataFile.SetIniPath(dataFileName);

    Log::Write("\nReading ped data...\n");

    for (auto& iniData : dataFile.data)
    {
        Log::Write("%s\n", iniData.first.c_str());

        int i = 0;
        std::string section = iniData.first;

        if (section[0] >= '0' && section[0] <= '9')
            i = std::stoi(iniData.first);
        else
        {
            CModelInfo::GetModelInfo(section.data(), &i);
            pedVars->pedModels.insert({ (unsigned short)i, section });
        }

        if (isValidPedId(i))
        {
            pedVars->pedHasVariations.insert((unsigned short)i);

            for (unsigned j = 0; j < 16; j++)
                if (j < 6)
                    pedVars->variations[i][j] = dataFile.ReadLine(section, areas[j].first, READ_PEDS);
                else
                    pedVars->variations[i][j] = vectorUnion(dataFile.ReadLine(section, areas[j].first, READ_PEDS), pedVars->variations[i][areas[j].second]);

            pedVars->wantedVariations[i][0] = dataFile.ReadLine(section, "Wanted1", READ_PEDS);
            pedVars->wantedVariations[i][1] = dataFile.ReadLine(section, "Wanted2", READ_PEDS);
            pedVars->wantedVariations[i][2] = dataFile.ReadLine(section, "Wanted3", READ_PEDS);
            pedVars->wantedVariations[i][3] = dataFile.ReadLine(section, "Wanted4", READ_PEDS);
            pedVars->wantedVariations[i][4] = dataFile.ReadLine(section, "Wanted5", READ_PEDS);
            pedVars->wantedVariations[i][5] = dataFile.ReadLine(section, "Wanted6", READ_PEDS);


            for (const auto& j : pedVars->variations[i])
                for (const auto& k : j)
                    if (k > 0 && k != i)
                        vectorPushUnique(pedVars->originalModels[k], static_cast<unsigned short>(i));
            
            for (const auto& keyValue : iniData.second)
                if (zones.contains(keyValue.first))
                    for (auto variation : dataFile.ReadLine(section, keyValue.first, READ_PEDS))
                        if (variation > 0 && variation != i)
                            vectorPushUnique(pedVars->originalModels[variation], static_cast<unsigned short>(i));

            for (auto &it : pedVars->originalModels)
                std::sort(it.second.begin(), it.second.end());

            if (dataFile.ReadBoolean(section, "MergeZonesWithCities", false))
                pedVars->mergeZones.insert((unsigned short)i);

            if (dataFile.ReadBoolean(section, "DontInheritBehaviour", false))
                pedVars->dontInheritBehaviourModels.insert((unsigned short)i);
        }

        if (dataFile.ReadBoolean(section, "UseParentVoice", false))
            pedVars->useParentVoice.insert((unsigned short)i);

        auto vec = dataFile.ReadLine(section, "Voice", READ_PEDS);
        if (!vec.empty())
            pedVars->voices.insert({ (unsigned short)i, vec });
    }

    pedOptions->recursiveVariations = dataFile.ReadBoolean("Settings", "RecursiveVariations", true);
    pedOptions->enableCloneRemover = dataFile.ReadBoolean("Settings", "EnableCloneRemover", false);
    pedOptions->cloneRemoverDisableOnMission = dataFile.ReadBoolean("Settings", "CloneRemoverDisableOnMission", true);
    pedOptions->cloneRemoverVehicleOccupants = dataFile.ReadBoolean("Settings", "CloneRemoverIncludeVehicleOccupants", false);
    pedOptions->cloneRemoverSpawnDelay = dataFile.ReadInteger("Settings", "CloneRemoverSpawnDelay", 3);
    pedOptions->cloneRemoverIncludeVariations = dataFile.ReadLine("Settings", "CloneRemoverIncludeVariations", READ_PEDS);
    pedOptions->cloneRemoverExclusions = dataFile.ReadLine("Settings", "CloneRemoverExcludeModels", READ_PEDS);

    Log::Write("\n");
}

void PedVariations::Process()
{
    if (pedOptions->enableCloneRemover)
    {
        auto it = pedVars->pedTimeSinceSpawn.begin();
        while (it != pedVars->pedTimeSinceSpawn.end())
            if ((clock() - it->second) / CLOCKS_PER_SEC < pedOptions->cloneRemoverSpawnDelay)
                it++;
            else
                it = pedVars->pedTimeSinceSpawn.erase(it);
    }

    ProcessDrugDealers();

    while (!pedVars->stack.empty())
    {
        const auto deletePed = [](CPed* ped)
        {
            if (IsPedPointerValid(ped))
            {
                if (ped->m_pIntelligence)
                    ped->m_pIntelligence->FlushImmediately(false);
                CTheScripts::RemoveThisPed(ped);
            }
        };

        const auto pedDeleteVeh = [deletePed](CPed* ped)
        {
            CVehicle* veh = ped->m_pVehicle;
            if (ped->m_pVehicle->m_pDriver == ped)
            {
                deletePed(veh->m_pDriver);
                for (auto& i : veh->m_apPassengers)
                    deletePed(i);
                DestroyVehicleAndDriverAndPassengers(veh);
            }
            else
                for (auto& i : veh->m_apPassengers)
                    deletePed(i);
        };

        const auto isCarEmpty = [](CVehicle* veh)
        {
            if (IsPedPointerValid(veh->m_pDriver))
                return false;
            else
                for (auto &i : veh->m_apPassengers)
                    if (IsPedPointerValid(i))
                        return false;

            return true;
        };

        CPed* ped = pedVars->stack.top();
        pedVars->stack.pop();

        if (IsPedPointerValid(ped) && isValidPedId(ped->m_nModelIndex))
            if (!pedVars->currentVariations[ped->m_nModelIndex].empty() && pedVars->currentVariations[ped->m_nModelIndex][0] == 0 && ped->m_nCreatedBy != 2) //Delete models with a 0 id variation
            {
                if (IsVehiclePointerValid(ped->m_pVehicle))
                    pedDeleteVeh(ped);
                else
                    deletePed(ped);
            }

        if (IsPedPointerValid(ped) && pedOptions->enableCloneRemover && ped->m_nCreatedBy != 2 && CPools::ms_pPedPool && 
            !(pedOptions->cloneRemoverDisableOnMission && CTheScripts::IsPlayerOnAMission())) //Clone remover
        {
            bool includeVariations = std::find(pedOptions->cloneRemoverIncludeVariations.begin(), pedOptions->cloneRemoverIncludeVariations.end(), ped->m_nModelIndex) != pedOptions->cloneRemoverIncludeVariations.end();
            if (pedDelaySpawn(ped->m_nModelIndex, includeVariations)) //Delete peds spawned before SpawnTime
            {
                if (!IsVehiclePointerValid(ped->m_pVehicle))
                    deletePed(ped);
                else if (pedOptions->cloneRemoverVehicleOccupants && !isCarEmpty(ped->m_pVehicle) && ped->m_pVehicle->m_nVehicleSubClass != eVehicleType::VEHICLE_TRAIN)
                    pedDeleteVeh(ped);
            }

            if (IsPedPointerValid(ped) && !vectorHasId(pedOptions->cloneRemoverExclusions, ped->m_nModelIndex) && ped->m_nModelIndex > 0) //Delete peds already spawned
            {
                if (includeVariations)
                {
                    auto it = pedVars->originalModels.find(ped->m_nModelIndex);
                    if (it != pedVars->originalModels.end())
                        for (auto& i : it->second)
                            pedVars->pedTimeSinceSpawn.insert({ i, clock() });
                }
                else
                    pedVars->pedTimeSinceSpawn.insert({ ped->m_nModelIndex, clock() });

                for (CPed* ped2 : CPools::ms_pPedPool)
                    if (IsPedPointerValid(ped2) && ped2 != ped && compareOriginalModels(ped->m_nModelIndex, ped2->m_nModelIndex, includeVariations))
                    {
                        if (!IsVehiclePointerValid(ped->m_pVehicle))
                        {
                            deletePed(ped);
                            break;
                        }
                        else if (pedOptions->cloneRemoverVehicleOccupants && !isCarEmpty(ped->m_pVehicle) && ped->m_pVehicle->m_nVehicleSubClass != eVehicleType::VEHICLE_TRAIN)
                        {
                            pedDeleteVeh(ped);
                            break;
                        }
                    }
            }
        }
    }
}

void PedVariations::ProcessDrugDealers(bool reset)
{
    if (reset)
    {
        dealersFrames = 0;
        pedVars->drugDealers.clear();
    }
    else
    {
        if (dealersFrames < 10)
            dealersFrames++;

        if (dealersFrames == 10)
        {
            Log::Write("Applying drug dealer fix...\n");
         
            for (auto& i : { 28, 29, 30, 254 })
                for (auto &j : pedVars->variations[i])
                    for (auto &k : j)
                        if (k > MAX_PED_ID && !pedVars->drugDealers.contains(k))
                        {
                            Log::Write((std::find(addedIDs.begin(), addedIDs.end(), k) != addedIDs.end()) ? "%uSP\n" : "%u\n", k);
                            CTheScripts::ScriptsForBrains.AddNewScriptBrain(CTheScripts::StreamedScripts.GetProperIndexFromIndexUsedByScript(19), (short)k, 100, 0, -1, -1.0);
                            pedVars->drugDealers.insert(k);
                        }

            Log::Write("\n");
            dealersFrames = 11;
        }
    }
}

void PedVariations::UpdateVariations()
{
    const CWanted* wanted = FindPlayerWanted(-1);

    for (auto& modelid : pedVars->pedHasVariations)
    {
        pedVars->currentVariations[modelid] = vectorUnion(pedVars->variations[modelid][4], pedVars->variations[modelid][currentTown]);

        std::string section = pedVars->pedModels.contains(modelid) ? pedVars->pedModels[modelid] : std::to_string(modelid);

        std::vector<unsigned short> vec = dataFile.ReadLine(section, currentZone, READ_PEDS);
        if (!vec.empty())
        {
            if (pedVars->mergeZones.contains(modelid))
                pedVars->currentVariations[modelid] = vectorUnion(pedVars->currentVariations[modelid], vec);
            else
                pedVars->currentVariations[modelid] = vec;
        }

        vec = dataFile.ReadLine(section, currentInterior, READ_PEDS);
        if (!vec.empty())
            pedVars->currentVariations[modelid] = vectorUnion(pedVars->currentVariations[modelid], vec);

        if (wanted)
        {
            const unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            if (!pedVars->wantedVariations[modelid][wantedLevel].empty() && !pedVars->currentVariations[modelid].empty())
                vectorfilterVector(pedVars->currentVariations[modelid], pedVars->wantedVariations[modelid][wantedLevel]);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  LOGGING   ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

void PedVariations::LogCurrentVariations()
{
    Log::Write("pedCurrentVariations\n");
    for (int i = 0; i < MAX_PED_ID; i++)
        if (!pedVars->currentVariations[i].empty())
        {
            Log::Write("%d: ", i);
            for (auto j : pedVars->currentVariations[i])
            {
                const char* suffix = " ";
                if (std::find(addedIDs.begin(), addedIDs.end(), j) != addedIDs.end())
                    suffix = "SP ";
                Log::Write("%u%s", j, suffix);
            }
            Log::Write("\n");
        }
}

void PedVariations::LogDataFile()
{
    if (GetFileAttributes(dataFileName) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
        Log::Write("\n%s not found!\n\n", dataFileName);
    else
        Log::Write("##############################\n"
                   "## ModelVariations_Peds.ini ##\n"
                   "##############################\n%s\n", fileToString(dataFileName).c_str());
}

void PedVariations::LogVariations()
{
    Log::Write("\nPed Variations:\n");
    for (unsigned int i = 0; i < MAX_PED_ID; i++)
        for (unsigned int j = 0; j < 16; j++)
            if (!pedVars->variations[i][j].empty())
            {
                Log::Write("%u: ", i);
                for (unsigned int k = 0; k < 16; k++)
                    if (!pedVars->variations[i][k].empty())
                    {
                        Log::Write("(%u) ", k);
                        for (const auto& l : pedVars->variations[i][k])
                            Log::Write((std::find(addedIDs.begin(), addedIDs.end(), l) != addedIDs.end()) ? "%uSP " : "%u ", l);
                    }

                Log::Write("\n", i);
                break;
            }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

int __cdecl getKillsByPlayer(int)
{
    int sum = 0;

    for (auto i : destroyedModelCounters)
        sum += i;

    return sum;
}

template<std::uintptr_t address>
int __fastcall SetModelIndexHooked(CEntity* _this, void*, int index)
{
    int retVal = callMethodOriginalAndReturn<int, address>(_this, index);

    if (isValidPedId(_this->m_nModelIndex) && !pedVars->currentVariations[_this->m_nModelIndex].empty())
    {
        const unsigned short variationModel = vectorGetRandom(pedVars->currentVariations[_this->m_nModelIndex]);
        if (variationModel > 0 && variationModel != _this->m_nModelIndex)
        {
            loadModels({ variationModel }, PRIORITY_REQUEST, true);
            const unsigned short originalModel = _this->m_nModelIndex;
            _this->DeleteRwObject();

            if (pedOptions->recursiveVariations)
                _this->SetModelIndex(variationModel);
            else 
                callMethodOriginalAndReturn<int, address>(_this, variationModel);

            if (!pedVars->dontInheritBehaviourModels.contains(originalModel))
                _this->m_nModelIndex = originalModel;
            modelIndex = variationModel;
        }
    }

    return retVal;
}

template <std::uintptr_t address>
void __fastcall UpdateRpHAnimHooked(CEntity* entity)
{
    callMethodOriginal<address>(entity);

    if (modelIndex > 0)
        entity->m_nModelIndex = modelIndex;
    modelIndex = 0;
}

template <std::uintptr_t address>
char __fastcall CAEPedSpeechAudioEntity__InitialiseHooked(CAEPedSpeechAudioEntity* _this, void*, CPed* ped)
{
    if (ped != NULL)
    {
        const auto currentModel = ped->m_nModelIndex;
        unsigned short newModel = 0;

        auto it = pedVars->voices.find(ped->m_nModelIndex);
        if (it != pedVars->voices.end() && !it->second.empty())
            newModel = vectorGetRandom(it->second);

        if (pedVars->useParentVoice.contains(ped->m_nModelIndex))
        {
            it = pedVars->originalModels.find(ped->m_nModelIndex);
            if (it != pedVars->originalModels.end() && !it->second.empty())
                newModel = it->second[0];
        }

        if (newModel > 0)
        {
            ped->m_nModelIndex = it->second[0];
            char retVal = callMethodOriginalAndReturn<char, address>(_this, ped);
            ped->m_nModelIndex = currentModel;
            return retVal;
        }
    }

    return callMethodOriginalAndReturn<char, address>(_this, ped);
}

void PedVariations::InstallHooks(bool enableSpecialPeds)
{
    //Count of killable model IDs
    if (enableSpecialPeds)
    {
        bool gameHOODLUM = plugin::GetGameVersion() != GAME_10US_COMPACT;
        bool notModified = true;

        if (!memcmp(0x43DE6C, "66 FF 04 45 50 9A 96 00") ||
            !memcmp(0x43DF5B, "66 FF 04 45 50 9A 96 00") ||
            !memcmp((gameHOODLUM ? 0x1561634U : 0x43D6A4), "8D 04 45 50 9A 96 00") ||
            !memcmp((gameHOODLUM ? 0x1564C2BU : 0x43D6CB), "66 8B 04 55 50 9A 96 00"))
        {
            notModified = false;
        }

        if (gameHOODLUM)
        {
            if (!memcmp(0x43D6E0, "E9 1B C2 12 01"))
                notModified = false;
        }
        else if (!memcmp(0x43D6E0, "8B 4C 24 04 56"))
            notModified = false;

        if (notModified)
        {
            injector::MakeInline<0x43DE6C, 0x43DE6C + 8>([](injector::reg_pack& regs)
            {
                destroyedModelCounters[regs.eax * 2]++;
            });

            injector::MakeInline<0x43DF5B, 0x43DF5B + 8>([](injector::reg_pack& regs)
            {
                destroyedModelCounters[regs.eax * 2]++;
            });

            auto leaEAX = [](injector::reg_pack& regs)
            {
                regs.eax = reinterpret_cast<uint32_t>(&(destroyedModelCounters[regs.eax * 2]));
            };

            auto movAX = [](injector::reg_pack& regs)
            {
                regs.eax = (regs.eax & 0xFFFF0000) | static_cast<uint32_t>(destroyedModelCounters[regs.edx * 2]);
            };

            if (gameHOODLUM)
            {
                injector::MakeInline<0x1561634U, 0x1561634U + 7>(leaEAX);
                injector::MakeInline<0x1564C2BU, 0x1564C2BU + 8>(movAX);
            }
            else
            {
                injector::MakeInline<0x43D6A4, 0x43D6A4 + 7>(leaEAX);
                injector::MakeInline<0x43D6CB, 0x43D6CB + 8>(movAX);
            }

            injector::MakeJMP(0x43D6E0, getKillsByPlayer);

            maxPedID = 20000;
        }
        else
            Log::Write("Count of killable model IDs not increased. %s\n", (LoadedModules::IsModLoaded(MOD_FLA) ? "FLA is loaded." : "FLA is NOT loaded."));
    }


    hookCall(0x5E4890, SetModelIndexHooked<0x5E4890>, "CEntity::SetModelIndex");
    hookCall(0x5E49EF, UpdateRpHAnimHooked<0x5E49EF>, "CEntity::UpdateRpHAnim");

    hookCall(0x5DDBB8, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DDBB8>, "CAEPedSpeechAudioEntity::Initialise"); //CCivilianPed
    hookCall(0x5DDD24, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DDD24>, "CAEPedSpeechAudioEntity::Initialise"); //CCopPed
    hookCall(0x5DE388, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DE388>, "CAEPedSpeechAudioEntity::Initialise"); //CEmergencyPed
}
