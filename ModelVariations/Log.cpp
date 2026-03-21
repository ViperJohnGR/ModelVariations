#include "Log.hpp"

#include <cstdarg>
#include <mutex>
#include <set>
#include <vector>

#include <Windows.h>

constexpr auto logBufferSize = 10000;

HANDLE logfile = INVALID_HANDLE_VALUE;
std::set<std::uintptr_t> modifiedAddresses;
std::mutex logMutex;

bool Log::Open(const std::string &filename)
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(logfile);
		logfile = INVALID_HANDLE_VALUE;
	}
	
	logfile = CreateFile(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);

	modifiedAddresses.clear();
	return logfile != INVALID_HANDLE_VALUE;
}

bool Log::Close()
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile == INVALID_HANDLE_VALUE)
		return true;

	auto retVal = CloseHandle(logfile);
	logfile = INVALID_HANDLE_VALUE;
	return retVal;
}

bool Log::Write(const char* format, ...)
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile == INVALID_HANDLE_VALUE)
		return false;

	static thread_local std::vector<char> buffer(logBufferSize);

	va_list argptr;
	va_start(argptr, format);

	vsnprintf(buffer.data(), logBufferSize, format, argptr);

	va_end(argptr);

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, buffer.data(), strlen(buffer.data()), &bytesWritten, NULL) == 0 && GetLastError() != ERROR_IO_PENDING)
		return false;

	if (strlen(buffer.data()) != bytesWritten)
		return false;

	return true;
}

bool Log::LogTextFile(const std::string& filename)
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile == INVALID_HANDLE_VALUE)
		return false;

	HANDLE inFile = CreateFile(filename.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (inFile == INVALID_HANDLE_VALUE)
		return false;

	auto filesize = GetFileSize(inFile, NULL);
	if (filesize == INVALID_FILE_SIZE)
	{
		CloseHandle(inFile);
		return false;
	}

	std::string fileString(filesize, 0);

	DWORD lpNumberOfBytesRead = 0;
	ReadFile(inFile, &fileString[0], filesize, &lpNumberOfBytesRead, NULL);

	CloseHandle(inFile);

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, fileString.data(), fileString.size(), &bytesWritten, NULL) == 0 && GetLastError() != ERROR_IO_PENDING)
		return false;

	if (fileString.size() != bytesWritten)
		return false;

	return true;	
}

bool Log::LogModifiedAddress(std::uintptr_t address, const char* format, ...)
{
	std::lock_guard<std::mutex> lock(logMutex);

	if (logfile == INVALID_HANDLE_VALUE || modifiedAddresses.contains(address))
		return false;

	static thread_local std::vector<char> buffer(logBufferSize);

	va_list argptr;
	va_start(argptr, format);

	vsnprintf(buffer.data(), logBufferSize, format, argptr);

	va_end(argptr);

	DWORD bytesWritten = 0;
	if (WriteFile(logfile, buffer.data(), strlen(buffer.data()), &bytesWritten, NULL) == 0 && GetLastError() != ERROR_IO_PENDING)
		return false;

	if (strlen(buffer.data()) != bytesWritten)
		return false;

	modifiedAddresses.insert(address);		

	return true;
}
