#include "Peds.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "Log.hpp"

#include <plugin.h>
#include <CModelInfo.h>
#include <CPed.h>
#include <CTheScripts.h>

#include <array>
#include <stack>

#include <shlwapi.h>

constexpr int MAX_PED_ID = 300;

static const char* dataFileName = "ModelVariations_Peds.ini";
static DataReader dataFile(dataFileName);
int16_t destroyedModelCounters[20000];

std::array<std::vector<unsigned short>, 16> pedVariations[MAX_PED_ID];
std::array<std::vector<unsigned short>, 6> pedWantedVariations[MAX_PED_ID];

std::map<unsigned short, int> pedTimeSinceLastSpawned;
std::map<unsigned short, std::vector<unsigned short>> pedOriginalModels;
std::map<unsigned short, std::string> pedModels;

std::set<unsigned short> dontInheritBehaviourModels;
std::set<unsigned short> pedMergeZones;
std::set<unsigned short> pedHasVariations;

std::stack<CPed*> pedStack;

std::vector<unsigned short> pedCurrentVariations[MAX_PED_ID];

//INI Options
bool useParentVoices = false;
bool enableCloneRemover = false;
bool cloneRemoverVehicleOccupants = false;
int cloneRemoverSpawnDelay = 3;
std::vector<unsigned short> cloneRemoverIncludeVariations;
std::vector<unsigned short> cloneRemoverExclusions;

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
        if (pedTimeSinceLastSpawned.find(model) != pedTimeSinceLastSpawned.end())
            return true;
    }
    else
    {
        auto it = pedOriginalModels.find(model);
        if (it != pedOriginalModels.end())
            for (auto& i : it->second)
                if (pedTimeSinceLastSpawned.find(i) != pedTimeSinceLastSpawned.end())
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
        auto it1 = pedOriginalModels.find(model1);
        auto it2 = pedOriginalModels.find(model2);
        if (it1 != pedOriginalModels.end() && it2 != pedOriginalModels.end())
            return std::find_first_of(it1->second.begin(), it1->second.end(), it2->second.begin(), it2->second.end()) != it1->second.end();
        else
        {
            unsigned short model = 0;
            std::vector<unsigned short>* vec = NULL;
            if (it1 != pedOriginalModels.end())
            {
                model = model2;
                vec = &it1->second;
            }
            else if (it2 != pedOriginalModels.end())
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
    pedStack.push(ped);
}

void PedVariations::ClearData()
{
    for (int i = 0; i < MAX_PED_ID; i++)
        for (unsigned short j = 0; j < 16; j++)
        {
            pedVariations[i][j].clear();
            if (j < 6)
                pedWantedVariations[i][j].clear();
        }

    //maps
    pedTimeSinceLastSpawned.clear();
    pedOriginalModels.clear();
    pedModels.clear();

    //sets
    dontInheritBehaviourModels.clear();
    pedMergeZones.clear();
    pedHasVariations.clear();
    cloneRemoverIncludeVariations.clear();

    //stacks
    while (!pedStack.empty()) pedStack.pop();

    //vectors
    cloneRemoverExclusions.clear();
    for (int i = 0; i < MAX_PED_ID; i++)
        pedCurrentVariations[i].clear();

    dataFile.data.clear();

    enableCloneRemover = 0;
}

void PedVariations::LoadData()
{
    dataFile.SetIniPath(dataFile.GetIniPath());

    Log::Write("\nPed sections detected:\n");

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
            pedModels.insert({ (unsigned short)i, section });
        }

        if (isValidPedId(i))
        {
            pedHasVariations.insert((unsigned short)i);

            pedVariations[i][0] = dataFile.ReadLine(section, "Countryside", READ_PEDS);
            pedVariations[i][1] = dataFile.ReadLine(section, "LosSantos", READ_PEDS);
            pedVariations[i][2] = dataFile.ReadLine(section, "SanFierro", READ_PEDS);
            pedVariations[i][3] = dataFile.ReadLine(section, "LasVenturas", READ_PEDS);
            pedVariations[i][4] = dataFile.ReadLine(section, "Global", READ_PEDS);
            pedVariations[i][5] = dataFile.ReadLine(section, "Desert", READ_PEDS);

            pedVariations[i][6] = vectorUnion(dataFile.ReadLine(section, "TierraRobada", READ_PEDS), pedVariations[i][5]);
            pedVariations[i][7] = vectorUnion(dataFile.ReadLine(section, "BoneCounty", READ_PEDS), pedVariations[i][5]);
            pedVariations[i][8] = vectorUnion(dataFile.ReadLine(section, "RedCounty", READ_PEDS), pedVariations[i][0]);
            pedVariations[i][9] = vectorUnion(dataFile.ReadLine(section, "Blueberry", READ_PEDS), pedVariations[i][8]);
            pedVariations[i][10] = vectorUnion(dataFile.ReadLine(section, "Montgomery", READ_PEDS), pedVariations[i][8]);
            pedVariations[i][11] = vectorUnion(dataFile.ReadLine(section, "Dillimore", READ_PEDS), pedVariations[i][8]);
            pedVariations[i][12] = vectorUnion(dataFile.ReadLine(section, "PalominoCreek", READ_PEDS), pedVariations[i][8]);
            pedVariations[i][13] = vectorUnion(dataFile.ReadLine(section, "FlintCounty", READ_PEDS), pedVariations[i][0]);
            pedVariations[i][14] = vectorUnion(dataFile.ReadLine(section, "Whetstone", READ_PEDS), pedVariations[i][0]);
            pedVariations[i][15] = vectorUnion(dataFile.ReadLine(section, "AngelPine", READ_PEDS), pedVariations[i][14]);


            pedWantedVariations[i][0] = dataFile.ReadLine(section, "Wanted1", READ_PEDS);
            pedWantedVariations[i][1] = dataFile.ReadLine(section, "Wanted2", READ_PEDS);
            pedWantedVariations[i][2] = dataFile.ReadLine(section, "Wanted3", READ_PEDS);
            pedWantedVariations[i][3] = dataFile.ReadLine(section, "Wanted4", READ_PEDS);
            pedWantedVariations[i][4] = dataFile.ReadLine(section, "Wanted5", READ_PEDS);
            pedWantedVariations[i][5] = dataFile.ReadLine(section, "Wanted6", READ_PEDS);


            for (unsigned int j = 0; j < 16; j++)
                for (unsigned int k = 0; k < pedVariations[i][j].size(); k++)
                    if (pedVariations[i][j][k] > 0 && pedVariations[i][j][k] != i)
                    {
                        if (pedOriginalModels.find(pedVariations[i][j][k]) != pedOriginalModels.end())
                            pedOriginalModels[pedVariations[i][j][k]].push_back((unsigned short)i);
                        else
                            pedOriginalModels.insert({ pedVariations[i][j][k], {(unsigned short)i} });
                    }

            for (auto it : pedOriginalModels)
                std::sort(it.second.begin(), it.second.end());

            if (dataFile.ReadBoolean(section, "MergeZonesWithCities", false))
                pedMergeZones.insert((unsigned short)i);

            if (dataFile.ReadBoolean(section, "DontInheritBehaviour", false))
                dontInheritBehaviourModels.insert((unsigned short)i);
        }
    }

    useParentVoices = dataFile.ReadBoolean("Settings", "UseParentVoices", false);
    enableCloneRemover = dataFile.ReadBoolean("Settings", "EnableCloneRemover", false);
    cloneRemoverVehicleOccupants = dataFile.ReadBoolean("Settings", "CloneRemoverIncludeVehicleOccupants", false);
    cloneRemoverSpawnDelay = dataFile.ReadInteger("Settings", "CloneRemoverSpawnDelay", 3);
    cloneRemoverIncludeVariations = dataFile.ReadLine("Settings", "CloneRemoverIncludeVariations", READ_PEDS);
    cloneRemoverExclusions = dataFile.ReadLine("Settings", "CloneRemoverExcludeModels", READ_PEDS);

    Log::Write("\n");
}

void PedVariations::Process()
{
    if (enableCloneRemover)
    {
        auto it = pedTimeSinceLastSpawned.begin();
        while (it != pedTimeSinceLastSpawned.end())
            if ((clock() - it->second) / CLOCKS_PER_SEC < cloneRemoverSpawnDelay)
                it++;
            else
                it = pedTimeSinceLastSpawned.erase(it);
    }

    ProcessDrugDealers();

    while (!pedStack.empty())
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
                for (int i = 0; i < 8; i++)
                    if (IsPedPointerValid(veh->m_apPassengers[i]))
                        return false;

            return true;
        };

        CPed* ped = pedStack.top();
        pedStack.pop();

        if (IsPedPointerValid(ped) && isValidPedId(ped->m_nModelIndex))
            if (!pedCurrentVariations[ped->m_nModelIndex].empty() && pedCurrentVariations[ped->m_nModelIndex][0] == 0 && ped->m_nCreatedBy != 2) //Delete models with a 0 id variation
            {
                if (IsVehiclePointerValid(ped->m_pVehicle))
                    pedDeleteVeh(ped);
                else
                    deletePed(ped);
            }

        if (IsPedPointerValid(ped) && enableCloneRemover && ped->m_nCreatedBy != 2 && CPools::ms_pPedPool) //Clone remover
        {
            bool includeVariations = std::find(cloneRemoverIncludeVariations.begin(), cloneRemoverIncludeVariations.end(), ped->m_nModelIndex) != cloneRemoverIncludeVariations.end();
            if (pedDelaySpawn(ped->m_nModelIndex, includeVariations)) //Delete peds spawned before SpawnTime
            {
                if (!IsVehiclePointerValid(ped->m_pVehicle))
                    deletePed(ped);
                else if (cloneRemoverVehicleOccupants && !isCarEmpty(ped->m_pVehicle))
                    pedDeleteVeh(ped);
            }

            if (IsPedPointerValid(ped) && !vectorHasId(cloneRemoverExclusions, ped->m_nModelIndex) && ped->m_nModelIndex > 0) //Delete peds already spawned
            {
                if (includeVariations)
                {
                    auto it = pedOriginalModels.find(ped->m_nModelIndex);
                    if (it != pedOriginalModels.end())
                        for (auto& i : it->second)
                            pedTimeSinceLastSpawned.insert({ i, clock() });
                }
                else
                    pedTimeSinceLastSpawned.insert({ ped->m_nModelIndex, clock() });

                for (CPed* ped2 : CPools::ms_pPedPool)
                    if (IsPedPointerValid(ped2) && ped2 != ped && compareOriginalModels(ped->m_nModelIndex, ped2->m_nModelIndex, includeVariations))
                    {
                        if (!IsVehiclePointerValid(ped->m_pVehicle))
                        {
                            deletePed(ped);
                            break;
                        }
                        else if (cloneRemoverVehicleOccupants && !isCarEmpty(ped->m_pVehicle))
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
        dealersFrames = 0;
    else
    {
        if (dealersFrames < 10)
            dealersFrames++;

        if (dealersFrames == 10)
        {
            Log::Write("Applying drug dealer fix...\n");
         
            int id = 28;

            while (id < 255)
            {
                for (unsigned int i = 0; i < 16; i++)
                    if (!pedVariations[id][i].empty())
                        for (auto& j : pedVariations[id][i])
                            if (j > MAX_PED_ID)
                            {
                                Log::Write("%u\n", j);
                                CTheScripts::ScriptsForBrains.AddNewScriptBrain(CTheScripts::StreamedScripts.GetProperIndexFromIndexUsedByScript(19), (short)j, 100, 0, -1, -1.0);
                            }

                if (id < 30) id++;
                else if (id == 30) id = 254;
                else id = 255;
            }

            Log::Write("\n");
            dealersFrames = 11;
        }
    }
}

void PedVariations::UpdateVariations()
{
    const CWanted* wanted = FindPlayerWanted(-1);

    for (auto& modelid : pedHasVariations)
    {
        pedCurrentVariations[modelid] = vectorUnion(pedVariations[modelid][4], pedVariations[modelid][currentTown]);

        std::string section;
        auto it = pedModels.find(modelid);
        if (it != pedModels.end())
            section = it->second;
        else
            section = std::to_string(modelid);

        std::vector<unsigned short> vec = dataFile.ReadLine(section, currentZone, READ_PEDS);
        if (!vec.empty())
        {
            if (pedMergeZones.find(modelid) != pedMergeZones.end())
                pedCurrentVariations[modelid] = vectorUnion(pedCurrentVariations[modelid], vec);
            else
                pedCurrentVariations[modelid] = vec;
        }

        vec = dataFile.ReadLine(section, currentInterior, READ_PEDS);
        if (!vec.empty())
            pedCurrentVariations[modelid] = vectorUnion(pedCurrentVariations[modelid], vec);

        if (wanted)
        {
            const unsigned int wantedLevel = (wanted->m_nWantedLevel > 0) ? (wanted->m_nWantedLevel - 1) : (wanted->m_nWantedLevel);
            if (!pedWantedVariations[modelid][wantedLevel].empty() && !pedCurrentVariations[modelid].empty())
                vectorfilterVector(pedCurrentVariations[modelid], pedWantedVariations[modelid][wantedLevel]);
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
        if (!pedCurrentVariations[i].empty())
        {
            Log::Write("%d: ", i);
            for (auto j : pedCurrentVariations[i])
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
        Log::Write("\n%s not found!\n\n", PathFindFileName(dataFileName));
    else
        Log::Write("##############################\n"
                   "## ModelVariations_Peds.ini ##\n"
                   "##############################\n%s\n", Log::FileToString(dataFileName).c_str());
}

void PedVariations::LogVariations()
{
    Log::Write("\nPed Variations:\n");
    for (unsigned int i = 0; i < MAX_PED_ID; i++)
        for (unsigned int j = 0; j < 16; j++)
            if (!pedVariations[i][j].empty())
            {
                Log::Write("%u: ", i);
                for (unsigned int k = 0; k < 16; k++)
                    if (!pedVariations[i][k].empty())
                    {
                        Log::Write("(%u) ", k);
                        for (const auto& l : pedVariations[i][k])
                            Log::Write("%u ", l);
                    }

                Log::Write("\n", i);
                break;
            }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template<std::uintptr_t address>
int __fastcall SetModelIndexHooked(CEntity* _this, void*, int index)
{
    int retVal = callMethodOriginalAndReturn<int, address>(_this, index);

    if (isValidPedId(_this->m_nModelIndex) && !pedCurrentVariations[_this->m_nModelIndex].empty())
    {
        const unsigned short variationModel = vectorGetRandom(pedCurrentVariations[_this->m_nModelIndex]);
        if (variationModel > 0 && variationModel != _this->m_nModelIndex)
        {
            loadModels({ variationModel }, GAME_REQUIRED, true);
            const unsigned short originalModel = _this->m_nModelIndex;
            _this->DeleteRwObject();
            _this->SetModelIndex(variationModel);
            if (dontInheritBehaviourModels.find(originalModel) == dontInheritBehaviourModels.end())
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
    //entity->UpdateRpHAnim();
    if (modelIndex > 0)
        entity->m_nModelIndex = modelIndex;
    modelIndex = 0;
}

template <std::uintptr_t address>
char __fastcall CAEPedSpeechAudioEntity__InitialiseHooked(CAEPedSpeechAudioEntity* _this, void*, CPed* ped)
{
    if (ped != NULL)
    {
        auto it = pedOriginalModels.find(ped->m_nModelIndex);
        if (it != pedOriginalModels.end() && !it->second.empty())
        {
            auto parentModel = it->second[0];
            auto currentModel = ped->m_nModelIndex;
            ped->m_nModelIndex = parentModel;
            char retVal = callMethodOriginalAndReturn<char, address>(_this, ped);
            ped->m_nModelIndex = currentModel;
            return retVal;
        }
    }

    return callMethodOriginalAndReturn<char, address>(_this, ped);
}

void PedVariations::InstallHooks(bool enableSpecialPeds, bool isFLA)
{
    //Count of killable model IDs
    if (enableSpecialPeds)
    {
        bool gameHOODLUM = plugin::GetGameVersion() != GAME_10US_COMPACT;
        bool notModified = true;

        if (*(uint32_t*)0x43DE6C != 0x4504FF66 || *(uint32_t*)0x43DE70 != 0x00969A50 ||
            *(uint32_t*)0x43DF5B != 0x4504FF66 || *(uint32_t*)0x43DF5F != 0x00969A50 ||
            *(uint32_t*)(gameHOODLUM ? 0x1561634U : 0x43D6A4) != 0x5045048D || *(uint32_t*)(gameHOODLUM ? 0x1561638U : 0x43D6A8) != 0xB900969A ||
            *(uint32_t*)(gameHOODLUM ? 0x1564C2BU : 0x43D6CB) != 0x55048B66 || *(uint32_t*)(gameHOODLUM ? 0x1564C2FU : 0x43D6CF) != 0x00969A50)
        {
            notModified = false;
        }

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
        }
        else
            Log::Write("Count of killable model IDs not increased. %s\n", (isFLA ? "FLA is loaded." : "FLA is NOT loaded."));
    }


    hookCall(0x5E4890, SetModelIndexHooked<0x5E4890>, "CEntity::SetModelIndex");
    hookCall(0x5E49EF, UpdateRpHAnimHooked<0x5E49EF>, "UpdateRpHAnim");
    if (useParentVoices)
    {
        hookCall(0x5DDBB8, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DDBB8>, "CAEPedSpeechAudioEntity__Initialise"); //CCivilianPed
        hookCall(0x5DDD24, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DDD24>, "CAEPedSpeechAudioEntity__Initialise"); //CCopPed
        hookCall(0x5DE388, CAEPedSpeechAudioEntity__InitialiseHooked<0x5DE388>, "CAEPedSpeechAudioEntity__Initialise"); //CEmergencyPed
    }
   
}
