#include "PedWeapons.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Log.hpp"

#include <plugin.h>
#include <CModelInfo.h>
#include <CPed.h>
#include <CTheScripts.h>

#include <stack>
#include <string>

static const char* dataFileName = "ModelVariations_PedWeapons.ini";
static DataReader dataFile(dataFileName);

std::unordered_map<unsigned short, std::string> wepPedModels;
std::unordered_map<unsigned short, std::string> wepVehModels;

std::stack<CPed*> pedWepStack;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PedWeaponVariations::AddToStack(CPed* ped)
{
    pedWepStack.push(ped);
}

void PedWeaponVariations::ClearData()
{
    wepPedModels.clear();
    wepVehModels.clear();

    dataFile.data.clear();
}

void PedWeaponVariations::LoadData()
{
    dataFile.SetIniPath(dataFileName);

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

        std::string section = wepPedModels.contains(ped->m_nModelIndex) ? wepPedModels[ped->m_nModelIndex] : std::to_string(ped->m_nModelIndex);
        const bool mergeWeapons = dataFile.ReadBoolean(section, "MergeZonesWithGlobal", false);

        if (dataFile.ReadBoolean(section, "DisableOnMission", false) && CTheScripts::IsPlayerOnAMission())
            continue;

        eWeaponType originalWeapons[13];
        for (int i = 0; i < 13; i++)
            originalWeapons[i] = ped->m_aWeapons[i].m_eWeaponType;

        const int originalSlot = ped->m_nActiveWeaponSlot;
        bool wepChanged = false;

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
                    wepChanged |= changeWeapon((k) ? "Global" : section, wantedStr + vehId + currentZone + "|WEAPONFORCE");


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
                                wepChanged |= changeWeapon((k) ? "Global" : section, wantedStr + vehId + currentZone + "|SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType);

                            if ((changeWeapon((k) ? "Global" : section, wantedStr + vehId + "WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType) == true) ? (wepChanged = true) : false)
                                changeZoneWeapon = rand<bool>();

                            if (changeZoneWeapon || mergeWeapons == 0)
                                wepChanged |= changeWeapon((k) ? "Global" : section, wantedStr + vehId + currentZone + "|WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType);

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
