#include "LogUtil.hpp"

#include <iomanip>
#include <fstream>
#include <sstream>

#pragma comment (lib, "bcrypt.lib")


bool compareLower(const char* a, const char* b)
{
    for (int i = 0; a[i] && b[i]; i++)
    {
        char c = (a[i] >= 'A' && a[i] <= 'Z') ? (a[i] + 32) : a[i];
        char d = (b[i] >= 'A' && b[i] <= 'Z') ? (b[i] + 32) : b[i];
        if (c != d)
            return false;
    }

    return true;
}

std::string hashFile(const char* filename)
//NTSTATUS hashFile(BYTE* hash, BYTE* data, unsigned int len)
{
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL)
        return "";

    fseek(fp, 0, SEEK_END);
    int filesize = ftell(fp);
    BYTE* hash = (BYTE*)calloc(50, 1);
    if (hash == NULL || filesize < 1)
    {
        fclose(fp);
        return "";
    }
    fseek(fp, 0, SEEK_SET);

    BYTE* filebuf = (BYTE*)calloc((size_t)filesize, 1);
    if (filebuf == NULL)
    {
        fclose(fp);
        free(hash);
        return "";
    }

    if ((int)fread(filebuf, 1, (size_t)filesize, fp) != filesize)
    {
        fclose(fp);
        free(hash);
        free(filebuf);
        return "";
    }
    fclose(fp);

    BCRYPT_ALG_HANDLE hProvider = NULL;
    BCRYPT_HASH_HANDLE ctx = NULL;

    BCryptOpenAlgorithmProvider(&hProvider, BCRYPT_SHA256_ALGORITHM, NULL, 0);

    BCryptCreateHash(hProvider, &ctx, NULL, 0, NULL, 0, 0);
    if (ctx != NULL)
    {
        BCryptHashData(ctx, filebuf, (ULONG)filesize, 0);
        BCryptFinishHash(ctx, hash, 32, 0);
    }
    free(filebuf);

    if (ctx != NULL)
        BCryptDestroyHash(ctx);

    if (hProvider != NULL)
        BCryptCloseAlgorithmProvider(hProvider, 0);

    std::stringstream stream;

    for (int i = 0; i < 32; i++)
        stream << std::setfill('0') << std::setw(2) << std::hex << (int)(hash[i]);

    free(hash);
    std::string hashString(stream.str());
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

void checkCallModified(const std::string &callName, unsigned int callAddress, bool isDirectAddress)
{
    unsigned int functionAddress = (isDirectAddress == false) ? injector::GetBranchDestination(callAddress).as_int() : *(unsigned int*)callAddress;
    std::string modulePath = getParentModuleName(functionAddress);
    unsigned int baseAddress = 0;

    std::string moduleName = modulePath.substr(modulePath.find_last_of("/\\") + 1);

    if (compareLower(moduleName.c_str(), MOD_NAME))
        return;
    if (callChecks.find({ callAddress , moduleName}) != callChecks.end())
        return;

    callChecks.insert({ callAddress, moduleName});

    if (functionAddress > 0)
    {
        std::pair<unsigned int, std::string> prev;
        for (auto it = modulesSet.begin(); it != modulesSet.end(); it++)
            if (it->first > functionAddress)
            {
                baseAddress = prev.first;
                break;
            }
            else if (std::next(it) == modulesSet.end())
                baseAddress = it->first;
            else 
            {
                prev.first = it->first;
                prev.second = it->second;
            }
    }

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
    unsigned char* c = reinterpret_cast<unsigned char*>(address);

    for (int i = 0; i < nBytes; i++, ss << " ")
        ss << std::hex << static_cast<unsigned int>(c[i]);

    return ss.str();
}
