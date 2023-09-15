#include "DataReader.hpp"
#include "FuncUtil.hpp"

#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CStreaming.h>
#include <CWeaponInfo.h>

#include <shlwapi.h>

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
		return this->CIniReader::ReadInteger(szSection, szKey, bolDefaultValue) != 0;
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

std::vector<unsigned short> DataReader::ReadLine(std::string_view section, std::string_view key, modelTypeToRead parseType)
{
	std::vector<unsigned short> retVector;

	std::string iniString = this->ReadString(section, key, "");

	if (iniString.empty())
		return retVector;

	auto line = splitString(iniString, ',');
	for (auto &token : line)
	{
		int modelid = 0;
		token = trimString(token);

		if (parseType == READ_WEAPONS)
		{
			int weaponType = -1;
			if (token[0] >= '0' && token[0] <= '9')
				weaponType = atoi(token.data());

			if (weaponType > -1 && weaponType < 1000 && CWeaponInfo::GetWeaponInfo((eWeaponType)weaponType, 1) != NULL)
				retVector.push_back((unsigned short)weaponType);
		}
		else if (parseType == READ_GROUPS)
		{
			if (strncmp(token.data(), "Group", 5) == 0)
				retVector.push_back((unsigned short)(token[5] - '0'));
		}
		else if (parseType == READ_TUNING)
		{
			if (_strnicmp(token.data(), "paintjob", 8) == 0)
			{
				retVector.push_back((unsigned short)atoi(token.data()+8)-1U);
			}
			else if (token[0] != 'G')
			{
				auto mInfo = CModelInfo::GetModelInfo(token.data(), &modelid);
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
				modelid = atoi(token.data());
				mInfo = CModelInfo::GetModelInfo(modelid);
			}
			else
				mInfo = CModelInfo::GetModelInfo(token.data(), &modelid);

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
			else if (parseType == READ_PEDS && !(token[0] >= '0' && token[0] <= '9') && CModelInfo::ms_modelInfoPtrs && *CModelInfo::ms_modelInfoPtrs)
				for (uint16_t i = 1326; i < maxPedID; i++)
					if (CModelInfo::GetModelInfo(i) == NULL)
					{
						if (CStreaming::ms_pExtraObjectsDir->FindItem(token.data()))
						{
							auto pedInfo = ((CPedModelInfo * (__cdecl*)(int))injector::GetBranchDestination(0x5B74A7).as_int())(i);
							if (pedInfo)
							{
								pedInfo->SetColModel((CColModel*)0x968DF0, false);
								CStreaming::RequestSpecialModel(i, token.data(), 0);
								retVector.push_back(i);
								addedIDs.push_back(i);
								pedInfo->m_nPedType = 4;
							}
						}
						break;					
					}
		}
	}
	
	std::sort(retVector.begin(), retVector.end());
	return retVector;
}

std::vector<unsigned short> DataReader::ReadLineUnique(std::string_view section, std::string_view key, modelTypeToRead parseType)
{
	auto vec = ReadLine(section, key, parseType);
	vec.erase(unique(vec.begin(), vec.end()), vec.end());
	return vec;
}
