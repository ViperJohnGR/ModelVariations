#pragma once

#include <CDirectory.h>
#include <CLoadedCarGroup.h>
#include <CPedModelInfo.h>



#define pedsModels reinterpret_cast<CPedModelInfo*>(*reinterpret_cast<unsigned int**>(0x4C6518)+1)
#define pedsModelsCount **reinterpret_cast<unsigned int**>(0x4C6518)

#define CModelInfo__AddPedModel reinterpret_cast<CPedModelInfo * (__cdecl*)(int)>(injector::GetBranchDestination(0x5B74A7).as_int())

#define CStreaming__ms_pExtraObjectsDir (**reinterpret_cast<CDirectory***>(0x409F6C))
#define CStreaming__ms_pedsLoaded (*reinterpret_cast<int **>(0x40A5A0))
#define CStreaming__ms_numPedsLoaded (**reinterpret_cast<int **>(0x40A71F))
#define CStreaming__ms_vehiclesLoaded (*reinterpret_cast<CLoadedCarGroup**>(0x40B997))

#define CTheScripts__StreamedScripts reinterpret_cast<void*>(*(uintptr_t*)0x476D51)
#define CExternalScripts__findByScmIndex reinterpret_cast<short(__thiscall*)(void*, short)>(injector::GetBranchDestination(0x476D56).as_int())
#define CTheScripts__ScriptsForBrains reinterpret_cast<void*>(*(uintptr_t*)0x476D7D)
#define CScriptsForBrains__AddNewScriptBrain reinterpret_cast<void(__thiscall*)(void*, short, short, short, char, char, float)>(injector::GetBranchDestination(0x476D86).as_int())

#define CTheZones__NavigationZoneArray (*reinterpret_cast<CZone**>(0x572BB6 + 1))

#define CVehicleModelInfo__CLinkedUpgradeList__FindOtherUpgrade reinterpret_cast<short(__thiscall*)(uint32_t, uint16_t)>(injector::GetBranchDestination(0x4986BB).as_int())
#define CVehicleModelInfo__ms_linkedUpgrades (*reinterpret_cast<unsigned*>(0x4986B7))
