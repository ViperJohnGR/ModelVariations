#pragma once

#include <map>
#include <string>
#include <memory>


#include <injector/assembly.hpp>

struct hookinfo {
    std::string name;
    void* originalFunction;
    void* changedFunction;
    bool isVTableAddress;
};

extern std::map<unsigned int, hookinfo> hookedCalls;

inline void hookCall(unsigned int address, void* pFunction, std::string name, bool isVTableAddress = false)
{
    void* originalAddress;
    if (isVTableAddress)
    {
        originalAddress = *(void**)address;
        *(void**)address = pFunction;
    }
    else
        originalAddress = (void*)injector::MakeCALL(address, pFunction).as_int();

    hookedCalls.insert({ address, {name, originalAddress, pFunction, isVTableAddress} });
}

template <unsigned int address, typename... Args>
inline void callOriginal(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        reinterpret_cast<void(__cdecl*)(Args...)>(it->second.originalFunction)(args...);
}

template <typename Ret, unsigned int address, typename... Args>
inline Ret callOriginalAndReturn(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        return reinterpret_cast<Ret(__cdecl*)(Args...)>(it->second.originalFunction)(args...);

    return 0;
}


template <unsigned int address, typename C, typename... Args>
inline void callMethodOriginal(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        reinterpret_cast<void(__thiscall*)(C, Args...)>(it->second.originalFunction)(_this, args...);
}

template <typename Ret, unsigned int address, typename C, typename... Args>
inline Ret callMethodOriginalAndReturn(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
        return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(it->second.originalFunction)(_this, args...);

    return 0;
}
