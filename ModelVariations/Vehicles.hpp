#pragma once
#include "DataReader.hpp"

#include <CAutomobile.h>
#include <CPed.h>
#include <CVector.h>

#include <set>
#include <stack>
#include <vector>
#include <unordered_set>


struct rgba
{
    BYTE r;
    BYTE g;
    BYTE b;
    BYTE a;
};

extern int exeVersion;

extern DataReader iniVeh;

extern std::ofstream logfile;

extern std::array<std::vector<unsigned short>, 16> vehVariations[212];
extern std::array<std::vector<unsigned short>, 6> vehWantedVariations[212];
extern std::map<unsigned short, std::array<std::vector<unsigned short>, 16>> vehGroups;
extern std::map<unsigned short, std::array<std::vector<unsigned short>, 16>> vehTuning;
extern std::map<unsigned short, std::array<std::vector<unsigned short>, 6>> vehGroupWantedVariations;
extern std::map<unsigned short, unsigned short> vehOriginalModels;
extern std::map<unsigned short, std::vector<unsigned short>> vehDrivers;
extern std::map<unsigned short, std::vector<unsigned short>> vehPassengers;
extern std::map<unsigned short, std::vector<unsigned short>> vehDriverGroups[9];
extern std::map<unsigned short, std::vector<unsigned short>> vehPassengerGroups[9];
extern std::map<unsigned short, BYTE> modelNumGroups;
extern std::map<unsigned short, std::pair<CVector, float>> LightPositions;
extern std::map<unsigned short, rgba> LightColors;
extern std::map<unsigned short, rgba> LightColors2;
extern std::map<unsigned short, std::vector<unsigned short>> vehCurrentTuning;
extern std::map<unsigned short, std::string> vehModels;
extern std::map<unsigned short, BYTE> tuningRarities;

extern std::vector<unsigned short> vehCurrentVariations[212];
extern std::vector<unsigned short> vehCarGenExclude;
extern std::vector<unsigned short> vehInheritExclude;

extern std::set<unsigned short> parkedCars;
extern std::set<unsigned short> vehHasVariations;

extern std::stack<std::pair<CVehicle*, std::array<std::vector<unsigned short>, 17>>> tuningStack;

extern int loadAllVehicles;

extern int enableLog;
extern int changeCarGenerators;
extern int enableSideMissions;
extern int enableAllSideMissions;
extern int enableLights;
extern int enableSiren;
extern int disablePayAndSpray;
extern int enableSpecialFeatures;
extern int changeScriptedCars;

extern char currentZone[8];
extern BYTE currentTown;
extern std::set<unsigned short> vehMergeZones;

extern std::set<unsigned short> vehUseOnlyGroups;

extern void filterWantedVariations(std::vector<unsigned short>& vec, std::vector<unsigned short>& wantedVec);
extern bool IdExists(std::vector<unsigned short>& vec, int id);

void installVehicleHooks();
void readVehicleIni(bool firstTime, std::string gamePath);

void hookTaxi();
