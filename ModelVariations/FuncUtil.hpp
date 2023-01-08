#pragma once

#include <algorithm>
#include <iterator>
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

inline bool compareUpper(const char* a, const char* b)
{
    for (int i = 0; a[i] || b[i]; i++)
        if (toupper(a[i]) != toupper(b[i]))
            return false;

    return true;
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
