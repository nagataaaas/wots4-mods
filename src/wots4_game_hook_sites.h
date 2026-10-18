#pragma once
// What every group of the ModLoader's game hooks shares: where the game is
// loaded, and how a hook site is checked, reserved and installed.
// Addresses are virtual addresses of the exe (ImageBase 0x400000).
#include "wots4_host_registry.h"
#include "wots4_mod_loader.h"
#include <MinHook.h>
#include <cstdio>
#include <cstring>

namespace wots4::game {

constexpr uintptr_t exeImageBase = 0x400000;

inline uintptr_t gameBase;
inline HMODULE hookOwner;  // The ModLoader; its reservations keep the Mods off the hook sites

inline uintptr_t gameAddress(uint32_t virtualAddress) {
    return gameBase + virtualAddress - exeImageBase;
}

template <class T>
T& field(void* object, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// A place to hook: the original bytes to expect, and how many bytes the hook
// owns (whole instructions, at least the 5 of a jump). Operands that the
// loader relocates (ASLR) are left out of the bytes.
struct HookSite {
    uint32_t address;
    uint32_t length;
    uint8_t expected[12];
    uint32_t expectedLength;
    void* detour;
    void** original;  // Trampoline to the original code, or nullptr for mid-function hooks
};

// Code a hook relies on without hooking it, such as how the game reads a field.
struct ExpectedCode {
    uint32_t address;
    uint8_t bytes[13];
    uint32_t length;
};

template <size_t N>
bool sitesMatch(const HookSite (&sites)[N]) {
    for (const HookSite& site : sites) {
        if (memcmp(reinterpret_cast<void*>(gameAddress(site.address)), site.expected, site.expectedLength)) return false;
    }
    return true;
}

template <size_t N>
bool codeMatches(const ExpectedCode (&code)[N]) {
    for (const ExpectedCode& expected : code) {
        if (memcmp(reinterpret_cast<void*>(gameAddress(expected.address)), expected.bytes, expected.length)) return false;
    }
    return true;
}

template <size_t N>
bool claimSites(const HookSite (&sites)[N]) {
    for (const HookSite& site : sites) {
        if (!claimHook(hookOwner, gameAddress(site.address), site.length)) return false;
    }
    return true;
}

// Creates and enables every hook of a group. On failure, removes the ones it made.
// MinHook must be initialized.
template <size_t N>
bool installSites(const HookSite (&sites)[N], const char* groupName) {
    size_t created = 0;
    MH_STATUS status = MH_OK;
    for (; created < N; ++created) {
        const HookSite& site = sites[created];
        void* unusedTrampoline = nullptr;
        status = MH_CreateHook(reinterpret_cast<void*>(gameAddress(site.address)), site.detour,
            site.original ? site.original : &unusedTrampoline);
        if (status != MH_OK) break;
    }
    if (created == N) {
        for (const HookSite& site : sites) {
            status = MH_QueueEnableHook(reinterpret_cast<void*>(gameAddress(site.address)));
            if (status != MH_OK) break;
        }
        if (status == MH_OK) status = MH_ApplyQueued();
        if (status == MH_OK) return true;
    }
    for (size_t i = 0; i < created; ++i) MH_RemoveHook(reinterpret_cast<void*>(gameAddress(sites[i].address)));
    char line[160];
    sprintf_s(line, "could not install the %s hooks (MinHook: %s); that service is unavailable",
        groupName, MH_StatusToString(status));
    registryLog(nullptr, line);
    return false;
}

// The game texts, from wots4_game_hooks.cpp.
// True when the game's menus are Japanese (the language the ModLoader shows Mod texts in).
bool menusInJapanese();
// GameTextUI number in the game's language, or nullptr. No checks against an unsupported exe.
const wchar_t* gameTextString(uint32_t number);
// Sets text.text from the source of the language. False if a game text is missing or memory runs out.
bool resolveText(OptionText& text, bool japanese);

}  // namespace wots4::game
