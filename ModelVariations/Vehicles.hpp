#pragma once

#include <CVehicle.h>

#include <set>
#include <string>
#include <vector>

#include <psapi.h>

extern bool forceEnable;
extern char currentZone[8];
extern unsigned int currentTown;
extern std::vector<std::pair<std::string, MODULEINFO>> loadedModules;

class VehicleVariations
{
public:
	static void AddToStack(CVehicle * veh);
	static void ClearData();
	static void LoadData();
	static void Process();
	static void UpdateVariations();

	//Logging
	static void LogCurrentVariations();
	static void LogDataFile();
	static void LogVariations();

	//Call hooks
	static void InstallHooks();
};
