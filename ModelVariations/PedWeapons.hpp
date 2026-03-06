#pragma once

extern std::atomic<uint64_t> currentZone;

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
