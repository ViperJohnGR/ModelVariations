#pragma once

#include <CZone.h>

#include <string>
#include <unordered_map>
#include <vector>

extern std::unordered_map<std::string, std::vector<CZone*>> presetAllZones;
extern int maxPedID;
extern std::atomic<uint64_t> currentZone;

struct pedTimeGroup {
	unsigned short start = 0;
	unsigned short end = 0;
	std::vector<unsigned short> variations;
};

class PedVariations
{
public:
	static void ClearData();
	static void LoadData();
	static void Process();
	static void ProcessDrugDealers(bool reset = false);
	static void UpdateVariations();
	static void DrawDebugInfo();

	//Logging
	static void LogCurrentVariations();
	static void LogDataFile();
	static void LogVariations();

	//Call hooks
	static void InstallHooks(bool enableSpecialPeds);

	static std::vector<unsigned short> GetVariationOriginalModels(const int modelIndex);
};
