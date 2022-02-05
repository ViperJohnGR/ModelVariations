#pragma once

#include <windows.h>
#include <string>


std::string fileToString(const std::string& filename);
bool folderExists(const std::string& foldername);
bool fileExists(const std::string& filename);
DWORD getFilesize(const std::string& filename);
