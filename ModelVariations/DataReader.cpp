#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Log.hpp"
#include "SA.hpp"

#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CWeaponInfo.h>

DataReader::DataReader(const char* filename)
{
	Load(filename);
}

void DataReader::Load(const char* filename)
{
	std::string file = fileToString(filename);

	data.clear();

	std::vector<std::string> sections;
	std::string key, value;

	for (std::string &line : splitString(file, "\n\r"))
	{		 
		if (auto pos = line.find_first_of(";#"); pos < line.size())
			line.resize(pos);
		if (auto pos = line.find("//"); pos < line.size())
			line.resize(pos);

		line = trimString(line);
		if (line.empty())
			continue;

		if (line.front() == '[' && line.back() == ']')
		{
			sections.clear();

			std::string inner = line.substr(1);
			inner.resize(inner.size()-1);

			for (std::string &token : splitString(inner, ','))
			{
				token = trimString(token);
				if (!token.empty())
					sections.push_back(token);
			}
		}
		else if (auto pos = line.find_first_of('='); pos < line.size())
		{
			key = trimString(line.substr(0, pos));
			value = trimString(line.substr(pos+1));

			if (!key.empty() && !value.empty())
				for (auto& s : sections)
						data[s][key] = value;
		}
	}
}

int DataReader::ReadInteger(const std::string &section, const std::string &key, int defaultValue)
{
	int value = defaultValue;
	if (auto itSection = data.find(section); itSection != data.end())
		if (auto itKey = itSection->second.find(key); itKey != itSection->second.end())
			value = fast_atoi(itKey->second.c_str());
	
	return (value == INT_MAX) ? defaultValue : value;
}

float DataReader::ReadFloat(const std::string& section, const std::string& key, float defaultValue)
{
	if (auto itSection = data.find(section); itSection != data.end())
		if (auto itKey = itSection->second.find(key); itKey != itSection->second.end())
		{
			float value = floatFromString(itKey->second.c_str());
			if (!std::isnan(value))
				return value;
		}

	return defaultValue;
}

bool DataReader::ReadBoolean(const std::string &section, const std::string &key, bool defaultValue)
{
	auto str = this->ReadString(section, key, "");
	if (_stricmp("true", str.c_str()) == 0)
		return true;
	else if (_stricmp("false", str.c_str()) == 0)
		return false;

	return this->ReadInteger(section, key, defaultValue) != 0;
}

std::string DataReader::ReadString(const std::string& section, const std::string& key, const std::string &defaultValue)
{
	if (auto itSection = data.find(section); itSection != data.end())
		if (auto itKey = itSection->second.find(key); itKey != itSection->second.end())
			return itKey->second;

	return defaultValue;
}

std::vector<unsigned short> DataReader::ReadLine(const std::string& section, const std::string& key, dataTypeToRead parseType)
{
	static bool reachedMaxCapacity = false;
	std::vector<unsigned short> retVector;

	std::string iniString = this->ReadString(section, key, "");
	CPedModelInfo* mInfo7 = (CPedModelInfo*)CModelInfo::GetModelInfo(7);

	if (iniString.empty() || mInfo7 == NULL)
		return retVector;

	for (std::string &str : splitString(iniString, ','))
	{
		int modelid = 0;
		auto trimmed = trimString(str);
		if (trimmed.empty())
			continue;

		auto token = trimmed.c_str();

		if (parseType == READ_NUMS)
		{
			if (token[0] >= '0' && token[0] <= '9')
				retVector.push_back((unsigned short)fast_atoi(token));
		}
		else if (parseType == READ_WEAPONS)
		{
			int weaponType = -1;
			if (token[0] >= '0' && token[0] <= '9')
				weaponType = fast_atoi(token);

			if (weaponType > -1 && weaponType < 1000 && CWeaponInfo::GetWeaponInfo((eWeaponType)weaponType, 1) != NULL)
				retVector.push_back((unsigned short)weaponType);
		}
		else if (parseType == READ_OCCUPANT_GROUPS)
		{
			if (strncmp(token, "OccupantGroup", 13) == 0)
				retVector.push_back((unsigned short)(token[13] - '0'));
		}
		else if (parseType == READ_TUNING)
		{
			if (_strnicmp(token, "paintjob", 8) == 0)
			{
				retVector.push_back((unsigned short)fast_atoi(token+8)-1U);
			}
			else if (token[0] != 'G')
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
		else if (parseType == READ_TRAILERS)
		{
			if (strncmp(token, "Trailers", 8) == 0)
				retVector.push_back((unsigned short)(token[8] - '0'));
		}
		else
		{
			CBaseModelInfo* mInfo = NULL;
			if (token[0] >= '0' && token[0] <= '9')
			{
				modelid = fast_atoi(token);
				if (modelid < 0 || modelid > 65535)
				{
					Log::Write("Error reading key %s in [%s]: invalid model id %s\n", key.c_str(), section.c_str(), token);
					return {};
				}
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
			else if (parseType == READ_PEDS && !(token[0] >= '0' && token[0] <= '9') && !reachedMaxCapacity)
			{
				if (CStreaming__ms_pExtraObjectsDir->m_nNumEntries >= CStreaming__ms_pExtraObjectsDir->m_nCapacity)
				{
					reachedMaxCapacity = true;
					Log::Write("WARNING: The number of extra object directory entries is has reached max capacity (%u)\n", CStreaming__ms_pExtraObjectsDir->m_nCapacity);
				}
				else if (CStreaming__ms_pExtraObjectsDir->FindItem(token) && mInfo7)
				{
					static unsigned short startID = 1326;
					for (unsigned short i = startID; i < maxPedID; i++)
						if (CModelInfo::GetModelInfo(i) == NULL)
						{
							startID = i;
							auto pedInfo = CModelInfo__AddPedModel(i);
							if (pedInfo)
							{
								pedInfo->SetColModel((CColModel*)0x968DF0, false);
								CStreaming__RequestSpecialModel(i, token, 0);
								CStreaming::SetModelIsDeletable(i);
								CStreaming::SetModelTxdIsDeletable(i);
								retVector.push_back(i);
								addedIDs[i] = token;
								pedInfo->m_nPedType = ePedType::PED_TYPE_CIVMALE;
								pedInfo->m_nRadio1 = mInfo7->m_nRadio1;
								pedInfo->m_nRadio2 = mInfo7->m_nRadio2;
							}
							break;
						}
				}
				else
					Log::Write("Could not find model %s\n", token);
			}
		}
	}
	
	std::sort(retVector.begin(), retVector.end());
	return retVector;
}

std::vector<std::vector<unsigned short>> DataReader::ReadTrailerLine(const std::string& section, const std::string& key)
{
	std::vector<std::vector<unsigned short>> retVector;

	std::string iniString = this->ReadString(section, key, "");

	if (iniString.empty())
		return retVector;

	for (std::string &token : splitString(iniString, ','))
	{
		int modelid = 0;
		token = trimString(token);
		if (token.empty())
			continue;

		if (token[0] != '[')
		{
			CBaseModelInfo* mInfo = NULL;
			if (token[0] >= '0' && token[0] <= '9')
			{
				modelid = fast_atoi(token.c_str());
				if (modelid < 0 || modelid > 65535)
				{
					Log::Write("Error reading key %s in [%s]: invalid model id %s\n", key.c_str(), section.c_str(), token.c_str());
					return {};
				}
				mInfo = CModelInfo::GetModelInfo(modelid);
			}
			else
				mInfo = CModelInfo::GetModelInfo(token.c_str(), &modelid);


			if (mInfo != NULL && mInfo->GetModelType() == MODEL_INFO_VEHICLE)
				retVector.push_back({ (unsigned short)modelid });
		}
		else
		{
			std::string inner = token.substr(1);
			if (auto pos = inner.find(']'); pos != std::string::npos)
				inner.resize(pos);
			else
				continue;

			retVector.push_back({});
			for (std::string &s : splitString(inner, '-'))
			{
				s = trimString(s);
				if (s.empty())
					continue;

				CBaseModelInfo* mInfo = NULL;
				if (s[0] >= '0' && s[0] <= '9')
				{
					modelid = fast_atoi(s.c_str());
					if (modelid < 0 || modelid > 65535)
					{
						Log::Write("Error reading key %s in [%s]: invalid model id %s\n", key.c_str(), section.c_str(), s.c_str());
						return {};
					}
					mInfo = CModelInfo::GetModelInfo(modelid);
				}
				else
					mInfo = CModelInfo::GetModelInfo(s.c_str(), &modelid);

				if (mInfo != NULL && mInfo->GetModelType() == MODEL_INFO_VEHICLE)
					retVector.back().push_back((unsigned short)modelid);
			}
		}
	}

	return retVector;
}

std::vector<unsigned short> DataReader::ReadLineUnique(const std::string& section, const std::string& key, dataTypeToRead parseType)
{
	auto vec = ReadLine(section, key, parseType);
	vec.erase(unique(vec.begin(), vec.end()), vec.end());
	return vec;
}
