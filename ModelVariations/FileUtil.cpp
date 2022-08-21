#include "FileUtil.hpp"
#include <sys/stat.h>

#include <fstream>
#include <sstream>


DWORD getFilesize(const std::string& filename)
{
    HANDLE hFile = CreateFile(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;
    const DWORD filesize = GetFileSize(hFile, NULL);
    CloseHandle(hFile);
    return filesize;
}

bool folderExists(const std::string& foldername)
{
    struct stat buffer;
    return (stat(foldername.c_str(), &buffer) == 0);
}

bool fileExists(const std::string &filename)
{
    FILE* fp = fopen(filename.c_str(), "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

std::string fileToString(const std::string& filename)
{
    std::stringstream ss;
    std::ifstream file(filename);

    if (file.is_open())
        ss << file.rdbuf();

    return ss.str();
}
