#include "Memory.hpp"

#include <charconv>
#include <psapi.h>

bool memcmp(std::uintptr_t address, const char* value)
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

void WriteMemory(std::uintptr_t address, const char *value)
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

size_t getMemoryUsage()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return pmc.PrivateUsage;

    return 0;
}

bool isAddressValid(std::uintptr_t address)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0)
        return false; // Query failed

    return (mbi.State == MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

bool isAddressValid(void* address)
{
    return isAddressValid(reinterpret_cast<std::uintptr_t>(address));
}

