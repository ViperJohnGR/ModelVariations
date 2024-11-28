#include "Peds.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "SA.hpp"

#include <plugin.h>
#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CPed.h>
#include <CTheScripts.h>

#include <array>
#include <stack>

constexpr int MAX_ORIGINAL_PED_ID = 300;

static const char* dataFileName = "ModelVariations_Peds.ini";
static DataReader dataFile(dataFileName);
int16_t destroyedModelCounters[20000];

struct tPedVars {
    std::array<std::vector<unsigned short>, 16> variations[MAX_ORIGINAL_PED_ID];
    std::array<std::vector<unsigned short>, 6> wantedVariations[MAX_ORIGINAL_PED_ID];
    std::array<std::unordered_map<uint64_t, std::vector<unsigned short>>, MAX_ORIGINAL_PED_ID> zoneVariations;

    std::map<unsigned short, int> pedTimeSinceSpawn;
    std::unordered_map<unsigned short, std::vector<unsigned short>> originalModels;
    std::unordered_map<unsigned short, std::string> pedModels;
    std::unordered_map<unsigned short, bool> useParentVoice;
    std::unordered_map<unsigned short, std::vector<unsigned short>> voices;
    std::unordered_map<unsigned short, unsigned int> animGroups;

    std::stack<CPed*> stack;

    std::vector<unsigned short> currentVariations[MAX_ORIGINAL_PED_ID];
    std::vector<unsigned short> dontInheritBehaviourModels;
    std::vector<unsigned short> mergeZones;
    std::vector<unsigned short> mergeInteriors;
    std::vector<unsigned short> pedHasVariations;
};

std::unique_ptr<tPedVars> pedVars(new tPedVars);


struct tPedOptions {
    bool recursiveVariations = true;
    bool useParentVoices = false;
    bool enableCloneRemover = false;
    bool cloneRemoverDisableOnMission = true;
    bool cloneRemoverVehicleOccupants = false;
    int cloneRemoverSpawnDelay = 3;
    std::vector<unsigned short> cloneRemoverIncludeVariations;
    std::vector<unsigned short> cloneRemoverExclusions;
};

std::unique_ptr<tPedOptions> pedOptions(new tPedOptions);

int dealersFrames = 0;
unsigned short variationModel = 0;

bool isValidPedId(int id)
{
    if (id <= 0 || id >= MAX_ORIGINAL_PED_ID)
        return false;
    if (id >= 190 && id <= 195)
        return false;

    return true;
}

std::vector<unsigned short> PedVariations::GetVariationOriginalModels(const int modelIndex)
{
    if (modelIndex < 400)
        return { (unsigned short)modelIndex };

    auto it = pedVars->originalModels.find((unsigned short)modelIndex);
    if (it != pedVars->originalModels.end())
        return it->second;

    return { (unsigned short)modelIndex };
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

void PedVariations::ClearData()
{
    pedVars.reset(new tPedVars);
    pedOptions.reset(new tPedOptions);

    dataFile.data.clear();
}

void PedVariations::LoadData()
{
    dataFile.Load(dataFileName);

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

        if (i <= 0)
            continue;

        unsigned short modelIndex = static_cast<unsigned short>(i);

        if (isValidPedId(modelIndex))
        {
            pedVars->pedHasVariations.push_back(modelIndex);

            for (unsigned j = 0; j < 16; j++)
                if (j < 6)
                    pedVars->variations[modelIndex][j] = dataFile.ReadLine(section, areas[j].first, READ_PEDS);
                else
                    pedVars->variations[modelIndex][j] = vectorUnion(dataFile.ReadLine(section, areas[j].first, READ_PEDS), pedVars->variations[modelIndex][areas[j].second]);

            pedVars->wantedVariations[modelIndex][0] = dataFile.ReadLine(section, "Wanted1", READ_PEDS);
            pedVars->wantedVariations[modelIndex][1] = dataFile.ReadLine(section, "Wanted2", READ_PEDS);
            pedVars->wantedVariations[modelIndex][2] = dataFile.ReadLine(section, "Wanted3", READ_PEDS);
            pedVars->wantedVariations[modelIndex][3] = dataFile.ReadLine(section, "Wanted4", READ_PEDS);
            pedVars->wantedVariations[modelIndex][4] = dataFile.ReadLine(section, "Wanted5", READ_PEDS);
            pedVars->wantedVariations[modelIndex][5] = dataFile.ReadLine(section, "Wanted6", READ_PEDS);


            for (const auto& j : pedVars->variations[modelIndex])
                for (const auto& k : j)
                    if (k > 0 && k != i)
                        vectorPushUnique(pedVars->originalModels[k], modelIndex);
            
            for (const auto& keyValue : iniData.second)
                if (std::binary_search(zoneNames.begin(), zoneNames.end(), keyValue.first))
                {
                    auto vec = dataFile.ReadLine(section, keyValue.first, READ_PEDS);
                    if (!vec.empty())
                    {
                        char tmp[8] = {};
                        strncpy(tmp, keyValue.first.c_str(), 7);
                        pedVars->zoneVariations[modelIndex][*reinterpret_cast<uint64_t*>(tmp)] = vec;
                    }

                    for (auto variation : vec)
                        if (variation > 0 && variation != modelIndex)
                            vectorPushUnique(pedVars->originalModels[variation], modelIndex);
                }

            for (auto &it : pedVars->originalModels)
                std::sort(it.second.begin(), it.second.end());

            if (dataFile.ReadBoolean(section, "MergeZonesWithCities", false))
                pedVars->mergeZones.push_back(modelIndex);

            if (dataFile.ReadBoolean(section, "DontInheritBehaviour", false))
                pedVars->dontInheritBehaviourModels.push_back(modelIndex);

            if (dataFile.ReadBoolean(section, "MergeInteriorsWithCitiesAndZones", false))
                pedVars->mergeInteriors.push_back(modelIndex);
        }

        int parentVoice = dataFile.ReadInteger(section, "UseParentVoice", -1);
        if (parentVoice > -1)
            pedVars->useParentVoice[modelIndex] = static_cast<bool>(parentVoice);

        std::string animGroupString = dataFile.ReadString(section, "AnimGroup", "");
        if (!animGroupString.empty() && CAnimManager__ms_numAnimAssocDefinitions > 0)
            for (int j = CAnimManager__ms_numAnimAssocDefinitions - 1; j >= 0; j--)
            {
                char* animString = CAnimManager__GetAnimGroupName(j);
                if (animString != NULL && strcmp(animGroupString.c_str(), animString) == 0)
                {
                    pedVars->animGroups[modelIndex] = (unsigned)j;
                    break;
                }
            }

        auto vec = dataFile.ReadLine(section, "Voice", READ_PEDS);
        if (!vec.empty())
            pedVars->voices.insert({ modelIndex, vec });
    }

    std::sort(pedVars->dontInheritBehaviourModels.begin(), pedVars->dontInheritBehaviourModels.end());
    std::sort(pedVars->mergeZones.begin(), pedVars->mergeZones.end());
    std::sort(pedVars->pedHasVariations.begin(), pedVars->pedHasVariations.end());

    pedOptions->recursiveVariations = dataFile.ReadBoolean("Settings", "RecursiveVariations", true);
    pedOptions->useParentVoices = dataFile.ReadBoolean("Settings", "UseParentVoices", false);
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
    }
    else
    {
        if (dealersFrames < 10)
            dealersFrames++;

        if (dealersFrames == 10)
        {
            Log::Write("Applying drug dealer fix...\n");
         
            for (auto& it : pedVars->originalModels)
                if (it.first > MAX_ORIGINAL_PED_ID)
                    for (auto& originalModel : it.second)
                        if (originalModel == 28 || originalModel == 29 || originalModel == 30 || originalModel == 254)
                        {
                            Log::Write((std::find(addedIDs.begin(), addedIDs.end(), it.first) != addedIDs.end()) ? "%uSP\n" : "%u\n", it.first);
                            auto findByScmIndex = CExternalScripts__findByScmIndex(CTheScripts__StreamedScripts, 19);

                            CScriptsForBrains__AddNewScriptBrain(CTheScripts__ScriptsForBrains, findByScmIndex, (short)it.first, 100, 0, -1, -1.0);
                            break;
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

        std::string section;
        
        if (auto it = pedVars->pedModels.find(modelid); it != pedVars->pedModels.end())
            section = it->second;
        else
            section = std::to_string(modelid);

        if (auto it = pedVars->zoneVariations[modelid].find(*reinterpret_cast<uint64_t*>(currentZone)); it != pedVars->zoneVariations[modelid].end())
        {
            if (!it->second.empty())
            {
                if (vectorHasId(pedVars->mergeZones, modelid))
                    pedVars->currentVariations[modelid] = vectorUnion(pedVars->currentVariations[modelid], it->second);
                else
                    pedVars->currentVariations[modelid] = it->second;
            }
        }

        if (currentInterior[0] != 0)
        {
            if (vectorHasId(pedVars->mergeInteriors, modelid))
                pedVars->currentVariations[modelid] = vectorUnion(pedVars->currentVariations[modelid], dataFile.ReadLine(section, currentInterior, READ_PEDS));
            else if (auto vec = dataFile.ReadLine(section, currentInterior, READ_PEDS); !vec.empty())
                pedVars->currentVariations[modelid] = vec;
        }

        if (wanted)
        {
            const unsigned int wantedLevel = wanted->m_nWantedLevel - (wanted->m_nWantedLevel ? 1 : 0);
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
    for (int i = 0; i < MAX_ORIGINAL_PED_ID; i++)
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
    for (unsigned int i = 0; i < MAX_ORIGINAL_PED_ID; i++)
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
        const unsigned short newModel = vectorGetRandom(pedVars->currentVariations[_this->m_nModelIndex]);
        if (newModel > 0 && newModel != _this->m_nModelIndex)
        {
            loadModels({ newModel }, PRIORITY_REQUEST, true);
            const unsigned short originalModel = _this->m_nModelIndex;
            _this->DeleteRwObject();

            if (pedOptions->recursiveVariations)
                _this->SetModelIndex(newModel);
            else 
                callMethodOriginalAndReturn<int, address>(_this, newModel);

            if (!vectorHasId(pedVars->dontInheritBehaviourModels, originalModel))
                _this->m_nModelIndex = originalModel;
            variationModel = newModel;
        }
    }

    return retVal;
}

template <std::uintptr_t address>
void __fastcall UpdateRpHAnimHooked(CPed* entity)
{
    callMethodOriginal<address>(entity);

    if (auto it = pedVars->animGroups.find(variationModel > 0 ? variationModel : entity->m_nModelIndex); it != pedVars->animGroups.end())
        entity->m_nAnimGroup = it->second;

    if (variationModel > 0)
        entity->m_nModelIndex = variationModel;
    variationModel = 0;
}

template <std::uintptr_t address>
char __fastcall CAEPedSpeechAudioEntity__InitialiseHooked(CAEPedSpeechAudioEntity* _this, void*, CPed* ped)
{
    if (ped != NULL)
    {
        const auto currentModel = ped->m_nModelIndex;
        unsigned short newModel = 0;

        bool useParentVoice = false;

        if (auto it = pedVars->useParentVoice.find(ped->m_nModelIndex); it != pedVars->useParentVoice.end())
            useParentVoice = it->second;
        else
            useParentVoice = pedOptions->useParentVoices;

        if (useParentVoice)
        {
            auto it = pedVars->originalModels.find(ped->m_nModelIndex);
            if (it != pedVars->originalModels.end() && !it->second.empty())
                newModel = it->second[0];
        }

        auto it = pedVars->voices.find(ped->m_nModelIndex);
        if (it != pedVars->voices.end() && !it->second.empty())
            newModel = vectorGetRandom(it->second);

        if (newModel > 0)
        {
            ped->m_nModelIndex = newModel;
            char retVal = callMethodOriginalAndReturn<char, address>(_this, ped);
            ped->m_nModelIndex = currentModel;
            return retVal;
        }
    }

    return callMethodOriginalAndReturn<char, address>(_this, ped);
}

template <std::uintptr_t address>
CPhysical* __fastcall CPhysicalHooked(CPed* _this)
{
    CPhysical* retVal = callMethodOriginalAndReturn<CPhysical*, address>(_this);
    pedVars->stack.push(_this);
    return retVal;
}

void PedVariations::InstallHooks(bool enableSpecialPeds)
{
    if (enableSpecialPeds)
    {
        //Extra objects directory
        if (*reinterpret_cast<uint32_t*>(0x5B8DE0) == 550)
            WriteMemory<uint32_t>(0x5B8DE0, 4000);
        else
            Log::Write("Extra objects directory limit was not increased.\n");

        bool gameHOODLUM = plugin::GetGameVersion() != GAME_10US_COMPACT;
        bool notModified = true;

        //Count of killable model IDs
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
            Log::Write("Count of killable model IDs was not increased. %s\n", (LoadedModules::IsModLoaded(MOD_FLA) ? "FLA is loaded." : "FLA is NOT loaded."));
    }


    hookCall(0x5E4890, SetModelIndexHooked<0x5E4890>, "CEntity::SetModelIndex");
    hookCall(0x5E49EF, UpdateRpHAnimHooked<0x5E49EF>, "CEntity::UpdateRpHAnim");

    hookCall(0x5DDBB8, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DDBB8>, "CAEPedSpeechAudioEntity::Initialise"); //CCivilianPed
    hookCall(0x5DDD24, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DDD24>, "CAEPedSpeechAudioEntity::Initialise"); //CCopPed
    hookCall(0x5DE388, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DE388>, "CAEPedSpeechAudioEntity::Initialise"); //CEmergencyPed

    hookCall(0x5E8052, CPhysicalHooked<0x5E8052>, "CPhysical::CPhysical"); //CPed::CPed
}
