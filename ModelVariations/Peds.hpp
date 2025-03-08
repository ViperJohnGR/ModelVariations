#pragma once

#include <CPed.h>
#include <CZone.h>

extern std::unordered_map<std::string, std::vector<CZone*>> presetAllZones;
extern int maxPedID;
extern char currentZone[8];
extern const char* currentInterior;

class PedVariations
{
public:
	static void ClearData();
	static void LoadData();
	static void Process();
	static void ProcessDrugDealers(bool reset = false);
	static void UpdateVariations();

	//Logging
	static void LogCurrentVariations();
	static void LogDataFile();
	static void LogVariations();

	//Call hooks
	static void InstallHooks(bool enableSpecialPeds);

	static std::vector<unsigned short> GetVariationOriginalModels(const int modelIndex);
};
