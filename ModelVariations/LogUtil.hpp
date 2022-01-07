#pragma once

#include <set>
#include <string>
#include <iostream>

#define MOD_VERSION "5.0"
#ifdef _DEBUG
#define MOD_NAME "ModelVariations_d.asi"
#else
#define MOD_NAME "ModelVariations.asi"
#endif

extern std::ofstream logfile;
extern std::set<std::pair<unsigned int, std::string>> modulesSet;
extern std::set<unsigned int> callChecks;

extern int enableLog;
extern int enableLights;
extern bool enableSideMissions;
extern int enableSiren;
extern int disablePayAndSpray;

std::string hashFile(const char* filename, int& filesize);
unsigned int getAddressFromCall(unsigned char* data);
void checkCallModified(const char* callName, unsigned int originalAddress);
void checkAllCalls();