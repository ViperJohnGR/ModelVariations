#include "DataReader.hpp"

#include <CModelInfo.h>
#include <CWeaponInfo.h>

int DataReader::ReadInteger(std::string_view szSection, std::string_view szKey, int iDefaultValue)
{
	try
	{
		return this->CIniReader::ReadInteger(szSection, szKey, iDefaultValue);
	}
	catch (...)
	{
		return iDefaultValue;
	}
}

float DataReader::ReadFloat(std::string_view szSection, std::string_view szKey, float fltDefaultValue)
{
	try
	{
		return this->CIniReader::ReadFloat(szSection, szKey, fltDefaultValue);
	}
	catch (...)
	{
		return fltDefaultValue;
	}
}

bool DataReader::ReadBoolean(std::string_view szSection, std::string_view szKey, bool bolDefaultValue)
{
	try
	{
		return this->CIniReader::ReadBoolean(szSection, szKey, bolDefaultValue);
	}
	catch (...)
	{
		return bolDefaultValue;
	}
}

std::string DataReader::ReadString(std::string_view szSection, std::string_view szKey, std::string_view szDefaultValue)
{
	try
	{
		return this->CIniReader::ReadString(szSection, szKey, szDefaultValue);
	}
	catch (...)
	{
		std::string retString(szDefaultValue);
		return retString;
	}
}

std::vector<unsigned short> DataReader::ReadLine(std::string section, std::string key, modelTypeToRead parseType)
{
	std::vector<unsigned short> retVector;

	std::string iniString = this->ReadString(section, key, "");

	if (!iniString.empty())
	{
		char* tkString = new char[iniString.size() + 1];
		strcpy(tkString, iniString.c_str());

		char* token = strtok(tkString, ",");
		int modelid = 0;

		while (token != NULL)
		{
			while (token[0] == ' ')
				token++;

			if (parseType == READ_WEAPONS)
			{
				int weaponType = -1;
				if (token[0] >= '0' && token[0] <= '9')
					weaponType = atoi(token);

				if (CWeaponInfo::GetWeaponInfo((eWeaponType)weaponType, 1) != NULL)
					retVector.push_back((unsigned short)weaponType);
			}
			else if (parseType == READ_GROUPS)
			{
				if (strncmp(token, "Group", 5) == 0)
					retVector.push_back((unsigned short)(token[5] - '0'));
				break;
			}
			else if (parseType == READ_TUNING)
			{
				if (token[0] != 'G')
				{
					auto mInfo = CModelInfo::GetModelInfo(token, &modelid);
					if (mInfo != NULL)
					{
						const auto modelType = mInfo->GetModelType();
						if (modelType != MODEL_INFO_VEHICLE && modelType != MODEL_INFO_PED && modelType != MODEL_INFO_WEAPON && modelid > 300)
							retVector.push_back((unsigned short)modelid);
					}
				}
			}
			else
			{
				CBaseModelInfo* mInfo = NULL;
				if (token[0] >= '0' && token[0] <= '9')
				{
					modelid = atoi(token);
					mInfo = CModelInfo::GetModelInfo(modelid);
				}
				else
					mInfo = CModelInfo::GetModelInfo(token, &modelid);

				if (mInfo != NULL)
				{
					if (parseType == READ_VEHICLES)
					{
						if (mInfo->GetModelType() == MODEL_INFO_VEHICLE || modelid == 0)
							retVector.push_back((unsigned short)modelid);
					}
					else if (parseType == READ_PEDS)
					{
						if (mInfo->GetModelType() == MODEL_INFO_PED)
							retVector.push_back((unsigned short)modelid);
					}
				}
			}

			token = strtok(NULL, ",");
		}

		delete[] tkString;
	}
	std::sort(retVector.begin(), retVector.end());
	return retVector;
}
