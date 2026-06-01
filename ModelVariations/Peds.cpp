#include "Peds.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "Memory.hpp"
#include "SA.hpp"

#include <plugin.h>
#include <CClock.h>
#include <CFont.h>
#include <CModelInfo.h>
#include <CPed.h>
#include <CPopCycle.h>
#include <CSprite.h>
#include <CTaskComplexCopInCar.h>
#include <CTheZones.h>
#include <CWeather.h>
#include <CWorld.h>

#include <array>
#include <stack>
#include <chrono>

static const char* dataFileName = "ModelVariations_Peds.ini";
static DataReader dataFile(dataFileName);
std::vector<int16_t> destroyedModelCounters;

struct tPedVars {
    std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>> variations;
    std::unordered_map<unsigned short, std::array<std::vector<unsigned short>, 6>> wantedVariations;
    std::map<unsigned short, std::vector<unsigned short>> currentVariations;
    std::unordered_map<unsigned short, std::vector<pedTimeGroup>> timeGroups;
    std::unordered_map<unsigned short, std::set<unsigned short>> activeTimeGroups;


    std::map<unsigned short, std::chrono::steady_clock::time_point> pedTimeSinceSpawn;
    std::unordered_map<unsigned short, unsigned short> originalModels;
    std::unordered_map<unsigned short, bool> useParentVoice;
    std::unordered_map<unsigned short, std::vector<unsigned short>> voices;
    std::unordered_map<unsigned short, unsigned int> animGroups;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherSunny;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherRainy;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherFoggy;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherSandstorm;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherWindy;

    std::set<unsigned short> pedHasVariations;

    std::stack<CPed*> stack;

    std::vector<unsigned short> disableOnMission;
    std::vector<unsigned short> dontInheritBehaviourModels;
    std::vector<unsigned short> mergeInteriors;
};

std::unique_ptr<tPedVars> pedVars(new tPedVars);


struct tPedOptions {
    bool recursiveVariations = true;
    bool useParentVoices = false;
    bool improveCivilianVariety = false;
};

std::unique_ptr<tPedOptions> pedOptions(new tPedOptions);

unsigned short variationModel = 0;

std::map<CPed*, unsigned short> changedVoices;

struct
{
    bool isRainy = false;
    bool isSandstorm = false;
    bool isFoggy = false;
    bool isWindy = false;
    bool isSunny = false;
} weatherState;

bool isValidPedId(int id)
{
    if (id < 1)
        return false;
    if (id >= 190 && id <= 195)
        return false;

    return true;
}

unsigned short PedVariations::GetVariationOriginalModel(const int modelIndex)
{
    auto it = pedVars->originalModels.find((unsigned short)modelIndex);
    if (it != pedVars->originalModels.end())
        return it->second;

    return { (unsigned short)modelIndex };
}

bool isPedVisible(CPed* ped) 
{
    if (ped == NULL)
        return false;

    CVector camPos = TheCamera.m_vecGameCamPos;
    CVector targetPos = ped->GetPosition();

    CColPoint hitPoint;
    CEntity* hitEntity = nullptr;

    bool hitSomething = CWorld::ProcessLineOfSight(
        camPos,
        targetPos,
        hitPoint,
        hitEntity,
        true,   // buildings
        false,   // vehicles
        false,  // peds
        true,   // objects
        true,   // dummies
        true,   // doSeeThroughCheck
        true,   // doCameraIgnoreCheck
        false   // doShootThroughCheck
    );

    return !hitSomething || hitEntity == ped;
}

bool canPedDriveVeh(int pedModel, int vehModel)
{
    CPedModelInfo* pedInfo = reinterpret_cast<CPedModelInfo*>(CModelInfo::GetModelInfo(pedModel));
    CVehicleModelInfo *vehInfo = reinterpret_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(vehModel));
    if (pedInfo && vehInfo)
        return (pedInfo->m_nCarsCanDriveMask & (1U << vehInfo->m_nVehicleClass));

    return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PedVariations::ClearData()
{
    pedVars.reset(new tPedVars());
    pedOptions.reset(new tPedOptions());

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

        if (!section.empty() && section[0] >= '0' && section[0] <= '9')
            i = fast_atoi(iniData.first.c_str());
        else
            CModelInfo::GetModelInfo(section.data(), &i);

        if (i <= 0 || i > 65535)
            continue;

        unsigned short modelIndex = static_cast<unsigned short>(i);
        if (isValidPedId(modelIndex))
        {
            for (auto& kvp : iniData.second)
                if (auto it = presetAllZones.find(kvp.first); it != presetAllZones.end())
                {
                    auto vec = dataFile.ReadLine(section, kvp.first, READ_PEDS);

                    if (!vec.empty())
                    {
                        pedVars->pedHasVariations.insert(modelIndex);
                        if (it->second.empty()) //Global
                        {
                            for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
                            {
                                CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);
                                uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                                pedVars->variations[zoneName][modelIndex] = vectorUnion(pedVars->variations[zoneName][modelIndex], vec);
                            }
                        }
                        else for (auto zone : it->second)
                        {
                            uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                            pedVars->variations[zoneName][modelIndex] = vectorUnion(pedVars->variations[zoneName][modelIndex], vec);
                        }
                    }
                }

            bool mergeZones = dataFile.ReadBoolean(section, "MergeZonesWithAreas", false);

            for (auto& kvp : iniData.second)
            {
                if (kvp.first.size() > 1 && !islower(kvp.first[1])) //also includes interiors
                {
                    auto vec = dataFile.ReadLine(section, kvp.first, READ_PEDS);
                    if (!vec.empty())
                    {
                        pedVars->pedHasVariations.insert(modelIndex);
                        uint64_t zoneName = 0;
                        strncpy((char*)&zoneName, kvp.first.c_str(), 8);
                        pedVars->variations[zoneName][modelIndex] = mergeZones ? vectorUnion(pedVars->variations[zoneName][modelIndex], vec) : vec;
                    }
                }
            }

            for (unsigned j = 0; j < 6; j++)
            {
                auto vec = dataFile.ReadLine(section, "Wanted" + std::to_string(j+1), READ_PEDS);
                if (vec.empty())
                    continue;
                pedVars->wantedVariations[modelIndex][j] = vec;
            }

            for (const auto& j : pedVars->variations)
                if (auto it = j.second.find(modelIndex); it != j.second.end())
                    for (auto variation : it->second)
                        if (variation > 0 && variation != modelIndex)
                            pedVars->originalModels.insert({ variation, modelIndex });

            for (unsigned int j = 0; j < 9; j++)
            {
                auto groupStart = dataFile.ReadInteger(section, "TimeGroup" + std::to_string(j + 1) + "Start", -1);
                if (groupStart > -1)
                {
                    auto groupEnd = dataFile.ReadInteger(section, "TimeGroup" + std::to_string(j + 1) + "End", -1);
                    if (groupEnd > -1)
                    {
                        auto vec = dataFile.ReadLine(section, "TimeGroup" + std::to_string(j + 1), READ_PEDS);

                        if (!vec.empty())
                        {
                            pedVars->timeGroups[modelIndex].push_back(pedTimeGroup((unsigned short)groupStart, (unsigned short)groupEnd, vec));
                            continue;
                        }
                    }
                }
                break;
            }

            if (dataFile.ReadBoolean(section, "DontInheritBehaviour", false))
                pedVars->dontInheritBehaviourModels.push_back(modelIndex);

            if (dataFile.ReadBoolean(section, "MergeInteriorsWithAreasAndZones", false))
                pedVars->mergeInteriors.push_back(modelIndex);

            if (dataFile.ReadBoolean(section, "DisableOnMission", false))
                pedVars->disableOnMission.push_back(modelIndex);
        }
        
        auto vec = dataFile.ReadLine(section, "WeatherSunny", READ_PEDS);
        if (!vec.empty())
            pedVars->weatherSunny[modelIndex] = vec;

        vec = dataFile.ReadLine(section, "WeatherRainy", READ_PEDS);
        if (!vec.empty())
            pedVars->weatherRainy[modelIndex] = vec;

        vec = dataFile.ReadLine(section, "WeatherFoggy", READ_PEDS);
        if (!vec.empty())
            pedVars->weatherFoggy[modelIndex] = vec;

        vec = dataFile.ReadLine(section, "WeatherSandstorm", READ_PEDS);
        if (!vec.empty())
            pedVars->weatherSandstorm[modelIndex] = vec;

        vec = dataFile.ReadLine(section, "WeatherWindy", READ_PEDS);
        if (!vec.empty())
            pedVars->weatherWindy[modelIndex] = vec;

        int parentVoice = dataFile.ReadInteger(section, "UseParentVoice", -1);
        if (parentVoice > -1)
            pedVars->useParentVoice[modelIndex] = static_cast<bool>(parentVoice);

        std::string animGroupString = dataFile.ReadString(section, "AnimGroup", "");
        if (!animGroupString.empty() && CAnimManager__ms_numAnimAssocDefinitions > 0)
            for (int j = CAnimManager__ms_numAnimAssocDefinitions - 1; j >= 0; j--)
            {
                const char* animString = CAnimManager__GetAnimGroupName(j);
                if (animString != NULL && strcmp(animGroupString.c_str(), animString) == 0)
                {
                    pedVars->animGroups[modelIndex] = (unsigned)j;
                    break;
                }
            }

        vec = dataFile.ReadLine(section, "Voice", READ_PEDS);
        if (!vec.empty())
            pedVars->voices.insert({ modelIndex, vec });
    }

    std::sort(pedVars->dontInheritBehaviourModels.begin(), pedVars->dontInheritBehaviourModels.end());
    std::sort(pedVars->mergeInteriors.begin(), pedVars->mergeInteriors.end());
    std::sort(pedVars->disableOnMission.begin(), pedVars->disableOnMission.end());

    pedOptions->recursiveVariations = dataFile.ReadBoolean("Settings", "RecursiveVariations", true);
    pedOptions->useParentVoices = dataFile.ReadBoolean("Settings", "UseParentVoices", false);
    pedOptions->improveCivilianVariety = dataFile.ReadBoolean("Settings", "ImproveCivilianVariety", false);

    Log::Write("\n");
}

void PedVariations::Process()
{
    bool isRainy = CWeather__IsRainy();
    bool isSandstorm = CWeather::Sandstorm > 0.29;
    bool isFoggy = CWeather::Foggyness > 0.4;
    bool isWindy = CWeather::Wind > 0.29;
    bool isSunny = !isRainy && !isSandstorm && !isFoggy && !isWindy;

    bool weatherChanged = isRainy != weatherState.isRainy ||
                          isSandstorm != weatherState.isSandstorm ||
                          isFoggy != weatherState.isFoggy ||
                          isWindy != weatherState.isWindy ||
                          isSunny != weatherState.isSunny;

    weatherState = { isRainy, isSandstorm, isFoggy, isWindy, isSunny };

    int variationsUpdateQueued = 0;

    int gameTime = (CClock::ms_nGameClockHours * 100 + CClock::ms_nGameClockMinutes);

    for (auto& it : pedVars->activeTimeGroups)
        for (auto it2 = it.second.begin(); it2 !=it.second.end();)
        {
            unsigned short index = *it2;

            if (!isTimeInRange(gameTime, pedVars->timeGroups[it.first][index].start, pedVars->timeGroups[it.first][index].end))
            {
                it2 = it.second.erase(it2);
                variationsUpdateQueued = it.first;
            }
            else
            {
                ++it2;
            }
        }

    for (const auto& it : pedVars->timeGroups)
        for (unsigned int i = 0; i < it.second.size(); i++)
        {
            if (isTimeInRange(gameTime, it.second[i].start, it.second[i].end))
                if (pedVars->activeTimeGroups[it.first].insert((unsigned short)i).second == true)
                    variationsUpdateQueued = it.first;
        }

    if (weatherChanged)
    {
        char gameTimeString[7] = {};
        snprintf(gameTimeString, 6, "%02d:%02d", CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes);
        Log::Write("\n[%s] Updating ped variations due to weather change. Current weather: %d %d %d %d %d. Game time: %s\n", getDatetime(false, true, true).c_str(), isRainy, isSandstorm, isFoggy, isWindy, isSunny, gameTimeString);
        UpdateVariations();
        PedVariations::LogCurrentVariations();
        Log::Write("\n");
        weatherChanged = false;
    }

    if (variationsUpdateQueued > 0)
    {
        char gameTimeString[7] = {};
        snprintf(gameTimeString, 6, "%02d:%02d", CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes);
        Log::Write("Updating ped variations due to model %d time groups. Game time: %s\n", variationsUpdateQueued, gameTimeString);
        UpdateVariations();
        PedVariations::LogCurrentVariations();
        Log::Write("\n");
        Log::Write("Active time groups\n");
        for (auto it : pedVars->activeTimeGroups)
        {
            if (!it.second.empty())
            {
                Log::Write("%d: ", it.first);
                for (auto j : it.second)
                    Log::Write("%u ", j + 1);
                Log::Write("\n");
            }
        }
        Log::Write("\n\n");
        variationsUpdateQueued = 0;
    }

    std::erase_if(changedVoices, [](std::pair<CPed*, unsigned short> it) {
        return !IsPedPointerValid(it.first);
    });

    ProcessDrugDealers();

    while (!pedVars->stack.empty())
    {
        CPed* ped = pedVars->stack.top();
        pedVars->stack.pop();

        if (ped && isValidPedId(ped->m_nModelIndex))
        {
            auto it = pedVars->currentVariations.find(ped->m_nModelIndex);
            if (it != pedVars->currentVariations.end() && !it->second.empty() && it->second[0] == 0 && ped->m_nCreatedBy != 2) //Delete models with a 0 id variation
            {
                CVehicle* veh = ped->m_pVehicle;
                if (IsVehiclePointerValid(veh) && veh->m_nCreatedBy != eVehicleCreatedBy::MISSION_VEHICLE && veh->m_pDriver == ped)
                    veh->bFadeOut = true;
                else
                    destroyPed(ped);
            }
        }
    }
}

void PedVariations::ProcessDrugDealers(bool reset)
{
    static int dealersFrames = 0;

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
                if (it.first > 300)
                    if (it.second == 28 || it.second == 29 || it.second == 30 || it.second == 254)
                    {
                        Log::Write(addedIDs.contains(it.first) ? "%uSP\n" : "%u\n", it.first);
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
    pedVars->currentVariations.clear();

    auto player = FindPlayerPed();
    auto interiorVariations = (player->m_pEnex) ? pedVars->variations.find(*reinterpret_cast<const uint64_t*>(player->m_pEnex)) : pedVars->variations.end();
    auto zoneVariations = pedVars->variations.find(*reinterpret_cast<uint64_t*>(currentZone));
    
    for (auto& modelid : pedVars->pedHasVariations)
    {
        bool modelHasInteriorVariations = false;

        if (interiorVariations != pedVars->variations.end())
            if (auto it = interiorVariations->second.find(modelid); it != interiorVariations->second.end())
            {
                pedVars->currentVariations[modelid] = it->second;
                modelHasInteriorVariations = true;
            }

        if ((!modelHasInteriorVariations || vectorHasId(pedVars->mergeInteriors, modelid)) && zoneVariations != pedVars->variations.end())
        {
            if (auto it = zoneVariations->second.find(modelid); it != zoneVariations->second.end())
                pedVars->currentVariations[modelid] = vectorUnion(it->second, pedVars->currentVariations[modelid]);
        }

        if (wanted)
        {
            const unsigned int wantedLevel = wanted->m_nWantedLevel - (wanted->m_nWantedLevel ? 1 : 0);
            if (auto it = pedVars->wantedVariations.find(modelid); it != pedVars->wantedVariations.end())
            {
                if (!it->second[wantedLevel].empty() && !pedVars->currentVariations[modelid].empty())
                    vectorfilterVector(pedVars->currentVariations[modelid], it->second[wantedLevel]);
            }
        }

        if (weatherState.isRainy)
        {
            auto it = pedVars->weatherRainy.find(modelid);
            if (it != pedVars->weatherRainy.end() && !it->second.empty())
                vectorfilterVector(pedVars->currentVariations[modelid], it->second);
        }

        if (weatherState.isSandstorm)
        {
            auto it = pedVars->weatherSandstorm.find(modelid);
            if (it != pedVars->weatherSandstorm.end() && !it->second.empty())
                vectorfilterVector(pedVars->currentVariations[modelid], it->second);
        }

        if (weatherState.isFoggy)
        {
            auto it = pedVars->weatherFoggy.find(modelid);
            if (it != pedVars->weatherFoggy.end() && !it->second.empty())
                vectorfilterVector(pedVars->currentVariations[modelid], it->second);
        }

        if (weatherState.isWindy)
        {
            auto it = pedVars->weatherWindy.find(modelid);
            if (it != pedVars->weatherWindy.end() && !it->second.empty())
                vectorfilterVector(pedVars->currentVariations[modelid], it->second);
        }

        if (weatherState.isSunny)
        {
            auto it = pedVars->weatherSunny.find(modelid);
            if (it != pedVars->weatherSunny.end() && !it->second.empty())
                vectorfilterVector(pedVars->currentVariations[modelid], it->second);
        }

        if (auto it = pedVars->activeTimeGroups.find(modelid); it != pedVars->activeTimeGroups.end())
            for (auto i : it->second)
                vectorfilterVector(pedVars->currentVariations[modelid], pedVars->timeGroups[modelid][i].variations);
    }
}

void PedVariations::DrawDebugInfo(float fontSize)
{
    auto* pedPool = CPools::ms_pPedPool;
    if (!pedPool)
        return;

    float fontSizew = RsGlobal.maximumHeight/640.0f * fontSize;
    float fontSizeh = fontSizew * 2.2f;

    // Text style
    CFont::SetBackground(false, false);
    CFont::SetOrientation(ALIGN_CENTER);
    CFont::SetProportional(true);
    CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetScale(fontSizew, fontSizeh);
    CFont::SetEdge(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetColor(CRGBA(127, 255, 255, 255));

    for (int i = 0; i < pedPool->m_nSize; ++i)
    {
        CPed* ped = pedPool->GetAt(i);
        if (!IsPedPointerValid(ped) || ped->m_nModelIndex < 7 || !ped->IsAlive() || !isPedVisible(ped))
            continue;

        // Position a little above the vehicle
        CVector pos = ped->GetPosition();

        RwV3d worldPos;
        worldPos.x = pos.x;
        worldPos.y = pos.y;
        worldPos.z = pos.z + 1.2f;

        RwV3d screenPos;
        float w, h;
        if (!CSprite::CalcScreenCoors(worldPos, &screenPos, &w, &h, true, true))
            continue;

        const float lineOffset = (RsGlobal.maximumHeight / 640.0f) * fontSize * 35.0f;
        char line1[32] = {};
        char line2[64] = {};
        std::snprintf(line1, sizeof(line1), "0x%08X", reinterpret_cast<std::uintptr_t>(ped));
        std::snprintf(line2, sizeof(line2), "%u %s", ped->m_nModelIndex, modelNames.contains(ped->m_nModelIndex) ? modelNames[ped->m_nModelIndex].c_str() : "");

        CFont::PrintString(screenPos.x, screenPos.y, line1);
        CFont::PrintString(screenPos.x, screenPos.y + lineOffset, line2);

        std::string nextLine;
       
        if (auto it = pedVars->originalModels.find(ped->m_nModelIndex); it != pedVars->originalModels.end())
        {
            nextLine = "Parent model: " + std::to_string(it->second);
            CFont::PrintString(screenPos.x, screenPos.y + lineOffset * 2.0f, nextLine.c_str());
        }

        if (auto it = changedVoices.find(ped); it != changedVoices.end())
        {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "Voice: %u", it->second);
            CFont::PrintString(screenPos.x, screenPos.y + lineOffset * 3.0f, buffer);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  LOGGING   ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

void PedVariations::LogCurrentVariations()
{
    if (!Log::Write("pedCurrentVariations"))
        return;

    if (pedVars->currentVariations.empty())
        Log::Write(" is empty\n");
    else
        Log::Write("\n");

    for (auto it : pedVars->currentVariations)
        if (!it.second.empty())
        {
            Log::Write("%d: ", it.first);
            for (auto j : it.second)
            {
                const char* suffix = " ";
                if (addedIDs.contains(j))
                    suffix = "SP ";
                Log::Write("%u%s", j, suffix);
            }
            Log::Write("\n");
        }
}

void PedVariations::LogDataFile()
{
    if (!fileExists(dataFileName))
        Log::Write("\n%s not found!\n\n", dataFileName);
    else
    {
        Log::Write("%s\n", printFilenameWithBorder(dataFileName, '#').c_str());
        Log::LogTextFile(dataFileName);
        Log::Write("\n");
    }
}

void PedVariations::LogVariations()
{
    if (!Log::Write("Ped Variations:\n"))
        return;

    std::map<unsigned short, std::set<unsigned short>> variationsMap;
    for (const auto& it : pedVars->variations)
    {
        for (const auto &i : it.second)
            for (auto j : i.second)
                variationsMap[i.first].insert(j);
    }

    for (const auto& i : variationsMap)
    {
        Log::Write("%u: ", i.first);
        for (auto j : i.second)
        {
            const char* suffix = " ";
            if (addedIDs.contains(j))
                suffix = "SP ";
            Log::Write("%u%s", j, suffix);
        }
        Log::Write("\n");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template <std::uintptr_t address>
int __cdecl getKillsByPlayer(int player)
{
    int sum = 0;

    for (int i = maxPedID * player; i < maxPedID * (player+1); i++)
        sum += destroyedModelCounters[i];

    return sum;
}

template <std::uintptr_t address>
void __fastcall SetModelIndexHooked(CEntity* _this, void*, const int index)
{
    if (index < 7 || index > 65535 || (vectorHasId(pedVars->disableOnMission, index) && CTheScripts__IsPlayerOnAMission()))
        return callMethodOriginal<address>(_this, index);

    auto it = pedVars->currentVariations.find((unsigned short)index);
    if (isValidPedId(index) && it != pedVars->currentVariations.end() && !it->second.empty())
    {
        const unsigned short newModel = vectorGetRandom(it->second);
        if (newModel > 0 && newModel != index)
        {
            if (auto loadState = loadModel(newModel, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
            {
                Log::Write("Error loading ped model %d (%s) %s. Using original model %d.\n", newModel, modelNames.contains(newModel) ? modelNames[newModel].c_str() : "", getLoadStateString(loadState).c_str(), index);
                return callMethodOriginal<address>(_this, index);
            }

            if (pedOptions->recursiveVariations)
                _this->SetModelIndex(newModel);
            else 
                callMethodOriginal<address>(_this, newModel);

            if (!vectorHasId(pedVars->dontInheritBehaviourModels, index))
                _this->m_nModelIndex = (unsigned short)index;
            variationModel = newModel;
            return;
        }
    }

    callMethodOriginal<address>(_this, index);
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
            if (it != pedVars->originalModels.end())
                newModel = it->second;
        }

        auto it = pedVars->voices.find(ped->m_nModelIndex);
        if (it != pedVars->voices.end() && !it->second.empty())
            newModel = vectorGetRandom(it->second);

        if (newModel > 0)
        {
            changedVoices[ped] = newModel;
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
    changedVoices.erase(_this);
    CPhysical* retVal = callMethodOriginalAndReturn<CPhysical*, address>(_this);
    pedVars->stack.push(_this);
    return retVal;
}

//Improper fix for crash 0x68FB5C
template <std::uintptr_t address>
void* __fastcall CreateNextSubTaskHooked(CTaskComplexCopInCar* _this, void*, CPed* ped)
{
    if (_this == NULL)
        Log::Write("CreateNextSubTaskHooked _this is NULL. ped is 0x%08X\n", ped);
    else if (ped == NULL)
        Log::Write("CreateNextSubTaskHooked ped is NULL. _this is 0x%08X\n", _this);
    else if (_this->m_pVehicle == NULL)
    {
        Log::Write("CTaskComplexCopInCar 0x%08X of ped 0x%08X with model index %u has NULL vehicle pointer.\n", _this, ped, ped->m_nModelIndex);
        auto subtask = _this->GetSubTask();
        if (subtask && subtask->GetId() == TASK_SIMPLE_CAR_DRIVE)
        {
            if (ped->m_pVehicle)
                ped->m_pVehicle->m_autoPilot.m_nCarMission = MISSION_NONE;
            else
                Log::Write("CTaskComplexCopInCar ped->m_pVehicle is NULL.\n");

            uint8_t originalData[7];
            injector::ReadMemoryRaw(0x68FB5C, originalData, 7, true);
            injector::MakeNOP(0x68FB5C, 7);
            auto retVal = callMethodOriginalAndReturn<void*, address>(_this, ped);
            injector::WriteMemoryRaw(0x68FB5C, originalData, 7, true);
            return retVal;
        }
    }

    return callMethodOriginalAndReturn<void*, address>(_this, ped);
}

template <std::uintptr_t address>
bool __cdecl PedIsAcceptableInCurrentZoneHooked(int a1)
{
    for (CPed *ped : CPools::ms_pPedPool)
    {
        if (ped && ped->m_nModelIndex == a1)
            return false;
    }

    return callOriginalAndReturn<bool, address>(a1);
}

template <std::uintptr_t address>
int __cdecl ChooseCivilianOccupationForVehicleHooked(char male, CVehicle* a2)
{
    auto vehDrivers = { 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32, 33, 34, 
                        35, 36, 37, 40, 43, 44, 45, 46, 47, 48, 50, 55, 56, 57, 58, 59, 60, 66, 67, 68, 69, 71, 72, 73, 
                        82, 83, 84, 90, 91, 93, 94, 95, 97, 98, 100, 101, 128, 131, 132, 133, 134, 135, 136, 137, 138, 
                        139, 140, 141, 142, 143, 147, 148, 150, 151, 153, 154, 157, 158, 159, 160, 161, 162, 168, 169, 
                        170, 181, 182, 183, 184, 185, 186, 187, 188, 198, 199, 200, 201, 202, 206, 210, 211, 212, 213, 
                        215, 216, 217, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 233, 234, 235, 236, 240, 
                        241, 242, 247, 248, 250, 255, 260, 261, 262, 263 };


    auto modelid = callOriginalAndReturn<int, address>(male, a2);
    CVehicleModelInfo* vehModelInfo = (CVehicleModelInfo*)CModelInfo::GetModelInfo(a2->m_nModelIndex);

    if (modelid < 9)
    {
        auto specificDriver = CPopulation::FindSpecificDriverModelForCar_ToUse(a2->m_nModelIndex);
        if (specificDriver > 0 && loadModel(specificDriver, PRIORITY_REQUEST, true) == LOADSTATE_LOADED)
            return specificDriver;


        for (int i : vehDrivers)
        {
            auto mInfo = CModelInfo::GetModelInfo(i);
            if (mInfo && vehModelInfo && mInfo->GetModelType() == MODEL_INFO_PED && CPopCycle::IsPedAppropriateForCurrentZone(i) && (canPedDriveVeh(i, a2->m_nModelIndex) || a2->m_nModelIndex == 422))
            {
                bool modelExists = false;
                for (CPed* ped : CPools::ms_pPedPool)
                    if (ped && ped->m_nModelIndex == i)
                    {
                        modelExists = true;
                        break;
                    }

                if (!modelExists)
                {
                    loadModel(i, PRIORITY_REQUEST, true);
                    return i;
                }
            }
        }
    }
   
    std::pair<unsigned short, unsigned short> leastUsedModel = { 7, 65535 };

    for (auto i : vehDrivers)
    {
        auto mInfo = CModelInfo::GetModelInfo(i);
        if (mInfo && mInfo->m_nRefCount > 0 && mInfo->m_nRefCount <= leastUsedModel.second)
        {
            if (mInfo->m_nRefCount == leastUsedModel.second && rand<bool>())
                leastUsedModel.first = static_cast<unsigned short>(i);
            else
            {
                leastUsedModel.first = static_cast<unsigned short>(i);
                leastUsedModel.second = mInfo->m_nRefCount;
            }
        }
    }

    return leastUsedModel.first;
}

void PedVariations::InstallHooks(bool enableSpecialPeds)
{
    if (enableSpecialPeds)
    {
        bool gameHOODLUM = isGameHOODLUM();
        bool notModified = true;

        //Count of killable model IDs
        if (!memcmp(0x43DE6C, "66 FF 04 45 50 9A 96 00") ||
            !memcmp(0x43DF5B, "66 FF 04 45 50 9A 96 00") ||
            !memcmp((gameHOODLUM ? 0x1561634U : 0x43D6A4), "8D 04 45 50 9A 96 00") ||
            !memcmp((gameHOODLUM ? 0x1564C2BU : 0x43D6CB), "66 8B 04 55 50 9A 96 00"))
        {
            notModified = false;
        }

        if (notModified && maxPedID > -1)
        {
            destroyedModelCounters.resize(maxPedID * 2);

            injector::WriteMemory<int16_t*>(0x43DE70, &destroyedModelCounters[0], true);
            injector::WriteMemory<int16_t*>(0x43DF5F, &destroyedModelCounters[0], true);

            if (gameHOODLUM)
            {
                injector::WriteMemory<int16_t*>(0x1561637, &destroyedModelCounters[0], true);
                injector::WriteMemory<uint32_t>(0x156163C, maxPedID, true);

                injector::WriteMemory<int16_t*>(0x1564C2F, &destroyedModelCounters[0], true);
            }
            else
            {
                injector::WriteMemory<int16_t*>(0x43D6A7, &destroyedModelCounters[0], true);
                injector::WriteMemory<uint32_t>(0x43D6AC, maxPedID, true);

                injector::WriteMemory<int16_t*>(0x43D6CF, &destroyedModelCounters[0], true);
            }

            hookCall(0x47360D, getKillsByPlayer<0x47360D>, "CDarkel::FindTotalPedsKilledByPlayer");
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

    hookCall(0x870A4C, CreateNextSubTaskHooked<0x870A4C>, "CTaskComplexCopInCar::CreateNextSubTask", true);

    if (pedOptions->improveCivilianVariety)
    {
        hookCall(0x61302B, PedIsAcceptableInCurrentZoneHooked<0x61302B>, "CPopCycle::PedIsAcceptableInCurrentZone"); //CPopulation::ChooseCivilianOccupation
        hookCall(0x61330D, PedIsAcceptableInCurrentZoneHooked<0x61330D>, "CPopCycle::PedIsAcceptableInCurrentZone"); //CPopulation::ChooseCivilianOccupation
        hookCall(0x613B32, ChooseCivilianOccupationForVehicleHooked<0x613B32>, "CPopulation::ChooseCivilianOccupationForVehicle"); //CPopulation::AddPedInCar
    }
}
