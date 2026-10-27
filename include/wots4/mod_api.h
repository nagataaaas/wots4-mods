#pragma once
// Mod API v2: the x86 C ABI between the ModLoader (dinput8.dll) and each
// mods\<name>\mod.dll. See docs/MOD_API.md.
// Context strings live through initialization; copy them if needed later.
// Host and modules live to process exit. No STL crosses this ABI.
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace wots4 {

constexpr uint32_t modApiVersion = 2;

// Limits of the options pages. Neither the number of pages nor the number of items
// of a page has a limit: the menus scroll, and so does a page with more items than
// its frame shows, with arrows above and below.
constexpr uint32_t optionsItemsPerScreen = 8;  // Items a page shows at once
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

// A text of an options page in both menu languages of the game. The ModLoader shows
// japanese when the game's menus are in Japanese and english otherwise.
struct LocalizedText {
    const wchar_t* japanese;
    const wchar_t* english;
};

// A game text already follows the game's language, so it is the same in both.
inline LocalizedText localizedGameText(uint32_t number) {
    return {gameText(number), gameText(number)};
}

enum OptionKind : uint32_t {
    optionToggle = 0,  // Two choices: choices[0] is "on" (value 1), choices[1] is "off" (value 0)
    optionChoice = 3,  // 2 to maxOptionChoices choices; the value is the index
    // A label without a value that groups the items below it, shown as 【label】 (Japanese
    // menus) or [label] (English menus), so up to maxOptionTextChars - 2 characters.
    // The cursor passes over it.
    // No choices (nullptr, 0) and defaultValue 0; its entry in values is not used.
    optionHeader = 4,
    // A row that shows a text in the value column while another item has one of some
    // values, and nothing otherwise; the row stays either way. The cursor passes over it.
    // Make it with optionNoticeItem. Its entry in values is not used.
    optionNotice = 5,
    // A row with a label that shows one of its choices (1 to maxOptionChoices) in the value
    // column, chosen by the Mod and not by the player, such as a connection state. The cursor
    // passes over it. While the page is shown, the ModLoader calls read every frame and shows
    // the choice of the value it writes for this item; the values of the other items are not
    // used then. A value past the choices shows the first one.
    optionStatus = 6,
};
// The cursor passes over one header, notice or status only, so two of them are never
// next to each other. A page needs an item that is none of them.

struct OptionItem {
    OptionKind kind;
    LocalizedText label;
    const LocalizedText* choices;
    uint32_t choiceCount;
    uint32_t defaultValue;  // The value the page's 初期設定に戻す button sets, like the values of read
};

// A notice that shows text while item number item (a toggle or a choice item of the same
// page) has one of values. The fields hold: label = text (up to maxOptionTextChars),
// choices = nullptr, choiceCount = bit v set for each value v, defaultValue = item.
// A value past the item's choices makes addOptionsPage refuse the page.
inline OptionItem optionNoticeItem(LocalizedText text, uint32_t item, std::initializer_list<uint32_t> values) {
    constexpr uint32_t outsideChoices = 1u << 31;
    uint32_t valueBits = 0;
    for (uint32_t value : values) valueBits |= value < maxOptionChoices ? 1u << value : outsideChoices;
    return {optionNotice, text, nullptr, valueBits, item};
}

// Called on the game thread. values has one entry per item, in the order of addOptionsPage.
struct OptionsPageCallbacks {
    void* context;
    // The page is about to be shown. Write the current value of every item to values.
    // On a page with a status item, also every frame while the page is shown: keep it quick.
    void (WINAPI* read)(void* context, uint32_t* values, uint32_t count);
    // The player changed one item.
    void (WINAPI* changed)(void* context, uint32_t item, uint32_t value);
};

using GameEventCallback = void (WINAPI*)(void* context);

// ---------------------------------------------------------------- conversations
// A talk with a townsperson shows its choices in a window of up to 4 lines. A Mod may add
// a line to the choices of an action script and, once the player picks it, show menus of
// its own in the same window. Scripts and texts are numbered as in the game's packs:
// script is the action script's ID (the ghost of the graveyard runs script 143) and a
// text is a number of that script's texts (its choices are texts 601 to 604).

constexpr uint32_t maxConversationLines = 4;        // The game's choice window shows 4 lines at most
constexpr uint32_t maxConversationTextChars = 60;   // Per line; 4 lines fit the window's 255 characters
constexpr uint32_t maxConversationMenuItems = 16;   // Past 4 lines, the ModLoader pages them with 次へ / Next
constexpr uint32_t maxScriptTextChars = 255;        // The talk's balloon holds 255 characters
constexpr uint32_t maxScriptTextVariants = 8;
constexpr uint32_t conversationCancelled = 0xffffffff;

enum ConversationPlacement : uint32_t {
    conversationBeforeLast = 0,  // Before the game's last line, which is usually 用は無い / I don't need anything
    conversationLast = 1,
};

// The choice being shown. Valid only during the callback.
struct ConversationContext {
    uint32_t size;          // sizeof(ConversationContext)
    int32_t script;         // Action script ID
    int32_t choiceText;     // The text of the game's choices
    uint32_t speakerId;     // Instance ID of the townsperson the player talks to
};

typedef uint32_t ConversationSession;  // One talk's choices; never 0

enum ConversationReply : uint32_t {
    conversationEnd = 0,       // Ends the talk, as if the player had closed the window
    conversationContinue = 1,  // The callback called showConversationMenu
};

// Called on the game thread when the choices open. TRUE shows the line.
using ConversationCondition = BOOL (WINAPI*)(void* context, const ConversationContext* conversation);
// Called on the game thread. item is the index of the chosen item (0 for an added line), or
// conversationCancelled when a Mod menu was left without a choice (closed, left alone, walked
// away); then the game goes on as it would and the reply is ignored.
using ConversationSelected = ConversationReply (WINAPI*)(void* context, const ConversationContext* conversation,
    ConversationSession session, uint32_t item);

struct ConversationChoice {
    uint32_t size;                // sizeof(ConversationChoice)
    int32_t script;
    const int32_t* choiceTexts;   // The choices that get the line
    uint32_t choiceTextCount;     // 1 or more
    LocalizedText label;          // Up to maxConversationTextChars
    ConversationPlacement placement;
    ConversationCondition shown;  // nullptr: always shown
    ConversationSelected selected;
    void* context;
};

// Which text the game is about to show.
struct ScriptTextQuery {
    uint32_t size;  // sizeof(ScriptTextQuery)
    int32_t script;
    int32_t text;
};
// Called on the game thread each time the game reads the text. Returns the index of the
// variant to show, or -1 for the game's text. Keep it quick: it may be called more than
// once for one line.
using ScriptTextChooser = int32_t (WINAPI*)(void* context, const ScriptTextQuery* query);

struct ScriptTextReplacement {
    uint32_t size;  // sizeof(ScriptTextReplacement)
    int32_t script;
    int32_t text;
    const LocalizedText* variants;  // Up to maxScriptTextChars each; lines separated by L'\n'
    uint32_t variantCount;          // 1 to maxScriptTextVariants
    ScriptTextChooser choose;       // nullptr: always variants[0]
    void* context;
};

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
    BOOL (WINAPI* addOptionsPage)(HMODULE owner, const LocalizedText* title,
        const OptionItem* items, uint32_t count, const OptionsPageCallbacks* callbacks);
    // Called once per game frame, after the game's update.
    BOOL (WINAPI* onFrame)(HMODULE owner, GameEventCallback callback, void* context);
    // Called after the game wrote its options to S4-SETTINGS.ini.
    BOOL (WINAPI* onOptionsSaved)(HMODULE owner, GameEventCallback callback, void* context);

    // HostApi only grows at the end, and an older ModLoader may lack the members
    // below. A Mod that uses one checks size >= offsetof(HostApi, member) + sizeof(member).

    // GameTextUI number in the game's language, or nullptr if the game has not loaded
    // its texts yet, the number is out of range, or this version of the game is not
    // supported. The game owns the string: copy it on the game thread before keeping it.
    const wchar_t* (WINAPI* findGameText)(uint32_t number);

    // Conversations. addConversationChoice and replaceScriptText are accepted only during
    // Wots4ModInitialize and copy everything.
    // Adds a line to the game's choices. Lines of several Mods follow the Mods' load order.
    BOOL (WINAPI* addConversationChoice)(HMODULE owner, const ConversationChoice* choice);
    // Shows another text where the game shows one of its script texts.
    BOOL (WINAPI* replaceScriptText)(HMODULE owner, const ScriptTextReplacement* replacement);
    // Accepted only inside a ConversationSelected call to owner for session. Copies the items
    // (1 to maxConversationMenuItems, up to maxConversationTextChars each); the window shows
    // them once the callback returns conversationContinue, and selected gets the choice.
    BOOL (WINAPI* showConversationMenu)(HMODULE owner, ConversationSession session,
        const LocalizedText* items, uint32_t count, ConversationSelected selected, void* context);
};

// True when the ModLoader's HostApi has member: an older ModLoader's ends earlier.
#define WOTS4_HOST_HAS(host, member) \
    ((host)->size >= offsetof(wots4::HostApi, member) + sizeof((host)->member))

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
