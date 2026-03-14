#pragma once

#include <CTheZones.h>

#include <string>
#include <unordered_map>
#include <vector>

extern std::unordered_map<std::string, std::vector<CZone*>> presetAllZones;
extern bool forceEnableGlobal;
extern std::atomic<uint64_t> currentZone;

struct vehTimeGroup {
	unsigned short start = 0;
	unsigned short end = 0;
	std::vector<unsigned short> occupantGroups;
	std::vector<unsigned short> trailers;
	std::vector<unsigned short> variations;
};

class VehicleVariations
{
public:
	static void ClearData();
	static void LoadData();
	static void Process();
	static void UpdateVariations();
	static void DrawDebugInfo();

	//Logging
	static void LogCurrentVariations();
	static void LogDataFile();
	static void LogVariations();

	//Call hooks
	static void InstallHooks();
};
