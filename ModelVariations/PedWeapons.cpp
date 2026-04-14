#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "Log.hpp"
#include "Peds.hpp"
#include "PedWeapons.hpp"
#include "SA.hpp"

#include <plugin.h>
#include <CModelInfo.h>
#include <CPed.h>

#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

static const char* dataFileName = "ModelVariations_PedWeapons.ini";
static DataReader dataFile(dataFileName);

std::unordered_map<unsigned short, std::string> wepPedModels;
std::unordered_map<unsigned short, std::string> wepVehModels;

std::stack<CPed*> pedWepStack;

std::vector<unsigned short> pedHasWeaponVariations;
std::vector<std::pair<CPed*, int>> weaponWatchers;
const char* slotStrings[13] = {"SLOT0", "SLOT1", "SLOT2", "SLOT3", "SLOT4", "SLOT5", "SLOT6", "SLOT7", "SLOT8", "SLOT9", "SLOT10", "SLOT11", "SLOT12"};
bool iniHasGlobal = false;

int lastMissionLoaded = -1;

bool isIdValidForWatcher(unsigned short id)
{
    for (auto i : PedVariations::GetVariationOriginalModels(id))
        switch (i)  //NOTE: drug dealers only work with WEAPONFORCE because they are initially unarmed
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
    wepPedModels.clear();
    wepVehModels.clear();
    pedHasWeaponVariations.clear();
    iniHasGlobal = false;

    dataFile.data.clear();
}

void PedWeaponVariations::LoadData()
{
    dataFile.Load(dataFileName);

    Log::Write("\nReading ped weapon data...\n");

    for (auto& iniData : dataFile.data)
    {
        Log::Write("%s\n", iniData.first.c_str());

        if (iniData.first == "Global")
            iniHasGlobal = true;

        int modelid = 0;
        std::string section = iniData.first;

        if (!(section[0] >= '0' && section[0] <= '9'))
        {
            CModelInfo::GetModelInfo(section.data(), &modelid);
            if (modelid > 0)
            {
                pedHasWeaponVariations.push_back((unsigned short)modelid);
                wepPedModels.insert({ (unsigned short)modelid, section });
            }
        }
        else
        {
            modelid = fast_atoi(section.c_str());
            if (modelid > 0 && modelid < 65535)
                pedHasWeaponVariations.push_back((unsigned short)modelid);
        }

        for (auto& keys : iniData.second)
        {
            std::string name;
            if (keys.first.starts_with("WANTED"))
            {
                auto modelNameStart = keys.first.find("|") + 1;
                name = keys.first.substr(modelNameStart, keys.first.find("|", modelNameStart) - modelNameStart);
            }
            else
                name = keys.first.substr(0, keys.first.find("|"));
            if (CModelInfo::GetModelInfo(name.data(), &modelid))
                wepVehModels.insert({ (unsigned short)modelid, name });
        }
    }

    std::sort(pedHasWeaponVariations.begin(), pedHasWeaponVariations.end());

    Log::Write("\n");
}

void PedWeaponVariations::Process()
{
    while (!pedWepStack.empty())
    {
        CPed* ped = pedWepStack.top();
        pedWepStack.pop();

        if (!IsPedPointerValid(ped) || ped->m_nModelIndex < 7 || (!vectorHasId(pedHasWeaponVariations, ped->m_nModelIndex) && !iniHasGlobal))
            continue;

        bool wepChanged = false;

        const auto changeWeapon = [&](const std::string& section, const std::string& key, eWeaponType originalWeaponId = WEAPONTYPE_UNARMED) -> bool
        {
            std::vector<unsigned short> vec = dataFile.ReadLine(section, key, READ_WEAPONS);
            if (!vec.empty())
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
                        Log::Write("Error loading weapon model %d (%s) %s\n", wInfo->m_nModelId, modelNames.contains((unsigned short)wInfo->m_nModelId) ? modelNames[(unsigned short)wInfo->m_nModelId].c_str() : "", getLoadStateString(loadState).c_str());
                        return false;
                    }

                    if (originalWeaponId > WEAPONTYPE_UNARMED)
                        ped->ClearWeapon(originalWeaponId);
                    else
                        ped->ClearWeapons();

                    ped->GiveWeapon(weaponId, 9999, true);

                    if (originalWeaponId == WEAPONTYPE_UNARMED)
                        ped->SetCurrentWeapon((int)wInfo->m_nSlot);

                    wepChanged = true;
                    return true;
                }
            }

            return false;
        };

        std::string section;
        if (auto it = wepPedModels.find(ped->m_nModelIndex); it != wepPedModels.end())
            section = it->second;
        else
            section = std::to_string(ped->m_nModelIndex);

        const bool mergeWeapons = dataFile.ReadBoolean(section, "MergeZonesWithGlobal", false);
        bool isOnMission = CTheScripts__IsPlayerOnAMission();
        bool pedInVehicle = IsVehiclePointerValid(ped->m_pVehicle);

        if (dataFile.ReadBoolean(section, "DisableOnMission", false) && isOnMission)
            continue;

        std::array<std::string, 13> weaponStrings;
        for (int i = 0; i < 13; i++)
            if (ped->m_aWeapons[i].m_eWeaponType > 0)
                weaponStrings[i] = "WEAPON" + std::to_string(ped->m_aWeapons[i].m_eWeaponType);

        const int originalSlot = ped->m_nSelectedWepSlot;
        char zoneString[9] = {};
        *reinterpret_cast<uint64_t*>(zoneString) = *reinterpret_cast<uint64_t*>(currentZone);
        auto player = FindPlayerPed();
        const CWanted* wanted = FindPlayerWanted(-1);
        unsigned int wantedLevel = wanted ? wanted->m_nWantedLevel : 0;

        if (player->m_pEnex)
            strncpy(zoneString, reinterpret_cast<char*>(player->m_pEnex), 8);

        const std::string missionString = (isOnMission) ? ("MISSION" + std::to_string(lastMissionLoaded) + "|") : "";
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
            for (int k = (iniHasGlobal ? 0 : 1); k < 2; k++)
            {
                const std::string& activeSection = (k == 1) ? section : "Global";
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
                    if (changeWeapon(activeSection, wantedVehString + "WEAPONFORCE"))
                        changeZoneWeaponForce = rand<bool>();

                    std::string wantedVehZoneString = wantedVehString + zoneString + '|';

                    if (changeZoneWeaponForce || !mergeWeapons)
                        changeWeapon(activeSection, wantedVehZoneString + "WEAPONFORCE");

                    if (!wepChanged)
                    {
                        for (int i = 0; i < 13; i++)
                            if (!weaponStrings[i].empty())
                            {
                                bool changeZoneWeapon = true;
                                bool changeZoneSlot = true;

                                if (changeWeapon(activeSection, wantedVehString + slotStrings[i], ped->m_aWeapons[i].m_eWeaponType))
                                    changeZoneSlot = rand<bool>();

                                if ((changeZoneSlot || !mergeWeapons))
                                    changeWeapon(activeSection, wantedVehZoneString + slotStrings[i], ped->m_aWeapons[i].m_eWeaponType);

                                if (changeWeapon(activeSection, wantedVehString + weaponStrings[i], ped->m_aWeapons[i].m_eWeaponType))
                                    changeZoneWeapon = rand<bool>();

                                if ((changeZoneWeapon || !mergeWeapons))
                                    changeWeapon(activeSection, wantedVehZoneString + weaponStrings[i], ped->m_aWeapons[i].m_eWeaponType);
                            }

                        if (wepChanged)
                            ped->SetCurrentWeapon(originalSlot);
                    }
                }
            }
    }
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
        Log::LogTextFile(dataFileName);
        Log::Write("\n");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

template <std::uintptr_t address>
CPed* __fastcall CPedHooked(CPed* ped, void*, int pedType)
{
    CPed* retVal = callMethodOriginalAndReturn<CPed*, address>(ped, pedType);
    pedWepStack.push(ped);
    return retVal;
}

template <std::uintptr_t address>
void __fastcall GiveWeaponAtStartOfFightHooked(CPed* ped)
{
    if (ped && ped->m_nCreatedBy != 2 && ped->m_aWeapons[ped->m_nSelectedWepSlot].m_eWeaponType == WEAPONTYPE_UNARMED)
        switch (ped->m_nPedType)
        {
            case PED_TYPE_CRIMINAL:
            case PED_TYPE_PROSTITUTE:
                for (auto& i : weaponWatchers)
                    if (i.first == ped)
                        return callMethodOriginal<address>(ped);
                
                pedWepStack.push(ped);
        }

    return callMethodOriginal<address>(ped);
}

template <std::uintptr_t address>
int __fastcall GiveWeaponHooked(CPed* ped, void*, int weaponID, int ammo, int a4)
{
    for (auto it = weaponWatchers.begin();it != weaponWatchers.end();)
    {
        if (it->first == ped)
        {
            weaponID = it->second;
            const CWeaponInfo* wInfo = CWeaponInfo::GetWeaponInfo((eWeaponType)weaponID, 1);
            if (wInfo != NULL && wInfo->m_nModelId >= 321)
                if (auto loadState = loadModel(wInfo->m_nModelId, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
                    Log::Write("Error loading weapon model %d (%s) %s\n", wInfo->m_nModelId, modelNames.contains((unsigned short)wInfo->m_nModelId) ? modelNames[(unsigned short)wInfo->m_nModelId].c_str() : "", getLoadStateString(loadState).c_str());

            break;
        }

        if (!IsPedPointerValid(it->first))
            it = weaponWatchers.erase(it);
        else
            it++;
    }

    return callMethodOriginalAndReturn<int, address>(ped, weaponID, ammo, a4);
}

template <std::uintptr_t address>
int16_t __fastcall CollectParametersHooked(void* _this, void*, unsigned __int16 a2)
{
    auto retVal = callMethodOriginalAndReturn<int16_t, address>(_this, a2);

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

template <std::uintptr_t address>
bool __fastcall DoWeHaveWeaponAvailableHooked(CPed* ped, void*, eWeaponType weapId)
{
    auto slot = CWeaponInfo::GetWeaponInfo(weapId, 1)->m_nSlot;
    if (ped->m_aWeapons[slot].m_eWeaponType > WEAPONTYPE_UNARMED)
        return true;

    return false;
}

template <std::uintptr_t address>
void __cdecl CTimer__SuspendHooked()
{
    callOriginal<address>();
    lastMissionLoaded = ScriptParams[0];
}


void PedWeaponVariations::InstallHooks()
{
    hookCall(0x5DDB92, CPedHooked<0x5DDB92>, "CPed::CPed"); //CCivilianPed::CCivilianPed
    hookCall(0x5DDC81, CPedHooked<0x5DDC81>, "CPed::CPed"); //CCop::CCop
    hookCall(0x5DE362, CPedHooked<0x5DE362>, "CPed::CPed"); //CEmergencyPed::CEmergencyPed

    hookCall(0x62A12E, GiveWeaponAtStartOfFightHooked<0x62A12E>, "CPed::GiveWeaponAtStartOfFight"); //CTaskSimpleFightingControl::ProcessPed
    hookCall(0x47D335, GiveWeaponHooked<0x47D335>, "CPed::GiveWeapon"); //01B2: GIVE_WEAPON_TO_CHAR
    hookCall(0x47D4AC, CollectParametersHooked<0x47D4AC>, "CRunningScript::CollectParameters"); //01B9: SET_CURRENT_CHAR_WEAPON
    hookCall(0x48AE9E, CollectParametersHooked<0x48AE9E>, "CRunningScript::CollectParameters"); //0491: HAS_CHAR_GOT_WEAPON
    hookCall(0x68BBA0, DoWeHaveWeaponAvailableHooked<0x68BBA0>, "CTaskComplexPolicePursuit::SetWeapon"); //CTaskComplexPolicePursuit::SetWeapon
    hookCall(0x68BB32, DoWeHaveWeaponAvailableHooked<0x68BB32>, "CTaskComplexPolicePursuit::SetWeapon"); //CTaskComplexPolicePursuit::SetWeapon
    hookCall(0x489955, CTimer__SuspendHooked<0x489955>, "CTimer::Suspend"); //0417: LOAD_AND_LAUNCH_MISSION_INTERNAL
}
