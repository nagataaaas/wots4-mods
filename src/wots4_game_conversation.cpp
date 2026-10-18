// The ModLoader's hooks into the talks. A townsperson's action script shows its
// choices with command 70 (0x413cc0): it reads the choice text, builds a request
// and constructs a choice object (0x4198e0), which opens the balloon manager's
// choice window (0x5aeb90) and is updated (0x419a50) until a line is decided or
// the window is left. Three hooks serve the Mods:
// - Before the constructor copies the request, the Mods' lines are placed among the
//   game's, and the request gets the joined text and the targets of the shown lines.
// - After the update decided a line, a Mod line runs its callback instead, and a Mod
//   menu or another page opens in a new window while the same object waits again.
// - Every action script text is read through 0x41e650; a replaced text is swapped there.
// Addresses are virtual addresses of the exe (ImageBase 0x400000).
#include "wots4_game_conversation.h"
#include "wots4_conversation.h"
#include "wots4_game_hook_sites.h"

namespace wots4::game {
namespace {

// ---------------------------------------------------------------- the game's scripts

// AsScriptDB, made per script by 0x41dfd0. texts holds the count, then {text, offset of the
// string from texts} sorted by text, which 0x41e740 searches.
struct ScriptTable {
    int32_t id;
    const uint8_t* script;
    const uint8_t* texts;
};

// The brain that runs the choice command (its esi up to the constructor call, 0x413ff2): one
// call record per call depth, the first field of each being the script table it runs.
constexpr uint32_t brainCallDepth = 0x20c;
constexpr uint32_t brainCallRecords = 0x0c;
constexpr uint32_t callRecordSize = 0x20;

constexpr uint32_t openChoiceWindow = 0x5aeb90;  // esi = balloon manager, push text; ret 4

const ScriptTable* runningScript(const uint8_t* brain) {
    const uint32_t depth = *reinterpret_cast<const uint32_t*>(brain + brainCallDepth);
    return *reinterpret_cast<const ScriptTable* const*>(brain + brainCallRecords + depth * callRecordSize);
}

// The string of a script's text, as 0x41e740 finds it, or nullptr.
const wchar_t* findScriptText(const ScriptTable* table, int32_t text) {
    if (!table || !table->texts) return nullptr;
    const auto count = *reinterpret_cast<const int32_t*>(table->texts);
    const auto entries = reinterpret_cast<const int32_t*>(table->texts + sizeof(int32_t));
    int32_t low = 0, high = count - 1;
    while (low <= high) {
        const int32_t middle = low + (high - low) / 2;
        const int32_t found = entries[middle * 2];
        if (found == text) return reinterpret_cast<const wchar_t*>(table->texts + entries[middle * 2 + 1]);
        if (found < text) low = middle + 1;
        else high = middle - 1;
    }
    return nullptr;
}

// Each text is looked up once per table: the scripts stay loaded.
const wchar_t* gameTextOf(FoundScriptText& found, const ScriptTable* table) {
    if (found.scriptTable != table) {
        found.gameText = findScriptText(table, found.text);
        found.scriptTable = table;
    }
    return found.gameText;
}

void openWindow(void* balloons, const wchar_t* text) {
    const uintptr_t open = gameAddress(openChoiceWindow);
    __asm {
        push esi
        mov esi, balloons
        push text
        call open
        pop esi
    }
}

// ---------------------------------------------------------------- texts in the game's language

bool textsInJapanese;
bool conversationTextsResolved;

// Mod texts are resolved once, when the first talk needs them: the game has loaded its texts
// and decided its language by then. A text that cannot be shown stays empty and is skipped.
void resolveConversationTexts() {
    if (conversationTextsResolved) return;
    conversationTextsResolved = true;
    textsInJapanese = menusInJapanese();
    for (ConversationChoiceDefinition& choice : hostRegistry.conversationChoices) {
        const bool shown = resolveText(choice.label, textsInJapanese) && !choice.label.text.empty() &&
            choice.label.text.size() <= maxConversationTextChars && choice.label.text.find(L'\n') == std::wstring::npos;
        if (shown) continue;
        choice.label.text.clear();
        registryLog(choice.owner, "a conversation line's game text is missing or does not fit one line; the line is not shown");
    }
    for (ScriptTextReplacementDefinition& replacement : hostRegistry.scriptTextReplacements) {
        for (OptionText& variant : replacement.variants) {
            if (resolveText(variant, textsInJapanese) && variant.text.size() <= maxScriptTextChars) continue;
            variant.text.clear();
            registryLog(replacement.owner, "a script text variant's game text is missing or too long; the game's text is shown instead");
        }
    }
}

// ---------------------------------------------------------------- the talk being shown

constexpr const wchar_t* japaneseNextLine = L"次へ";
constexpr const wchar_t* englishNextLine = L"Next";

struct ModMenu {
    HMODULE owner;
    ConversationSelected selected;
    void* context;
    uint32_t count;
    wchar_t items[maxConversationMenuItems][maxConversationTextChars + 1];
};

// One choice window at a time: a new choice object replaces the one before.
struct ActiveConversation {
    void* choice = nullptr;  // The choice object the menus are shown in, or nullptr
    ConversationSession session = 0;
    ConversationContext context{};
    wchar_t gameText[maxChoiceWindowChars + 1];  // The game's lines, which the first menu points into
    uint16_t gameTargets[maxConversationLines];
    ConversationMenu menu;
    bool inModMenu = false;
    ModMenu modMenu;
};

ActiveConversation active;
ConversationSession lastSession;
wchar_t windowText[maxChoiceWindowChars + 1];  // The window copies it when it opens (0x5b21a0)

// Set only while a Mod's selected callback runs, for showConversationMenu.
struct SelectedCall {
    bool running = false;
    HMODULE owner = nullptr;
    ModMenu menu;      // What showConversationMenu copied
    bool menuShown = false;
};
SelectedCall selectedCall;

// The text replaced last, so that a replaced choice text is still recognized as the game's.
struct ReplacedText {
    const wchar_t* shown = nullptr;
    const wchar_t* game = nullptr;
};
ReplacedText lastReplaced;

MenuLine nextLine() {
    const wchar_t* text = textsInJapanese ? japaneseNextLine : englishNextLine;
    return {MenuLineKind::next, 0, text, static_cast<uint32_t>(wcslen(text))};
}

uint32_t shownLines(MenuLine* lines) {
    return pageLines(active.menu, active.menu.page, nextLine(), lines);
}

ConversationSession nextSession() {
    if (++lastSession == 0) ++lastSession;
    return lastSession;
}

// The Mods' lines for this choice text, in the order they were registered, if their conditions show them.
uint32_t collectModLines(const ScriptTable* table, const wchar_t* gameText, PlacedLine* lines, uint32_t capacity) {
    uint32_t count = 0;
    auto& choices = hostRegistry.conversationChoices;
    for (uint32_t i = 0; i < choices.size() && count < capacity; ++i) {
        ConversationChoiceDefinition& choice = choices[i];
        if (choice.script != table->id || choice.label.text.empty()) continue;
        for (FoundScriptText& found : choice.choiceTexts) {
            if (gameTextOf(found, table) != gameText) continue;
            active.context.choiceText = found.text;
            if (!choice.shown || choice.shown(choice.context, &active.context)) {
                const auto length = static_cast<uint32_t>(choice.label.text.size());
                lines[count++] = {{MenuLineKind::modChoice, i, choice.label.text.c_str(), length}, choice.placement};
            }
            break;
        }
    }
    return count;
}

// Before the choice object copies the request: places the Mods' lines among the game's.
void __cdecl beforeChoiceOpens(ChoiceRequest* request, const uint8_t* brain, void* choice) {
    active.choice = nullptr;
    const ScriptTable* table = runningScript(brain);
    if (!table || !request->text) return;
    resolveConversationTexts();
    // A choice text a Mod replaced is recognized by the game's string it replaced.
    const wchar_t* gameText = request->text == lastReplaced.shown ? lastReplaced.game : request->text;
    active.context = {sizeof(ConversationContext), table->id, 0, request->speakerId};
    PlacedLine modLines[maxMenuLines];
    const uint32_t modLineCount = collectModLines(table, gameText, modLines, maxMenuLines - maxConversationLines);
    if (!modLineCount) return;

    const size_t gameLength = wcsnlen(request->text, maxChoiceWindowChars + 1);
    if (gameLength > maxChoiceWindowChars) return;
    memcpy(active.gameText, request->text, (gameLength + 1) * sizeof(wchar_t));
    MenuLine gameLines[maxConversationLines];
    const uint32_t gameLineCount = splitGameLines(active.gameText, gameLines);
    if (!placeLines(gameLines, gameLineCount, modLines, modLineCount, active.menu)) return;

    MenuLine lines[maxConversationLines];
    const uint32_t lineCount = shownLines(lines);
    if (!composeWindowText(lines, lineCount, windowText)) {
        registryLog(nullptr, "a talk's choices with the Mods' lines are longer than the window's 255 characters; the Mods' lines are not shown");
        return;
    }
    memcpy(active.gameTargets, request->targets, sizeof(active.gameTargets));
    fillTargets(lines, lineCount, active.gameTargets, request->closedTarget, request->targets);
    request->text = windowText;
    active.choice = choice;
    active.session = nextSession();
    active.inModMenu = false;
}

// ---------------------------------------------------------------- choices

ConversationReply runSelected(HMODULE owner, ConversationSelected selected, void* context, uint32_t item) {
    selectedCall.running = true;
    selectedCall.owner = owner;
    selectedCall.menuShown = false;
    const ConversationReply reply = selected(context, &active.context, active.session, item);
    selectedCall.running = false;
    return reply;
}

void showModMenu(const ModMenu& menu) {
    active.modMenu = menu;
    active.inModMenu = true;
    active.menu.count = 0;
    active.menu.page = 0;
    for (uint32_t i = 0; i < active.modMenu.count; ++i) {
        const wchar_t* text = active.modMenu.items[i];
        active.menu.add({MenuLineKind::modItem, i, text, static_cast<uint32_t>(wcslen(text))});
    }
}

uint32_t endTalk(void* choice) {
    active.choice = nullptr;
    return leaveAsClosed(choice);
}

// Opens the window again on the menu's page; the object waits for it as for the first window.
uint32_t showPage(void* choice) {
    MenuLine lines[maxConversationLines];
    const uint32_t lineCount = shownLines(lines);
    if (!composeWindowText(lines, lineCount, windowText)) return endTalk(choice);
    waitAgain(choice);
    openWindow(choiceField<void*>(choice, choiceBalloons), windowText);
    return 0;
}

uint32_t afterModCallback(void* choice, ConversationReply reply) {
    if (reply != conversationContinue || !selectedCall.menuShown) return endTalk(choice);
    showModMenu(selectedCall.menu);
    return showPage(choice);
}

int32_t windowCursorOf(void* choice) {
    void* balloons = choiceField<void*>(choice, choiceBalloons);
    void* window = choiceField<void*>(balloons, balloonsChoiceWindow);
    return window ? choiceField<int32_t>(window, windowCursor) : -1;
}

uint32_t onLineDecided(void* choice) {
    MenuLine lines[maxConversationLines];
    const uint32_t lineCount = shownLines(lines);
    const int32_t cursor = windowCursorOf(choice);
    if (cursor < 0 || static_cast<uint32_t>(cursor) >= lineCount) return endTalk(choice);
    const MenuLine& line = lines[cursor];
    switch (line.kind) {
    case MenuLineKind::game:
        active.choice = nullptr;
        return decideTarget(choice, active.gameTargets[line.value]);
    case MenuLineKind::next:
        active.menu.page = (active.menu.page + 1) % pageCount(active.menu);
        return showPage(choice);
    case MenuLineKind::modChoice: {
        const ConversationChoiceDefinition& definition = hostRegistry.conversationChoices[line.value];
        return afterModCallback(choice, runSelected(definition.owner, definition.selected, definition.context, 0));
    }
    case MenuLineKind::modItem: {
        const ModMenu& menu = active.modMenu;
        return afterModCallback(choice, runSelected(menu.owner, menu.selected, menu.context, line.value));
    }
    }
    return endTalk(choice);
}

using ChoiceUpdate = uint32_t(__thiscall*)(void* choice);
void* choiceUpdateOriginal;
void* choiceConstructOriginal;
void* scriptTextOriginal;

// 0x419a50: the choice object's update (vtable 0x97f39c [1], thiscall, no arguments). It is
// called only through the vtable, so the usual calling convention holds.
uint32_t __fastcall choiceUpdateDetour(void* choice, void*) {
    const uint32_t result = reinterpret_cast<ChoiceUpdate>(choiceUpdateOriginal)(choice);
    if (choice != active.choice || result == 0) return result;
    if (result == choiceDecided) return onLineDecided(choice);
    if (active.inModMenu) {
        const ModMenu& menu = active.modMenu;
        runSelected(menu.owner, menu.selected, menu.context, conversationCancelled);
    }
    active.choice = nullptr;
    return result;
}

// ---------------------------------------------------------------- script texts

const wchar_t* __cdecl chooseScriptText(const ScriptTable* table, const wchar_t* gameText) {
    if (!table || !gameText) return gameText;
    resolveConversationTexts();
    for (ScriptTextReplacementDefinition& replacement : hostRegistry.scriptTextReplacements) {
        if (replacement.script != table->id || gameTextOf(replacement.found, table) != gameText) continue;
        int32_t variant = 0;
        if (replacement.choose) {
            const ScriptTextQuery query{sizeof(ScriptTextQuery), table->id, replacement.found.text};
            variant = replacement.choose(replacement.context, &query);
        }
        if (variant < 0 || static_cast<uint32_t>(variant) >= replacement.variants.size()) continue;
        const std::wstring& text = replacement.variants[variant].text;
        if (text.empty()) continue;
        lastReplaced = {text.c_str(), gameText};
        return text.c_str();
    }
    return gameText;
}

// The hooks below save every register, the flags and the x87/SSE state around the C++ code,
// like those of wots4_game_hooks.cpp: the game's optimizer may rely on more than the usual
// calling convention around these direct calls.
// Stack after "push eax" (the fxsave pointer): +4 the 0x220-byte save area, +0x224 the pushad
// registers (esi at +0x228, ebx at +0x234, eax at +0x240), +0x244 the flags.

// 0x4198e0: the choice object's constructor. ebx = the request, push the object's memory;
// ret 4. esi is still the caller's brain (0x413cc0), which the constructor does not read.
__declspec(naked) void choiceConstructHook() {
    __asm {
        pushfd
        pushad
        sub esp, 0x220
        lea eax, [esp + 15]
        and eax, 0xfffffff0
        fxsave [eax]
        push eax
        fninit
        push dword ptr [esp + 0x24c]      ; the object (after the return address at +0x248)
        push dword ptr [esp + 0x228 + 4]  ; esi: the brain
        push dword ptr [esp + 0x234 + 8]  ; ebx: the request
        call beforeChoiceOpens
        add esp, 12
        pop eax
        fxrstor [eax]
        add esp, 0x220
        popad
        popfd
        jmp dword ptr [choiceConstructOriginal]
    }
}

// 0x41e650: reads an action script text. eax = the script table, push the instruction, push
// the function; ret 8; returns the string.
__declspec(naked) void scriptTextHook() {
    __asm {
        push eax                       ; the script table, for after the call
        push dword ptr [esp + 0xc]     ; the instruction
        push dword ptr [esp + 0xc]     ; the function
        call dword ptr [scriptTextOriginal]
        pushfd
        pushad
        sub esp, 0x220
        lea ecx, [esp + 15]
        and ecx, 0xfffffff0
        fxsave [ecx]
        push ecx
        fninit
        push eax                       ; the game's text
        push dword ptr [esp + 0x24c]   ; the script table (at +0x248 before this push)
        call chooseScriptText
        add esp, 8
        mov [esp + 0x240], eax         ; returned in eax by popad
        pop ecx
        fxrstor [ecx]
        add esp, 0x220
        popad
        popfd
        lea esp, [esp + 4]
        ret 8
    }
}

// The update is hooked with the constructor: the lines placed there are decided here.
const HookSite conversationSites[] = {
    {0x4198e0, 5, {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68}, 6, choiceConstructHook, &choiceConstructOriginal},
    {0x419a50, 5, {0x55, 0x8b, 0xec, 0x51, 0x53}, 5, choiceUpdateDetour, &choiceUpdateOriginal},
};
const HookSite scriptTextSites[] = {
    {0x41e650, 6, {0x55, 0x8b, 0xec, 0x53, 0x8b, 0xd8}, 6, scriptTextHook, &scriptTextOriginal},
};

// The fields of the brain, the request, the choice object and the window, as the game uses them.
const ExpectedCode conversationCode[] = {
    // mov eax,[esi+0x20c]; mov edx,[esi]; mov ecx,eax; shl ecx,5  (the call depth)
    {0x413cea, {0x8b, 0x86, 0x0c, 0x02, 0x00, 0x00, 0x8b, 0x16, 0x8b, 0xc8, 0xc1, 0xe1, 0x05}, 13},
    {0x413cfa, {0x8d, 0x4c, 0x31, 0x0c}, 4},  // lea ecx,[ecx+esi+0xc]  (the call record)
    // mov edx,[ecx+8]; push edx; push eax; mov eax,[ecx]  (the record's script table, for 0x41e650)
    {0x413ee4, {0x8b, 0x51, 0x08, 0x52, 0x50, 0x8b, 0x01}, 7},
    // cmp eax,edi; je; push eax; lea ebx,[ebp-0x3c]; call  (esi is still the brain)
    {0x413fea, {0x3b, 0xc7, 0x74, 0x0b, 0x50, 0x8d, 0x5d, 0xc4, 0xe8}, 9},
    {0x41e726, {0x8b, 0x73, 0x08, 0x51, 0xe8}, 5},  // mov esi,[ebx+8]; push ecx; call  (the table's texts)
    {0x41e743, {0x8b, 0x06, 0x57}, 3},              // mov eax,[esi]  (their count)
    {0x5aebb6, {0x33, 0xff, 0x39, 0x7e, 0x08}, 5},  // xor edi,edi; cmp [esi+8],edi  (esi = balloons)
    {0x419a8d, {0x8b, 0x46, 0x28}, 3},              // mov eax,[esi+0x28]  (the balloons)
    {0x419aa2, {0x83, 0xb8, 0x20, 0x02, 0x00, 0x00, 0x05}, 7},  // the window is open at state 5
    {0x419ac5, {0xc6, 0x46, 0x3c, 0x01}, 4},                    // the window opened
    // movzx eax,word [esi+0x20]; mov edx,4; mov [esi+4],eax  (closed: the closed target)
    {0x419ab4, {0x0f, 0xb7, 0x46, 0x20, 0xba, 0x04, 0x00, 0x00, 0x00, 0x89, 0x46, 0x04}, 12},
    {0x419bc0, {0x8b, 0x80, 0x18, 0x02, 0x00, 0x00}, 6},  // mov eax,[eax+0x218]  (the cursor)
    // movzx eax,word [esi+eax*2+0x18]; mov [esi+4],eax; mov dword [esi+0xc],3  (decided)
    {0x419bd8, {0x0f, 0xb7, 0x44, 0x46, 0x18, 0x89, 0x46, 0x04, 0xc7, 0x46, 0x0c, 0x03}, 12},
    {0x419c0b, {0x8b, 0x4e, 0x2c, 0x89, 0x4e, 0x30}, 6},  // the idle count restarts from its limit
};

}  // namespace

void prepareConversationHooks() {
    const bool codeAsExpected = hostRegistry.supported.gameTexts && codeMatches(conversationCode);
    hostRegistry.supported.conversations = codeAsExpected && sitesMatch(conversationSites) && claimSites(conversationSites);
    hostRegistry.supported.scriptTexts = codeAsExpected && sitesMatch(scriptTextSites) && claimSites(scriptTextSites);
    if (!hostRegistry.supported.conversations)
        registryLog(nullptr, "the talk choice code is not as expected; addConversationChoice is unavailable");
    if (!hostRegistry.supported.scriptTexts)
        registryLog(nullptr, "the script text code is not as expected; replaceScriptText is unavailable");
}

bool wantsConversationHooks() {
    return !hostRegistry.conversationChoices.empty() || !hostRegistry.scriptTextReplacements.empty();
}

void installConversationHooks() {
    // A choice text replaced by a Mod is still recognized, so both groups are independent.
    if (!hostRegistry.conversationChoices.empty() && installSites(conversationSites, "talk choice")) {
        char line[96];
        sprintf_s(line, "talk choice hooks installed for %u Mod line(s)",
            static_cast<unsigned>(hostRegistry.conversationChoices.size()));
        registryLog(nullptr, line);
    }
    if (!hostRegistry.scriptTextReplacements.empty() && installSites(scriptTextSites, "script text")) {
        char line[96];
        sprintf_s(line, "script text hook installed for %u replaced text(s)",
            static_cast<unsigned>(hostRegistry.scriptTextReplacements.size()));
        registryLog(nullptr, line);
    }
}

namespace {

// The item in the game's language, or false if it is a missing game text or does not fit one line.
bool copyMenuItem(const LocalizedText& item, wchar_t* target) {
    const wchar_t* source = textsInJapanese ? item.japanese : item.english;
    if (isGameText(source)) source = gameTextString(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source)));
    const size_t length = boundedTextLength(source, maxConversationTextChars);
    if (!length || wmemchr(source, L'\n', length)) return false;
    wmemcpy(target, source, length);
    target[length] = 0;
    return true;
}

const char* conversationMenuProblem(HMODULE owner, ConversationSession session, const LocalizedText* items,
    uint32_t count, ConversationSelected selected) {
    if (!selectedCall.running || owner != selectedCall.owner || session != active.session)
        return "it was called outside a selected callback of this Mod for this talk";
    if (!items || count == 0 || count > maxConversationMenuItems) return "a menu needs 1 to 16 items";
    if (!selected) return "the selected callback is required";
    for (uint32_t i = 0; i < count; ++i) {
        if (!isValidConversationLine(items[i]))
            return "an item is missing in a language, empty, longer than 60 characters, or has a line break";
    }
    return nullptr;
}

}  // namespace

BOOL WINAPI showConversationMenu(HMODULE owner, ConversationSession session, const LocalizedText* items,
    uint32_t count, ConversationSelected selected, void* context) {
    if (const char* problem = conversationMenuProblem(owner, session, items, count, selected)) {
        registryLog(owner, std::string("showConversationMenu refused: ") + problem);
        return FALSE;
    }
    ModMenu& menu = selectedCall.menu;
    for (uint32_t i = 0; i < count; ++i) {
        if (copyMenuItem(items[i], menu.items[i])) continue;
        registryLog(owner, "showConversationMenu refused: an item's game text is missing or does not fit one line");
        return FALSE;
    }
    menu.owner = owner;
    menu.selected = selected;
    menu.context = context;
    menu.count = count;
    selectedCall.menuShown = true;
    return TRUE;
}

}  // namespace wots4::game
