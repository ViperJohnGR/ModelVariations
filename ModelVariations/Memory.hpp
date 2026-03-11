#pragma once

#include "Log.hpp"

#include <injector/injector.hpp>

bool memcmp(std::uintptr_t address, const char* value);
void WriteMemory(std::uintptr_t address, const char *value);

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

size_t getMemoryUsage();
bool isAddressValid(std::uintptr_t address);
bool isAddressValid(void* address);


template <typename T>
T* getPointerFromAddress(std::uintptr_t address, T* fallback, int depth = 1)
{
    if (!isAddressValid(address))
    {
        Log::Write("Address 0x%08X is invalid.\n", address);
        return fallback;
    }

    auto p = reinterpret_cast<void*>(address);

    for (int i = 0; i < depth; ++i)
    {
        if (!isAddressValid(p))
        {
            Log::Write("Pointer chain broken at %d/%d (0x%08X invalid). Using fallback 0x%08X\n", i, depth-1, p, fallback);
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
