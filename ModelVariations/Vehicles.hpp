#pragma once

#include <CVehicle.h>

#include <set>
#include <string>

extern char currentZone[8];
extern unsigned int currentTown;
extern std::set<std::pair<std::uintptr_t, std::string>> modulesSet;

class VehicleVariations
{
public:
	static void AddToStack(CVehicle * veh);
	static void ClearData();
	static void LoadData(std::string gamePath);
	static void Process();
	static void UpdateVariations();

	//Logging
	static void LogCurrentVariations();
	static void LogDataFile();
	static void LogVariations();

	//Call hooks
	static void HookTaxi();
	static void InstallHooks();
};
