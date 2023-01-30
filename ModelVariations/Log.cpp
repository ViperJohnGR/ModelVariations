#include "Log.hpp"

#include <cstdarg>
#include <sstream>

HANDLE Log::logfile = INVALID_HANDLE_VALUE;
std::set<std::uintptr_t> Log::modifiedAddresses;
std::vector<char> Log::buffer;

bool Log::Open(std::string_view filename)
{
	FindClose(logfile);
	
	logfile = CreateFile(filename.data(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
	if (logfile == INVALID_HANDLE_VALUE)
		return false;

	if (buffer.empty())
		buffer = std::vector<char>(MAX_LOG_WRITE_SIZE);

	return true;
}

bool Log::Close()
{
	return CloseHandle(logfile);
}

bool Log::Write(const char* format, ...)
{
	va_list argptr;
	va_start(argptr, format);
	if (logfile == INVALID_HANDLE_VALUE)
		return false;

	vsnprintf(buffer.data(), MAX_LOG_WRITE_SIZE-1, format, argptr);

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, buffer.data(), strlen(buffer.data()), &bytesWritten, NULL) == 0)
		return false;

	if (strlen(buffer.data()) != bytesWritten)
		return false;

	va_end(argptr);

	return true;
}


std::string Log::FileToString(std::string_view filename)
{
	std::stringstream ss;
	std::ifstream file(filename.data());

	if (file.is_open())
		ss << file.rdbuf();

	return ss.str();
}

bool Log::LogModifiedAddress(std::uintptr_t address, const char* format, ...)
{
	if (logfile == INVALID_HANDLE_VALUE || modifiedAddresses.find(address) != modifiedAddresses.end())
		return false;

	va_list argptr;
	va_start(argptr, format);

	vsnprintf(buffer.data(), MAX_LOG_WRITE_SIZE-1, format, argptr);

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, buffer.data(), strlen(buffer.data()), &bytesWritten, NULL) == 0)
		return false;

	if (strlen(buffer.data()) != bytesWritten)
		return false;

	va_end(argptr);
	modifiedAddresses.insert(address);		

	return true;
}
