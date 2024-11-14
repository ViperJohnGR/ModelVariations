#pragma once

#include "Log.hpp"
#include "LoadedModules.hpp"

#include <unordered_map>
#include <string>
#include <memory>


#include <injector/assembly.hpp>

struct hookinfo {
    std::string name;
    void* originalFunction;
    void* changedFunction;
    bool isVTableAddress;
};

extern std::unordered_map<std::uintptr_t, std::string> hooksASM;
extern std::unordered_map<std::uintptr_t, hookinfo> hookedCalls;
extern bool forceEnable;

bool hookASM(std::uintptr_t address, std::string_view originalData, injector::memory_pointer_raw hookDest, std::string_view funcName);
void hookCall(std::uintptr_t address, void* pFunction, std::string name, bool isVTableAddress = false);

template <std::uintptr_t address, typename... Args>
void callOriginal(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        reinterpret_cast<void(__cdecl*)(Args...)>(it->second.originalFunction)(args...);
}

template <typename Ret, std::uintptr_t address, typename... Args>
Ret callOriginalAndReturn(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        return reinterpret_cast<Ret(__cdecl*)(Args...)>(it->second.originalFunction)(args...);

    return 0;
}


template <std::uintptr_t address, typename C, typename... Args>
void callMethodOriginal(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        reinterpret_cast<void(__thiscall*)(C, Args...)>(it->second.originalFunction)(_this, args...);
}

template <typename Ret, std::uintptr_t address, typename C, typename... Args>
Ret callMethodOriginalAndReturn(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(it->second.originalFunction)(_this, args...);

    return 0;
}


template <std::uintptr_t at, std::uintptr_t len = 5, class FuncT>
void MakeInline(const char *funcName, const char* originalData, FuncT func)
{
    if (forceEnable || memcmp(at, originalData))
        injector::MakeInline<at, at+len>(func);
    else
        Log::LogModifiedAddress(at, "Modified method detected: %s - 0x%08X is %s\n", funcName, at, bytesToString(at, len).c_str());
}
