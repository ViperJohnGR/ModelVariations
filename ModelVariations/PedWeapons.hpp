#pragma once

#include <CPed.h>

extern char currentZone[8];
extern const char* currentInterior;

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
