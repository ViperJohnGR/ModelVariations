#pragma once

#include <vector>
#include <IniReader.h>

#include <CModelInfo.h>

inline std::vector<unsigned short> vectorUnion(std::vector<unsigned short>& vec1, std::vector<unsigned short>& vec2)
{
    std::vector<unsigned short> vec;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vec));
    return vec;
}

inline void vectorUnion(std::vector<unsigned short>& vec1, std::vector<unsigned short>& vec2, std::vector<unsigned short>& dest)
{
    dest.clear();
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(dest));
}

inline std::vector<unsigned short> iniLineParser(std::string section, std::string key, CIniReader* ini, bool parseGroups = false)
{
    std::vector<unsigned short> retVector;
    if (ini == NULL)
        return retVector;

    std::string iniString = ini->ReadString(section, key, "");

    if (!iniString.empty())
    {
        char* tkString = new char[iniString.size() + 1];
        strcpy(tkString, iniString.c_str());

        char* token = strtok(tkString, ",");
        int modelid = 0;

        while (token != NULL)
        {
            if (parseGroups)
            {
                if (strncmp(token, "Group", 5) == 0)
                    retVector.push_back((unsigned short)(token[5] - '0'));
            }
            else if(token[0] >= '0' && token[0] <= '9')
                retVector.push_back((unsigned short)atoi(token));
            else if (CModelInfo::GetModelInfo(token, &modelid) != NULL)
                retVector.push_back((unsigned short)modelid);

            token = strtok(NULL, ",");
        }

        delete[] tkString;
    }
    std::sort(retVector.begin(), retVector.end());
    return retVector;
}
