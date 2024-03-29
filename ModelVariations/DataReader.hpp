#pragma once

#include <IniReader.h>
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

class DataReader : public CIniReader
{
public:
	DataReader() : CIniReader() {};
	explicit DataReader(std::string_view szFileName) : CIniReader(szFileName) {};

	int ReadInteger(std::string_view szSection, std::string_view szKey, int iDefaultValue);
	float ReadFloat(std::string_view szSection, std::string_view szKey, float fltDefaultValue);
	bool ReadBoolean(std::string_view szSection, std::string_view szKey, bool bolDefaultValue);
	std::string ReadString(std::string_view szSection, std::string_view szKey, std::string_view szDefaultValue);
	std::vector<unsigned short> ReadLine(std::string_view section, std::string_view key, modelTypeToRead parseType);
	std::vector<unsigned short> ReadLineUnique(std::string_view section, std::string_view key, modelTypeToRead parseType);
};
