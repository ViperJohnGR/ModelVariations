#include "LogUtil.hpp"

#include <algorithm>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <ntstatus.h>
#include <Psapi.h>

#pragma comment (lib, "bcrypt.lib")

std::ofstream logfile;

bool compareUpper(const char* a, const char* b)
{
    for (int i = 0; a[i] || b[i]; i++)
        if (toupper(a[i]) != toupper(b[i]))
            return false;

    return true;
}

bool strcasestr(std::string src, std::string sub)
{
    std::for_each(src.begin(), src.end(), [](char& c) {
        c = (char)::toupper(c);
    });

    std::for_each(sub.begin(), sub.end(), [](char& c) {
        c = (char)::toupper(c);
    });

    if (src.find(sub) != std::string::npos)
        return true;

    return false;
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
            DWORD lpNumberOfBytesRead = 0;
            BCRYPT_ALG_HANDLE hProvider = NULL;
            BCRYPT_HASH_HANDLE ctx = NULL;
            auto filebuf = std::vector<BYTE>(filesize + 1);

            if (ReadFile(hFile, filebuf.data(), filesize, &lpNumberOfBytesRead, NULL))
                if (BCryptOpenAlgorithmProvider(&hProvider, BCRYPT_SHA256_ALGORITHM, NULL, 0) == STATUS_SUCCESS)
                    if (BCryptCreateHash(hProvider, &ctx, NULL, 0, NULL, 0, 0) == STATUS_SUCCESS && ctx != NULL)
                    {
                        auto hash = std::vector<BYTE>(32);
                        std::stringstream stream;
                        BCryptHashData(ctx, filebuf.data(), filesize, 0);
                        BCryptFinishHash(ctx, hash.data(), 32, 0);
                        BCryptDestroyHash(ctx);
                        BCryptCloseAlgorithmProvider(hProvider, 0);

                        for (auto &i : hash)
                            stream << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(i);

                        hashString = stream.str();
                    }
        }
        CloseHandle(hFile);
    }
    
    return hashString;
}

std::pair<unsigned int, std::string> getAddressBaseModule(uint32_t functionAddress)
{
    std::pair<unsigned int, std::string> moduleInfo = {0, ""};

    for (auto it = modulesSet.begin(); it != modulesSet.end(); it++)
    {
        if (it->first > functionAddress)
            return moduleInfo;

        moduleInfo.first = it->first;
        moduleInfo.second = it->second;
    }

    return moduleInfo;
}

void checkCallModified(const std::string &callName, unsigned int callAddress, bool isDirectAddress)
{
    const unsigned int functionAddress = (isDirectAddress == false) ? injector::GetBranchDestination(callAddress).as_int() : *reinterpret_cast<unsigned int*>(callAddress);
    std::string modulePath = getAddressBaseModule(functionAddress).second;
    std::string moduleName = modulePath.substr(modulePath.find_last_of("/\\") + 1);

    if (compareUpper(moduleName.c_str(), MOD_NAME))
        return;
    if (callChecks.find({ callAddress , moduleName}) != callChecks.end())
        return;

    callChecks.insert({ callAddress, moduleName});

    logfile << "Modified call found: " << callName << " 0x" << std::uppercase << std::hex << callAddress << " 0x" << functionAddress << " ";
    logfile << moduleName << " 0x" << getAddressBaseModule(functionAddress).first << std::endl;
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
    if (logfile.is_open())
        for (auto it : hookedCalls)
            checkCallModified(it.second.name, it.first, it.second.isVTableAddress);
}

void logModified(unsigned int address, const std::string &message)
{
    if (logfile.is_open() && modifiedAddresses.find(address) == modifiedAddresses.end())
    {
        logfile << message << std::endl;
        modifiedAddresses.insert(address);
    }
}

void getLoadedModules(bool &isOLA, bool &isFLA)
{
    modulesSet.clear();
    isOLA = false;
    isFLA = false;

    HMODULE modules[500] = {};
    HANDLE hProcess = GetCurrentProcess();
    DWORD cbNeeded = 0;

    if (EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded))
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
        {
            char szModName[MAX_PATH] = {};
            if (GetModuleFileNameEx(hProcess, modules[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
            {
                if (strcasestr(szModName, "III.VC.SA.LimitAdjuster"))
                    isOLA = true;
                else if (strcasestr(szModName, "fastman92limitAdjuster"))
                    isFLA = true;
                modulesSet.insert(std::make_pair((unsigned int)modules[i], szModName));
            }
        }
}

std::string getDatetime(bool printDate, bool printTime, bool printMs)
{
    SYSTEMTIME systime;
    GetSystemTime(&systime);
    std::stringstream ss;
    std::string ms;

    if (printMs)
        ms += "." + std::to_string(systime.wMilliseconds);

    if (printDate)
    {
        ss << systime.wDay << "/" << systime.wMonth << "/" << systime.wYear;

        if (!printTime)
            return ss.str();

        ss << " ";
    }    

    ss << std::setfill('0') << std::setw(2) << systime.wHour << ":"
       << std::setfill('0') << std::setw(2) << systime.wMinute << ":"
       << std::setfill('0') << std::setw(2) << systime.wSecond << ms;

    return ss.str();
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
        ss << std::setfill('0') << std::setw(2) << std::uppercase << std::hex << static_cast<unsigned int>(c[i]);

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
