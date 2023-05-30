#pragma once

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <charconv>
#include <type_traits>
#include <vector>

#include <CGeneral.h>
#include <CMessages.h>
#include <CStreaming.h>


inline void printMessage(const char* message, unsigned int time)
{
    CMessages::AddMessageJumpQ(const_cast<char*>(message), time, 0, false);
}


/////////////
// Loading //
/////////////

inline void loadModels(std::vector<int> vec, int Streamingflags, bool loadImmediately)
{
    for (auto& i : vec)
        CStreaming::RequestModel(i, Streamingflags);

    if (loadImmediately)
        CStreaming::LoadAllRequestedModels(false);
}

inline void loadModels(int rangeMin, int rangeMax, int Streamingflags, bool loadImmediately)
{
    for (int i = rangeMin; i <= rangeMax; i++)
        CStreaming::RequestModel(i, Streamingflags);

    if (loadImmediately)
        CStreaming::LoadAllRequestedModels(false);
}


////////////
// Memory //
////////////

inline bool memcmp(std::uintptr_t address, const char* value)
{
    while (*value)
    {
        if (*value == ' ')
            value++;
        uint8_t iValue = 0;

        std::from_chars(value, value + 2, iValue, 16);
        if (*reinterpret_cast<uint8_t*>(address) != iValue)
            return false;
        value += 2;
        address++;
    }

    return true;
}

template <typename T>
inline void WriteMemory(std::uintptr_t address, int value)
{
    injector::WriteMemory<T>(address, (T)value, true);
}

template <typename T>
inline void WriteMemory(std::uintptr_t address, unsigned int value)
{
    injector::WriteMemory<T>(address, (T)value, true);
}


////////////
// Random //
////////////

template <typename T>
inline T rand(int min, unsigned int max)
{
    return (T)CGeneral::GetRandomNumberInRange(min, (int)max);
}

template <typename T>
inline bool rand()
{
    static_assert(std::is_same_v<T, bool>, "invalid type for template");

    return (bool)CGeneral::GetRandomNumberInRange(0, 2);
}


/////////////
// Strings //
/////////////

inline std::string bytesToString(std::uintptr_t address, int nBytes)
{
    std::stringstream ss;
    const unsigned char* c = reinterpret_cast<unsigned char*>(address);

    for (int i = 0; i < nBytes; i++, ss << " ")
        ss << std::setfill('0') << std::uppercase << std::setw(2) << std::hex << static_cast<unsigned int>(c[i]);

    return ss.str();
}

inline std::string fileToString(std::string_view filename)
{
    std::stringstream ss;
    std::ifstream file(filename.data());

    if (file.is_open())
        ss << file.rdbuf();

    return ss.str();
}

inline bool compareUpper(const char* a, const char* b)
{
    for (int i = 0; a[i] || b[i]; i++)
        if (toupper(a[i]) != toupper(b[i]))
            return false;

    return true;
}

inline std::string getFilenameFromPath(std::string_view path)
{
    std::string filename = path.data();
    return filename.substr(filename.find_last_of("/\\") + 1);
}

inline bool strcasestr(std::string src, std::string sub)
{
    std::for_each(src.begin(), src.end(), [](char& c) {
        c = (char)::toupper(c);
    });

    std::for_each(sub.begin(), sub.end(), [](char& c) {
        c = (char)::toupper(c);
    });

    if (src.find(sub) != std::string::npos)
        return true;

    return false;
}


/////////////
// Vectors //
/////////////

inline void vectorfilterVector(std::vector<unsigned short>& vec, std::vector<unsigned short>& filterVec)
{
    bool matchFound = false;
    std::vector<unsigned short> vec2 = vec;

    auto it = vec.begin();
    while (it != vec.end())
        if (std::find(filterVec.begin(), filterVec.end(), *it) != filterVec.end())
        {
            matchFound = true;
            ++it;
        }
        else
            it = vec.erase(it);

    if (matchFound == false)
        vec = vec2;
}

inline unsigned short vectorGetRandom(std::vector<unsigned short>& vec)
{
    return vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
}

inline bool vectorHasId(std::vector<unsigned short>& vec, int id)
{
    if (vec.size() < 1)
        return false;

    if (std::find(vec.begin(), vec.end(), id) != vec.end())
        return true;

    return false;
}

inline std::vector<unsigned short> vectorUnion(std::vector<unsigned short> vec1, std::vector<unsigned short>& vec2)
{
    std::vector<unsigned short> vecOut;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vecOut));
    return vecOut;
}
