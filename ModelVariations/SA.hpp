#pragma once

#include <CDirectory.h>
#include <CLoadedCarGroup.h>
#include <CMenuManager.h>
#include <CPedModelInfo.h>
#include <CStreamingInfo.h>
#include <CZone.h>

template <typename Ret, std::uintptr_t address, typename C, typename... Args>
constexpr Ret getDynamicMethod(C _this, Args... args)
{
	return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(injector::GetBranchDestination(address).as_int())(_this, args...);
}

template <typename Ret, std::uintptr_t address, typename... Args>
constexpr Ret getDynamicFunction(Args... args)
{
	return reinterpret_cast<Ret(__cdecl*)(Args...)>(injector::GetBranchDestination(address).as_int())(args...);
}

#define pedsModels reinterpret_cast<CPedModelInfo*>(*reinterpret_cast<unsigned int**>(0x4C6518)+1)
#define pedsModelsCount **reinterpret_cast<unsigned int**>(0x4C6518)

#define FrontEndMenuManager (*reinterpret_cast<CMenuManager**>(0x53C6C0))

#define CAnimManager__ms_numAnimAssocDefinitions (**reinterpret_cast<int**>(0x4D5674))
inline char* CAnimManager__GetAnimGroupName(int index) { return getDynamicFunction<char*, 0x5B7516>(index); }

inline short CExternalScripts__findByScmIndex(void* _this, short scmIndex) { return getDynamicMethod<short, 0x476D56>(_this, scmIndex); }

inline CPedModelInfo* CModelInfo__AddPedModel(int id) { return getDynamicFunction<CPedModelInfo*, 0x5B74A7>(id); }

#define CPopulation__m_iCarsPerGroup *reinterpret_cast<int*>(0x40ADB8)

#define CPopulation__m_AppropriateLoadedCars (*reinterpret_cast<CLoadedCarGroup**>(0x421383))
#define CPopulation__m_nNumCarsInGroup (*reinterpret_cast<short**>(0x406F48))
#define CPopulation__m_CarGroups (*reinterpret_cast<short**>(0x421948))

#define CStreaming__ms_pExtraObjectsDir (**reinterpret_cast<CDirectory***>(0x409F6C))
#define CStreaming__ms_pedsLoaded (*reinterpret_cast<int**>(0x40A5A0))
#define CStreaming__ms_memoryAvailable (**reinterpret_cast<uint32_t**>(0x40E146))
#define CStreaming__ms_memoryUsed (**reinterpret_cast<uint32_t**>(0x408ACA))
#define CStreaming__ms_numPedsLoaded (**reinterpret_cast<int**>(0x40A71F))
#define CStreaming__ms_vehiclesLoaded (*reinterpret_cast<CLoadedCarGroup**>(0x40B997))
#define CStreaming__ms_aInfoForModel (*reinterpret_cast<CStreamingInfo**>(0x5B8AE8))
inline void CStreaming__LoadAllRequestedModels(bool bOnlyPriorityRequests) { getDynamicFunction<void, 0x49B421>(bOnlyPriorityRequests); }
inline void CStreaming__RequestModel(int model, int flags) { getDynamicFunction<void, 0x40A612>(model, flags); }
inline void CStreaming__RequestSpecialModel(int slot, char* name, int flags) { getDynamicFunction<void, 0x40B45E>(slot, name, flags); }
inline void CStreaming__RequestVehicleUpgrade(int model, int flags) { getDynamicFunction<void, 0x447E83>(model, flags); }
inline void CStreaming__SetMissionDoesntRequireModel(int model) { getDynamicFunction<void, 0x40B49D>(model); }

inline bool CTheScripts__IsPlayerOnAMission() { return getDynamicFunction<bool, 0x571582>(); }
inline void CTheScripts__RemoveThisPed(void* ped) { getDynamicFunction<void, 0x409DE2>(ped); }
#define CTheScripts__ScriptsForBrains reinterpret_cast<void*>(*(uintptr_t*)0x476D7D)
#define CTheScripts__StreamedScripts reinterpret_cast<void*>(*(uintptr_t*)0x476D51)

inline void CScriptsForBrains__AddNewScriptBrain(void* _this, short index, short model, short chanceOfInit, char attachType, char type, float radius) { getDynamicMethod<void, 0x476D86>(_this, index, model, chanceOfInit, attachType, type, radius); }

inline bool CTheZones__PointLiesWithinZone(void* point, void* zone) { return getDynamicFunction<bool, 0x572BCE>(point, zone); }
#define CTheZones__NavigationZoneArray (*reinterpret_cast<unsigned char**>(0x572BB7))

inline short CVehicleModelInfo__CLinkedUpgradeList__FindOtherUpgrade(uint32_t _this, uint16_t a2) { return getDynamicMethod<short, 0x4986BB>(_this, a2); }
#define CVehicleModelInfo__CVehicleStructure__m_pInfoPool (**reinterpret_cast<CPool<CVehicleModelInfo::CVehicleStructure>***>(0x5B8FF9))
#define CVehicleModelInfo__ms_linkedUpgrades (*reinterpret_cast<unsigned*>(0x4986B7))

#define ScriptParams (*reinterpret_cast<int**>(0x46408A))
