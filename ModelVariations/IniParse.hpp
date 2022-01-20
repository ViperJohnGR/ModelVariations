#pragma once

#include <vector>
#include "IniReader/IniReader.h"

enum eVariationType
{
    PED_VARIATION,
    VEHICLE_VARIATION,
    PED_WEAPON_VARIATION,
    MODEL_SETTINGS
};

inline std::vector<short> iniLineParser(int type, int section, const char key[12], CIniReader* ini, bool parseGroups = false)
{
    std::vector<short> retVector;
    if (ini == NULL)
        return retVector;

    std::string sectionString;
    if (type == MODEL_SETTINGS)
        sectionString = (char*)section;
    else
        sectionString = std::to_string(section);

    std::string keyString;
    if (type == PED_VARIATION || type == VEHICLE_VARIATION || type == MODEL_SETTINGS)
        keyString = key;
    else
        keyString = std::to_string((int)(key));

    std::string iniString = ini->ReadString(sectionString, keyString, "");

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
    return retVector;
}