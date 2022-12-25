#pragma once
#include "DataReader.hpp"

#include <CAutomobile.h>
#include <CPed.h>
#include <CVector.h>
#include <CZone.h>

#include <set>
#include <stack>
#include <vector>

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
