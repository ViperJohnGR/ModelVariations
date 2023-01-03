#pragma once

#include <fstream>
#include <set>
#include <vector>

#define MAX_LOG_WRITE_SIZE 100000

class Log
{
	static std::ofstream logfile;
	static std::set<std::uintptr_t> modifiedAddresses;
	static std::vector<char> buffer;

public:
	static void Open(std::string_view filename);
	static void Close();
	static void Write(const char* format, ...);

	static std::string FileToString(std::string_view filename);
	static void LogModifiedAddress(std::uintptr_t address, const char* format, ...);

};
