#pragma once

#include <vector>
#include <algorithm>

#include <CGeneral.h>


inline std::vector<unsigned short> vectorUnion(std::vector<unsigned short> vec1, std::vector<unsigned short>& vec2)
{
    std::vector<unsigned short> vec;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vec));
    return vec;
}

inline unsigned short vectorGetRandom(std::vector<unsigned short> &vec)
{
    return vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
}

template <typename T>
inline T rand(int min, int max)
{
    return (T)CGeneral::GetRandomNumberInRange(min, max);
}

template <typename T>
inline T rand(int min, unsigned int max)
{
    return (T)CGeneral::GetRandomNumberInRange(min, (int)max);
}

template <typename T>
inline T rand(int max)
{
    return (T)CGeneral::GetRandomNumberInRange(0, max);
}

template <typename T>
inline T rand(unsigned int max)
{
    return (T)CGeneral::GetRandomNumberInRange(0, (int)max);
}

template <typename T>
inline T rand()
{
    return (T)CGeneral::GetRandomNumberInRange(0, 2);
}
