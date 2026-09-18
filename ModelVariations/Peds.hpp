#pragma once

#include <CZone.h>

#include <string>
#include <unordered_map>
#include <vector>

extern std::unordered_map<unsigned short, std::string> modelNames;
extern std::unordered_map<std::string, std::vector<CZone*>> presetAllZones;
extern int maxPedID;
extern char currentMission[9];

enum class debugDrawPedStats : uint32_t
{
	POINTER = 1,
	MODEL = 2,
	CREATED_BY = 4,
	PED_TYPE = 8,
	REF_COUNT = 16,
	PROOFS = 32,
	HEALTH = 64,
	ARMOUR = 128,
	PARENT_MODEL = 256,
	VOICE = 512
};

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
	static void DrawDebugInfo(float fontSize, uint32_t debugOptions);

	//Logging
	static void LogCurrentVariations();
	static void LogDataFile();
	static void LogVariations();

	//Call hooks
	static void InstallHooks(bool enableSpecialPeds);
};
