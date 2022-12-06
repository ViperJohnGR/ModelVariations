#include "LogUtil.hpp"

#include <iomanip>
#include <fstream>
#include <sstream>
#include <ntstatus.h>

#pragma comment (lib, "bcrypt.lib")


bool compareLower(const char* a, const char* b)
{
    for (int i = 0; a[i] || b[i]; i++)
        if (toupper(a[i]) != toupper(b[i]))
            return false;

    return true;
}

std::string hashFile(const char* filename)
{
    std::string hashString = "";
    HANDLE hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        const auto filesize = GetFileSize(hFile, NULL);
        if (filesize != INVALID_FILE_SIZE)
        {
            DWORD lpNumberOfBytesRead;
            BCRYPT_ALG_HANDLE hProvider = NULL;
            BCRYPT_HASH_HANDLE ctx = NULL;
            BYTE* filebuf = new BYTE[filesize + 1];

            if (ReadFile(hFile, filebuf, filesize, &lpNumberOfBytesRead, NULL))
                if (BCryptOpenAlgorithmProvider(&hProvider, BCRYPT_SHA256_ALGORITHM, NULL, 0) == STATUS_SUCCESS)
                    if (BCryptCreateHash(hProvider, &ctx, NULL, 0, NULL, 0, 0) == STATUS_SUCCESS && ctx != NULL)
                    {
                        BYTE* hash = new BYTE[50];
                        std::stringstream stream;
                        BCryptHashData(ctx, filebuf, filesize, 0);
                        BCryptFinishHash(ctx, hash, 32, 0);
                        BCryptDestroyHash(ctx);
                        BCryptCloseAlgorithmProvider(hProvider, 0);

                        for (int i = 0; i < 32; i++)
                            stream << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(hash[i]);

                        hashString = stream.str();
                        delete[] hash;
                    }
            delete[] filebuf;
        }
        CloseHandle(hFile);
    }
    
    return hashString;
}

std::string getParentModuleName(unsigned int address)
{
    std::string emptyString;
    std::pair<unsigned int, std::string> prev;
    if (address == 0)
        return emptyString;

    for (auto it = modulesSet.begin(); it != modulesSet.end(); it++)
        if (it->first > address)
            return prev.second;
        else if (std::next(it) == modulesSet.end())
            return it->second;
        else
        {
            prev.first = it->first;
            prev.second = it->second;
        }
    
    return emptyString;
}

std::pair<unsigned int, std::string> getAddressBaseModule(uint32_t functionAddress)
{
    std::pair<unsigned int, std::string> moduleInfo = {0, ""};

    std::pair<unsigned int, std::string> prev;
    for (auto it = modulesSet.begin(); it != modulesSet.end(); it++)
        if (it->first > functionAddress)
        {
            moduleInfo = prev;
            break;
        }
        else if (std::next(it) == modulesSet.end())
        {
            moduleInfo.first = it->first;
            moduleInfo.second = it->second;
        }
        else
        {
            prev.first = it->first;
            prev.second = it->second;
        }

    return moduleInfo;
}

void checkCallModified(const std::string &callName, unsigned int callAddress, bool isDirectAddress)
{
    const unsigned int functionAddress = (isDirectAddress == false) ? injector::GetBranchDestination(callAddress).as_int() : *(unsigned int*)callAddress;
    std::string modulePath = getParentModuleName(functionAddress);
    unsigned int baseAddress = 0;

    std::string moduleName = modulePath.substr(modulePath.find_last_of("/\\") + 1);

    if (compareLower(moduleName.c_str(), MOD_NAME))
        return;
    if (callChecks.find({ callAddress , moduleName}) != callChecks.end())
        return;

    callChecks.insert({ callAddress, moduleName});

    if (functionAddress > 0)
        baseAddress = getAddressBaseModule(functionAddress).first;

    logfile << "Modified call found: " << callName << " 0x" << std::uppercase << std::hex << callAddress << " 0x" << functionAddress << " ";
    logfile << moduleName << " 0x" << baseAddress << std::endl;
}

std::string getWindowsVersion()
{
    std::string retString;
    char str[255] = {};
    DWORD cbData = 254;
    HKEY hkey;

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hkey, "CurrentBuild", NULL, NULL, (LPBYTE)str, &cbData) == ERROR_SUCCESS)
        {
            retString += "OS build ";
            retString += str;
            retString += " ";
        }
        RegCloseKey(hkey);
    }
	
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hkey, "PROCESSOR_ARCHITECTURE", NULL, NULL, (LPBYTE)str, &cbData) == ERROR_SUCCESS)
            retString += str;
        RegCloseKey(hkey);
    }

    return retString;
}

void checkAllCalls()
{
    if (enableLog == 0)
        return;

    for (auto it : hookedCalls)
        checkCallModified(it.second.name, it.first, (it.second.isVTableAddress == true) ? true : false);
}

void logModified(unsigned int address, const std::string &message)
{
    if (logfile.is_open() && modifiedAddresses.find(address) == modifiedAddresses.end())
    {
        logfile << message << std::endl;
        modifiedAddresses.insert(address);
    }
}

std::string printToString(const char* format, ...)
{
    char buf[256] = {};

    va_list argptr;
    va_start(argptr, format);
    vsnprintf(buf, 255, format, argptr);
    va_end(argptr);

    std::string retString(buf);
    return retString;
}

std::string bytesToString(unsigned int address, int nBytes)
{
    std::stringstream ss;
    const unsigned char* c = reinterpret_cast<unsigned char*>(address);

    for (int i = 0; i < nBytes; i++, ss << " ")
        ss << std::setfill('0') << std::setw(2) << std::hex << static_cast<unsigned int>(c[i]);

    return ss.str();
}

std::string fileToString(const std::string& filename)
{
    std::stringstream ss;
    std::ifstream file(filename);

    if (file.is_open())
        ss << file.rdbuf();

    return ss.str();
}
