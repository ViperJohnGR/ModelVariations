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

extern bool loadAllVehicles;

extern char currentZone[8];
extern unsigned int currentTown;

extern void filterWantedVariations(std::vector<unsigned short>& vec, std::vector<unsigned short>& wantedVec);

void readVehicleIni(bool firstTime, const char* iniPath, std::string gamePath);
void clearVehicles();
void updateVehicleVariations();

void addToVehicleStack(CVehicle* veh);
void processVehicleStacks();
void logCurrentVehicleVariations();
void logVehicleVariations();

void hookTaxi();

void installVehicleHooks();
