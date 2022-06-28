#include "DataReader.hpp"

#include <CModelInfo.h>

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

std::vector<unsigned short> DataReader::ReadLine(std::string section, std::string key, int parseType)
{
	//parseType
	//0 - Normal
	//1 - Groups
	//2 - Tuning parts

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
			if (parseType == 1 && strncmp(token, "Group", 5) == 0)
				retVector.push_back((unsigned short)(token[5] - '0'));
			else if (token[0] >= '0' && token[0] <= '9' && parseType != 2 )
				retVector.push_back((unsigned short)atoi(token));
			else
			{
				auto *mInfo = CModelInfo::GetModelInfo(token, &modelid);
				if (mInfo != NULL)
					if ( (mInfo->GetModelType() != 6 && mInfo->GetModelType() != 7 && parseType == 2 && modelid > 300) || (((mInfo->GetModelType() == 6 || mInfo->GetModelType() == 7) && parseType == 0)) )
						retVector.push_back((unsigned short)modelid);
			}

			token = strtok(NULL, ",");
		}

		delete[] tkString;
	}
	std::sort(retVector.begin(), retVector.end());
	return retVector;
}
