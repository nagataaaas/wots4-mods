#pragma once
// The menus of a talk's choice window, without touching the game: which lines a page
// shows, the text the window gets, and how the choice object is set to wait again or
// to end. wots4_game_conversation.cpp runs them on the game's objects.
#include <wots4/mod_api.h>
#include <cstring>

namespace wots4 {

// The window copies its text into 0x200 bytes (0x5b21a0), so 255 characters and the terminator.
constexpr uint32_t maxChoiceWindowChars = 255;
// The game's lines, the Mods' lines for one choice and the Next line of the last page.
constexpr uint32_t maxMenuLines = 32;
// Past maxConversationLines lines, a page shows this many and then 次へ / Next.
constexpr uint32_t linesPerPagedPage = maxConversationLines - 1;

enum class MenuLineKind : uint8_t {
    game,       // value: the game's line, which picks its jump target
    modChoice,  // value: index into hostRegistry.conversationChoices
    modItem,    // value: the item of the Mod's menu
    next,       // Shows the next page
};

// A line's text is not terminated; it points into text that lives while the menu does.
struct MenuLine {
    MenuLineKind kind;
    uint32_t value;
    const wchar_t* text;
    uint32_t length;
};

struct ConversationMenu {
    MenuLine lines[maxMenuLines];
    uint32_t count = 0;
    uint32_t page = 0;

    bool add(const MenuLine& line) {
        if (count == maxMenuLines) return false;
        lines[count++] = line;
        return true;
    }
};

// The lines of a game text, at most maxConversationLines like the window (0x5b21a0).
inline uint32_t splitGameLines(const wchar_t* text, MenuLine* lines) {
    uint32_t count = 0;
    while (count < maxConversationLines && *text) {
        const wchar_t* end = wcschr(text, L'\n');
        const auto length = static_cast<uint32_t>(end ? end - text : wcslen(text));
        lines[count] = {MenuLineKind::game, count, text, length};
        ++count;
        if (!end) break;
        text = end + 1;
    }
    return count;
}

// A Mod line to place among the game's lines.
struct PlacedLine {
    MenuLine line;
    ConversationPlacement placement;
};

// The game's lines with the Mods' lines placed among them, in the order given.
// False if they do not all fit.
inline bool placeLines(const MenuLine* gameLines, uint32_t gameLineCount, const PlacedLine* modLines,
    uint32_t modLineCount, ConversationMenu& menu) {
    menu.count = 0;
    menu.page = 0;
    const uint32_t gameLinesBefore = gameLineCount ? gameLineCount - 1 : 0;
    bool fits = true;
    for (uint32_t i = 0; i < gameLinesBefore; ++i) fits &= menu.add(gameLines[i]);
    for (uint32_t i = 0; i < modLineCount; ++i) {
        if (modLines[i].placement == conversationBeforeLast) fits &= menu.add(modLines[i].line);
    }
    if (gameLineCount) fits &= menu.add(gameLines[gameLineCount - 1]);
    for (uint32_t i = 0; i < modLineCount; ++i) {
        if (modLines[i].placement == conversationLast) fits &= menu.add(modLines[i].line);
    }
    return fits;
}

inline uint32_t pageCount(const ConversationMenu& menu) {
    if (menu.count <= maxConversationLines) return 1;
    return (menu.count + linesPerPagedPage - 1) / linesPerPagedPage;
}

// The lines a page shows, with next as its last line when the menu has more than one page.
// Returns how many (up to maxConversationLines).
inline uint32_t pageLines(const ConversationMenu& menu, uint32_t page, const MenuLine& next, MenuLine* shown) {
    if (menu.count <= maxConversationLines) {
        memcpy(shown, menu.lines, menu.count * sizeof(MenuLine));
        return menu.count;
    }
    const uint32_t first = page * linesPerPagedPage;
    const uint32_t remaining = first < menu.count ? menu.count - first : 0;
    const uint32_t count = remaining < linesPerPagedPage ? remaining : linesPerPagedPage;
    memcpy(shown, menu.lines + first, count * sizeof(MenuLine));
    shown[count] = next;
    return count + 1;
}

// Joins the lines with line breaks into buffer. The length, or 0 if they do not fit the window.
inline uint32_t composeWindowText(const MenuLine* lines, uint32_t count, wchar_t* buffer) {
    uint32_t length = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t needed = lines[i].length + (i ? 1 : 0);
        if (length + needed > maxChoiceWindowChars) return 0;
        if (i) buffer[length++] = L'\n';
        memcpy(buffer + length, lines[i].text, lines[i].length * sizeof(wchar_t));
        length += lines[i].length;
    }
    buffer[length] = 0;
    return length;
}

// ---------------------------------------------------------------- the game's choice objects

// The request the choice command (0x413cc0) builds for the choice object's constructor
// (0x4198e0), which copies it to the object's +0x10.
struct ChoiceRequest {
    uint32_t speakerId;
    const wchar_t* text;
    uint16_t targets[maxConversationLines];  // The jump target of each line
    uint16_t closedTarget;                    // The window was closed
    uint16_t timeoutTarget;                   // Left alone too long
    uint16_t walkedAwayTarget;
    uint16_t flagTarget;
};
static_assert(sizeof(ChoiceRequest) == 0x18, "the choice command builds 0x18 bytes");

// The choice object (vtable 0x97f39c). Its update (0x419a50) returns choiceDecided with the
// line's target in choiceResultTarget, choiceLeft with another target, or 0 while it waits.
constexpr uint32_t choiceResultTarget = 0x04;  // int32; -1 while undecided
constexpr uint32_t choiceFlags = 0x08;         // Bit 1: a line was decided (0x41f460 sets it)
constexpr uint32_t choiceDecidedFlag = 0x2;
constexpr uint32_t choiceResult = 0x0c;
constexpr uint32_t choiceRequestCopy = 0x10;
constexpr uint32_t choiceBalloons = 0x28;      // CBalloonManager
constexpr uint32_t choiceIdleLimit = 0x2c;     // Updates left alone before the timeout target
constexpr uint32_t choiceIdleLeft = 0x30;
constexpr uint32_t choiceWindowOpened = 0x3c;  // byte; once set, a window that is not open means closed
constexpr uint32_t choiceDecided = 3;
constexpr uint32_t choiceLeft = 4;

constexpr uint32_t balloonsChoiceWindow = 0x08;  // The open window
constexpr uint32_t windowCursor = 0x218;         // int32: the line under the cursor

template <class T>
T& choiceField(void* object, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

inline const ChoiceRequest& requestOf(void* choice) {
    return choiceField<ChoiceRequest>(choice, choiceRequestCopy);
}

// Makes the object wait for a choice again, as its constructor left it, while the window
// opens again. The idle count starts from its limit, which the frame rate Mods may change.
inline void waitAgain(void* choice) {
    choiceField<int32_t>(choice, choiceResultTarget) = -1;
    choiceField<uint32_t>(choice, choiceFlags) &= ~choiceDecidedFlag;
    choiceField<uint32_t>(choice, choiceResult) = 0;
    choiceField<int32_t>(choice, choiceIdleLeft) = choiceField<int32_t>(choice, choiceIdleLimit);
    choiceField<uint8_t>(choice, choiceWindowOpened) = 0;
}

// Ends the talk as the game does when the window is closed.
inline uint32_t leaveAsClosed(void* choice) {
    choiceField<int32_t>(choice, choiceResultTarget) = requestOf(choice).closedTarget;
    choiceField<uint32_t>(choice, choiceResult) = choiceLeft;
    return choiceLeft;
}

inline uint32_t decideTarget(void* choice, uint16_t target) {
    choiceField<int32_t>(choice, choiceResultTarget) = target;
    choiceField<uint32_t>(choice, choiceResult) = choiceDecided;
    return choiceDecided;
}

// The targets for the shown lines: a game line keeps its own, any other line gets the closed
// target, so that the talk ends if the line were decided without the ModLoader.
inline void fillTargets(const MenuLine* lines, uint32_t count, const uint16_t* gameTargets, uint16_t closedTarget,
    uint16_t* targets) {
    for (uint32_t i = 0; i < maxConversationLines; ++i) {
        const bool gameLine = i < count && lines[i].kind == MenuLineKind::game;
        targets[i] = gameLine ? gameTargets[lines[i].value] : closedTarget;
    }
}

}  // namespace wots4
