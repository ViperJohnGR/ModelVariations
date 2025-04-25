#pragma once

extern char currentZone[8];

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
