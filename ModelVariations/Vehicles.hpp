#pragma once
#include "IniParse.hpp"

#include "CAutomobile.h"
#include "CPed.h"
#include "CVector.h"

#include <vector>
#include <unordered_set>

extern CIniReader iniVeh;
//extern BYTE vehNumVariations[212];

extern std::ofstream logfile;

extern std::array<std::vector<short>, 16> vehVariations[212];
extern std::array<std::vector<short>, 16> vehWantedVariations[212];
extern std::map<short, std::vector<short>> vehDrivers;
extern std::map<short, std::vector<short>> vehPassengers;
extern std::map<short, std::vector<short>> vehDriverGroups[9];
extern std::map<short, std::vector<short>> vehPassengerGroups[9];
extern std::map<short, BYTE> modelNumGroups;
extern std::map<int, CVector> LightPositions;

extern std::vector<short> vehCurrentVariations[212];
extern std::map<short, short> vehOriginalModels;
extern std::vector<short> vehCarGenExclude;
extern int loadAllVehicles;

extern int enableLog;
extern int changeCarGenerators;
extern bool enableSideMissions;
extern int enableAllSideMissions;
extern int enableLights;

extern void(__fastcall *ProcessControlOriginal)(CAutomobile*);
extern void(__fastcall *PreRenderOriginal)(CAutomobile*);
/*
extern void(*ProcessSuspensionOriginal)(CAutomobile*);
extern void(*SetupSuspensionLinesOriginal)(CAutomobile*);
extern void(*DoBurstAndSoftGroundRatiosOriginal)(CAutomobile*);
extern void(*CAutomobileRenderOriginal)(CAutomobile*);
extern void(__fastcall *VehicleDamageOriginal)(CAutomobile*, void*, float, __int16, int, RwV3d*, RwV3d*, signed int);
extern char(__fastcall *BurstTyreOriginal)(CAutomobile*, void*, char, char);
extern int(__fastcall *ProcessEntityCollisionOriginal)(CAutomobile*, void*, CVehicle*, CColPoint*);
*/

extern void vectorUnion(std::vector<short>& vec1, std::vector<short>& vec2, std::vector<short>& dest);
extern bool isGameModelPolice(int model);

void installVehicleHooks();
void readVehicleIni();

void hookTaxi();