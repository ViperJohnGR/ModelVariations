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
	if (logfile == INVALID_HANDLE_VALUE)
		return false;

	va_list argptr;
	va_start(argptr, format);

	vsnprintf(buffer.data(), MAX_LOG_WRITE_SIZE-1, format, argptr);

	va_end(argptr);

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, buffer.data(), strlen(buffer.data()), &bytesWritten, NULL) == 0 && GetLastError() != ERROR_IO_PENDING)
		return false;

	if (strlen(buffer.data()) != bytesWritten)
		return false;

	return true;
}


bool Log::LogModifiedAddress(std::uintptr_t address, const char* format, ...)
{
	if (logfile == INVALID_HANDLE_VALUE || modifiedAddresses.find(address) != modifiedAddresses.end())
		return false;

	va_list argptr;
	va_start(argptr, format);

	vsnprintf(buffer.data(), MAX_LOG_WRITE_SIZE-1, format, argptr);

	va_end(argptr);

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, buffer.data(), strlen(buffer.data()), &bytesWritten, NULL) == 0 && GetLastError() != ERROR_IO_PENDING)
		return false;

	if (strlen(buffer.data()) != bytesWritten)
		return false;

	modifiedAddresses.insert(address);		

	return true;
}
