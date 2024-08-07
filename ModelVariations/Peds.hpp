#pragma once

#include <CPed.h>

#include <set>

extern const std::pair<std::string, unsigned> areas[];
extern int maxPedID;
extern char currentZone[8];
extern unsigned int currentTown;
extern const char* currentInterior;
extern std::set<std::string> zones;

class PedVariations
{
public:
	static void AddToStack(CPed* veh);
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
