#pragma once

#include <windows.h>
#include <string>

enum eExeVersion
{
    SA_EXE_NONE,
    SA_EXE_HOODLUM,
    SA_EXE_COMPACT,
    SA_EXE_UNKNOWN
};

const std::string exeHashes[2] = { "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26",     //HOODLUM
                                   "25580ae242c6ecb6f6175ca9b4c912aa042f33986ded87f20721b48302adc9c9" };   //Compact

std::string fileToString(const std::string& filename);
bool folderExists(const std::string& foldername);
bool fileExists(const std::string& filename);
DWORD getFilesize(const std::string& filename);
