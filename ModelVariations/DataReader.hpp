#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

extern std::unordered_map<unsigned short, std::string> modelNames;
extern std::unordered_map<unsigned short, std::string> addedIDs;
extern int maxPedID;

enum dataTypeToRead
{
	READ_VEHICLES,
	READ_PEDS,
	READ_WEAPONS,
	READ_OCCUPANT_GROUPS,
	READ_TUNING,
	READ_TRAILERS,
	READ_NUMS
};

class DataReader
{
private:
	std::string file; // Owns the character storage referenced by data.

public:

	DataReader() = default;
	DataReader(const char* filename);
	DataReader(const DataReader&) = delete;
	DataReader& operator=(const DataReader&) = delete;
	DataReader(DataReader&&) = delete;
	DataReader& operator=(DataReader&&) = delete;

	void Clear();
	void Load(const char* filename);

	int ReadInteger(std::string_view section, std::string_view key, int defaultValue);
	unsigned int ReadHex(std::string_view section, std::string_view key, unsigned int defaultValue);
	float ReadFloat(std::string_view section, std::string_view key, float defaultValue);
	bool ReadBoolean(std::string_view section, std::string_view key, bool defaultValue);
	std::string ReadString(std::string_view section, std::string_view key, std::string_view defaultValue);
	std::vector<unsigned short> ReadLine(std::string_view section, std::string_view key, dataTypeToRead parseType);
	std::vector<std::vector<unsigned short>> ReadTrailerLine(std::string_view section, std::string_view key);
	std::vector<unsigned short> ReadLineUnique(std::string_view section, std::string_view key, dataTypeToRead parseType);

	std::unordered_map<std::string_view, std::unordered_map<std::string_view, std::string_view>> data;

private:
	const std::string_view* FindValue(std::string_view section, std::string_view key) const;
};
