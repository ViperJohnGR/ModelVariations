#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "Log.hpp"
#include "SA.hpp"

#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CStreaming.h>
#include <CWeaponInfo.h>

bool reachedMaxCapacity = false;

DataReader::DataReader(std::string_view filename)
{
	Load(filename);
}

bool DataReader::Load(std::string_view filename)
{
	std::ifstream file(filename.data());

	if (!file.is_open())
		return false;

	std::vector<std::string> sections;
	std::string key, value, line;

	while (std::getline(file, line)) 
	{
		if (size_t pos = line.find_first_of(";#"); pos != std::string::npos)
			line.resize(pos);
		else if (pos = line.find("//"); pos != std::string::npos)
			line.resize(pos);

		line = trimString(line);

		if (!line.empty() && line.front() == '[' && line.back() == ']')
		{
			sections.clear();

			auto section = line.substr(1, line.size()-2);
			for (auto i : splitString(section, ','))
				sections.push_back(trimString(i));
		}
		else if (size_t pos = line.find('='); pos != std::string::npos)
		{
			key = trimString(line.substr(0, pos));
			value = trimString(line.substr(pos + 1));

			if (!key.empty() && !value.empty())
				for (auto &i : sections)
					if (!i.empty())
						data[i][key] = value;
		}
	}

	return true;
}

int DataReader::ReadInteger(std::string_view section, std::string_view key, int defaultValue)
{
	try
	{
		if (auto itSection = data.find((std::string)section); itSection != data.end())
			if (auto itKey = itSection->second.find((std::string)key); itKey != itSection->second.end())
				return std::stoi(itKey->second);
	}
	catch (std::invalid_argument&) {}
	catch (std::out_of_range&) {}

	return defaultValue;
}

float DataReader::ReadFloat(std::string_view section, std::string_view key, float defaultValue)
{
	try
	{
		if (auto itSection = data.find((std::string)section); itSection != data.end())
			if (auto itKey = itSection->second.find((std::string)key); itKey != itSection->second.end())
				return std::stof(itKey->second);
	}
	catch (std::invalid_argument&) {}
	catch (std::out_of_range&) {}

	return defaultValue;
}

bool DataReader::ReadBoolean(std::string_view section, std::string_view key, bool defaultValue)
{
	try
	{
		if (auto itSection = data.find((std::string)section); itSection != data.end())
			if (auto itKey = itSection->second.find((std::string)key); itKey != itSection->second.end())
				return /*(itKey->second == "true") || */(std::stoi(itKey->second) != 0);
	}
	catch (std::invalid_argument&) {}
	catch (std::out_of_range&) {}

	return defaultValue;
}

std::string DataReader::ReadString(std::string_view section, std::string_view key, std::string defaultValue)
{
	if (auto itSection = data.find((std::string)section); itSection != data.end())
		if (auto itKey = itSection->second.find((std::string)key); itKey != itSection->second.end())
			return itKey->second;

	return defaultValue;
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
			else if (parseType == READ_PEDS && !(token[0] >= '0' && token[0] <= '9') && CModelInfo::GetModelInfo(0) && !reachedMaxCapacity)
			{
				if (CStreaming__ms_pExtraObjectsDir->m_nNumEntries >= CStreaming__ms_pExtraObjectsDir->m_nCapacity)
				{
					reachedMaxCapacity = true;
					Log::Write("WARNING: The number of extra object directory entries is has reached max capacity (%u)\n", CStreaming__ms_pExtraObjectsDir->m_nCapacity);
				}
				else if (CStreaming__ms_pExtraObjectsDir->FindItem(token.data()))
				{
					for (uint16_t i = 1326; i < maxPedID; i++)
						if (CModelInfo::GetModelInfo(i) == NULL)
						{
							//Log::Write("Adding model %s to id %d... ", token.data(), i);
							auto pedInfo = CModelInfo__AddPedModel(i);
							if (pedInfo)
							{
								pedInfo->SetColModel((CColModel*)0x968DF0, false);
								CStreaming::RequestSpecialModel(i, token.data(), 0);
								retVector.push_back(i);
								addedIDs.push_back(i);
								pedInfo->m_nPedType = 4;
							}
							//Log::Write("OK\n");
							break;
						}
				}
				else
					Log::Write("Could not find model %s\n", token.data());
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
