#pragma once

#include <vector>
#include <algorithm>


inline std::vector<unsigned short> vectorUnion(std::vector<unsigned short> vec1, std::vector<unsigned short>& vec2)
{
    std::vector<unsigned short> vec;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vec));
    return vec;
}
