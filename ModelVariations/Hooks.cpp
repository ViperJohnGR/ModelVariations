#include "Helpers.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "Memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
    constexpr std::size_t MaxHookDescriptors = 512;

    std::array<hookinfo, MaxHookDescriptors> hookedCalls;
    std::size_t hookedCallCount = 0;
    std::array<asmhookinfo, MaxHookDescriptors> hooksASM;
    std::size_t asmHookCount = 0;
    std::size_t sharedCallStateCount = 0;

    template <typename Descriptor, std::size_t Size>
    void storeHookDescriptor(std::array<Descriptor, Size>& descriptors, std::size_t& count, const Descriptor& descriptor)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (descriptors[i].address == descriptor.address)
            {
                descriptors[i] = descriptor;
                return;
            }
        }

        if (count < descriptors.size())
            descriptors[count++] = descriptor;
        else
            Log::Write("Error! Hook diagnostic descriptor capacity exceeded at address 0x%08X\n", descriptor.address);
    }
}

SharedCallHookState* currentSharedCallHook = nullptr;

void logMissingOriginalFunction(std::uintptr_t address)
{
    Log::Write("Error! Original function not found for address 0x%08X\n", address);
}

void logMissingOriginalMethod(std::uintptr_t address)
{
    Log::Write("Error! Original method not found for address 0x%08X\n", address);
}

std::span<const hookinfo> getHookedCalls() noexcept
{
    return { hookedCalls.data(), hookedCallCount };
}

std::span<const asmhookinfo> getASMHooks() noexcept
{
    return { hooksASM.data(), asmHookCount };
}

std::size_t getSharedCallStateCount()
{
    return sharedCallStateCount;
}

std::size_t getNumMaxHooks()
{
    return MaxHookDescriptors;
}

__declspec(noinline) SharedCallHookState* __fastcall hookSharedCallImpl(SharedCallHookState* state, void* thunk, const char* name, bool isVTableAddress)
{
    // Each compiled thunk owns one state. Reinstalling it must not capture the
    // thunk itself as the original function and create an infinite call loop.
    if (state->originalFunction)
        return state;

    if (void* originalFunction = hookCallImpl(state->address, thunk, name, isVTableAddress))
    {
        state->originalFunction = originalFunction;
        ++sharedCallStateCount;
        return state;
    }

    return nullptr;
}

bool hookASM(std::uintptr_t address, std::size_t numberOfBytes, injector::memory_pointer_raw hookDest, const char* funcName)
{
    if (memoryMatchesOriginalExe(address, numberOfBytes) || forceEnableGlobal || forceEnable.contains(address))
    {
        injector::MakeJMP(address, hookDest);
        storeHookDescriptor(hooksASM, asmHookCount, { address, funcName });
        return true;
    }
    
    std::string bytes = bytesToString(address, numberOfBytes);
    auto branchDestination = injector::GetBranchDestination(address).as_int();
    std::string moduleName = LoadedModules::GetModuleAtAddress(branchDestination).first;
    const char* funcType = (strstr(funcName, "::") != nullptr) ? "Modified method" : "Modified function";

    if (branchDestination)
        Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s %s 0x%08X\n", funcType, funcName, address, bytes.c_str(), getFilenameFromPath(moduleName).c_str(), branchDestination);
    else
        Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s\n", funcType, funcName, address, bytes.c_str());

    return false;
}

void* hookCallImpl(std::uintptr_t address, void* pFunction, const char* name, bool isVTableAddress)
{
    void* originalAddress = nullptr;
    if (isVTableAddress)
    {
        originalAddress = *reinterpret_cast<void**>(address);

        if (!isAddressValid(originalAddress)) //We assume 'address' is valid since we provided it
        {
            Log::Write("Invalid vtable entry: 0x%08X is %s\n", address, bytesToString(address, 4).c_str());
            return nullptr;
        }

        injector::WriteMemory(address, pFunction, true);
    }
    else
    {
        if (isAddressValid(injector::GetBranchDestination(address).as_int()))
            originalAddress = reinterpret_cast<void*>(injector::MakeCALL(address, pFunction).as_int());
        else
        {
            if (name)
                Log::LogModifiedAddress(address, "Modified function call detected: %s - 0x%08X is %s\n", name, address, bytesToString(address, 5).c_str());
            else
                Log::LogModifiedAddress(address, "Modified function call detected: 0x%08X is %s\n", address, bytesToString(address, 5).c_str());

            return nullptr;
        }
    }

    storeHookDescriptor(hookedCalls, hookedCallCount, { address, name, originalAddress, pFunction, isVTableAddress });
    Log::Write("Added call hook %s<0x%X>\n", name ? name : "UnknownHook", address);

    return originalAddress;
}
