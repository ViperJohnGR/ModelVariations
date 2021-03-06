#pragma once

#include <vector>
#include <algorithm>
#include <iterator>


inline std::vector<unsigned short> vectorUnion(std::vector<unsigned short>& vec1, std::vector<unsigned short>& vec2)
{
    std::vector<unsigned short> vec;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vec));
    return vec;
}

inline void vectorUnion(std::vector<unsigned short>& vec1, std::vector<unsigned short>& vec2, std::vector<unsigned short>& dest)
{
    dest.clear();
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(dest));
}

inline std::string bytesToString(unsigned int address, int nBytes)
{
    std::stringstream ss;
    unsigned char* c = reinterpret_cast<unsigned char*>(address);

    for (int i = 0; i < nBytes; i++, ss << " ")
        ss << std::hex << static_cast<unsigned int>(c[i]);

    return ss.str();
}
