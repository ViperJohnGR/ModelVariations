#include "PedWeapons.hpp"
#include "Peds.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Hooks.hpp"
#include "Log.hpp"

#include <plugin.h>
#include <CModelInfo.h>
#include <CPed.h>
#include <CTheScripts.h>

#include <stack>
#include <string>
#include <vector>

static const char* dataFileName = "ModelVariations_PedWeapons.ini";
static DataReader dataFile(dataFileName);

std::unordered_map<unsigned short, std::string> wepPedModels;
std::unordered_map<unsigned short, std::string> wepVehModels;

std::stack<CPed*> pedWepStack;

std::vector<std::pair<CPed*, int>> weaponWatchers;

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

    dataFile.data.clear();
}

void PedWeaponVariations::LoadData()
{
    dataFile.Load(dataFileName);

    Log::Write("\nReading ped weapon data...\n");

    for (auto& iniData : dataFile.data)
    {
        Log::Write("%s\n", iniData.first.c_str());

        int modelid = 0;
        std::string section = iniData.first;

        if (!(section[0] >= '0' && section[0] <= '9'))
            CModelInfo::GetModelInfo(section.data(), &modelid);
        if (modelid > 0)
            wepPedModels.insert({ (unsigned short)modelid, section });

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

    Log::Write("\n");
}

void PedWeaponVariations::Process()
{
    while (!pedWepStack.empty())
    {
        CPed* ped = pedWepStack.top();
        pedWepStack.pop();

        if (!IsPedPointerValid(ped) || FindPlayerPed() == ped)
            continue;

        const auto changeWeapon = [ped](std::string_view section, std::string_view key, eWeaponType originalWeaponId = WEAPON_UNARMED) -> bool
        {
            std::vector<unsigned short> vec = dataFile.ReadLine(section, key, READ_WEAPONS);
            if (!vec.empty())
            {
                eWeaponType weaponId = (eWeaponType)vectorGetRandom(vec);
                const CWeaponInfo* wInfo = CWeaponInfo::GetWeaponInfo(weaponId, 1);

                if (wInfo != NULL && wInfo->m_nModelId1 >= 321)
                {
                    if (isIdValidForWatcher(ped->m_nModelIndex))
                    {
                        weaponWatchers.push_back({ ped, weaponId });
                        return true;
                    };

                    loadModels({ wInfo->m_nModelId1 }, PRIORITY_REQUEST, true);

                    if (originalWeaponId > WEAPON_UNARMED)
                        ped->ClearWeapon(originalWeaponId);
                    else
                        ped->ClearWeapons();

                    ped->GiveWeapon(weaponId, 9999, true);

                    if (originalWeaponId == WEAPON_UNARMED)
                        ped->SetCurrentWeapon((int)wInfo->m_nSlot);

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

        if (dataFile.ReadBoolean(section, "DisableOnMission", false) && CTheScripts::IsPlayerOnAMission())
            continue;

        eWeaponType originalWeapons[13];
        for (int i = 0; i < 13; i++)
            originalWeapons[i] = ped->m_aWeapons[i].m_eWeaponType;

        const int originalSlot = ped->m_nActiveWeaponSlot;
        bool wepChanged = false;
        std::string zoneString = currentZone;
        if (currentInterior[0] != 0)
            zoneString = currentInterior;

        for (int k = 0; k < 2; k++)
            for (int j = 0; j < 4; j++)
            {
                if (wepChanged)
                    break;

                std::string vehId;
                std::string wantedStr;

                if (j < 2)
                {
                    if (IsVehiclePointerValid(ped->m_pVehicle))
                    {
                        auto it = wepVehModels.find(ped->m_pVehicle->m_nModelIndex);
                        if (it != wepVehModels.end())
                            vehId = it->second + "|";
                        else
                            vehId = std::to_string(ped->m_pVehicle->m_nModelIndex) + "|";
                    }
                    else
                        continue;
                }
                if (j == 0 || j == 2)
                {
                    CWanted* wanted = FindPlayerWanted(-1);
                    if (wanted && wanted->m_nWantedLevel > 0)
                        wantedStr = "WANTED" + std::to_string(wanted->m_nWantedLevel) + "|";
                    else
                        continue;
                }

                bool changeZoneWeaponForce = true;
                if ((wepChanged = changeWeapon((k) ? "Global" : section, wantedStr + vehId + "WEAPONFORCE")) == true)
                    changeZoneWeaponForce = rand<bool>();

                if (changeZoneWeaponForce || !mergeWeapons)
                    wepChanged |= changeWeapon((k) ? "Global" : section, wantedStr + vehId + zoneString + "|WEAPONFORCE");


                if (!wepChanged)
                    for (int i = 0; i < 13; i++)
                        if (originalWeapons[i] > 0)
                        {
                            const eWeaponType weaponId = originalWeapons[i];
                            bool changeZoneWeapon = true;
                            bool changeZoneSlot = true;
                            //const int currentSlot = ped->m_nActiveWeaponSlot;

                            if ((wepChanged = changeWeapon((k) ? "Global" : section, wantedStr + vehId + "SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType)) == true)
                                changeZoneSlot = rand<bool>();

                            if (changeZoneSlot || mergeWeapons == 0)
                                wepChanged |= changeWeapon((k) ? "Global" : section, wantedStr + vehId + zoneString + "|SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType);

                            if ((changeWeapon((k) ? "Global" : section, wantedStr + vehId + "WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType) == true) ? (wepChanged = true) : false)
                                changeZoneWeapon = rand<bool>();

                            if (changeZoneWeapon || mergeWeapons == 0)
                                wepChanged |= changeWeapon((k) ? "Global" : section, wantedStr + vehId + zoneString + "|WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType);

                            if (wepChanged)
                                ped->SetCurrentWeapon(originalSlot);
                        }
            }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  LOGGING   ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

void PedWeaponVariations::LogDataFile()
{
    if (GetFileAttributes(dataFileName) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
        Log::Write("\n%s not found!\n\n", dataFileName);
    else
        Log::Write("####################################\n"
                   "## ModelVariations_PedWeapons.ini ##\n"
                   "####################################\n%s\n", fileToString(dataFileName).c_str());
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
    assert(ped != NULL);

    if (ped->m_nCreatedBy != 2 && ped->m_aWeapons[ped->m_nActiveWeaponSlot].m_eWeaponType == WEAPON_UNARMED)
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
            if (wInfo != NULL && wInfo->m_nModelId1 >= 321)
                loadModels({wInfo->m_nModelId1}, PRIORITY_REQUEST, true);
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
int16_t __fastcall CollectParametersHooked(CRunningScript * _this, void*, unsigned __int16 a2)
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

void PedWeaponVariations::InstallHooks()
{
    hookCall(0x5DDB92, CPedHooked<0x5DDB92>, "CPed::CPed"); //CCivilianPed::CCivilianPed
    hookCall(0x5DDC81, CPedHooked<0x5DDC81>, "CPed::CPed"); //CCop::CCop
    hookCall(0x5DE362, CPedHooked<0x5DE362>, "CPed::CPed"); //CEmergencyPed::CEmergencyPed

    hookCall(0x62A12E, GiveWeaponAtStartOfFightHooked<0x62A12E>, "CPed::GiveWeaponAtStartOfFight"); //CTaskSimpleFightingControl::ProcessPed
    hookCall(0x47D335, GiveWeaponHooked<0x47D335>, "CPed::GiveWeapon"); //01B2: GIVE_WEAPON_TO_CHAR
    hookCall(0x47D4AC, CollectParametersHooked<0x47D4AC>, "CRunningScript::CollectParameters"); //01B9: SET_CURRENT_CHAR_WEAPON
    hookCall(0x48AE9E, CollectParametersHooked<0x48AE9E>, "CRunningScript::CollectParameters"); //0491: HAS_CHAR_GOT_WEAPON
}
