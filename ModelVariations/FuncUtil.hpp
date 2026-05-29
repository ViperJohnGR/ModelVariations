#pragma once

#include "LoadedModules.hpp"

#include <algorithm>
#include <iterator>
#include <charconv>
#include <type_traits>
#include <utility>
#include <vector>

#include <CGeneral.h>
#include <CMessages.h>

#include <ntstatus.h>


inline bool isGameHOODLUM()
{
    return (plugin::GetGameVersion() == GAME_10US_HOODLUM);
}

inline bool isGameCompact()
{
    return (plugin::GetGameVersion() == GAME_10US_COMPACT);
}

inline CVector2D convert3DVectorTo2D(const CVector& vec)
{
    return { vec.x, vec.y };
}

inline void printMessage(const char* message, unsigned int time)
{
    CMessages::AddMessageJumpQ(const_cast<char*>(message), time, 0, false);
}

inline std::string getFullPath(const std::string& filename)
{
    return filename.find(':') != std::string::npos ? filename : (LoadedModules::GetSelfDirectory() + '\\' + filename);
}

inline std::string printFilenameWithBorder(const std::string &name, const char ch = '#') 
{
    std::string outString;
    size_t line_width = name.size() + 6; // "## " + name + " ##"


    for (size_t i = 0;i<line_width;i++)
        outString += ch;

    outString += "\n";
    outString += std::string(2, ch) + " " + name + " " + std::string(2, ch);
    outString += "\n";

    for (size_t i = 0;i<line_width;i++)
        outString += ch;

    return outString;
}

inline std::string hashFile(const HANDLE& hFile, DWORD filesize = 0)
{
    std::string hashString;

    if (hFile == INVALID_HANDLE_VALUE)
        return "";

    if (filesize == 0)
        filesize = GetFileSize(hFile, NULL);

    if (filesize != INVALID_FILE_SIZE)
    {
        DWORD lpNumberOfBytesRead = 0;
        BCRYPT_ALG_HANDLE hProvider = NULL;
        BCRYPT_HASH_HANDLE ctx = NULL;
        auto filebuf = std::vector<BYTE>(filesize + 1);

        if (ReadFile(hFile, filebuf.data(), filesize, &lpNumberOfBytesRead, NULL) && lpNumberOfBytesRead == filesize)
            if (BCryptOpenAlgorithmProvider(&hProvider, BCRYPT_SHA256_ALGORITHM, NULL, 0) == STATUS_SUCCESS)
                if (BCryptCreateHash(hProvider, &ctx, NULL, 0, NULL, 0, 0) == STATUS_SUCCESS && ctx != NULL)
                {
                    auto hashArray = std::vector<BYTE>(32);
                    hashString.resize(65);
                    BCryptHashData(ctx, filebuf.data(), filesize, 0);
                    BCryptFinishHash(ctx, hashArray.data(), 32, 0);
                    BCryptDestroyHash(ctx);
                    BCryptCloseAlgorithmProvider(hProvider, 0);
                    int hashLen = 0;

                    for (BYTE i : hashArray)
                        hashLen += snprintf(hashString.data() + hashLen, 65U - hashLen, "%02x", i);
                }
    }

    return hashString;
}

inline std::string hashFile(const std::string& filename)
{
    HANDLE hFile = CreateFile(getFullPath(filename).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return "";

    auto hash = hashFile(hFile);
    CloseHandle(hFile);
    return hash;
}

inline bool fileExists(const std::string& filename)
{
    return GetFileAttributes(getFullPath(filename).c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline bool isTimeInRange(int timeNow, int timeStart, int timeEnd)
{
    if (timeStart <= timeEnd) // Normal range (same day)
        return timeNow >= timeStart && timeNow <= timeEnd;

    return timeNow >= timeStart || timeNow <= timeEnd; // Wrap-around past midnight
}

inline std::string getDatetime(bool printDate, bool printTime, bool printMs)
{
    SYSTEMTIME systime;
    GetSystemTime(&systime);
    char str[255] = {};
    int i = 0;

    if (printDate)
    {
        i += snprintf(str, 254, "%d/%d/%d", systime.wDay, systime.wMonth, systime.wYear);
        if (printTime)
            i += snprintf(str + i, 254U - i, " ");
    }

    if (printTime)
    {
        i += snprintf(str + i, 254U - i, "%02d:%02d:%02d", systime.wHour, systime.wMinute, systime.wSecond);

        if (printMs)
            snprintf(str + i, 254U - i, ".%03d", systime.wMilliseconds);
    }

    return str;
}

////////////
// Random //
////////////

template <typename T>
T rand(int min, unsigned int max)
{
    return (T)CGeneral::GetRandomNumberInRange(min, (int)max);
}

template <typename T>
bool rand()
{
    static_assert(std::is_same_v<T, bool>, "invalid type for template");

    return (bool)CGeneral::GetRandomNumberInRange(0, 2);
}


/////////////
// Strings //
/////////////

inline std::string bytesToString(std::uintptr_t address, unsigned int nBytes)
{
    const unsigned char* c = reinterpret_cast<unsigned char*>(address);
    std::string result;
    char buffer[4] = {};

    for (unsigned int i = 0; i < nBytes; ++i)
    {
        if (i > 0)
            result += ' ';
        std::snprintf(buffer, sizeof(buffer), "%02X", c[i]);
        result += buffer;
    }

    return result;
}

inline std::string fileToString(const std::string &filename)
{
    std::string str;

    HANDLE hFile = CreateFile(getFullPath(filename).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return str;

    auto filesize = GetFileSize(hFile, NULL);
    if (filesize == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return str;
    }

    str.resize(filesize);
    DWORD lpNumberOfBytesRead = 0;
    ReadFile(hFile, &str[0], filesize, &lpNumberOfBytesRead, NULL);

    CloseHandle(hFile);
    return str;
}

inline std::string getFilenameFromPath(const std::string &path)
{
    return path.substr(path.find_last_of("/\\") + 1);
}

inline bool strcasestr(std::string src, std::string sub)
{
    std::for_each(src.begin(), src.end(), [](char& c) {
        c =  ((c & 0x80) == 0) ? (char)::toupper(c) : c;
    });

    std::for_each(sub.begin(), sub.end(), [](char& c) {
        c = ((c & 0x80) == 0) ? (char)::toupper(c) : c;
    });

    if (src.find(sub) != std::string::npos)
        return true;

    return false;
}

inline std::vector<std::string> splitString(const std::string &s, char separator)
{
    std::vector<std::string> out;

    std::size_t start = 0;
    while (start <= s.size())
    {
        const std::size_t pos = s.find(separator, start);
        const std::size_t end = (pos == std::string::npos) ? s.size() : pos;

        if (end > start) // non-empty token
            out.emplace_back(s.substr(start, end - start));

        if (pos == std::string::npos)
            break;

        start = pos + 1;
    }

    return out;
}

inline std::vector<std::string> splitString(const std::string& s, const std::string& separators)
{
    std::vector<std::string> out;

    std::size_t start = 0;

    while (start < s.size())
    {
        // Skip leading separators
        start = s.find_first_not_of(separators, start);
        if (start == std::string::npos) break;

        // Find end of token
        std::size_t end = s.find_first_of(separators, start);
        if (end == std::string::npos)
        {
            out.emplace_back(s.substr(start));
            break;
        }

        out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }

    return out;
}

inline std::string trimString(const std::string& str) 
{
    size_t first = str.find_first_not_of(" \t\n\r");
    size_t last = str.find_last_not_of(" \t\n\r");

    if (first == std::string::npos)
        return "";

    return str.substr(first, (last - first + 1));
}

//https://stackoverflow.com/questions/16826422/c-most-efficient-way-to-convert-string-to-int-faster-than-atoi
inline int fast_atoi(const char* str, bool returnAtInvalidChar = false)
{
    int val = 0;
    bool negative = (*str == '-') ? true : false;
    if (negative)
        str++;

    while (*str)
    {
        if (*str < '0' || *str > '9')
        {
            if (returnAtInvalidChar)
                return negative ? -val : val;
            return INT_MAX;
        }
        val = val * 10 + (*str++ - '0');
    }
    return negative ? -val : val;
}

inline bool floatFromString(const std::string& str, float& x)
{
    float value{};

    const char* first = str.data();
    const char* last = str.data() + str.size();

    auto [ptr, ec] = std::from_chars(first, last, value, std::chars_format::general);

    if (ec != std::errc{} || ptr != last)
        return false;

    x = value;
    return true;
}

inline bool uIntFromString(const std::string& str, unsigned int& x)
{
    unsigned int value{};

    const char* first = str.data();
    const char* last = str.data() + str.size();

    auto [ptr, ec] = std::from_chars(first, last, value, 10);

    if (ec != std::errc{} || ptr != last)
        return false;

    x = value;
    return true;
}


/////////////
// Vectors //
/////////////

inline void vectorfilterVector(std::vector<unsigned short>& vec, const std::vector<unsigned short>& filterVec)
{
    if (filterVec.empty())
        return;

    std::vector<unsigned short> vec2;

    for (auto i : vec)
        if (std::find(filterVec.begin(), filterVec.end(), i) != filterVec.end())
            vec2.push_back(i);

    if (!vec2.empty())
        vec = std::move(vec2);
}

inline unsigned short vectorGetRandom(const std::vector<unsigned short>& vec)
{
    if (vec.empty())
        return 0;
    return vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
}

inline bool vectorHasId(const std::vector<unsigned short>& vec, int id)
{
    if (vec.size() < 1)
        return false;

    return std::find(vec.begin(), vec.end(), id) != vec.end();
}

inline bool vectorPushUnique(std::vector<unsigned short>& vec, unsigned short value)
{
    if (std::find(vec.begin(), vec.end(), value) == vec.end())
    {
        vec.push_back(value);
        return true;
    }

    return false;
}

inline std::vector<unsigned short> vectorUnion(const std::vector<unsigned short>& vec1, const std::vector<unsigned short>& vec2)
{
    if (vec1.empty())
        return vec2;

    if (vec2.empty())
        return vec1;

    std::vector<unsigned short> vecOut;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vecOut));
    return vecOut;
}
