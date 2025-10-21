#pragma once

#include "Log.hpp"
#include "SA.hpp"

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <charconv>
#include <type_traits>
#include <vector>

#include <CGeneral.h>
#include <CMessages.h>
#include <CStreaming.h>

#include <ntstatus.h>
#include <psapi.h>


inline bool isGameHOODLUM()
{
    return (plugin::GetGameVersion() == GAME_10US_HOODLUM);
}

inline bool isGameCompact()
{
    return (plugin::GetGameVersion() == GAME_10US_COMPACT);
}

inline CVector2D convert3DVectorTo2D(const CVector vec)
{
    return { vec.x, vec.y };
}

inline void printMessage(const char* message, unsigned int time)
{
    CMessages::AddMessageJumpQ(const_cast<char*>(message), time, 0, false);
}

inline unsigned int integerPow(unsigned int x, unsigned int power)
{
    for (unsigned int i = 1; i < power; i++)
        x *= x;

    return x;
}

inline void printFilenameWithBorder(const char* name, const char ch = '#') 
{
    size_t total_width = strlen(name) + 6; // "## " + name + " ##"

    for (size_t i = 0; i < total_width; ++i) Log::Write("%c", ch);
    Log::Write("\n");

    Log::Write("%c%c %s %c%c\n", ch, ch, name, ch, ch);

    for (size_t i = 0; i < total_width; ++i) Log::Write("%c", ch);
    Log::Write("\n");
}

inline std::string hashFile(HANDLE& hFile, DWORD filesize = 0)
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

        if (ReadFile(hFile, filebuf.data(), filesize, &lpNumberOfBytesRead, NULL))
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

                    for (auto& i : hashArray)
                        hashLen += snprintf(hashString.data() + hashLen, 65U - hashLen, "%02x", i);
                }
    }

    return hashString;
}

inline std::string hashFile(const char* filename)
{
    HANDLE hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        Log::Write("Error hashing '%s'. Couldn't open file.\n", filename);
        return "";
    }

    return hashFile(hFile);
}

inline size_t getMemoryUsage()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
        return pmc.PrivateUsage;

    return 0;
}


/////////////
// Loading //
/////////////

inline void loadModels(const std::vector<int> &vec, int Streamingflags, bool loadImmediately)
{
    for (auto i : vec)
        CStreaming__RequestModel(i, Streamingflags);

    if (loadImmediately)
        CStreaming__LoadAllRequestedModels(false);
}

inline void loadModels(int rangeMin, int rangeMax, int Streamingflags, bool loadImmediately)
{
    for (int i = rangeMin; i <= rangeMax; i++)
        CStreaming__RequestModel(i, Streamingflags);

    if (loadImmediately)
        CStreaming__LoadAllRequestedModels(false);
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
void WriteMemory(std::uintptr_t address, int value)
{
    injector::WriteMemory<T>(address, (T)value, true);
}

template <typename T>
void WriteMemory(std::uintptr_t address, unsigned int value)
{
    injector::WriteMemory<T>(address, (T)value, true);
}

inline void WriteMemory(std::uintptr_t address, const char *value)
{
    while (*value)
    {
        if (*value == ' ')
            value++;
        uint8_t iValue = 0;

        std::from_chars(value, value + 2, iValue, 16);
        injector::WriteMemory<uint8_t>(address, iValue, true);
        value += 2;
        address++;
    }
}

inline bool isAddressValid(std::uintptr_t address)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void*)address, &mbi, sizeof(mbi)) == 0)
        return false; // Query failed

    return (mbi.State == MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

inline bool isAddressValid(void* address)
{
    return isAddressValid(reinterpret_cast<std::uintptr_t>(address));
}

template <typename T>
T* getPointerFromAddress(std::uintptr_t address, T* fallback, int depth = 1)
{
    if (!isAddressValid(address))
    {
        Log::Write("Address 0x%X is invalid.\n", address);
        return fallback;
    }

    auto p = reinterpret_cast<void*>(address);

    for (int i = 0; i < depth; ++i)
    {
        if (!isAddressValid(p))
        {
            Log::Write("Pointer chain broken at %d/%d (0x%X invalid). Using fallback 0x%X\n", i, depth-1, p, fallback);
            return fallback;
        }

        p = *reinterpret_cast<void**>(p);
    }

    return reinterpret_cast<T*>(p);
}

template <typename T>
T* getPointerFromAddress(std::uintptr_t address, std::uintptr_t fallback, int depth = 1)
{
    return getPointerFromAddress<T>(address, reinterpret_cast<T*>(fallback), depth);
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

    HANDLE hFile = CreateFile(filename.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return str;

    auto filesize = GetFileSize(hFile, NULL);
    if (filesize == INVALID_FILE_SIZE)
    {
        CloseHandle(hFile);
        return str;
    }

    str.resize(filesize+1);
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
        c = (char)::toupper(c);
    });

    std::for_each(sub.begin(), sub.end(), [](char& c) {
        c = (char)::toupper(c);
    });

    if (src.find(sub) != std::string::npos)
        return true;

    return false;
}

inline std::vector<std::string> splitString(const std::string &sv, const char separator)
{
    std::vector<std::string> result;

    size_t start = 0;

    for(size_t i = 0;; i++)
    {
        if (sv[i] == separator || sv[i] == 0)
        {
            result.push_back(sv.substr(start, i - start));
            if (sv[i] == 0)
                break;
            else
                start = i + 1;
        }
    }

    return result;
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

inline float strtof(const char *str)
{
    const char* dot = str;

    while (1)
    {
        if (*dot == '.' || *dot == 0)
            break;
        dot++;
    }

    bool negative = str[0] == '-' ? true : false;
    if (negative)
        str++;

    int integerPart = fast_atoi(str, true);
    //if (integerPart == INT_MAX)
        //return NAN;

    if (*dot == 0)
        return static_cast<float>(integerPart);

    int decimalPart = fast_atoi(dot+1);
    if (decimalPart == INT_MAX)
        return NAN;

    auto finalFloat = (float)integerPart + (float)decimalPart / (float)integerPow(10, strlen(dot + 1));

    return negative ? -finalFloat : finalFloat;
}

inline size_t ltrim(char** str, size_t n)
{
    size_t len = (n > 0) ? n : strlen(*str);
    size_t i = 0;

    while (i < len && isspace((*str)[i]))
        i++;

    *str += i;

    return len - i;
}

inline size_t rtrim(char** str, size_t n)
{
    size_t len = (n > 0) ? n : strlen(*str);

    if (len == 0)
        return 0;

    for (size_t i = len - 1; isspace((*str)[i]); i--)
    {
        (*str)[i] = 0;
        len--;
    }

    return len;
}

inline size_t trim(char** str, size_t n = 0)
{
    size_t len = ltrim(str, n);
    len = rtrim(str, len);
    return len;
}

inline char* strtok(char* str, const char* delim, size_t* token_length) {
    static char* next_token = NULL;
    if (str != NULL)
        next_token = str;
    else if (next_token == NULL)
        return NULL; // No more tokens

    // Skip leading delimiters
    char* start = next_token;
    while (*start && strchr(delim, *start))
        start++;

    if (*start == '\0') 
    {
        next_token = NULL; // End of string reached
        return NULL;
    }

    // Find the end of the token
    char* end = start;
    while (*end && !strchr(delim, *end))
        end++;

    // If a token is found, update `next_token`
    if (*end) 
    {
        *end = '\0';        // Null-terminate the token
        next_token = end + 1; // Move to the next character after the delimiter
    }
    else
        next_token = NULL; // End of the string

    if (token_length)
        *token_length = (size_t)(end - start);

    return start;
}

/////////////
// Vectors //
/////////////

inline void vectorfilterVector(std::vector<unsigned short>& vec, std::vector<unsigned short>& filterVec)
{
    if (filterVec.empty())
        return;

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

inline unsigned short vectorGetRandom(const std::vector<unsigned short>& vec)
{
    assert(vec.size() > 0);
    return vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
}

inline bool vectorHasId(const std::vector<unsigned short>& vec, int id)
{
    if (vec.size() < 1)
        return false;

    if (vec.size() < 50)
        return std::find(vec.begin(), vec.end(), id) != vec.end();

    return std::binary_search(vec.begin(), vec.end(), id);
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
