#pragma once

#include <map>
#include <string>
#include <vector>

extern std::vector<unsigned short> addedIDs;
extern int maxPedID;

enum modelTypeToRead
{
	READ_VEHICLES,
	READ_PEDS,
	READ_WEAPONS,
	READ_GROUPS,
	READ_TUNING,
	READ_TRAILERS
};

class DataReader
{
public:
	DataReader(const char* filename);

	bool Load(const char* filename);

	int ReadInteger(const std::string& section, const std::string& key, int defaultValue);
	float ReadFloat(const std::string& section, const std::string& key, float defaultValue);
	bool ReadBoolean(const std::string& section, const std::string& key, bool defaultValue);
	std::string ReadString(const std::string& section, const std::string& key, std::string defaultValue);
	std::vector<unsigned short> ReadLine(const std::string& section, const std::string& key, modelTypeToRead parseType);
	std::vector<std::vector<unsigned short>> ReadTrailerLine(const std::string& section, const std::string& key);
	std::vector<unsigned short> ReadLineUnique(const std::string& section, const std::string& key, modelTypeToRead parseType);

	std::map<std::string, std::map<std::string, std::string>> data;
};
