#pragma once
#include "commonDef.h"
#include "IniReader/IniReader.h"

#include "CPed.h"

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

extern bool isPlayerInTaxi;
extern bool enableSideMissions;

extern std::vector<short> iniLineParser(eVariationType type, int section, const char key[12], CIniReader* ini);
extern void vectorUnion(std::vector<short>& vec1, std::vector<short>& vec2, std::vector<short>& dest);

void installVehicleHooks();
void readVehicleIni();

void hookTaxi();