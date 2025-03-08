#pragma once

#include <CTheZones.h>

#include <string>
#include <vector>

extern std::unordered_map<std::string, std::vector<CZone*>> presetAllZones;
extern bool forceEnableGlobal;
extern char currentZone[8];

class VehicleVariations
{
public:
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
