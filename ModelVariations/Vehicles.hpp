#pragma once

#include <CVehicle.h>

#include <string>
#include <vector>

#include <psapi.h>

extern const std::pair<std::string, unsigned> areas[];
extern bool forceEnable;
extern char currentZone[8];
extern unsigned int currentTown;

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
