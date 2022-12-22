#pragma once

#include "Hooks.hpp"

#include <set>
#include <string>
#include <iostream>

#define MOD_VERSION "8.5"
#ifdef _DEBUG
#define MOD_NAME "ModelVariations_d.asi"
#else
#define MOD_NAME "ModelVariations.asi"
#endif

extern std::ofstream logfile;
extern std::set<std::pair<unsigned int, std::string>> modulesSet;
extern std::set<std::pair<unsigned int, std::string>> callChecks;
extern std::set<unsigned int> modifiedAddresses;

std::string hashFile(const char* filename);
std::pair<unsigned int, std::string> getAddressBaseModule(uint32_t functionAddress);
std::string getWindowsVersion();
void checkAllCalls();
void logModified(unsigned int address, const std::string &message);
void getLoadedModules(bool& isOLA, bool& isFLA);
std::string getDatetime(bool printDate, bool printTime, bool printMs);
std::string printToString(const char* format, ...);
std::string bytesToString(unsigned int address, int nBytes);
std::string fileToString(const std::string& filename);

