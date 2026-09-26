#include "DataReader.hpp"
#include "Helpers.hpp"
#include "Log.hpp"
#include "Memory.hpp"
#include "SA.hpp"

#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CWeaponInfo.h>

namespace
{
	std::string_view cleanLine(std::string_view line)
	{
		for (std::size_t i = 0; i < line.size(); ++i)
			if (line[i] == ';' || line[i] == '#' ||	(line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/'))
			{
				line.remove_suffix(line.size() - i);
				break;
			}

		return trimView(line);
	}

	std::string_view popToken(std::string_view& list)
	{
		std::size_t end = 0;
		while (end < list.size() && list[end] != ',')
			++end;

		const std::string_view token = trimView(list.substr(0, end));
		if (end == list.size())
			list = {};
		else
			list.remove_prefix(end + 1);

		return token;
	}
}

DataReader::DataReader(const char* filename)
{
	Load(filename);
}

void DataReader::Clear()
{
	data.clear();
	file.clear();
}

void DataReader::Load(const char* filename)
{
	Clear();
	file = fileToString(filename);
	if (file.starts_with("\xEF\xBB\xBF"))
		file.erase(0, 3);

	std::string_view sections;
	std::size_t lineStart = 0;

	while (lineStart < file.size())
	{
		std::size_t lineEnd = lineStart;
		while (lineEnd < file.size() && file[lineEnd] != '\n' && file[lineEnd] != '\r')
			++lineEnd;

		std::string_view line = cleanLine(std::string_view(file.data() + lineStart, lineEnd - lineStart));

		lineStart = lineEnd;
		while (lineStart < file.size() && (file[lineStart] == '\n' || file[lineStart] == '\r'))
			++lineStart;

		if (line.empty())
			continue;

		if (line.front() == '[' && line.back() == ']')
		{
			sections = line.substr(1, line.size() - 2);
			continue;
		}

		std::size_t equals = 0;
		while (equals < line.size() && line[equals] != '=')
			++equals;

		if (equals < line.size())
		{
			std::string_view keys = trimView(line.substr(0, equals));
			const std::string_view value = trimView(line.substr(equals + 1));

			if (keys.empty() || value.empty())
				continue;

			std::string_view remainingSections = sections;
			while (!remainingSections.empty())
			{
				const std::string_view section = popToken(remainingSections);
				if (section.empty())
					continue;

				std::string_view remainingKeys = keys;
				while (!remainingKeys.empty())
				{
					const std::string_view key = popToken(remainingKeys);
					if (!key.empty())
						data[section][key] = value;
				}
			}
		}
	}
}

const std::string_view* DataReader::FindValue(std::string_view section, std::string_view key) const
{
	if (auto itSection = data.find(section); itSection != data.end())
		if (auto itKey = itSection->second.find(key); itKey != itSection->second.end())
			return &itKey->second;

	return nullptr;
}

int DataReader::ReadInteger(std::string_view section, std::string_view key, int defaultValue)
{
	int value = defaultValue;
	if (const std::string_view* text = FindValue(section, key))
		fromString<int>(*text, value);
	
	return value;
}

unsigned int DataReader::ReadHex(std::string_view section, std::string_view key, unsigned int defaultValue)
{
	unsigned value = defaultValue;
	if (const std::string_view* text = FindValue(section, key); text && text->size() >= 2 && (*text)[0] == '0' && (*text)[1] == 'x')
		fromString<unsigned int>(text->substr(2), value, 16);

	return value;
}

float DataReader::ReadFloat(std::string_view section, std::string_view key, float defaultValue)
{
	float value = defaultValue;
	if (const std::string_view* text = FindValue(section, key))
		fromString<float>(*text, value);

	return value;
}

bool DataReader::ReadBoolean(std::string_view section, std::string_view key, bool defaultValue)
{
	if (const std::string_view* text = FindValue(section, key))
	{
		if (strcasecmp("true", *text))
			return true;
		if (strcasecmp("false", *text))
			return false;

		int value = defaultValue;
		fromString<int>(*text, value);
		return value != 0;
	}

	return defaultValue;
}

std::string DataReader::ReadString(std::string_view section, std::string_view key, std::string_view defaultValue)
{
	if (const std::string_view* value = FindValue(section, key))
		return std::string(*value);

	return std::string(defaultValue);
}

std::vector<unsigned short> DataReader::ReadLine(std::string_view section, std::string_view key, dataTypeToRead parseType)
{
	static bool reachedMaxCapacity = false;
	std::vector<unsigned short> retVector;

	std::string iniString = this->ReadString(section, key, "");

	if (iniString.empty() || CModelInfo::GetModelInfo(7) == NULL)
		return retVector;

	for (std::string &str : splitString(iniString, ','))
	{
		int modelid = 0;
		auto trimmed = trimString(str);
		if (trimmed.empty())
			continue;

		int multiplier = 1;

		size_t start = trimmed.find('{');
		size_t end = trimmed.find('}', start);

		if (start != std::string::npos && end != std::string::npos && end > start) 
		{
			int n = 0;
			if (fromString<int>(trimmed.substr(start + 1, end - start - 1), n) && n > 0 && n < 10000)
				multiplier = n;
		}

		if (start != std::string::npos)
			trimmed.erase(start);

		auto token = trimmed.c_str();

		if (parseType == READ_NUMS)
		{
			int num = -1;
			if (fromString<int>(token, num) && num > -1 && num < 65536)
				for (int i = 0; i < multiplier; i++)
					retVector.push_back((unsigned short)num);
		}
		else if (parseType == READ_WEAPONS)
		{
			int weaponType = -1;
			if (token[0] >= '0' && token[0] <= '9')
				fromString<int>(token, weaponType);

			if (weaponType > -1 && weaponType < 1000 && CWeaponInfo::GetWeaponInfo((eWeaponType)weaponType, 1) != NULL)
				for (int i = 0; i < multiplier; i++)
					retVector.push_back((unsigned short)weaponType);
		}
		else if (parseType == READ_OCCUPANT_GROUPS)
		{
			if (strncmp(token, "OccupantGroup", 13) == 0)
			{
				int occupantGroup = 0;
				if (fromString<int>(token + 13, occupantGroup) && occupantGroup > 0 && occupantGroup < 256)
					for (int i = 0; i < multiplier; i++)
						retVector.push_back((unsigned short)occupantGroup);
			}
		}
		else if (parseType == READ_TUNING)
		{
			if (strcasestr(token, "paintjob"))
			{
				int paintjob = 0;
				if (fromString<int>(token + 8, paintjob) && paintjob > 0)
					for (int i = 0; i < multiplier; i++)
						retVector.push_back((unsigned short)paintjob-1U);
			}
			else
			{
				auto mInfo = CModelInfo::GetModelInfo(token, &modelid);
				if (mInfo != NULL)
				{
					const auto modelType = mInfo->GetModelType();
					if (modelType != MODEL_INFO_VEHICLE && modelType != MODEL_INFO_PED && modelType != MODEL_INFO_WEAPON && modelid > 300)
						for (int i = 0; i < multiplier; i++)
							retVector.push_back((unsigned short)modelid);
				}
			}
		}
		else if (parseType == READ_TRAILERS)
		{
			int trailer = 0;
			if (strncmp(token, "Trailers", 8) == 0 && fromString<int>(token + 8, trailer) && trailer > 0)
				for (int i = 0; i < multiplier; i++)
					retVector.push_back((unsigned short)trailer);
		}
		else
		{
			CBaseModelInfo* mInfo = NULL;
			if (token[0] >= '0' && token[0] <= '9')
			{
				if (!fromString<int>(token, modelid) || modelid < 0 || modelid > 65535)
				{
					Log::Write("Error reading key %s in [%s]: invalid model id %s\n", std::string(key).c_str(), std::string(section).c_str(), token);
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
						for (int i = 0; i < multiplier; i++)
							retVector.push_back((unsigned short)modelid);
				}
				else if (parseType == READ_PEDS)
				{
					if (mInfo->GetModelType() == MODEL_INFO_PED)
						for (int i = 0; i < multiplier; i++)
							retVector.push_back((unsigned short)modelid);
				}
			}
			else if (parseType == READ_PEDS && !(token[0] >= '0' && token[0] <= '9') && !reachedMaxCapacity)
			{
				CPedModelInfo* mInfo7 = reinterpret_cast<CPedModelInfo*>(CModelInfo::GetModelInfo(7));

				auto extraObjectsDir = CStreaming__ms_pExtraObjectsDir;

				Log::WriteVerbose("ExtraObjectsDir->m_nNumEntries = %d ExtraObjectsDir->m_nCapacity = %d\n", extraObjectsDir->m_nNumEntries, extraObjectsDir->m_nCapacity);

				if (extraObjectsDir->m_nNumEntries >= extraObjectsDir->m_nCapacity)
				{
					reachedMaxCapacity = true;
					Log::Write("WARNING: The number of extra object directory entries has reached max capacity (%u)\n", extraObjectsDir->m_nCapacity);
				}
				else if (extraObjectsDir->FindItem(token) && isAddressValid(mInfo7))
				{
					static unsigned short startID = 1326;
					for (unsigned short i = startID; i < std::min(maxPedID, 65535); i++)
						if (CModelInfo::GetModelInfo(i) == NULL)
						{
							startID = i;
							auto pedInfo = CModelInfo__AddPedModel(i);
							if (pedInfo)
							{
								pedInfo->SetColModel((CColModel*)0x968DF0, false);
								CStreaming__RequestSpecialModel(i, token, 0);
								CStreaming__SetModelIsDeletable(i);
								CStreaming__SetModelTxdIsDeletable(i);
								for (int j = 0; j < multiplier; j++)
									retVector.push_back(i);
								modelNames[i] = token;
								addedIDs[i] = token;
								pedInfo->m_nPedType = ePedType::PED_TYPE_CIVMALE;
								pedInfo->m_nRadio1 = mInfo7->m_nRadio1;
								pedInfo->m_nRadio2 = mInfo7->m_nRadio2;

								Log::WriteVerbose("Added new ped \"%s\" in id % u\n", token, i);
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

std::vector<std::vector<unsigned short>> DataReader::ReadTrailerLine(std::string_view section, std::string_view key)
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
				if (!fromString<int>(token, modelid) || modelid < 0 || modelid > 65535)
				{
					Log::Write("Error reading key %s in [%s]: invalid model id %s\n", std::string(key).c_str(), std::string(section).c_str(), token.c_str());
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
					if (!fromString<int>(s, modelid) || modelid < 0 || modelid > 65535)
					{
						Log::Write("Error reading key %s in [%s]: invalid model id %s\n", std::string(key).c_str(), std::string(section).c_str(), s.c_str());
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

std::vector<unsigned short> DataReader::ReadLineUnique(std::string_view section, std::string_view key, dataTypeToRead parseType)
{
	auto vec = ReadLine(section, key, parseType);
	vec.erase(unique(vec.begin(), vec.end()), vec.end());
	return vec;
}
