#include "LogUtil.hpp"

#include <iomanip>
#include <fstream>
#include <sstream>
#include <Windows.h>
#include <shlwapi.h>
#include <bcrypt.h>
#include <Psapi.h>

#pragma comment (lib, "Advapi32.lib")
#pragma comment (lib, "bcrypt.lib")
#pragma comment (lib, "shlwapi.lib")

std::string hashFile(const char* filename, int& filesize)
//NTSTATUS hashFile(BYTE* hash, BYTE* data, unsigned int len)
{
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL)
        return "";

    BYTE* hash = (BYTE*)calloc(50, 1);
    if (hash == NULL)
    {
        fclose(fp);
        return "";
    }

    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    BYTE* filebuf = (BYTE*)calloc(filesize, 1);
    if (fread(filebuf, 1, filesize, fp) != filesize)
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
        BCryptHashData(ctx, filebuf, filesize, 0);
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

unsigned int getAddressFromCall(unsigned char* data)
{
    return ((*data == 0xE8) ? ( (unsigned int)data + *(unsigned int*)(data + 1) + 5 ) : 0);
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

void checkCallModified(const char *callName, unsigned int originalAddress, bool directAddress = false)
{
    if (callChecks.find(*((unsigned int*)originalAddress)) != callChecks.end())
        return;

    unsigned int changedAddress = (directAddress == false) ? getAddressFromCall((BYTE*)originalAddress) : directAddress;
    std::string moduleName = getParentModuleName(changedAddress);
    unsigned int baseAddress = 0;

    if (moduleName == MOD_NAME)
        return;

    callChecks.insert(originalAddress);

    if (changedAddress > 0)
    {
        std::pair<unsigned int, std::string> prev;
        for (auto it = modulesSet.begin(); it != modulesSet.end(); it++)
            if (it->first > changedAddress)
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

    logfile << "Modified call found: " << callName << " 0x" << std::uppercase << std::hex << originalAddress << " 0x" << changedAddress << " ";
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
        }
        RegCloseKey(hkey);
    }


    return retString;
}

void checkAllCalls()
{
    if (enableLog == 0)
        return;

    //Peds
    checkCallModified("UpdateRpHAnim", 0x5E49EF);

    //Vehicles
    checkCallModified("ChooseModel", 0x43022A);
    checkCallModified("ChoosePoliceCarModel", 0x42C320);
    checkCallModified("ChoosePoliceCarModel", 0x43020E);
    checkCallModified("ChoosePoliceCarModel", 0x430283);

    checkCallModified("AddPoliceCarOccupants", 0x42BC26);
    checkCallModified("AddPoliceCarOccupants", 0x42C620);
    checkCallModified("AddPoliceCarOccupants", 0x431EE5);
    checkCallModified("AddPoliceCarOccupants", 0x499CBB);
    checkCallModified("AddPoliceCarOccupants", 0x499D6A);
    checkCallModified("AddPoliceCarOccupants", 0x49A5EB);
    checkCallModified("AddPoliceCarOccupants", 0x49A85E);
    checkCallModified("AddPoliceCarOccupants", 0x49A9AF);

    checkCallModified("CAutomobile", 0x42B909);
    checkCallModified("CAutomobile", 0x4998F0);
    checkCallModified("CAutomobile", 0x462217);
    checkCallModified("CAutomobile", 0x61354A);

    checkCallModified("PickRandomCar", 0x6F3583);
    checkCallModified("DoInternalProcessing", 0x6F3EC1);

    checkCallModified("CTrain", 0x6F7634);

    checkCallModified("CBoat", 0x42149E);
    checkCallModified("CBoat", 0x431FD0);
    checkCallModified("CBoat", 0x5D2ADC);

    checkCallModified("CHeli", 0x6CD3C3);
    checkCallModified("CHeli", 0x6C6590);
    checkCallModified("CHeli", 0x6C6568);
    checkCallModified("CHeli", 0x5D2C46);
    checkCallModified("GenerateHeli", 0x6C7ACA);

    checkCallModified("IsLawEnforcementVehicle", 0x42CDDD);
    checkCallModified("GenerateRoadBlockCopsForCar", 0x42CE07);
    checkCallModified("GetColModel", 0x4613EB);

    checkCallModified("AddAmbulanceOccupants", 0x42BBFB);
    checkCallModified("AddFiretruckOccupants", 0x42BC1A);

    checkCallModified("FindSpecificDriverModelForCar_ToUse", 0x613A43);
    checkCallModified("AddPedInCar", 0x6D1B0E);

    checkCallModified("SetUpDriverAndPassengersForVehicle", 0x431DE2);
    checkCallModified("SetUpDriverAndPassengersForVehicle", 0x431DF9);
    checkCallModified("SetUpDriverAndPassengersForVehicle", 0x431ED1);

    checkCallModified("IsLawEnforcementVehicle", 0x6B11C2);

    checkCallModified("PossiblyRemoveVehicle", 0x60C4E8);
    checkCallModified("PossiblyRemoveVehicle", 0x42CD55);

    if (changeScriptedCars == 1)
        checkCallModified("CreateCarForScript", 0x467B01);

    if (enableSpecialFeatures == 1)
    {
        checkCallModified("ProcessControl", 0x871148, true);
        checkCallModified("PreRender", 0x871164, true);
    }

    if (enableSiren == 1)
        checkCallModified("HasCarSiren", 0x6D8492);

    if (enableLights == 1 && enableSpecialFeatures == 1 && enableSiren == 1)
    {
        checkCallModified("RegisterCorona", 0x6ABA60);
        checkCallModified("RegisterCorona", 0x6ABB35);
        checkCallModified("RegisterCorona", 0x6ABC69);

        checkCallModified("AddLight", 0x6AB80F);
        checkCallModified("AddLight", 0x6ABBA6);
    }

    if (disablePayAndSpray == 1)
        checkCallModified("IsCarSprayable", 0x44AC75);

    if (enableSideMissions)
    {
        checkCallModified("IsLawEnforcementVehicle", 0x48DA81);
        checkCallModified("CollectParameters", 0x469612);
    }

}