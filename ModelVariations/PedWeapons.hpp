#pragma once

#include <CPed.h>

extern char currentZone[8];

class PedWeaponVariations
{
public:
	static void AddToStack(CPed* ped);
	static void ClearData();
	static void LoadData();
	static void Process();

	//Logging
	static void LogDataFile();
};
