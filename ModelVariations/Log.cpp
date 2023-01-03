#include "Log.hpp"

#include <cstdarg>
#include <sstream>

std::ofstream Log::logfile;
std::set<std::uintptr_t> Log::modifiedAddresses;
std::vector<char> Log::buffer;

void Log::Open(std::string_view filename)
{
	if (logfile.is_open())
		logfile.close();

	logfile.open(filename.data());

	if (logfile.is_open() && buffer.empty())
		buffer = std::vector<char>(MAX_LOG_WRITE_SIZE);
}

void Log::Close()
{
	if (logfile.is_open())
		logfile.close();
}

void Log::Write(const char* format, ...)
{
	va_list argptr;
	va_start(argptr, format);
	if (logfile.is_open())
	{
		vsnprintf(buffer.data(), MAX_LOG_WRITE_SIZE, format, argptr);

		logfile << buffer.data();
		logfile.flush();
	}
	va_end(argptr);
}


std::string Log::FileToString(std::string_view filename)
{
	std::stringstream ss;
	std::ifstream file(filename.data());

	if (file.is_open())
		ss << file.rdbuf();

	return ss.str();
}

void Log::LogModifiedAddress(std::uintptr_t address, const char* format, ...)
{
	if (logfile.is_open() && modifiedAddresses.find(address) == modifiedAddresses.end())
	{
		va_list argptr;
		va_start(argptr, format);

		vsnprintf(buffer.data(), MAX_LOG_WRITE_SIZE, format, argptr);

		logfile << buffer.data();
		logfile.flush();

		va_end(argptr);
		modifiedAddresses.insert(address);
	}
}
