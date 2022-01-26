#pragma once

#include <set>
#include <string>
#include <iostream>

#define MOD_VERSION "6.2"
#ifdef _DEBUG
#define MOD_NAME "ModelVariations_d.asi"
#else
#define MOD_NAME "ModelVariations.asi"
#endif

extern std::ofstream logfile;
extern std::set<std::pair<unsigned int, std::string>> modulesSet;
extern std::set<std::pair<unsigned int, std::string>> callChecks;

extern int enableLog;
extern int enableLights;
extern bool enableSideMissions;
extern int enableSiren;
extern int disablePayAndSpray;
extern int enableSpecialFeatures;
extern int changeScriptedCars;
extern int enableCloneRemover;

int getFilesize(const char* filename);
std::string hashFile(const char* filename);
unsigned int getAddressFromCall(unsigned char* data);
void checkCallModified(const char* callName, unsigned int originalAddress, bool directAccess);
std::string getWindowsVersion();
void checkAllCalls();
