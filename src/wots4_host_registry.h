#pragma once
// What the Mods registered through HostApi: options pages and game event
// callbacks. It does not touch the game; wots4_game_hooks.cpp reads it.
// Registration runs on the startup thread only, before any hook is enabled,
// so the hooks read it without locks.
#include <wots4/mod_api.h>
#include <algorithm>
#include <cwchar>
#include <new>
#include <string>
#include <vector>

namespace wots4 {

constexpr uint32_t maxGameEventHandlers = 32;  // Per event

// One language of a text as the Mod gave it: a copied string, or a game text number.
struct TextSource {
    uint32_t gameTextNumber;  // 0 for a string
    std::wstring string;
};

// Both languages of a text. wots4_game_hooks.cpp sets text from the one the game
// uses when the menus are first built (the game may not have loaded its texts, or
// decided its language, while the Mods initialize).
struct OptionText {
    TextSource japanese;
    TextSource english;
    std::wstring text;
};

struct OptionsItemDefinition {
    OptionKind kind;
    uint32_t defaultValue;
    OptionText label;
    std::vector<OptionText> choices;
    std::vector<const wchar_t*> choicePointers;  // Into choices; the game keeps these pointers
};

struct OptionsPageDefinition {
    HMODULE owner;
    OptionText title;
    std::vector<OptionsItemDefinition> items;
    OptionsPageCallbacks callbacks;
};

struct GameEventHandler {
    HMODULE owner;
    GameEventCallback callback;
    void* context;
};

// Which groups of game hooks match this exe. Set before the Mods initialize.
struct SupportedGameHooks {
    bool options = false;
    bool frame = false;
    bool optionsSaved = false;
    bool gameTexts = false;
};

struct HostRegistry {
    bool registrationOpen = false;  // True only while Mods initialize
    SupportedGameHooks supported;
    std::vector<OptionsPageDefinition> pages;
    std::vector<GameEventHandler> frameHandlers;
    std::vector<GameEventHandler> optionsSavedHandlers;
    bool textsResolved = false;  // Every OptionText::text is final; pointers into it are safe
    void (*log)(HMODULE owner, const std::string& message) = nullptr;
};
inline HostRegistry hostRegistry;

inline void registryLog(HMODULE owner, const std::string& message) {
    if (hostRegistry.log) hostRegistry.log(owner, message);
}

// Length of text if it is 1..maxChars characters, else 0.
inline size_t boundedTextLength(const wchar_t* text, size_t maxChars) {
    if (!text) return 0;
    size_t length = wcsnlen(text, maxChars + 1);
    return length <= maxChars ? length : 0;
}

// A game text number, or a string of 1..maxChars characters.
inline bool isValidText(const wchar_t* text, size_t maxChars) {
    return isGameText(text) || boundedTextLength(text, maxChars);
}

inline bool isValidText(const LocalizedText& text, size_t maxChars) {
    return isValidText(text.japanese, maxChars) && isValidText(text.english, maxChars);
}

inline TextSource copySource(const wchar_t* text) {
    if (isGameText(text)) return {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text)), {}};
    return {0, text};
}

inline OptionText copyText(const LocalizedText& text) {
    return {copySource(text.japanese), copySource(text.english), {}};
}

// A header shows in brackets: 【label】 in the Japanese menus, [label] in the English ones,
// whose font may lack the full-width brackets. With them, the label still fits 127 characters.
struct HeaderBrackets {
    const wchar_t* open;
    const wchar_t* close;
};
constexpr HeaderBrackets japaneseHeaderBrackets{L"【", L"】"};
constexpr HeaderBrackets englishHeaderBrackets{L"[", L"]"};
constexpr size_t headerBracketChars = 2;
constexpr size_t maxHeaderTextChars = maxOptionTextChars - headerBracketChars;

// nullptr if the item is valid, else why it is not.
inline const char* optionItemProblem(const OptionItem& item) {
    if (item.kind == optionHeader && !isValidText(item.label, maxHeaderTextChars))
        return "a header label is missing in a language, empty, or longer than 125 characters";
    if (!isValidText(item.label, maxOptionTextChars))
        return "an item label is missing in a language, empty, or longer than 127 characters";
    if (item.kind == optionHeader) {
        if (item.choiceCount != 0 || item.defaultValue != 0) return "a header item has choices or a default value";
        return nullptr;
    }
    if (item.kind == optionToggle && item.choiceCount != 2) return "a toggle item needs exactly 2 choices";
    if (item.kind == optionChoice && (item.choiceCount < 2 || item.choiceCount > maxOptionChoices))
        return "a choice item needs 2 to 10 choices";
    if (item.kind != optionToggle && item.kind != optionChoice) return "an item has an unknown kind";
    if (!item.choices) return "an item has no choices";
    if (item.defaultValue >= item.choiceCount) return "an item's default value is not one of its choices";
    for (uint32_t i = 0; i < item.choiceCount; ++i) {
        if (!isValidText(item.choices[i], maxOptionTextChars))
            return "a choice is missing in a language, empty, or longer than 127 characters";
    }
    return nullptr;
}

// nullptr if the page can be added, else why it cannot.
inline const char* optionsPageProblem(const LocalizedText* title, const OptionItem* items, uint32_t count,
    const OptionsPageCallbacks* callbacks) {
    if (!hostRegistry.registrationOpen) return "it was called outside Wots4ModInitialize";
    if (!hostRegistry.supported.options) return "this version of the game is not supported";
    if (!title || !isValidText(*title, maxOptionsTitleChars))
        return "the title is missing in a language, empty, or longer than 63 characters";
    if (!items || count == 0) return "a page needs at least 1 item";
    if (!callbacks || !callbacks->read || !callbacks->changed) return "the read and changed callbacks are required";
    bool hasSelectableItem = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (const char* problem = optionItemProblem(items[i])) return problem;
        const bool header = items[i].kind == optionHeader;
        // The game's input passes over one header only; the cursor would stop on a second one.
        if (header && i > 0 && items[i - 1].kind == optionHeader) return "two header items are next to each other";
        hasSelectableItem |= !header;
    }
    if (!hasSelectableItem) return "a page needs an item that is not a header";
    return nullptr;
}

inline OptionsItemDefinition copyOptionItem(const OptionItem& item) {
    OptionsItemDefinition copy{item.kind, item.defaultValue, copyText(item.label), {}, {}};
    for (uint32_t i = 0; i < item.choiceCount; ++i) copy.choices.push_back(copyText(item.choices[i]));
    return copy;
}

// Run whenever the texts reached their final place: moving or filling a short
// std::wstring moves its characters.
inline void bindChoicePointers(OptionsPageDefinition& page) {
    for (OptionsItemDefinition& item : page.items) {
        item.choicePointers.clear();
        for (const OptionText& choice : item.choices) item.choicePointers.push_back(choice.text.c_str());
    }
}

// HostApi::addOptionsPage
inline BOOL WINAPI addOptionsPage(HMODULE owner, const LocalizedText* title, const OptionItem* items, uint32_t count,
    const OptionsPageCallbacks* callbacks) {
    if (const char* problem = optionsPageProblem(title, items, count, callbacks)) {
        registryLog(owner, std::string("addOptionsPage refused: ") + problem);
        return FALSE;
    }
    // Pages may move while the Mods register; the game gets pointers into them
    // only after registration closed, so binding again each time is enough.
    // No exception may cross the C ABI back into the Mod.
    try {
        OptionsPageDefinition page{owner, copyText(*title), {}, *callbacks};
        for (uint32_t i = 0; i < count; ++i) page.items.push_back(copyOptionItem(items[i]));
        hostRegistry.pages.push_back(std::move(page));
        for (OptionsPageDefinition& registered : hostRegistry.pages) bindChoicePointers(registered);
    } catch (const std::bad_alloc&) {
        registryLog(owner, "addOptionsPage refused: out of memory");
        return FALSE;
    }
    return TRUE;
}

inline BOOL addGameEventHandler(std::vector<GameEventHandler>& handlers, bool supported, const char* functionName,
    HMODULE owner, GameEventCallback callback, void* context) {
    const char* problem = nullptr;
    if (!hostRegistry.registrationOpen) problem = "it was called outside Wots4ModInitialize";
    else if (!supported) problem = "this version of the game is not supported";
    else if (!callback) problem = "the callback is null";
    else if (handlers.size() >= maxGameEventHandlers) problem = "all 32 callbacks are already used";
    if (problem) {
        registryLog(owner, std::string(functionName) + " refused: " + problem);
        return FALSE;
    }
    try {
        if (handlers.empty()) handlers.reserve(maxGameEventHandlers);
    } catch (const std::bad_alloc&) {
        registryLog(owner, std::string(functionName) + " refused: out of memory");
        return FALSE;
    }
    handlers.push_back({owner, callback, context});  // Within the reserved capacity
    return TRUE;
}

// Forgets what a Mod registered, when it refuses to initialize. Before any hook is installed.
inline void removeRegistrations(HMODULE owner) {
    auto ownedBy = [owner](const auto& entry) { return entry.owner == owner; };
    auto& pages = hostRegistry.pages;
    pages.erase(std::remove_if(pages.begin(), pages.end(), ownedBy), pages.end());
    for (OptionsPageDefinition& page : pages) bindChoicePointers(page);
    auto& frame = hostRegistry.frameHandlers;
    frame.erase(std::remove_if(frame.begin(), frame.end(), ownedBy), frame.end());
    auto& saved = hostRegistry.optionsSavedHandlers;
    saved.erase(std::remove_if(saved.begin(), saved.end(), ownedBy), saved.end());
}

// HostApi::onFrame
inline BOOL WINAPI onFrame(HMODULE owner, GameEventCallback callback, void* context) {
    return addGameEventHandler(hostRegistry.frameHandlers, hostRegistry.supported.frame, "onFrame",
        owner, callback, context);
}

// HostApi::onOptionsSaved
inline BOOL WINAPI onOptionsSaved(HMODULE owner, GameEventCallback callback, void* context) {
    return addGameEventHandler(hostRegistry.optionsSavedHandlers, hostRegistry.supported.optionsSaved,
        "onOptionsSaved", owner, callback, context);
}

}  // namespace wots4
