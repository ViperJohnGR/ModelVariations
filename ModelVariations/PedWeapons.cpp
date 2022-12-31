#include "PedWeapons.hpp"
#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "LogUtil.hpp"

#include <plugin.h>
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
            CModelInfo::GetModelInfo(section.data(), &modelid);
        if (modelid > 0)
            wepPedModels.insert({ (unsigned short)modelid, section });

        for (auto& keys : iniData.second)
        {
            std::string name;
            if (keys.first.starts_with("WANTED"))
            {
                auto modelNameStart = keys.first.find("_") + 1;
                name = keys.first.substr(modelNameStart, keys.first.find("_", modelNameStart) - modelNameStart);
            }
            else
                name = keys.first.substr(0, keys.first.find("_"));
            if (CModelInfo::GetModelInfo(name.data(), &modelid))
                wepVehModels.insert({ (unsigned short)modelid, name });
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
        const bool mergeWeapons = dataFile.ReadBoolean(section, "MergeZonesWithGlobal", false);

        if (dataFile.ReadBoolean(section, "DisableOnMission", false) & isOnMission())
            continue;

        eWeaponType originalWeapons[13];
        for (int i = 0; i < 13; i++)
            originalWeapons[i] = ped->m_aWeapons[i].m_eWeaponType;

        for (int j = 0; j < 4; j++)
        {
            std::string vehId;
            std::string wantedStr;
            bool wepChanged = false;

            if (j == 1 || j == 3)
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
                    continue;
            }
            if (j > 1)
            {
                CWanted* wanted = FindPlayerWanted(-1);
                if (wanted && wanted->m_nWantedLevel > 0)
                    wantedStr = "WANTED" + std::to_string(wanted->m_nWantedLevel) + "_";
                else
                    break;
            }

            bool changeZoneWeaponForce = true;
            if ((wepChanged = changeWeapon(section, wantedStr + vehId + "WEAPONFORCE")) == true)
                changeZoneWeaponForce = rand<bool>();

            if (changeZoneWeaponForce || !mergeWeapons)
                wepChanged |= changeWeapon(section, wantedStr + vehId + currentZone + "_WEAPONFORCE");


            if (!wepChanged)
                for (int i = 0; i < 13; i++)
                    if (originalWeapons[i] > 0)
                    {
                        const eWeaponType weaponId = originalWeapons[i];
                        bool changeZoneWeapon = true;
                        bool changeZoneSlot = true;
                        const int currentSlot = ped->m_nActiveWeaponSlot;

                        if ((wepChanged = changeWeapon(section, wantedStr + vehId + "SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType)) == true)
                            changeZoneSlot = rand<bool>();

                        if (changeZoneSlot || mergeWeapons == 0)
                            wepChanged |= changeWeapon(section, wantedStr + vehId + currentZone + "_SLOT" + std::to_string(i), ped->m_aWeapons[i].m_eWeaponType);

                        if ((changeWeapon(section, wantedStr + vehId + "WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType) == true) ? (wepChanged = true) : false)
                            changeZoneWeapon = rand<bool>();

                        if (changeZoneWeapon || mergeWeapons == 0)
                            wepChanged |= changeWeapon(section, wantedStr + vehId + currentZone + "_WEAPON" + std::to_string(weaponId), ped->m_aWeapons[i].m_eWeaponType);

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