#pragma once

#include <fstream>
#include <set>
#include <vector>

#include <Windows.h>

#define MAX_LOG_WRITE_SIZE 100000

class Log
{
	static HANDLE logfile;
	static std::set<std::uintptr_t> modifiedAddresses;
	static std::vector<char> buffer;

public:
	static bool Open(std::string_view filename);
	static bool Close();
	static bool Write(const char* format, ...);

	static bool LogModifiedAddress(std::uintptr_t address, const char* format, ...);
};
