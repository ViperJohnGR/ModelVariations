#pragma once
#include "DataReader.hpp"

#include <CAutomobile.h>
#include <CPed.h>
#include <CVector.h>
#include <CZone.h>

#include <set>
#include <stack>
#include <vector>


extern DataReader iniVeh;
extern const char* vehIniPath;

extern bool loadAllVehicles;

extern char currentZone[8];
extern char lastZone[8];
extern unsigned int currentTown;

extern void filterWantedVariations(std::vector<unsigned short>& vec, std::vector<unsigned short>& wantedVec);

void readVehicleIni(bool firstTime, std::string gamePath);
void clearVehicles();
void updateVehicleVariations(CZone* zInfo);

void addToVehicleStack(CVehicle* veh);
void processVehicleStacks();
void printCurrentVehicleVariations();
void printVehicleVariations();

void hookTaxi();

void installVehicleHooks();
