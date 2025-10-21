#include "DataReader.hpp"
#include "FuncUtil.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "SA.hpp"

#include <CModelInfo.h>
#include <CPedModelInfo.h>
#include <CWeaponInfo.h>

bool reachedMaxCapacity = false;

DataReader::DataReader(const char* filename)
{
	Load(filename);
}

void DataReader::Load(const char* filename)
{
	std::string file = fileToString(filename);

	data.clear();

	std::vector<char*> sections;
	std::string key, value;
	size_t token_length = 0;

	for (char* line = strtok(file.data(), "\r\n", &token_length); line != NULL; line = strtok(NULL, "\r\n", &token_length))
	{		 
		for (auto pos = line; *pos;)
		{
			if (*pos == ';' || *pos == '#')
				*pos = 0;
			else if (*pos == '/' && *(pos + 1) == '/')
				*pos = 0;
			else
				pos++;
		}

		token_length = trim(&line);

		if (line[0] == '[' && line[token_length-1] == ']')
		{
			sections.clear();

			line++;
			token_length--;
			line[token_length - 1] = 0;

			for (char* token = strtok(line, ","); token != NULL; token = strtok(NULL, ","))
			{
				trim(&token);
				sections.push_back(token);
			}
		}
		else if (auto pos = strchr(line, '='); pos)
		{
			*pos = 0;
			trim(&line);
			key = line;

			line = pos + 1;
			trim(&line);
			value = line;

			if (!key.empty() && !value.empty())
				for (auto& i : sections)
					if (i[0] != 0)
						data[i][key] = value;
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
			float value = strtof(itKey->second.c_str());
			if (value != NAN)
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

std::string DataReader::ReadString(const std::string& section, const std::string& key, std::string defaultValue)
{
	if (auto itSection = data.find(section); itSection != data.end())
		if (auto itKey = itSection->second.find(key); itKey != itSection->second.end())
			return itKey->second;

	return defaultValue;
}

std::vector<unsigned short> DataReader::ReadLine(const std::string& section, const std::string& key, dataTypeToRead parseType)
{
	std::vector<unsigned short> retVector;

	std::string iniString = this->ReadString(section, key, "");

	if (iniString.empty())
		return retVector;

	for (char* token = strtok(iniString.data(), ","); token != NULL; token = strtok(NULL, ","))
	{
		int modelid = 0;
		trim(&token);

		if (parseType == READ_NUMS)
		{
			if (isdigit(token[0]))
				retVector.push_back((unsigned short)atoi(token));
		}
		else if (parseType == READ_WEAPONS)
		{
			int weaponType = -1;
			if (token[0] >= '0' && token[0] <= '9')
				weaponType = atoi(token);

			if (weaponType > -1 && weaponType < 1000 && CWeaponInfo::GetWeaponInfo((eWeaponType)weaponType, 1) != NULL)
				retVector.push_back((unsigned short)weaponType);
		}
		else if (parseType == READ_GROUPS)
		{
			if (strncmp(token, "Group", 5) == 0)
				retVector.push_back((unsigned short)(token[5] - '0'));
		}
		else if (parseType == READ_TUNING)
		{
			if (_strnicmp(token, "paintjob", 8) == 0)
			{
				retVector.push_back((unsigned short)atoi(token+8)-1U);
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
			else if (parseType == READ_PEDS && !(token[0] >= '0' && token[0] <= '9') && CModelInfo::GetModelInfo(0) && !reachedMaxCapacity)
			{
				if (CStreaming__ms_pExtraObjectsDir->m_nNumEntries >= CStreaming__ms_pExtraObjectsDir->m_nCapacity)
				{
					reachedMaxCapacity = true;
					Log::Write("WARNING: The number of extra object directory entries is has reached max capacity (%u)\n", CStreaming__ms_pExtraObjectsDir->m_nCapacity);
				}
				else if (CStreaming__ms_pExtraObjectsDir->FindItem(token))
				{
					for (uint16_t i = 1326; i < maxPedID; i++)
						if (CModelInfo::GetModelInfo(i) == NULL)
						{
							//Log::Write("Adding model %s to id %d... ", token.data(), i);
							auto pedInfo = CModelInfo__AddPedModel(i);
							if (pedInfo)
							{
								pedInfo->SetColModel((CColModel*)0x968DF0, false);
								CStreaming__RequestSpecialModel(i, token, 0);
								retVector.push_back(i);
								addedIDs.push_back(i);
								pedInfo->m_nPedType = 4;
							}
							//Log::Write("OK\n");
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

	for (char* token = strtok(iniString.data(), ","); token != NULL; token = strtok(NULL, ","))
	{
		int modelid = 0;
		trim(&token);

		if (token[0] != '[')
		{
			CBaseModelInfo* mInfo = NULL;
			if (token[0] >= '0' && token[0] <= '9')
			{
				modelid = atoi(token);
				mInfo = CModelInfo::GetModelInfo(modelid);
			}
			else
				mInfo = CModelInfo::GetModelInfo(token, &modelid);


			if (mInfo != NULL && mInfo->GetModelType() == MODEL_INFO_VEHICLE)
				retVector.push_back({ (unsigned short)modelid });
		}
		else
		{
			token++;
			retVector.push_back({});
			char model[9] = {};
			for (int i = 0; *token; token++)
			{
				if (*token == '-' || *token == ']')
				{
					char* trimmedModel = model;
					trim(&trimmedModel);

					CBaseModelInfo* mInfo = NULL;
					if (trimmedModel[0] >= '0' && trimmedModel[0] <= '9')
					{
						modelid = atoi(trimmedModel);
						mInfo = CModelInfo::GetModelInfo(modelid);
					}
					else
						mInfo = CModelInfo::GetModelInfo(trimmedModel, &modelid);

					if (mInfo != NULL && mInfo->GetModelType() == MODEL_INFO_VEHICLE)
						retVector.back().push_back((unsigned short)modelid);

					memset(model, 0, 9);
					i = 0;
				}
				else
					model[i++] = *token;
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
