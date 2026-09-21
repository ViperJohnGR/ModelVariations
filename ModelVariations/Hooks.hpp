#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <type_traits>


#include <injector/assembly.hpp>

struct hookinfo {
    std::uintptr_t address;
    const char* name;
    void* originalFunction;
    void* changedFunction;
    bool isVTableAddress;
};

struct asmhookinfo {
    std::uintptr_t address;
    const char* name;
};

extern bool forceEnableGlobal;
extern std::set<std::uintptr_t> forceEnable;

bool hookASM(std::uintptr_t address, std::size_t numberOfBytes, injector::memory_pointer_raw hookDest, const char* funcName);
void* hookCallImpl(std::uintptr_t address, void* pFunction, const char* name, bool isVTableAddress);
void logMissingOriginalFunction(std::uintptr_t address);
void logMissingOriginalMethod(std::uintptr_t address);

std::span<const hookinfo> getHookedCalls() noexcept;
std::span<const asmhookinfo> getASMHooks() noexcept;

std::size_t getSharedCallStateCount();
std::size_t getNumMaxHooks();

template <std::uintptr_t address>
struct OriginalHookSlot {
    static inline void* function = nullptr;
};

struct SharedCallHookState {
    std::uintptr_t address;
    void* originalFunction;
};

extern SharedCallHookState* currentSharedCallHook;
__declspec(noinline) SharedCallHookState* __fastcall hookSharedCallImpl(SharedCallHookState* state, void* thunk, const char* name, bool isVTableAddress);

// Constant-initialized instruction bytes belong in executable, read-only storage.
// Pointer fields receive ordinary linker/loader relocations; no code is emitted
// or patched by the hook installer.
#pragma section(".asm", execute, read)

namespace hook_detail {
    static_assert(sizeof(void*) == 4, "Shared-call thunks require an x86 build");

#pragma pack(push, 1)
    struct SharedCallThunkCode {
        std::uint8_t setStateOpcode[2];
        SharedCallHookState** currentStateSlot;
        SharedCallHookState* state;
        std::uint8_t jumpOpcode[2];
        const void* targetSlot;
    };
#pragma pack(pop)

    static_assert(sizeof(SharedCallThunkCode) == 16);
    static_assert(offsetof(SharedCallThunkCode, currentStateSlot) == 2);
    static_assert(offsetof(SharedCallThunkCode, state) == 6);
    static_assert(offsetof(SharedCallThunkCode, jumpOpcode) == 10);
    static_assert(offsetof(SharedCallThunkCode, targetSlot) == 12);

    template <auto Target>
    struct SharedCallTarget {
        // One immutable jump target slot per handler, shared by its hook sites.
        static inline constexpr auto function = Target;
    };

    template <std::uintptr_t address, auto Target>
    struct SharedCallHookSlot {
        static inline constinit SharedCallHookState state{ address, nullptr };

        // mov dword ptr [currentSharedCallHook], &state
        // jmp dword ptr [SharedCallTarget<Target>::function]
        // Neither instruction changes the argument registers, stack, or flags.
        __declspec(allocate(".asm"))
        static inline constinit const SharedCallThunkCode code = {
            { 0xC7, 0x05 }, &currentSharedCallHook, &state,
            { 0xFF, 0x25 }, &SharedCallTarget<Target>::function
        };
    };
}

struct CapturedOriginalCall {
    std::uintptr_t address;
    void* function;

    template <typename... Args>
    void call(Args... args) const
    {
        if (function)
            reinterpret_cast<void(__cdecl*)(Args...)>(function)(args...);
        else
            logMissingOriginalFunction(address);
    }

    template <typename Ret, typename... Args>
    Ret callAndReturn(Args... args) const
    {
        if (function)
            return reinterpret_cast<Ret(__cdecl*)(Args...)>(function)(args...);

        logMissingOriginalFunction(address);
        return Ret{};
    }

    template <typename C, typename... Args>
    void callMethod(C _this, Args... args) const
    {
        if (function)
            reinterpret_cast<void(__thiscall*)(C, Args...)>(function)(_this, args...);
        else
            logMissingOriginalMethod(address);
    }

    template <typename Ret, typename C, typename... Args>
    Ret callMethodAndReturn(C _this, Args... args) const
    {
        if (function)
            return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(function)(_this, args...);

        logMissingOriginalMethod(address);
        return Ret{};
    }
};

inline CapturedOriginalCall captureCurrentOriginalCall() noexcept
{
    if (currentSharedCallHook)
        return { currentSharedCallHook->address, currentSharedCallHook->originalFunction };

    return {};
}

template <std::uintptr_t address, auto Target>
__forceinline SharedCallHookState* hookSharedCall(const char* name, bool isVTableAddress = false)
{
    using TargetType = decltype(Target);
    static_assert(std::is_pointer_v<TargetType> && std::is_function_v<std::remove_pointer_t<TargetType>>, "Hook destination must be a function pointer");

    static_assert(Target != nullptr, "Hook destination must not be null");

    using Slot = hook_detail::SharedCallHookSlot<address, Target>;
    return hookSharedCallImpl(&Slot::state, const_cast<hook_detail::SharedCallThunkCode*>(&Slot::code), name, isVTableAddress);
}

template <std::uintptr_t address, typename Function>
void hookCall(Function pFunction, const char* name, bool isVTableAddress = false)
{
    static_assert(std::is_pointer_v<Function>, "Hook destination must be a function pointer");

    void* changedFunction = reinterpret_cast<void*>(pFunction);
    if (void* originalFunction = hookCallImpl(address, changedFunction, name, isVTableAddress))
        OriginalHookSlot<address>::function = originalFunction;
}
