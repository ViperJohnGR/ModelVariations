#pragma once

#include "Hooks.hpp"

#include <set>
#include <string>
#include <iostream>

#define MOD_VERSION "7.2"
#ifdef _DEBUG
#define MOD_NAME "ModelVariations_d.asi"
#else
#define MOD_NAME "ModelVariations.asi"
#endif

extern std::ofstream logfile;
extern std::set<std::pair<unsigned int, std::string>> modulesSet;
extern std::set<std::pair<unsigned int, std::string>> callChecks;

extern int enableLog;

std::string hashFile(const char* filename);
std::string getWindowsVersion();
void checkAllCalls();
