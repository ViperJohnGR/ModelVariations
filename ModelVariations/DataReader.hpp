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
	READ_TUNING
};

class DataReader
{
public:
	DataReader(std::string_view filename);

	bool Load(std::string_view filename);

	int ReadInteger(std::string_view section, std::string_view key, int defaultValue);
	float ReadFloat(std::string_view section, std::string_view key, float defaultValue);
	bool ReadBoolean(std::string_view section, std::string_view key, bool defaultValue);
	std::string ReadString(std::string_view section, std::string_view key, std::string defaultValue);
	std::vector<unsigned short> ReadLine(std::string_view section, std::string_view key, modelTypeToRead parseType);
	std::vector<unsigned short> ReadLineUnique(std::string_view section, std::string_view key, modelTypeToRead parseType);

	std::map<std::string, std::map<std::string, std::string>> data;
};
