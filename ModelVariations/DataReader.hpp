#pragma once

#include <IniReader.h>


class DataReader : public CIniReader
{
public:
	int ReadInteger(std::string_view szSection, std::string_view szKey, int iDefaultValue);
	float ReadFloat(std::string_view szSection, std::string_view szKey, float fltDefaultValue);
	bool ReadBoolean(std::string_view szSection, std::string_view szKey, bool bolDefaultValue);
	std::string ReadString(std::string_view szSection, std::string_view szKey, std::string_view szDefaultValue);
	std::vector<unsigned short> ReadLine(std::string section, std::string key, bool parseGroups = false);
};
