#include "PedWeapons.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "LogUtil.hpp"

#include <CModelInfo.h>
#include <CPed.h>
#include <CTheScripts.h>

#include <map>
#include <stack>
#include <string>
#include <shlwapi.h>

static const char* dataFileName = "ModelVariations_PedWeapons.ini";
static DataReader dataFile(dataFileName);

std::map<unsigned short, std::string> wepPedModels;
std::map<unsigned short, std::string> wepVehModels;

std::stack<CPed*> pedWepStack;

bool isOnMission()
{
    return (CTheScripts::OnAMissionFlag && *(CTheScripts::ScriptSpace + CTheScripts::OnAMissionFlag));
}

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
    dataFile.SetIniPath(dataFile.GetIniPath());

    if (logfile.is_open())
        logfile << "\nPed weapon sections detected:\n";

    for (auto& iniData : dataFile.data)
    {
        if (logfile.is_open())
            logfile << std::dec << iniData.first << "\n";

        int modelid = 0;
        std::string section = iniData.first;

        if (!(section[0] >= '0' && section[0] <= '9'))
            CModelInfo::GetModelInfo(const_cast<char*>(section.c_str()), &modelid);
        if (modelid > 0)
            wepPedModels.insert({ modelid, section });

        for (auto& keys : iniData.second)
        {
            std::string name = keys.first.substr(0, keys.first.find("_"));
            if (CModelInfo::GetModelInfo(const_cast<char*>(name.c_str()), &modelid))
                wepVehModels.insert({ modelid, name });
        }
    }

    if (logfile.is_open())
        logfile << std::endl;
}

void PedWeaponVariations::Process()
{
    while (!pedWepStack.empty())
    {
        CPed* ped = pedWepStack.top();
        pedWepStack.pop();

        if (!IsPedPointerValid(ped))
            continue;
        
        const auto changeWeapon = [ped](const std::string section, const std::string key, eWeaponType originalWeaponId = WEAPON_UNARMED) -> bool
        {
            std::vector<unsigned short> vec = dataFile.ReadLine(section, key, READ_WEAPONS);
            if (!vec.empty())
            {
                eWeaponType weaponId = (eWeaponType)vectorGetRandom(vec);
                const CWeaponInfo* wInfo = CWeaponInfo::GetWeaponInfo(weaponId, 1);

                if (wInfo != NULL && wInfo->m_nModelId1 >= 321)
                {
                    loadModels({ wInfo->m_nModelId1 }, GAME_REQUIRED, true);

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

        std::string section = std::to_string(ped->m_nModelIndex);
        auto wepModel = wepPedModels.find(ped->m_nModelIndex);
        if (wepModel != wepPedModels.end())
            section = wepModel->second;
        const auto mergeWeapons = dataFile.ReadBoolean(section, "MergeZonesWithGlobal", false);
        const auto disableOnMission = dataFile.ReadBoolean(section, "DisableOnMission", false) & isOnMission();
        std::string vehId;

        for (int j = 0; j < 2; j++)
        {
            bool wepChanged = false;

            if (j == 1)
            {
                if (IsVehiclePointerValid(ped->m_pVehicle))
                {
                    auto it = wepVehModels.find(ped->m_pVehicle->m_nModelIndex);
                    if (it != wepVehModels.end())
                        vehId = it->second + "_";
                    else
                        vehId = std::to_string(ped->m_pVehicle->m_nModelIndex) + "_";
                }
                else
                    break;
            }

            if (!disableOnMission)
            {
                bool changeZoneWeapon = true;
                if ((wepChanged = changeWeapon(section, vehId + "WEAPONFORCE")) == true)
                    changeZoneWeapon = rand<bool>();

                if ((changeZoneWeapon || mergeWeapons == 0))
                    wepChanged |= changeWeapon(section, vehId + currentZone + "_WEAPONFORCE");
            }

            if (!wepChanged && !disableOnMission)
                for (int i = 0; i < 13; i++)
                    if (ped->m_aWeapons[i].m_eWeaponType > 0)
                    {
                        const eWeaponType weaponId = ped->m_aWeapons[i].m_eWeaponType;
                        bool changeZoneWeapon = true;
                        bool changeZoneSlot = true;
                        const int currentSlot = ped->m_nActiveWeaponSlot;

                        if ((wepChanged = changeWeapon(section, vehId + "SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType)) == true)
                            changeZoneSlot = rand<bool>();

                        if (changeZoneSlot || mergeWeapons == 0)
                            wepChanged |= changeWeapon(section, vehId + currentZone + "_SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType);

                        if ((changeWeapon(section, vehId + "WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType) == true) ? (wepChanged = true) : false)
                            changeZoneWeapon = rand<bool>();

                        if (changeZoneWeapon || mergeWeapons == 0)
                            wepChanged |= changeWeapon(section, vehId + currentZone + "_WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType);

                        if (wepChanged)
                            ped->SetCurrentWeapon(currentSlot);
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
        logfile << "\n" << PathFindFileName(dataFileName) << " not found!\n" << std::endl;
    else
        logfile << "####################################\n"
                   "## ModelVariations_PedWeapons.ini ##\n"
                   "####################################\n" << fileToString(dataFileName) << std::endl;
}
