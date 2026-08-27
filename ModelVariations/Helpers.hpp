#pragma once

#include <CGeneral.h>
#include <CVector.h>

#include <charconv>
#include <string>
#include <type_traits>
#include <vector>


bool isGameHOODLUM();
bool isGameCompact();
CVector2D convert3DVectorTo2D(const CVector& vec);
std::string getFullPath(const std::string& filename);
std::string printFilenameWithBorder(std::string_view name, char ch = '#');
bool fileExists(const std::string& filename);
bool isTimeInRange(int timeNow, int timeStart, int timeEnd);
std::string getDatetime(bool printDate, bool printTime, bool printMs);
bool loadPESection(const char* filePath, int sectionIndex, std::vector<unsigned char>* buffer, unsigned int* size, std::string_view sectionName = {});

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

std::string mvsprintf(const char* fmt, va_list ap);
std::string msprintf(const char* fmt, ...);
char* copyString(char* dest, const char* src, size_t n);
char toUpper(char c);
std::string bytesToString(std::uintptr_t address, unsigned int nBytes);
std::string fileToString(const std::string& filename);
std::string getFilenameFromPath(const std::string& path);
bool strcasestr(std::string_view src, std::string_view sub);
bool strcasecmp(std::string_view s1, std::string_view s2, size_t n = 0);
std::vector<std::string> splitString(const std::string& s, char separator);
std::vector<std::string> splitString(const std::string& s, const std::string& separators);
std::string trimString(const std::string& str);
std::string_view trimView(std::string_view text);

template<class T>
bool fromString(std::string_view s, T& x, int base = 10)
{
    static_assert((std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_floating_point_v<T>);

    auto p = s.begin(), e = s.end(); 
    bool neg = p != e && *p == '-';
    if (p != e && (*p == '-' || *p == '+'))
        ++p;

    if constexpr (std::is_integral_v<T>) 
    {
        using U = std::make_unsigned_t<T>;
        if (base < 2 || base>36 || (!std::is_signed_v<T> && neg))
            return false;
        U max = std::is_signed_v<T> ? U(std::numeric_limits<T>::max()) + neg : U(-1), n = 0;
        auto b = p;

        for (; p != e; ++p)
        {
            unsigned d = *p - '0';
            if (d > 9) 
            {
                d = (*p | 32) - 'a' + 10;
                if (d < 10)
                    return false;       // Reject '@' and '`'
            }
            if (d >= unsigned(base) || n > (max - d) / base)
                return false;
            n = n * base + d;
        }

        if (p == b)
            return false;
        x = neg ? T(U(0) - n) : T(n);
    }
    else
    {
        if (base != 10)
            return false;

        constexpr uint64_t M = (uint64_t(1) << 53) - 1;
        uint64_t n = 0, scale = 1;
        bool dot = false, any = false;

        for (; p != e; ++p)
        {
            if (*p == '.' && !dot)
            {
                dot = true;
                continue;
            }

            unsigned d = unsigned(*p - '0');
            if (d > 9 || n > (M - d) / 10)
                return false;

            any = true;
            n = n * 10 + d;

            if (dot)
            {
                if (scale > M / 10)
                    return false;
                scale *= 10;
            }
        }

        if (!any)
            return false;

        double v = double(n) / double(scale);
        if (v > double(std::numeric_limits<T>::max()))
            return false;

        x = T(neg ? -v : v);
    }
    return true;
}


/////////////
// Vectors //
/////////////

void vectorfilterVector(std::vector<unsigned short>& vec, const std::vector<unsigned short>& filterVec);
unsigned short vectorGetRandom(const std::vector<unsigned short>& vec);
bool vectorHasId(const std::vector<unsigned short>& vec, int id);
bool vectorPushUnique(std::vector<unsigned short>& vec, unsigned short value);
std::vector<unsigned short> vectorUnion(const std::vector<unsigned short>& vec1, const std::vector<unsigned short>& vec2);
