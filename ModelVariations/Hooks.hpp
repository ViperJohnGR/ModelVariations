#pragma once

#include "Log.hpp"
#include "LoadedModules.hpp"

#include <unordered_map>
#include <set>
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
extern bool forceEnableGlobal;
extern std::set<std::uintptr_t> forceEnable;

bool hookASM(std::uintptr_t address, const std::string &originalData, injector::memory_pointer_raw hookDest, const std::string &funcName);
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
void MakeInline(const std::string &funcName, const char* originalData, FuncT func)
{
    std::string funcType = (funcName.find("::") != std::string::npos) ? "Modified method" : "Modified function";

    if (memcmp(at, originalData) || forceEnable.contains(at) || forceEnableGlobal)
        injector::MakeInline<at, at + len>(func);
    else
        Log::LogModifiedAddress(at, "%s detected: %s - 0x%08X is %s\n", funcType.c_str(), funcName.c_str(), at, bytesToString(at, len).c_str());
}
