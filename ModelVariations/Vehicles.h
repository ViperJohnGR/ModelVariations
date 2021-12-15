#pragma once
#include <vector>
#include "IniReader/IniReader.h"
#include "commonDef.h"

extern CIniReader iniVeh;
//extern BYTE vehNumVariations[212];

extern std::array<std::vector<short>, 6> vehVariations[212];
extern std::array<std::vector<short>, 6> vehWantedVariations[212];

extern std::vector<short> currentVehVariations[212];

extern std::vector<short> copModels;
extern std::vector<short> copBikeModels;
extern std::vector<short> swatModels;
extern std::vector<short> fbiModels;
extern std::vector<short> tankModels;
extern std::vector<short> barracksModels;
extern std::vector<short> patriotModels;
extern std::vector<short> heliModels;
extern std::vector<short> predatorModels;
extern std::vector<short> ambulanceModels;
extern std::vector<short> firetruckModels;
extern std::vector<short> taxiModels;
extern std::vector<short> pimpModels;
extern std::vector<short> burglarModels;
extern std::vector<short> trainModels;

extern bool isPlayerInTaxi;
extern bool enableSideMissions;

extern std::vector<short> iniLineParser(eVariationType type, int section, const char key[12], CIniReader* ini);

void installVehicleHooks();
void readVehicleIni();

void updateVehicles(int currentTown);


void hookTaxi();