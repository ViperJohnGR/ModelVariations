#pragma once
#include "IniParse.hpp"

#include "CAutomobile.h"
#include "CPed.h"
#include "CVector.h"

#include <set>
#include <vector>
#include <unordered_set>

extern CIniReader iniVeh;

extern std::ofstream logfile;

extern std::array<std::vector<short>, 16> vehVariations[212];
extern std::array<std::vector<short>, 6> vehWantedVariations[212];
extern std::map<short, std::array<std::vector<short>, 6>> vehGroupWantedVariations;
extern std::map<short, short> vehOriginalModels;
extern std::map<short, std::vector<short>> vehDrivers;
extern std::map<short, std::vector<short>> vehPassengers;
extern std::map<short, std::vector<short>> vehDriverGroups[9];
extern std::map<short, std::vector<short>> vehPassengerGroups[9];
extern std::map<short, BYTE> modelNumGroups;
extern std::map<short, std::pair<CVector, float>> LightPositions;

extern std::vector<short> vehCurrentVariations[212];
extern std::vector<short> vehCarGenExclude;
extern std::vector<short> vehInheritExclude;

extern std::set<short> parkedCars;

extern int loadAllVehicles;

extern int enableLog;
extern int changeCarGenerators;
extern bool enableSideMissions;
extern int enableAllSideMissions;
extern int enableLights;
extern int enableSiren;
extern int disablePayAndSpray;
extern int enableSpecialFeatures;
extern int changeScriptedCars;

extern char currentZone[8];

extern std::set<short> vehUseOnlyGroups;

extern void filterWantedVariations(std::vector<short>& vec, std::vector<short>& wantedVec);
extern bool IdExists(std::vector<short>& vec, int id);

void installVehicleHooks();
void readVehicleIni();

void hookTaxi();
