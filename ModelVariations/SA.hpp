#pragma once

#include <CDirectory.h>
#include <CLoadedCarGroup.h>
#include <CMenuManager.h>
#include <CPedModelInfo.h>
#include <CStreamingInfo.h>



#define pedsModels reinterpret_cast<CPedModelInfo*>(*reinterpret_cast<unsigned int**>(0x4C6518)+1)
#define pedsModelsCount **reinterpret_cast<unsigned int**>(0x4C6518)

#define FrontEndMenuManager (*reinterpret_cast<CMenuManager**>(0x53C6C0))

#define CAnimManager__ms_numAnimAssocDefinitions (**reinterpret_cast<int**>(0x4D5674))
#define CAnimManager__GetAnimGroupName (reinterpret_cast<char * (__cdecl*)(int)>(injector::GetBranchDestination(0x5B7516).as_int()))

#define CModelInfo__AddPedModel reinterpret_cast<CPedModelInfo * (__cdecl*)(int)>(injector::GetBranchDestination(0x5B74A7).as_int())

#define CPopulation__m_iCarsPerGroup *reinterpret_cast<int*>(0x40ADB8)

#define CPopulation__m_AppropriateLoadedCars (*reinterpret_cast<CLoadedCarGroup**>(0x421383))
#define CPopulation__m_nNumCarsInGroup (*reinterpret_cast<short**>(0x406F48))
#define CPopulation__m_CarGroups (*reinterpret_cast<short**>(0x421948))

#define CStreaming__ms_pExtraObjectsDir (**reinterpret_cast<CDirectory***>(0x409F6C))
#define CStreaming__ms_pedsLoaded (*reinterpret_cast<int **>(0x40A5A0))
#define CStreaming__ms_numPedsLoaded (**reinterpret_cast<int **>(0x40A71F))
#define CStreaming__ms_vehiclesLoaded (*reinterpret_cast<CLoadedCarGroup**>(0x40B997))
#define CStreaming__ms_aInfoForModel (*reinterpret_cast<CStreamingInfo**>(0x5B8AE8))

#define CTheScripts__StreamedScripts reinterpret_cast<void*>(*(uintptr_t*)0x476D51)
#define CExternalScripts__findByScmIndex reinterpret_cast<short(__thiscall*)(void*, short)>(injector::GetBranchDestination(0x476D56).as_int())
#define CTheScripts__ScriptsForBrains reinterpret_cast<void*>(*(uintptr_t*)0x476D7D)
#define CScriptsForBrains__AddNewScriptBrain reinterpret_cast<void(__thiscall*)(void*, short, short, short, char, char, float)>(injector::GetBranchDestination(0x476D86).as_int())

#define CTheZones__PointLiesWithinZone reinterpret_cast<bool(__cdecl*)(void*, void*)>(injector::GetBranchDestination(0x572BCE).as_int())
#define CTheZones__NavigationZoneArray (*reinterpret_cast<CZone**>(0x572BB7))

#define CVehicleModelInfo__CLinkedUpgradeList__FindOtherUpgrade reinterpret_cast<short(__thiscall*)(uint32_t, uint16_t)>(injector::GetBranchDestination(0x4986BB).as_int())
#define CVehicleModelInfo__ms_linkedUpgrades (*reinterpret_cast<unsigned*>(0x4986B7))
