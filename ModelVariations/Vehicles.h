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

extern std::vector<short> currentVehVariations[212];

extern std::unordered_set<short> copModels;
extern std::unordered_set<short> copBikeModels;
extern std::unordered_set<short> swatModels;
extern std::unordered_set<short> fbiModels;
extern std::unordered_set<short> tankModels;
extern std::unordered_set<short> barracksModels;
extern std::unordered_set<short> patriotModels;
extern std::unordered_set<short> heliModels;
extern std::unordered_set<short> predatorModels;
extern std::unordered_set<short> ambulanceModels;
extern std::unordered_set<short> firetruckModels;
extern std::unordered_set<short> taxiModels;
extern std::unordered_set<short> pimpModels;
extern std::unordered_set<short> burglarModels;
extern std::unordered_set<short> trainModels;

extern bool isPlayerInTaxi;
extern bool enableSideMissions;

extern std::vector<short> iniLineParser(eVariationType type, int section, const char key[12], CIniReader* ini);
extern void vectorUnion(std::vector<short>& vec1, std::vector<short>& vec2, std::vector<short>& dest);

void installVehicleHooks();
void readVehicleIni();

void hookTaxi();