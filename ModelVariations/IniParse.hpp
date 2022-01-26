#pragma once

#include <vector>
#include "IniReader/IniReader.h"

inline std::vector<short> vectorUnion(std::vector<short>& vec1, std::vector<short>& vec2)
{
    std::vector<short> vec;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vec));
    return vec;
}

inline void vectorUnion(std::vector<short>& vec1, std::vector<short>& vec2, std::vector<short>& dest)
{
    dest.clear();
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(dest));
}

inline bool fileExists(const char* filename)
{
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL)
        return false;
    fclose(fp);
    return true;
}

inline std::string fileToString(const char* filename)
{
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL)
        return "";

    fseek(fp, 0, SEEK_END);
    int filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* filebuf = (char*)calloc(filesize+1, 1);
    if (fread(filebuf, 1, filesize, fp) != filesize)
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

inline std::vector<short> iniLineParser(std::string section, std::string key, CIniReader* ini, bool parseGroups = false)
{
    std::vector<short> retVector;
    if (ini == NULL)
        return retVector;

    std::string iniString = ini->ReadString(section, key, "");

    if (!iniString.empty())
    {
        char* tkString = new char[iniString.size() + 1];
        strcpy(tkString, iniString.c_str());

        char* token = strtok(tkString, ",");

        while (token != NULL)
        {
            if (parseGroups)
            {
                if (strncmp(token, "Group", 5) == 0)
                    retVector.push_back(token[5] - '0');
            }
            else if(token[0] >= '0' && token[0] <= '9')
                retVector.push_back(atoi(token));

            token = strtok(NULL, ",");
        }

        delete[] tkString;
    }
    std::sort(retVector.begin(), retVector.end());
    return retVector;
}
