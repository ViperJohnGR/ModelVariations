#pragma once

#include <string>
#include <vector>

extern const std::pair<std::string, unsigned> areas[];
extern bool forceEnableGlobal;
extern char currentZone[8];
extern unsigned int currentTown;
extern std::vector<std::string> zoneNames;

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
