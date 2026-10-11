#pragma once
// Mod API v1: the x86 C ABI between the ModLoader (dinput8.dll) and each
// mods\<name>\mod.dll. See docs/MOD_API.md.
// Context strings live through initialization; copy them if needed later.
// Host and modules live to process exit. No STL crosses this ABI.
#include <windows.h>
#include <cstdint>

namespace wots4 {

constexpr uint32_t modApiVersion = 1;

// Services of the ModLoader. owner is the calling Mod's module (ModContext::module).
struct HostApi {
    uint32_t size, version;  // sizeof(HostApi), modApiVersion
    // Appends one line to mod_loader.log.
    void (WINAPI* log)(HMODULE owner, const char* message);
    // Reserves [address, address + length) for a hook. FALSE if it overlaps any reservation.
    BOOL (WINAPI* claim)(HMODULE owner, uintptr_t address, uint32_t length);
    // Removes every reservation of owner.
    void (WINAPI* releaseClaims)(HMODULE owner);
};

// Passed to Wots4ModInitialize.
struct ModContext {
    uint32_t size, version;  // sizeof(ModContext), modApiVersion
    HMODULE loader;          // The ModLoader
    HMODULE module;          // This Mod
    HMODULE game;            // The game exe. Addresses are relative to it (ASLR is on).
    const wchar_t* directory;  // This Mod's folder, absolute, ending with a backslash
    const HostApi* host;
};

// extern "C" BOOL WINAPI Wots4ModInitialize(const ModContext*). TRUE accepts, FALSE refuses.
using Initialize = BOOL (WINAPI*)(const ModContext*);

}  // namespace wots4
