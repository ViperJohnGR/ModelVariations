#include "Helpers.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "SA.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include <CMessages.h>

#include <ntstatus.h>


bool isGameHOODLUM()
{
    static const bool isHOODLUM = []
    {
        if (plugin::GetGameVersion() != GAME_10US_HOODLUM)
            return false;

        char exePath[MAX_PATH];
        const DWORD length = GetModuleFileName(NULL, exePath, MAX_PATH);

        if (length == 0 || length >= MAX_PATH)
            return false;

        return loadPESection(exePath, -1, NULL, NULL, ".HOODLUM");
    }();

    return isHOODLUM;
}

bool isGameCompact()
{
    return (plugin::GetGameVersion() == GAME_10US_COMPACT);
}

uint64_t charStringTo64(void* s)
{
    if (s == nullptr)
        return 0;

    uint64_t retVal = 0;
    memcpy(&retVal, s, 8);

    return retVal;
}

CVector2D convert3DVectorTo2D(const CVector& vec)
{
    return { vec.x, vec.y };
}

std::string getFullPath(const std::string& filename)
{
    return filename.find(':') != std::string::npos ? filename : (LoadedModules::GetSelfDirectory() + '\\' + filename);
}

[[nodiscard]]
std::string printFilenameWithBorder(std::string_view name, char ch)
{
    const std::size_t lineWidth = name.size() + 6;

    std::string out;
    out.reserve(3 * lineWidth + 2);

    out.append(lineWidth, ch);
    out.push_back('\n');

    out.append(2, ch);
    out.push_back(' ');
    out.append(name);
    out.push_back(' ');
    out.append(2, ch);
    out.push_back('\n');

    out.append(lineWidth, ch);

    return out;
}

bool fileExists(const std::string& filename)
{
    return GetFileAttributes(getFullPath(filename).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool isTimeInRange(int timeNow, int timeStart, int timeEnd)
{
    if (timeStart <= timeEnd) // Normal range (same day)
        return timeNow >= timeStart && timeNow <= timeEnd;

    return timeNow >= timeStart || timeNow <= timeEnd; // Wrap-around past midnight
}

std::string getDatetime(bool printDate, bool printTime, bool printMs)
{
    SYSTEMTIME systime;
    GetSystemTime(&systime);
    std::string str;

    if (printDate)
    {
        str = msprintf("%d/%d/%d", systime.wDay, systime.wMonth, systime.wYear);
        if (printTime)
            str += " ";
    }

    if (printTime)
    {
        str += msprintf("%02d:%02d:%02d", systime.wHour, systime.wMinute, systime.wSecond);

        if (printMs)
            str += msprintf(".%03d", systime.wMilliseconds);
    }

    return str;
}

bool loadPESection(const char* filePath, int sectionIndex, std::vector<unsigned char>* buffer, unsigned int* size, std::string_view sectionName)
{
    HANDLE hFile;
    HANDLE hFileMapping;
    LPVOID mapView;
    PIMAGE_DOS_HEADER dosHeader;
    PIMAGE_NT_HEADERS ntHeaders;
    PIMAGE_SECTION_HEADER sectionHeader;

    auto functionError = [&](const char* msg, int errorType)
    {
        Log::Write("Error loading executable section. %s.\n", msg);
        switch (errorType)
        {
        case 3:
            UnmapViewOfFile(mapView);
            [[fallthrough]];
        case 2:
            CloseHandle(hFileMapping);
            [[fallthrough]];
        case 1:
            CloseHandle(hFile);
        }

        return false;
    };
    
    hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return functionError("Failed to open file", 0);

    // Create a file mapping
    hFileMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hFileMapping == NULL)
        return functionError("Failed to create file mapping", 1);

    // Map the PE file into memory
    mapView = MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    if (mapView == NULL)
        return functionError("Failed to map view of file", 2);

    // Get the DOS header
    dosHeader = (PIMAGE_DOS_HEADER)mapView;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return functionError("Invalid DOS signature", 3);

    // Get the NT headers
    ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)mapView + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return functionError("Invalid NT signature", 3);

    // Get the section headers
    sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);

    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i, ++sectionHeader)
    {
        const char* rawName = reinterpret_cast<const char*>(sectionHeader->Name);

        std::size_t nameLength = 0;
        while (nameLength < IMAGE_SIZEOF_SHORT_NAME && rawName[nameLength] != '\0')
            ++nameLength;

        const std::string_view currentName(rawName, nameLength);

        const bool matches = sectionName.empty() ? i == sectionIndex : currentName == sectionName;

        if (!matches)
            continue;

        if (buffer != nullptr)
        {
            unsigned int tmpSize = 0;
            if (size == nullptr)
                size = &tmpSize;

            *size = sectionHeader->SizeOfRawData;
            buffer->resize(*size);

            memcpy(buffer->data(), static_cast<const BYTE*>(mapView) + sectionHeader->PointerToRawData, *size);
        }

        UnmapViewOfFile(mapView);
        CloseHandle(hFileMapping);
        CloseHandle(hFile);
        return true;
    }

    // Clean up resources
    UnmapViewOfFile(mapView);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);

    return false;
}

/////////////
// Strings //
/////////////

std::string mvsprintf(const char* f, va_list ap)
{
    if (!f) return {};

    std::vector<char> buffer(1024);

    while (buffer.size() <= 16 * 1024 * 1024)
    {
        va_list copy;
        va_copy(copy, ap);
        int len = vsnprintf_SA(buffer.data(), buffer.size(), f, copy);
        va_end(copy);

        if (len >= 0)
            return std::string(buffer.data(), static_cast<size_t>(len));

        buffer.resize(buffer.size() * 2);
    }

    return {};
}

std::string msprintf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string s = mvsprintf(fmt, ap);
    va_end(ap);
    return s;
}

char* copyString(char* dest, const char* src, size_t n) //Does not null-terminate
{
    if (dest == nullptr || src == nullptr)
        return nullptr;

    size_t i = 0;

    while (i < n && src[i] != '\0')
    {
        dest[i] = src[i];
        ++i;
    }

    return dest;
}

char toUpper(char c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

std::string bytesToString(std::uintptr_t address, unsigned int nBytes)
{
    const unsigned char* c = reinterpret_cast<unsigned char*>(address);
    std::string result;

    for (unsigned int i = 0; i < nBytes; ++i)
    {
        if (i > 0)
            result += ' ';
        result += msprintf("%02X", c[i]);
    }

    return result;
}

std::string fileToString(const std::string& filename)
{
    std::string str;

    HANDLE hFile = CreateFile(getFullPath(filename).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return str;

    auto filesize = GetFileSize(hFile, NULL);
    if (filesize == INVALID_FILE_SIZE || filesize == 0)
    {
        CloseHandle(hFile);
        return str;
    }

    str.resize(filesize);
    DWORD lpNumberOfBytesRead = 0;
    auto success = ReadFile(hFile, &str[0], filesize, &lpNumberOfBytesRead, NULL);

    CloseHandle(hFile);

    if (!success)
        return "";

    return str;
}

std::string getFilenameFromPath(const std::string& path)
{
    return path.substr(path.find_last_of("/\\") + 1);
}

bool strcasestr(std::string_view src, std::string_view sub)
{
    if (sub.empty())
        return true;

    if (sub.size() > src.size())
        return false;

    const char first = toUpper(sub.front());

    for (std::size_t start = 0; start <= src.size() - sub.size(); ++start)
    {
        if (toUpper(src[start]) != first)
            continue;

        std::size_t i = 1;
        while (i < sub.size() && toUpper(src[start + i]) == toUpper(sub[i]))
            ++i;

        if (i == sub.size())
            return true;
    }

    return false;
}

bool strcasecmp(std::string_view s1, std::string_view s2, size_t n)
{
    while (!s1.empty() && s1.back() == '\0')
        s1.remove_suffix(1);

    while (!s2.empty() && s2.back() == '\0')
        s2.remove_suffix(1);

    const size_t count = n == 0 ? std::min(s1.size(), s2.size()) : std::min({ s1.size(), s2.size(), n });

    for (size_t i = 0; i < count; ++i)
    {
        if (toUpper(s1[i]) != toUpper(s2[i]))
            return false;
    }

    if (n != 0 && count == n)
        return true;

    return s1.size() == s2.size();
}

std::vector<std::string> splitString(const std::string& s, char separator)
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

std::vector<std::string> splitString(const std::string& s, const std::string& separators)
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

std::string trimString(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    size_t last = str.find_last_not_of(" \t\n\r");

    if (first == std::string::npos)
        return "";

    return str.substr(first, (last - first + 1));
}

std::string_view trimView(std::string_view text)
{
    while (!text.empty())
    {
        const char c = text.front();
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        text.remove_prefix(1);
    }

    while (!text.empty())
    {
        const char c = text.back();
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        text.remove_suffix(1);
    }

    return text;
}


/////////////
// Vectors //
/////////////

void vectorfilterVector(std::vector<unsigned short>& vec, const std::vector<unsigned short>& filterVec)
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

std::vector<unsigned short> vectorReturnFilteredVector(const std::vector<unsigned short>& vec, const std::vector<unsigned short>& filterVec)
{
    if (filterVec.empty())
        return vec;

    std::vector<unsigned short> vec2;

    for (auto i : vec)
        if (std::find(filterVec.begin(), filterVec.end(), i) != filterVec.end())
            vec2.push_back(i);

    if (vec2.empty())
        return vec;

    return vec2;
}

unsigned short vectorGetRandom(const std::vector<unsigned short>& vec)
{
    if (vec.empty())
        return 0;
    return vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
}

bool vectorHasId(const std::vector<unsigned short>& vec, int id)
{
    if (vec.size() < 1)
        return false;

    return std::find(vec.begin(), vec.end(), id) != vec.end();
}

bool vectorPushUnique(std::vector<unsigned short>& vec, unsigned short value)
{
    if (std::find(vec.begin(), vec.end(), value) == vec.end())
    {
        vec.push_back(value);
        return true;
    }

    return false;
}

std::vector<unsigned short> vectorUnion(const std::vector<unsigned short>& vec1, const std::vector<unsigned short>& vec2)
{
    if (vec1.empty())
        return vec2;

    if (vec2.empty())
        return vec1;

    std::vector<unsigned short> vecOut;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vecOut));
    return vecOut;
}
