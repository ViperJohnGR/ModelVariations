#include "DataReader.hpp"
#include "Helpers.hpp"
#include "Hooks.hpp"
#include "Log.hpp"
#include "Peds.hpp"
#include "PedWeapons.hpp"
#include "SA.hpp"
#include "VariationData.hpp"

#include <plugin.h>
#include <CModelInfo.h>
#include <CPed.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

static const char* dataFileName = "ModelVariations_PedWeapons.ini";

std::unordered_map<unsigned short, std::string> wepVehModels;
std::unordered_map<unsigned short, std::unordered_map<std::string, std::vector<unsigned short>>> weaponVectors;

std::vector<CPed*> pedWepStack;

std::vector<unsigned short> disableOnMission;
std::vector<unsigned short> mergeZonesWithGlobal;
std::vector<std::pair<CPed*, int>> weaponWatchers;
std::map<CPed*, std::chrono::steady_clock::duration> delayedPeds;
std::map<CPed*, eWeaponType> delayedSlotChanges;

const char* slotStrings[13] = {"SLOT0", "SLOT1", "SLOT2", "SLOT3", "SLOT4", "SLOT5", "SLOT6", "SLOT7", "SLOT8", "SLOT9", "SLOT10", "SLOT11", "SLOT12"};
bool iniHasGlobal = false;

struct tPedWeaponOptions {
    bool weaponforceClearsWeapons = false;
    bool weaponforceDelayInVehicle = false;
    bool skipScriptedPeds = false;
    int giveWeaponDelay = 0;
};

static tPedWeaponOptions pedWeaponOptions;

bool isIdValidForWatcher(unsigned short id)
{
    switch (getVariationOriginalModel(id))  //NOTE: drug dealers only work with WEAPONFORCE because they are initially unarmed
    {
        case 28:
        case 29:
        case 30:
        case 163:
        case 164:
        case 254:
            return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PedWeaponVariations::ClearData()
{
    wepVehModels.clear();
    weaponVectors.clear();
    pedWepStack.clear();
    disableOnMission.clear();
    mergeZonesWithGlobal.clear();
    weaponWatchers.clear();
    delayedPeds.clear();
    iniHasGlobal = false;

    pedWeaponOptions = {};
}

void PedWeaponVariations::LoadData()
{
    DataReader dataFile(dataFileName);

    pedWeaponOptions.weaponforceClearsWeapons = dataFile.ReadBoolean("Settings", "WeaponforceClearsWeapons", false);
    pedWeaponOptions.weaponforceDelayInVehicle = dataFile.ReadBoolean("Settings", "WeaponforceDelayInVehicle", false);
    pedWeaponOptions.skipScriptedPeds = dataFile.ReadBoolean("Settings", "SkipScriptedPeds", false);
    pedWeaponOptions.giveWeaponDelay = dataFile.ReadInteger("Settings", "GiveWeaponDelay", false);

    Log::Write("\nReading ped weapon data...\n");

    for (auto& iniData : dataFile.data)
    {
        bool sectionIsGlobal = iniData.first == "Global";

        int modelid = 0;
        std::string section(iniData.first);
        Log::Write("%s\n", section.c_str());

        if (!sectionIsGlobal)
        {
            if (!(section[0] >= '0' && section[0] <= '9'))
                CModelInfo::GetModelInfo(section.data(), &modelid);
            else
                fromString<int>(section, modelid);
        }

        for (auto& kvp : iniData.second)
        {
            auto key = std::string(kvp.first);

            auto vec = dataFile.ReadLine(iniData.first, kvp.first, READ_WEAPONS);

            if (!vec.empty() && (modelid > (sectionIsGlobal ? -1 : 0)) && modelid < 65536)
                weaponVectors[static_cast<unsigned short>(modelid)][key] = vec;

            for (const std::string& token : splitString(key, '|'))
            {
                int vehModelId = 0;
                auto mInfo = CModelInfo::GetModelInfo(token.c_str(), &vehModelId);
                if (mInfo && mInfo->GetModelType() == MODEL_INFO_VEHICLE && vehModelId > 0 && vehModelId < 65536)
                {
                    wepVehModels.insert({ (unsigned short)vehModelId, token });
                    break;
                }
            }
        }

        if (dataFile.ReadBoolean(iniData.first, "MergeZonesWithGlobal", false))
        {
            if (section == "Global")
                mergeZonesWithGlobal.push_back(0);
            else if (modelid > 0 && modelid < 65536)
                mergeZonesWithGlobal.push_back(static_cast<unsigned short>(modelid));
        }

        if (dataFile.ReadBoolean(iniData.first, "DisableOnMission", false))
        {
            if (section == "Global")
                disableOnMission.push_back(0);
            else if (modelid > 0 && modelid < 65536)
                disableOnMission.push_back(static_cast<unsigned short>(modelid));
        }
    }

    std::sort(disableOnMission.begin(), disableOnMission.end());
    std::sort(mergeZonesWithGlobal.begin(), mergeZonesWithGlobal.end());

    if (weaponVectors.contains(0))
        iniHasGlobal = true;

    Log::Write("\n");
}

void PedWeaponVariations::Process()
{
    std::vector<CPed*> pedsToPush;

    for (auto it = delayedSlotChanges.begin(); it != delayedSlotChanges.end();)
    {
        if (!IsPedPointerValid(it->first))
            it = delayedSlotChanges.erase(it);
        else if (!it->first->bInVehicle)
        {
            it->first->SetCurrentWeapon(it->second);
            it = delayedSlotChanges.erase(it);
        }
        else
            ++it;
    }

    while (!pedWepStack.empty())
    {
        CPed* ped = pedWepStack.back();
        pedWepStack.pop_back();

        if (!IsPedPointerValid(ped))
        {
            delayedPeds.erase(ped);
            continue;
        }

        if (ped->m_nModelIndex < 7 || (!weaponVectors.contains(ped->m_nModelIndex) && !iniHasGlobal))
            continue;

        if (pedWeaponOptions.skipScriptedPeds && ped->m_nCreatedBy == 2)
            continue;

        if (pedWeaponOptions.giveWeaponDelay > 0)
        {
            auto it = delayedPeds.find(ped);
            if (it != delayedPeds.end())
            {
                if ((gameplayTimeSinceLoad - it->second) > std::chrono::milliseconds(pedWeaponOptions.giveWeaponDelay * 1000))
                    delayedPeds.erase(ped);
                else
                {
                    pedsToPush.push_back(ped);
                    continue;
                }
            }
            else
            {
                delayedPeds[ped] = gameplayTimeSinceLoad;
                pedsToPush.push_back(ped);
                continue;
            }
        }

        bool wepChanged = false;
        unsigned short pedModel = 0;
        auto wepVecIt = weaponVectors.end();

        const auto changeWeapon = [&](std::string key) -> bool
        {
            if (auto it = wepVecIt->second.find(key); it == wepVecIt->second.end())
                return false;
            else if (auto &vec = it->second; !vec.empty())
            {
                eWeaponType weaponId = (eWeaponType)vectorGetRandom(vec);
                const CWeaponInfo* wInfo = CWeaponInfo::GetWeaponInfo(weaponId, 1);

                if (wInfo != NULL && wInfo->m_nModelId >= 321)
                {
                    if (isIdValidForWatcher(ped->m_nModelIndex))
                    {
                        weaponWatchers.push_back({ ped, weaponId });
                        wepChanged = true;
                        return true;
                    };

                    if (auto loadState = loadModel(wInfo->m_nModelId, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
                    {
                        Log::Write("Error loading weapon model %d (%s) %s\n", wInfo->m_nModelId, modelNames.contains((unsigned short)wInfo->m_nModelId) ? modelNames[(unsigned short)wInfo->m_nModelId].c_str() : "", getLoadStateString(loadState));
                        return false;
                    }

                    bool isWeaponforce = key.find("WEAPONFORCE") != std::string::npos;

                    if (pedWeaponOptions.weaponforceClearsWeapons && isWeaponforce)
                        ped->ClearWeapons();
                        
                    Log::WriteVerbose("Giving ped 0x%08X with model id %u weapon %u (key %s)\n", ped, ped->m_nModelIndex, weaponId, key.c_str());
                    ped->GiveWeapon(weaponId, 9999, true);

                    if (isWeaponforce)
                    {
                        if (pedWeaponOptions.weaponforceDelayInVehicle && ped->m_pVehicle)
                            delayedSlotChanges[ped] = weaponId;
                        else
                            ped->SetCurrentWeapon(weaponId);
                    }

                    wepChanged = true;
                    return true;
                }
            }

            return false;
        };

        const bool mergeWeapons = vectorHasId(mergeZonesWithGlobal, ped->m_nModelIndex) || vectorHasId(mergeZonesWithGlobal, 0);
        bool isOnMission = CTheScripts__IsPlayerOnAMission();
        bool pedInVehicle = IsVehiclePointerValid(ped->m_pVehicle);

        if ((vectorHasId(disableOnMission, ped->m_nModelIndex) || vectorHasId(disableOnMission, 0)) && isOnMission)
            continue;

        std::array<std::string, 13> weaponStrings;
        for (int i = 0; i < 13; i++)
            if (ped->m_aWeapons[i].m_eWeaponType > 0)
                weaponStrings[i] = "WEAPON" + std::to_string(ped->m_aWeapons[i].m_eWeaponType);

        const int originalSlot = ped->m_nSelectedWepSlot;
        auto player = FindPlayerPed();
        const CWanted* wanted = FindPlayerWanted(-1);
        unsigned int wantedLevel = wanted ? wanted->m_nWantedLevel : 0;

        char zoneString[9] = {};
        if (player && player->m_pEnex)
            copyString(zoneString, reinterpret_cast<char*>(player->m_pEnex), 8);
        else if (currentZone)
            memcpy(zoneString, currentZone->m_szLabel, 8);

        const std::string missionString = (isOnMission) ? ("MISSION_" + std::string(currentMission) + "|") : "";
        const std::string wantedString = (wantedLevel > 0) ? ("WANTED" + std::to_string(wantedLevel) + "|") : "";
        std::string vehString = "ON_FOOT|";
        if (pedInVehicle)
        {
            auto it = wepVehModels.find(ped->m_pVehicle->m_nModelIndex);
            if (it != wepVehModels.end())
                vehString = it->second + "|";
            else
                vehString = std::to_string(ped->m_pVehicle->m_nModelIndex) + "|";
        }

        for (int m = (isOnMission ? 0 : 1); m < 2; m++)
            for (int k = 1; k >= (iniHasGlobal ? 0 : 1); --k)
            {
                pedModel = (k == 1) ? ped->m_nModelIndex : 0;
                wepVecIt = weaponVectors.find(pedModel);

                if (wepVecIt == weaponVectors.end())
                    continue;

                for (int j = 0; j < 4; j++)
                {
                    if (wepChanged)
                        break;

                    std::string wantedVehString;

                    if (m == 0)
                        wantedVehString = missionString;

                    if (j == 0 || j == 2)
                    {
                        if (wantedLevel > 0)
                            wantedVehString += wantedString;
                        else
                            continue;
                    }

                    if (j < 2)
                        wantedVehString += vehString;

                    bool changeZoneWeaponForce = true;
                    if (changeWeapon(wantedVehString + "WEAPONFORCE"))
                        changeZoneWeaponForce = rand<bool>();

                    std::string wantedVehZoneString = wantedVehString + zoneString + '|';

                    if (changeZoneWeaponForce || !mergeWeapons)
                        changeWeapon(wantedVehZoneString + "WEAPONFORCE");

                    if (!wepChanged)
                    {
                        for (int i = 0; i < 13; i++)
                            if (!weaponStrings[i].empty())
                            {
                                bool changeZoneWeapon = true;
                                bool changeZoneSlot = true;

                                if (changeWeapon(wantedVehString + slotStrings[i]))
                                    changeZoneSlot = rand<bool>();

                                if ((changeZoneSlot || !mergeWeapons))
                                    changeWeapon(wantedVehZoneString + slotStrings[i]);

                                if (changeWeapon(wantedVehString + weaponStrings[i]))
                                    changeZoneWeapon = rand<bool>();

                                if ((changeZoneWeapon || !mergeWeapons))
                                    changeWeapon(wantedVehZoneString + weaponStrings[i]);
                            }

                        if (wepChanged)
                            ped->SetCurrentWeapon(originalSlot);
                    }
                }
            }
    }

    for (auto ped : pedsToPush)
        pedWepStack.push_back(ped);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  LOGGING   ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

void PedWeaponVariations::LogDataFile()
{
    if (!fileExists(dataFileName))
        Log::Write("\n%s not found!\n\n", dataFileName);
    else
    {
        Log::Write("%s\n", printFilenameWithBorder(dataFileName, '#').c_str());
        Log::Write("%s\n", fileToString(dataFileName).c_str());
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

__declspec(noinline) CPed* __fastcall CPedHooked(CPed* ped, void*, int pedType)
{
    const auto originalCall = captureCurrentOriginalCall();

    std::erase(pedWepStack, ped);
    std::erase_if(weaponWatchers,
        [ped](const auto& watcher)
        {
            return watcher.first == ped;
        }
    );

    delayedPeds.erase(ped);
    delayedSlotChanges.erase(ped);

    CPed* retVal = originalCall.callMethodAndReturn<CPed*>(ped, pedType);

    if (retVal)
        pedWepStack.push_back(ped);

    return retVal;
}

__declspec(noinline) void __fastcall GiveWeaponAtStartOfFightHooked(CPed* ped)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (ped && ped->m_nCreatedBy != 2 && ped->m_aWeapons[ped->m_nSelectedWepSlot].m_eWeaponType == WEAPONTYPE_UNARMED)
        switch (ped->m_nPedType)
        {
            case PED_TYPE_CRIMINAL:
            case PED_TYPE_PROSTITUTE:
                for (auto& i : weaponWatchers)
                    if (i.first == ped)
                        return originalCall.callMethod(ped);
                
                pedWepStack.push_back(ped);
        }

    return originalCall.callMethod(ped);
}

__declspec(noinline) int __fastcall GiveWeaponHooked(CPed* ped, void*, int weaponID, int ammo, int a4)
{
    const auto originalCall = captureCurrentOriginalCall();

    for (auto it = weaponWatchers.begin();it != weaponWatchers.end();)
    {
        if (it->first == ped)
        {
            const CWeaponInfo* wInfo = CWeaponInfo::GetWeaponInfo((eWeaponType)it->second, 1);
            if (wInfo != NULL && wInfo->m_nModelId >= 321)
                if (auto loadState = loadModel(wInfo->m_nModelId, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
                    Log::Write("Error loading weapon model %d (%s) %s\n", wInfo->m_nModelId, modelNames.contains((unsigned short)wInfo->m_nModelId) ? modelNames[(unsigned short)wInfo->m_nModelId].c_str() : "", getLoadStateString(loadState));
                else
                    weaponID = it->second;

            break;
        }

        if (!IsPedPointerValid(it->first))
            it = weaponWatchers.erase(it);
        else
            it++;
    }

    return originalCall.callMethodAndReturn<int>(ped, weaponID, ammo, a4);
}

__declspec(noinline) int16_t __fastcall CollectParametersHooked(void* _this, void*, unsigned __int16 a2)
{
    const auto originalCall = captureCurrentOriginalCall();
    auto retVal = originalCall.callMethodAndReturn<int16_t>(_this, a2);

    if (!ScriptParams[1])
        return retVal;

    for (auto it = weaponWatchers.begin(); it != weaponWatchers.end();)
    {
        if (IsPedPointerValid(it->first))
        {
            if (ScriptParams[0] == CPools::GetPedRef(it->first) && ScriptParams[1] == 22)
            {
                ScriptParams[1] = it->second;
                break;
            }
            it++;
        }
        else
            it = weaponWatchers.erase(it);
    }   

    return retVal;
}

__declspec(noinline) bool __fastcall DoWeHaveWeaponAvailableHooked(CPed* ped, void*, eWeaponType weapId)
{
    if (!IsPedPointerValid(ped))
        return false;

    CWeaponInfo* wepInfo = CWeaponInfo::GetWeaponInfo(weapId, 1);
    if (wepInfo == NULL)
        return false;

    auto slot = wepInfo->m_nSlot;
    if (slot < 13 && ped->m_aWeapons[slot].m_eWeaponType > WEAPONTYPE_UNARMED)
        return true;

    return false;
}


void PedWeaponVariations::InstallHooks()
{
    hookSharedCall<0x5DDB92, CPedHooked>("CPed::CPed"); //CCivilianPed::CCivilianPed
    hookSharedCall<0x5DDC81, CPedHooked>("CPed::CPed"); //CCop::CCop
    hookSharedCall<0x5DE362, CPedHooked>("CPed::CPed"); //CEmergencyPed::CEmergencyPed

    hookSharedCall<0x62A12E, GiveWeaponAtStartOfFightHooked>("CPed::GiveWeaponAtStartOfFight"); //CTaskSimpleFightingControl::ProcessPed
    hookSharedCall<0x47D335, GiveWeaponHooked>("CPed::GiveWeapon"); //01B2: GIVE_WEAPON_TO_CHAR
    hookSharedCall<0x47D4AC, CollectParametersHooked>("CRunningScript::CollectParameters"); //01B9: SET_CURRENT_CHAR_WEAPON
    hookSharedCall<0x48AE9E, CollectParametersHooked>("CRunningScript::CollectParameters"); //0491: HAS_CHAR_GOT_WEAPON
    hookCall<0x68BBA0>(DoWeHaveWeaponAvailableHooked, "CPed::DoWeHaveWeaponAvailable"); //CTaskComplexPolicePursuit::SetWeapon
    hookCall<0x68BB32>(DoWeHaveWeaponAvailableHooked, "CPed::DoWeHaveWeaponAvailable"); //CTaskComplexPolicePursuit::SetWeapon
}
