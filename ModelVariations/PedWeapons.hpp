#pragma once

#include "DataReader.hpp"

#include <CPed.h>

class PedWeaponVariations
{
public:
	static void AddToStack(CPed* ped);
	static void ClearData();
	static void LoadData();
	static void Process(const char* currentZone);

	//Logging
	static void LogDataFile();
};
