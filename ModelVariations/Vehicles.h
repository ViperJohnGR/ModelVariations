#pragma once
#include "commonDef.h"
#include "IniReader/IniReader.h"

#include "CPed.h"
#include "CAutomobile.h"

#include <vector>
#include <unordered_set>

extern CIniReader iniVeh;
//extern BYTE vehNumVariations[212];

extern std::array<std::vector<short>, 16> vehVariations[212];
extern std::array<std::vector<short>, 16> vehWantedVariations[212];
extern std::map<short, std::vector<short>> vehDrivers;
extern std::map<short, std::vector<short>> vehPassengers;

extern std::vector<short> vehCurrentVariations[212];
extern std::map<short, short> vehOriginalModels;
extern std::vector<short> vehCarGenExclude;
extern int loadAllVehicles;

extern int changeCarGenerators;
extern bool isPlayerInTaxi;
extern bool enableSideMissions;
extern int enableAllSideMissions;
extern void(*PreRenderOriginal)(CAutomobile*);
extern void(*ProcessSuspensionOriginal)(CAutomobile*);
extern void(*SetupSuspensionLinesOriginal)(CAutomobile*);
extern void(*DoBurstAndSoftGroundRatiosOriginal)(CAutomobile*);
extern void(*CAutomobileRenderOriginal)(CAutomobile*);
extern void(__fastcall *VehicleDamageOriginal)(CAutomobile*, void*, float, __int16, int, RwV3d*, RwV3d*, signed int);
extern char(__fastcall *BurstTyreOriginal)(CAutomobile*, void*, char, char);
extern int(__fastcall *ProcessEntityCollisionOriginal)(CAutomobile*, void*, CVehicle*, CColPoint*);

extern std::vector<short> iniLineParser(eVariationType type, int section, const char key[12], CIniReader* ini);
extern void vectorUnion(std::vector<short>& vec1, std::vector<short>& vec2, std::vector<short>& dest);
extern bool isGameModelPolice(int model);

void installVehicleHooks();
void readVehicleIni();

void hookTaxi();