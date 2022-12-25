#pragma once

#include <CVehicle.h>

#include <string>

extern char currentZone[8];
extern unsigned int currentTown;

class VehicleVariations
{
public:
	static void AddToStack(CVehicle * veh);
	static void ClearData();
	static void LoadData(bool firstTime, std::string gamePath);
	static void Process();
	static void UpdateVariations();

	//Logging
	static void LogCurrentVariations();
	static void LogDataFile();
	static void LogVariations();

	//Call hooks
	static void hookTaxi();
	static void InstallHooks();
};
