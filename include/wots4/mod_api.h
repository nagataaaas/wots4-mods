#pragma once
// Mod API v2: the x86 C ABI between the ModLoader (dinput8.dll) and each
// mods\<name>\mod.dll. See docs/MOD_API.md.
// Context strings live through initialization; copy them if needed later.
// Host and modules live to process exit. No STL crosses this ABI.
#include <windows.h>
#include <cstdint>

namespace wots4 {

constexpr uint32_t modApiVersion = 2;

// Limits of the options pages. A page has no scrolling, and its frame holds 8 items.
constexpr uint32_t maxOptionsPages = 16;      // For all Mods together
constexpr uint32_t maxOptionsItems = 8;       // Per page
constexpr uint32_t maxOptionChoices = 10;     // Per choice item
constexpr uint32_t maxOptionsTitleChars = 63;  // The menu shows at most 63 characters
constexpr uint32_t maxOptionTextChars = 127;   // Item labels and choices

// Any text of an options page may be a game text instead of a string, like
// MAKEINTRESOURCE: gameText(503) shows GameTextUI number 503 in the game's language.
constexpr uint32_t maxGameTextNumber = 0xffff;
inline const wchar_t* gameText(uint32_t number) {
    return reinterpret_cast<const wchar_t*>(static_cast<uintptr_t>(number));
}
inline bool isGameText(const wchar_t* text) {
    return text && reinterpret_cast<uintptr_t>(text) <= maxGameTextNumber;
}

enum OptionKind : uint32_t {
    optionToggle = 0,  // Two choices: choices[0] is "on" (value 1), choices[1] is "off" (value 0)
    optionChoice = 3,  // 2 to maxOptionChoices choices; the value is the index
};

struct OptionItem {
    OptionKind kind;
    const wchar_t* label;
    const wchar_t* const* choices;
    uint32_t choiceCount;
};

// Called on the game thread. values has one entry per item, in the order of addOptionsPage.
struct OptionsPageCallbacks {
    void* context;
    // The page is about to be shown. Write the current value of every item to values.
    void (WINAPI* read)(void* context, uint32_t* values, uint32_t count);
    // The player changed one item.
    void (WINAPI* changed)(void* context, uint32_t item, uint32_t value);
};

using GameEventCallback = void (WINAPI*)(void* context);

// Services of the ModLoader. owner is the calling Mod's module (ModContext::module).
struct HostApi {
    uint32_t size, version;  // sizeof(HostApi), modApiVersion
    // Appends one line to mod_loader.log.
    void (WINAPI* log)(HMODULE owner, const char* message);
    // Reserves [address, address + length) for a hook. FALSE if it overlaps any reservation.
    BOOL (WINAPI* claim)(HMODULE owner, uintptr_t address, uint32_t length);
    // Removes every reservation of owner.
    void (WINAPI* releaseClaims)(HMODULE owner);

    // The functions below are accepted only during Wots4ModInitialize. They copy
    // every string and struct. FALSE if the arguments are out of the limits above
    // or this version of the game is not supported.

    // Adds a page after the game's options pages, on the title screen and in the pause menu.
    BOOL (WINAPI* addOptionsPage)(HMODULE owner, const wchar_t* title,
        const OptionItem* items, uint32_t count, const OptionsPageCallbacks* callbacks);
    // Called once per game frame, after the game's update.
    BOOL (WINAPI* onFrame)(HMODULE owner, GameEventCallback callback, void* context);
    // Called after the game wrote its options to S4-SETTINGS.ini.
    BOOL (WINAPI* onOptionsSaved)(HMODULE owner, GameEventCallback callback, void* context);
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
