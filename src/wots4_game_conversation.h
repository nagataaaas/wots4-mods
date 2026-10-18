#pragma once
// The ModLoader's hooks into the talks' choices and script texts, which serve
// HostApi::addConversationChoice, replaceScriptText and showConversationMenu.
// See wots4_game_conversation.cpp.
#include "wots4_host_registry.h"

namespace wots4::game {

// Checks the conversation code and reserves its hook sites. Sets hostRegistry.supported's
// conversations and scriptTexts. Called by prepareGameHooks once the exe version matched.
void prepareConversationHooks();

// True if a Mod registered a line or a text.
bool wantsConversationHooks();

// Installs the hooks the Mods registered for. MinHook must be initialized.
void installConversationHooks();

// HostApi::showConversationMenu
BOOL WINAPI showConversationMenu(HMODULE owner, ConversationSession session, const LocalizedText* items,
    uint32_t count, ConversationSelected selected, void* context);

}  // namespace wots4::game
