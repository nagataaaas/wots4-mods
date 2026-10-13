#pragma once
// The ModLoader's own hooks into the game, which serve HostApi::addOptionsPage,
// onFrame and onOptionsSaved, and the game texts of HostApi::findGameText.
// See wots4_game_hooks.cpp.
#include "wots4_host_registry.h"

namespace wots4::game {

// Checks the exe version and the bytes at every hook site, then reserves those
// ranges for the ModLoader (owner) so that no Mod hooks them directly.
// Sets hostRegistry.supported. Call before the Mods initialize.
void prepareGameHooks(HMODULE game, HMODULE owner);

// HostApi::findGameText. nullptr if this exe is not supported or the texts are not loaded.
const wchar_t* WINAPI findGameText(uint32_t number);

// Installs the hooks the Mods registered for. Call after the Mods initialized,
// on the thread that initialized them.
void installGameHooks();

}  // namespace wots4::game
