#pragma once

#include "Memory.hpp"

#include <CDirectory.h>
#include <CLoadedCarGroup.h>
#include <CMenuManager.h>
#include <CPedModelInfo.h>
#include <CPhysical.h>
#include <CPopulation.h>
#include <CRunningScript.h>
#include <CStreamingInfo.h>
#include <CWorld.h>


template <typename Ret, std::uintptr_t address, std::uintptr_t fallback, typename C, typename... Args>
constexpr Ret getDynamicMethod(C _this, Args... args)
{
    auto branchDestination = injector::GetBranchDestination(address).as_int();

    if (isAddressValid(branchDestination))
        return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(branchDestination)(_this, args...);

    Log::LogModifiedAddress(address, "Modified function call detected: 0x%08X is %s using fallback 0x%08X\n", address, bytesToString(address, 5).c_str(), fallback);
    return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(fallback)(_this, args...);
}

template <typename Ret, std::uintptr_t address, std::uintptr_t fallback, typename... Args>
constexpr Ret getDynamicFunction(Args... args)
{
    auto branchDestination = injector::GetBranchDestination(address).as_int();

    if (isAddressValid(branchDestination))
        return reinterpret_cast<Ret(__cdecl*)(Args...)>(branchDestination)(args...);

    Log::LogModifiedAddress(address, "Modified function call detected: 0x%08X is %s using fallback 0x%08X\n", address, bytesToString(address, 5).c_str(), fallback);
    return reinterpret_cast<Ret(__cdecl*)(Args...)>(fallback)(args...);
}

#define pedsModels reinterpret_cast<CPedModelInfo*>(getPointerFromAddress<unsigned int>(0x4C6518, 0xB478F8)+1)
#define pedsModelsCount (*getPointerFromAddress<unsigned int>(0x4C6518, 0xB478F8))

#define FrontEndMenuManager getPointerFromAddress<CMenuManager>(0x53C6C0, 0xBA6748)

#define CAnimManager__ms_numAnimAssocDefinitions (*getPointerFromAddress<int>(0x4D5674, 0xB4EA28))
inline char* CAnimManager__GetAnimGroupName(int index) { return getDynamicFunction<char*, 0x5B7516, 0x4D3A20>(index); }

inline short CExternalScripts__findByScmIndex(void* _this, short scmIndex) { return getDynamicMethod<short, 0x476D56, 0x470810>(_this, scmIndex); }

inline CPedModelInfo* CModelInfo__AddPedModel(int id) { return getDynamicFunction<CPedModelInfo*, 0x5B74A7, 0x4C67A0>(id); }

inline bool CPhysical__TestCollision(CPhysical* _this, bool applySpeed) { return reinterpret_cast<bool(__thiscall*)(CPhysical*, bool)>(0x54DEC0)(_this, applySpeed); }

#define CPopulation__m_iCarsPerGroup *reinterpret_cast<int*>(0x40ADB8)

#define CPopulation__m_AppropriateLoadedCars getPointerFromAddress<CLoadedCarGroup>(0x421383, 0xC0E9F8)
#define CPopulation__m_nNumCarsInGroup getPointerFromAddress<short>(0x406F48, (short*)CPopulation::m_nNumCarsInGroup)
#define CPopulation__m_CarGroups getPointerFromAddress<short>(0x421948, 0xC0ED38)

//#define CStreaming__ms_aInfoForModel getPointerFromAddress<CStreamingInfo>(0x408ADD, 0x8E4CC0)
#define CStreaming__ms_pExtraObjectsDir getPointerFromAddress<CDirectory>(0x409F6C, 0x8E48D0, 2)
#define CStreaming__ms_pedsLoaded getPointerFromAddress<int>(0x40A5A0, 0x8E4C00)
#define CStreaming__ms_memoryAvailable (*getPointerFromAddress<uint32_t>(0x40E146, 0x8A5A80))
#define CStreaming__ms_memoryUsed (*getPointerFromAddress<uint32_t>(0x408ACA, 0x8E4CB4))
#define CStreaming__ms_numPedsLoaded (*getPointerFromAddress<int>(0x40A71F, 0x8E4BB0))
#define CStreaming__ms_vehiclesLoaded getPointerFromAddress<CLoadedCarGroup>(0x40B997, 0x8E4C24)
inline void CStreaming__LoadAllRequestedModels(bool bOnlyPriorityRequests) { getDynamicFunction<void, 0x49B421, 0x40EA10>(bOnlyPriorityRequests); }
inline void CStreaming__RequestModel(int model, int flags) { getDynamicFunction<void, 0x40A612, 0x4087E0>(model, flags); }
inline void CStreaming__RequestSpecialModel(int slot, const char* name, int flags) { getDynamicFunction<void, 0x40B45E, 0x409D10>(slot, name, flags); }
inline void CStreaming__RequestVehicleUpgrade(int model, int flags) { getDynamicFunction<void, 0x447E83, 0x408C70>(model, flags); }
inline void CStreaming__SetMissionDoesntRequireModel(int model) { getDynamicFunction<void, 0x40B49D, 0x409C90>(model); }

inline bool CTheScripts__IsPlayerOnAMission() { return getDynamicFunction<bool, 0x571582, 0x464D50>(); }
inline void CTheScripts__RemoveThisPed(void* ped) { getDynamicFunction<void, 0x409DE2, 0x486240>(ped); }
#define CTheScripts__pActiveScripts getPointerFromAddress<CRunningScript>(0x468D76, 0xA8B42C)
#define CTheScripts__ScriptsForBrains getPointerFromAddress<std::uintptr_t>(0x476D7D, 0xA90CF0)
#define CTheScripts__StreamedScripts getPointerFromAddress<std::uintptr_t>(0x476D51, 0xA47B60)

inline void CScriptsForBrains__AddNewScriptBrain(void* _this, short index, short model, short chanceOfInit, char attachType, char type, float radius) { getDynamicMethod<void, 0x476D86, 0x46A930>(_this, index, model, chanceOfInit, attachType, type, radius); }

inline bool CTheZones__PointLiesWithinZone(void* point, void* zone) { return getDynamicFunction<bool, 0x572BCE, 0x572270>(point, zone); }
#define CTheZones__NavigationZoneArray getPointerFromAddress<unsigned char>(0x572BB7, 0xBA3798)

inline bool CWeather__IsRainy() { return getDynamicFunction<bool, 0x4AF5A1, 0x4ABF50>(); }

inline short CVehicleModelInfo__CLinkedUpgradeList__FindOtherUpgrade(uint32_t* _this, uint16_t a2) { return getDynamicMethod<short, 0x4986BB, 0x4C74D0>(_this, a2); }
#define CVehicleModelInfo__CVehicleStructure__m_pInfoPool getPointerFromAddress<CPool<CVehicleModelInfo::CVehicleStructure>>(0x5B8FF9, 0xB4E680, 2)
#define CVehicleModelInfo__ms_linkedUpgrades getPointerFromAddress<uint32_t>(0x4986B7, 0xB4E6D8)

#define ScriptParams (reinterpret_cast<int*>(0xA43C78))


inline std::string getLoadStateString(unsigned char loadState)
{
    switch (loadState)
    {
    case LOADSTATE_NOT_LOADED: return "LOADSTATE_NOT_LOADED";
    case LOADSTATE_LOADED: return "LOADSTATE_LOADED";
    case LOADSTATE_Requested: return "LOADSTATE_REQUESTED";
    case LOADSTATE_Channeled: return "LOADSTATE_CHANNELED";
    case LOADSTATE_Finishing: return "LOADSTATE_FINISHING";
    };

    return std::to_string(loadState);
}

inline unsigned char loadModel(int model, int streamingFlags, bool loadImmediately)
{
    if (model < 1)
        return false;

    unsigned short modelIndex = static_cast<unsigned short>(model);

    CStreaming__RequestModel(model, streamingFlags);

    if (loadImmediately)
        CStreaming__LoadAllRequestedModels(false);

    return CStreamingInfo::ms_pArrayBase[modelIndex].m_nLoadState;
}

inline void destroyPed(CPed* ped)
{
    if (!IsPedPointerValid(ped))
        return;

    if (ped->m_pIntelligence)
        ped->m_pIntelligence->FlushImmediately(false);

    if (ped->m_nCreatedBy == 2)
        CTheScripts__RemoveThisPed(ped);
    else
        CPopulation::RemovePed(ped);
}

inline void destroyVehicleAndOccupants(CVehicle* veh)
{
    if (!IsVehiclePointerValid(veh))
        return;

    if (IsPedPointerValid(veh->m_pDriver))
        destroyPed(veh->m_pDriver);

    for (int i = 0; i < 8; i++)
        if (IsPedPointerValid(veh->m_apPassengers[i]))
            destroyPed(veh->m_apPassengers[i]);

    CWorld::Remove(veh);
    delete veh;
}