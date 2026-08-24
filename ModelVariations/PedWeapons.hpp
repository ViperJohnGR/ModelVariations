#pragma once

#include <chrono>

extern CZone* currentZone;
extern std::chrono::steady_clock::duration gameplayTimeSinceLoad;
extern char currentMission[9];

class PedWeaponVariations
{
public:
	static void ClearData();
	static void LoadData();
	static void Process();

	//Logging
	static void LogDataFile();

	//Call hooks
	static void InstallHooks();
};
