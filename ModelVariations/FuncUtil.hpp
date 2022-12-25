#pragma once

#include <vector>
#include <algorithm>

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
inline T rand()
{
    return (T)CGeneral::GetRandomNumberInRange(0, 2);
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
    std::vector<unsigned short> vec;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vec));
    return vec;
}
