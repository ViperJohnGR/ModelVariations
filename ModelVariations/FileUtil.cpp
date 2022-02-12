#include "FileUtil.hpp"
#include <sys/stat.h>


DWORD getFilesize(const std::string& filename)
{
    HANDLE hFile = CreateFile(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;
    DWORD filesize = GetFileSize(hFile, NULL);
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
    FILE* fp = fopen(filename.c_str(), "rb");
    if (fp == NULL)
        return "";

    fseek(fp, 0, SEEK_END);
    int filesize = ftell(fp);
    if (filesize < 1)
    {
        fclose(fp);
        return "";
    }
    fseek(fp, 0, SEEK_SET);

    char* filebuf = (char*)calloc((size_t)filesize + 1, 1);
    if (filebuf == NULL)
    {
        fclose(fp);
        return "";
    }

    if (fread(filebuf, 1, (size_t)filesize, fp) != (size_t)filesize)
    {
        fclose(fp);
        free(filebuf);
        return "";
    }
    fclose(fp);

    std::string retString(filebuf);
    free(filebuf);
    retString.erase(std::remove(retString.begin(), retString.end(), 0x0D), retString.end());
    return retString;
}
