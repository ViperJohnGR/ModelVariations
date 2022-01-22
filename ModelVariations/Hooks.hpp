#pragma once

#include <map>

extern std::map<unsigned int, std::pair<void*, void*>> hookedCalls;

inline void hookCall(unsigned int address, void* pFunction)
{
    hookedCalls.insert(std::make_pair(address, std::make_pair(pFunction, (void*)injector::MakeCALL(address, pFunction).as_int())));
}

template <unsigned int address, typename... Args>
inline void callOriginal(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
    {
        unsigned int originalCall = (unsigned int)(it->second.second);
        reinterpret_cast<void(__cdecl*)(Args...)>(originalCall)(args...);
    }
}

template <typename Ret, unsigned int address, typename... Args>
inline Ret callOriginalAndReturn(Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
    {
        unsigned int originalCall = (unsigned int)(it->second.second);
        return reinterpret_cast<Ret(__cdecl*)(Args...)>(originalCall)(args...);
    }
    return 0;
}


template <unsigned int address, typename C, typename... Args>
inline void callMethodOriginal(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
    {
        unsigned int originalCall = (unsigned int)(it->second.second);
        reinterpret_cast<void(__thiscall*)(C, Args...)>(originalCall)(_this, args...);
    }
}

template <typename Ret, unsigned int address, typename C, typename... Args>
inline Ret callMethodOriginalAndReturn(C _this, Args... args)
{
    auto it = hookedCalls.find(address);
    if (it != hookedCalls.end())
    {
        unsigned int originalCall = (unsigned int)(it->second.second);
        return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(originalCall)(_this, args...);
    }
    return 0;
}
