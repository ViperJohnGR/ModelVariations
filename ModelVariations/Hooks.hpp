#pragma once

#include <unordered_map>
#include <set>
#include <string>


#include <injector/assembly.hpp>

struct hookinfo {
    std::string name;
    void* originalFunction;
    void* changedFunction;
    bool isVTableAddress;
};

extern std::unordered_map<std::uintptr_t, std::string> hooksASM;
extern std::unordered_map<std::uintptr_t, hookinfo> hookedCalls;
extern bool forceEnableGlobal;
extern std::set<std::uintptr_t> forceEnable;

bool hookASM(std::uintptr_t address, const std::string &originalData, injector::memory_pointer_raw hookDest, const std::string &funcName);
void hookCall(std::uintptr_t address, void* pFunction, const std::string &name, bool isVTableAddress = false);

template <std::uintptr_t address, typename... Args>
void callOriginal(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        reinterpret_cast<void(__cdecl*)(Args...)>(it->second.originalFunction)(args...);
    else
        Log::Write("Error! Original function not found for address 0x%08X\n", address);
}

template <typename Ret, std::uintptr_t address, typename... Args>
Ret callOriginalAndReturn(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        return reinterpret_cast<Ret(__cdecl*)(Args...)>(it->second.originalFunction)(args...);
    else
        Log::Write("Error! Original function not found for address 0x%08X\n", address);

    return Ret{};
}


template <std::uintptr_t address, typename C, typename... Args>
void callMethodOriginal(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        reinterpret_cast<void(__thiscall*)(C, Args...)>(it->second.originalFunction)(_this, args...);
    else
        Log::Write("Error! Original method not found for address 0x%08X\n", address);
}

template <typename Ret, std::uintptr_t address, typename C, typename... Args>
Ret callMethodOriginalAndReturn(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(it->second.originalFunction)(_this, args...);
    else
        Log::Write("Error! Original method not found for address 0x%08X\n", address);

    return Ret{};
}
