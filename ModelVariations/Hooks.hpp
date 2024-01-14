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

inline bool hookASM(std::uintptr_t address, std::string_view originalData, injector::memory_pointer_raw hookDest, std::string_view funcName)
{
    unsigned numBytes = originalData.size() / 3 + 1;

    if (!memcmp(address, originalData.data()) && forceEnable == false)
    {
        std::stringstream ss;

        for (unsigned j = 0; j < numBytes; j++)
            ss << std::setfill('0') << std::setw(2) << std::uppercase << std::hex << static_cast<unsigned int>(reinterpret_cast<unsigned char*>(address)[j]) << " ";

        std::string bytes = ss.str();
        std::string moduleName = LoadedModules::GetModuleAtAddress(injector::GetBranchDestination(address).as_int()).first;

        if (funcName.find("::") != std::string::npos)
            Log::LogModifiedAddress(address, "Modified method detected: %s - 0x%08X is %s %s\n", funcName.data(), address, bytes.c_str(), getFilenameFromPath(moduleName).c_str());
        else
            Log::LogModifiedAddress(address, "Modified function detected: %s - 0x%08X is %s %s\n", funcName.data(), address, bytes.c_str(), getFilenameFromPath(moduleName).c_str());

        return false;
    }

    injector::MakeJMP(address, hookDest);
    hooksASM[address] = funcName;
    return true;
}

inline void hookCall(std::uintptr_t address, void* pFunction, std::string name, bool isVTableAddress = false)
{
    void* originalAddress;
    if (isVTableAddress)
    {
        originalAddress = *reinterpret_cast<void**>(address);
        *reinterpret_cast<void**>(address) = pFunction;
    }
    else
        originalAddress = reinterpret_cast<void*>(injector::MakeCALL(address, pFunction).as_int());

    hookedCalls.insert({ address, {name, originalAddress, pFunction, isVTableAddress} });
}

template <std::uintptr_t address, typename... Args>
inline void callOriginal(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        reinterpret_cast<void(__cdecl*)(Args...)>(it->second.originalFunction)(args...);
}

template <typename Ret, std::uintptr_t address, typename... Args>
inline Ret callOriginalAndReturn(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        return reinterpret_cast<Ret(__cdecl*)(Args...)>(it->second.originalFunction)(args...);

    return 0;
}


template <std::uintptr_t address, typename C, typename... Args>
inline void callMethodOriginal(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        reinterpret_cast<void(__thiscall*)(C, Args...)>(it->second.originalFunction)(_this, args...);
}

template <typename Ret, std::uintptr_t address, typename C, typename... Args>
inline Ret callMethodOriginalAndReturn(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(it->second.originalFunction)(_this, args...);

    return 0;
}


template <std::uintptr_t at, std::uintptr_t len = 5, class FuncT>
inline void MakeInline(const char *funcName, const char* originalData, FuncT func)
{
    if (forceEnable || memcmp(at, originalData))
        injector::MakeInline<at, at+len>(func);
    else
        Log::LogModifiedAddress(at, "Modified method detected: %s - 0x%08X is %s\n", funcName, at, bytesToString(at, len).c_str());
}
