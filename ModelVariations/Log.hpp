#pragma once

#include <string>

class Log
{
public:
	static bool Open(const std::string &filename);
	static bool Close();
	static bool Write(const char* format, ...);

	static bool LogModifiedAddress(std::uintptr_t address, const char* format, ...);
};
