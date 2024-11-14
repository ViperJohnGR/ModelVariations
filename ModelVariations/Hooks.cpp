#include "Hooks.hpp"
#include "FuncUtil.hpp"

#include <string>
#include <unordered_map>

std::unordered_map<std::uintptr_t, std::string> hooksASM;
std::unordered_map<std::uintptr_t, hookinfo> hookedCalls;

bool hookASM(std::uintptr_t address, std::string_view originalData, injector::memory_pointer_raw hookDest, std::string_view funcName)
{
    unsigned numBytes = originalData.size() / 3 + 1;

    if (!memcmp(address, originalData.data()) && forceEnable == false)
    {
        std::stringstream ss;

        for (unsigned j = 0; j < numBytes; j++)
            ss << std::setfill('0') << std::setw(2) << std::uppercase << std::hex << static_cast<unsigned int>(reinterpret_cast<unsigned char*>(address)[j]) << " ";

        std::string bytes = ss.str();
        auto branchDestination = injector::GetBranchDestination(address).as_int();
        std::string moduleName = LoadedModules::GetModuleAtAddress(branchDestination).first;
        std::string funcType = (funcName.find("::") != std::string::npos) ? "Modified method" : "Modified function";

        if (branchDestination)
            Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s %s 0x%08X\n", funcType.c_str(), funcName.data(), address, bytes.c_str(), getFilenameFromPath(moduleName).c_str(), branchDestination);
        else
            Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s\n", funcType.c_str(), funcName.data(), address, bytes.c_str());

        return false;
    }

    injector::MakeJMP(address, hookDest);

    hooksASM[address] = funcName;

    return true;
}

void hookCall(std::uintptr_t address, void* pFunction, std::string name, bool isVTableAddress)
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
