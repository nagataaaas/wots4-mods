// Checks the talk menus: the pure helpers of wots4_conversation.h, then the ModLoader's
// conversation hooks against synthetic code. A block of memory stands in for the game:
// each hook site holds its original bytes followed by a jump to a small fake of the game
// function, and the probe runs the choice command's steps itself. No game exe is used.
#include "wots4_game_conversation.cpp"
#include <cstdio>
#include <iterator>
#include <string>

using namespace wots4;
using namespace wots4::game;

namespace {

unsigned tests, failures;

void check(const char* name, bool ok) {
    ++tests;
    if (!ok) ++failures;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

std::wstring joined(const MenuLine* lines, uint32_t count) {
    std::wstring text;
    for (uint32_t i = 0; i < count; ++i) {
        if (i) text += L'|';
        text.append(lines[i].text, lines[i].length);
    }
    return text;
}

MenuLine modLine(uint32_t value, const wchar_t* text) {
    return {MenuLineKind::modChoice, value, text, static_cast<uint32_t>(wcslen(text))};
}

// ---------------------------------------------------------------- pure helpers

void checkMenuHelpers() {
    MenuLine gameLines[maxConversationLines];
    check("a game text is split at its line breaks",
        splitGameLines(L"View\nCheck\nNothing", gameLines) == 3 && joined(gameLines, 3) == L"View|Check|Nothing" &&
            gameLines[2].kind == MenuLineKind::game && gameLines[2].value == 2);
    check("at most 4 game lines, like the window", splitGameLines(L"1\n2\n3\n4\n5", gameLines) == 4);

    splitGameLines(L"View\nCheck\nNothing", gameLines);
    const PlacedLine modLines[] = {{modLine(0, L"Fight"), conversationBeforeLast}, {modLine(1, L"Rest"), conversationLast},
        {modLine(2, L"Talk"), conversationBeforeLast}};
    ConversationMenu menu;
    check("Mod lines go before the last game line or after it, in their order",
        placeLines(gameLines, 3, modLines, 3, menu) && menu.count == 6 &&
            joined(menu.lines, menu.count) == L"View|Check|Fight|Talk|Nothing|Rest");

    const MenuLine next{MenuLineKind::next, 0, L"Next", 4};
    MenuLine shown[maxConversationLines];
    check("6 lines take 2 pages", pageCount(menu) == 2);
    check("a page shows 3 lines and Next", pageLines(menu, 0, next, shown) == 4 && joined(shown, 4) == L"View|Check|Fight|Next");
    check("the last page shows the rest and Next", pageLines(menu, 1, next, shown) == 4 &&
        joined(shown, 4) == L"Talk|Nothing|Rest|Next");
    placeLines(gameLines, 3, modLines, 1, menu);
    check("4 lines fit one page without Next", pageCount(menu) == 1 && pageLines(menu, 0, next, shown) == 4 &&
        joined(shown, 4) == L"View|Check|Fight|Nothing");

    wchar_t text[maxChoiceWindowChars + 1];
    check("the window text joins the lines with line breaks",
        composeWindowText(shown, 4, text) == 24 && std::wstring(text) == L"View\nCheck\nFight\nNothing");
    const std::wstring wide(64, L'w');
    const MenuLine wideLines[] = {{MenuLineKind::modItem, 0, wide.c_str(), 64}, {MenuLineKind::modItem, 1, wide.c_str(), 64},
        {MenuLineKind::modItem, 2, wide.c_str(), 64}, {MenuLineKind::modItem, 3, wide.c_str(), 64}};
    check("lines longer than the window's 255 characters are refused", composeWindowText(wideLines, 4, text) == 0);
    check("3 such lines fit (194 characters)", composeWindowText(wideLines, 3, text) == 194);

    const uint16_t gameTargets[] = {20, 30, 40, 0};
    uint16_t targets[maxConversationLines];
    fillTargets(shown, 4, gameTargets, 78, targets);
    check("game lines keep their targets, Mod lines get the closed target",
        targets[0] == 20 && targets[1] == 30 && targets[2] == 78 && targets[3] == 40);

    uint8_t choice[0x40] = {};
    choiceField<int32_t>(choice, choiceResultTarget) = 5;
    choiceField<uint32_t>(choice, choiceFlags) = 0x7;
    choiceField<uint32_t>(choice, choiceResult) = choiceDecided;
    choiceField<int32_t>(choice, choiceIdleLimit) = 300;
    choiceField<uint8_t>(choice, choiceWindowOpened) = 1;
    waitAgain(choice);
    check("waiting again clears the decision and the opened window, and restarts the idle count",
        choiceField<int32_t>(choice, choiceResultTarget) == -1 && choiceField<uint32_t>(choice, choiceFlags) == 0x5 &&
            choiceField<uint32_t>(choice, choiceResult) == 0 && choiceField<int32_t>(choice, choiceIdleLeft) == 300 &&
            choiceField<uint8_t>(choice, choiceWindowOpened) == 0);
    choiceField<ChoiceRequest>(choice, choiceRequestCopy).closedTarget = 78;
    check("leaving as closed jumps to the closed target",
        leaveAsClosed(choice) == choiceLeft && choiceField<int32_t>(choice, choiceResultTarget) == 78);
}

// ---------------------------------------------------------------- fake game memory

// Covers the hook sites, the code checks and the text manager.
constexpr uint32_t fakeStart = 0x400000;
constexpr uint32_t fakeEnd = 0x9f0000;
uint8_t* fakeImage;

uint8_t* fake(uint32_t virtualAddress) {
    return fakeImage + (virtualAddress - fakeStart);
}

void writeJump(uint32_t virtualAddress, const void* target) {
    uint8_t* at = fake(virtualAddress);
    at[0] = 0xe9;  // jmp rel32
    const auto relative = reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(at + 5);
    memcpy(at + 1, &relative, 4);
}

// The script table of the ghost (script 143) with its texts.
struct ScriptTexts {
    int32_t count;
    int32_t entries[4][2];
    wchar_t strings[4][128];
};
ScriptTexts ghostTexts;
ScriptTable ghostTable{143, nullptr, reinterpret_cast<const uint8_t*>(&ghostTexts)};
ScriptTable otherTable{7, nullptr, reinterpret_cast<const uint8_t*>(&ghostTexts)};

void setText(uint32_t index, int32_t text, const wchar_t* string) {
    ghostTexts.entries[index][0] = text;
    ghostTexts.entries[index][1] = static_cast<int32_t>(reinterpret_cast<uint8_t*>(ghostTexts.strings[index]) -
        reinterpret_cast<uint8_t*>(&ghostTexts));
    wcscpy_s(ghostTexts.strings[index], string);
}

void writeScriptTexts() {
    ghostTexts.count = 4;
    setText(0, 100, L"Welcome.");
    setText(1, 601, L"I want to check which items I can take.\nI don't need anything right now.");
    setText(2, 602, L"View the weapons.\nCheck the items.\nI don't need anything right now.");
    setText(3, 604, L"One.\nTwo.\nThree.\nFour.");
}

// The brain: call depth 0, and the record of depth 0 runs the table.
uint8_t brain[0x220];

// The balloon manager and its choice window
uint8_t window[0x25c];
uint8_t balloons[0x160];
unsigned windowsOpened;
std::wstring openedText;
void* openedBalloons;

void __cdecl recordOpenedWindow(void* manager, const wchar_t* text) {
    ++windowsOpened;
    openedBalloons = manager;
    openedText = text;
}

// 0x5aeb90: esi = the balloon manager, push the text; ret 4
__declspec(naked) void fakeOpenWindow() {
    __asm {
        push dword ptr [esp + 4]
        push esi
        call recordOpenedWindow
        add esp, 8
        ret 4
    }
}

// The constructor's body after the 5 bytes the trampoline runs (push ebp; mov ebp,esp; push -1)
// and its push imm32: copies the request (ebx) into the object, like 0x4198e0.
void* __cdecl fakeConstruct(uint8_t* choice, const ChoiceRequest* request) {
    memset(choice, 0, 0x40);
    choiceField<int32_t>(choice, choiceResultTarget) = -1;
    choiceField<ChoiceRequest>(choice, choiceRequestCopy) = *request;
    choiceField<void*>(choice, choiceBalloons) = balloons;
    choiceField<int32_t>(choice, choiceIdleLimit) = 300;
    choiceField<int32_t>(choice, choiceIdleLeft) = 300;
    recordOpenedWindow(balloons, request->text);
    return choice;
}

__declspec(naked) void fakeConstructBody() {
    __asm {
        mov esp, ebp
        pop ebp
        push ebx
        push dword ptr [esp + 8]
        call fakeConstruct
        add esp, 8
        ret 4
    }
}

// The update: the probe decides what it returns next.
enum class Player { waits, decides, closes };
Player player = Player::waits;
unsigned updates;

uint32_t __fastcall fakeUpdate(uint8_t* choice, void*) {
    ++updates;
    if (player == Player::waits) return 0;
    if (player == Player::closes) return leaveAsClosed(choice);
    const int32_t cursor = choiceField<int32_t>(window, windowCursor);
    return decideTarget(choice, requestOf(choice).targets[cursor]);
}

// After the trampoline's push ebp; mov ebp,esp; push ecx; push ebx
__declspec(naked) void fakeUpdateBody() {
    __asm {
        mov ebx, [ebp - 8]
        mov esp, ebp
        pop ebp
        jmp fakeUpdate
    }
}

unsigned textReads;

const wchar_t* __cdecl fakeScriptText(const ScriptTable* table, uint32_t, uint32_t text) {
    ++textReads;
    return findScriptText(table, static_cast<int32_t>(text));
}

// After the trampoline's push ebp; mov ebp,esp; push ebx; mov ebx,eax. In the fake, the
// instruction is the text number.
__declspec(naked) void fakeScriptTextBody() {
    __asm {
        pop ebx
        pop ebp
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        call fakeScriptText
        add esp, 12
        ret 8
    }
}

// GameTextUI and the menu language (0 is Japanese)
const wchar_t* gameTextStrings[200];
uint32_t gameTextTable[2] = {200, reinterpret_cast<uint32_t>(gameTextStrings)};
uint8_t gameTextManagerObject[0x50];

void writeFakeGame() {
    for (const HookSite& site : conversationSites) memcpy(fake(site.address), site.expected, site.expectedLength);
    for (const HookSite& site : scriptTextSites) memcpy(fake(site.address), site.expected, site.expectedLength);
    for (const ExpectedCode& code : conversationCode) memcpy(fake(code.address), code.bytes, code.length);
    memset(fake(0x4198e6), 0, 4);  // The operand of push imm32
    writeJump(0x4198ea, fakeConstructBody);
    writeJump(0x419a55, fakeUpdateBody);
    writeJump(0x41e656, fakeScriptTextBody);
    writeJump(openChoiceWindow, fakeOpenWindow);

    field<void*>(balloons, balloonsChoiceWindow) = window;
    field<const ScriptTable*>(brain, brainCallRecords) = &ghostTable;
    gameTextStrings[120] = L"Leave";
    field<uint32_t*>(gameTextManagerObject, 0x1c) = gameTextTable;
    field<uint32_t>(gameTextManagerObject, 0x4c) = 1;  // English
    *reinterpret_cast<void**>(fake(0x9e9680)) = gameTextManagerObject;
}

// ---------------------------------------------------------------- calling with registers

struct Registers {
    uint32_t eax, ecx, edx, ebx, esi, edi, ebp, stackArgument[2];
};
Registers* activeRegisters;
uintptr_t callTarget;
uint32_t stackArgumentCount;

// Calls callTarget with the registers and up to 2 stack arguments (stackArgument[0] on top),
// which the callee pops. pushad and popad keep this function's own registers.
__declspec(naked) void callWithActiveRegisters() {
    __asm {
        pushad
        mov eax, activeRegisters
        cmp stackArgumentCount, 2
        jne one
        push dword ptr [eax + 32]
    one:
        cmp stackArgumentCount, 0
        je none
        push dword ptr [eax + 28]
    none:
        mov ecx, [eax + 4]
        mov edx, [eax + 8]
        mov ebx, [eax + 12]
        mov esi, [eax + 16]
        mov edi, [eax + 20]
        mov ebp, [eax + 24]
        mov eax, [eax]
        call dword ptr [callTarget]
        push eax
        mov eax, activeRegisters
        mov [eax + 4], ecx
        mov [eax + 8], edx
        mov [eax + 12], ebx
        mov [eax + 16], esi
        mov [eax + 20], edi
        mov [eax + 24], ebp
        pop dword ptr [eax]
        popad
        ret
    }
}

Registers callAt(uint32_t virtualAddress, Registers registers, uint32_t arguments) {
    activeRegisters = &registers;
    callTarget = reinterpret_cast<uintptr_t>(fake(virtualAddress));
    stackArgumentCount = arguments;
    callWithActiveRegisters();
    return registers;
}

uint32_t address(const void* pointer) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
}

// ---------------------------------------------------------------- the choice command's steps

uint8_t choiceMemory[0x40];
constexpr uint16_t closedTarget = 78;

// What 0x413cc0 does: reads the choice text, then constructs the choice object.
const wchar_t* readText(const ScriptTable* table, int32_t text) {
    // [esp+4] the function (14 for talks), [esp+8] the instruction (here the text)
    const Registers in{address(table), 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, {14, static_cast<uint32_t>(text)}};
    const Registers out = callAt(0x41e650, in, 2);
    return reinterpret_cast<const wchar_t*>(out.eax);
}

bool constructRegistersKept;

void openChoices(const ScriptTable* table, int32_t text) {
    field<const ScriptTable*>(brain, brainCallRecords) = table;
    ChoiceRequest request{0x1234, readText(table, text), {10, 11, 12, 13}, closedTarget, 79, 80, 81};
    const Registers in{0, 0x11, 0x22, address(&request), address(brain), 0x55, 0x66, {address(choiceMemory)}};
    const Registers out = callAt(0x4198e0, in, 1);
    constructRegistersKept = out.eax == address(choiceMemory) && out.ebx == in.ebx && out.esi == in.esi &&
        out.edi == in.edi && out.ebp == in.ebp;
}

uint32_t decide(int32_t cursor) {
    choiceField<int32_t>(window, windowCursor) = cursor;
    player = Player::decides;
    const uint32_t result = reinterpret_cast<ChoiceUpdate>(fake(0x419a50))(choiceMemory);
    player = Player::waits;
    return result;
}

uint32_t closeWindow() {
    player = Player::closes;
    const uint32_t result = reinterpret_cast<ChoiceUpdate>(fake(0x419a50))(choiceMemory);
    player = Player::waits;
    return result;
}

int32_t resultTarget() {
    return choiceField<int32_t>(choiceMemory, choiceResultTarget);
}

std::wstring lines(std::initializer_list<const wchar_t*> parts) {
    std::wstring text;
    for (const wchar_t* part : parts) {
        if (!text.empty()) text += L'\n';
        text += part;
    }
    return text;
}

// ---------------------------------------------------------------- Mod side

const auto modA = reinterpret_cast<HMODULE>(0x10000);
const auto modB = reinterpret_cast<HMODULE>(0x20000);

struct ModState {
    BOOL show = TRUE;
    unsigned shownCalls, selectedCalls;
    uint32_t lastItem = 0;
    int32_t lastChoiceText = 0;
    uint32_t lastSpeaker = 0;
    ConversationSession lastSession = 0;
    // What the next selected call does: the menu it shows (empty: end the talk)
    std::vector<LocalizedText> nextMenu;
    BOOL menuAccepted = FALSE;
};
ModState stateA, stateB;
int32_t chosenVariant = 1;

BOOL WINAPI shownA(void* context, const ConversationContext* conversation) {
    auto state = static_cast<ModState*>(context);
    ++state->shownCalls;
    state->lastChoiceText = conversation->choiceText;
    return state->show;
}

ConversationReply WINAPI selectedA(void* context, const ConversationContext* conversation, ConversationSession session,
    uint32_t item) {
    auto state = static_cast<ModState*>(context);
    ++state->selectedCalls;
    state->lastItem = item;
    state->lastSession = session;
    state->lastSpeaker = conversation->speakerId;
    if (state->nextMenu.empty()) return conversationEnd;
    const HMODULE owner = state == &stateA ? modA : modB;
    state->menuAccepted = showConversationMenu(owner, session, state->nextMenu.data(),
        static_cast<uint32_t>(state->nextMenu.size()), selectedA, context);
    state->nextMenu.clear();
    return conversationContinue;
}

int32_t WINAPI chooseVariant(void*, const ScriptTextQuery* query) {
    return query->script == 143 && query->text == 100 ? chosenVariant : -1;
}

void registerMods() {
    hostRegistry = {};
    hostRegistry.supported.gameTexts = true;
    prepareConversationHooks();
    check("the synthetic conversation code is supported and reserved",
        hostRegistry.supported.conversations && hostRegistry.supported.scriptTexts);
    hostRegistry.registrationOpen = true;
    const int32_t choiceTexts[] = {601, 602, 604};
    const ConversationChoice fight{sizeof(ConversationChoice), 143, choiceTexts, 3, {L"強者たちと戦う", L"Fight the strong"},
        conversationBeforeLast, shownA, selectedA, &stateA};
    const ConversationChoice leave{sizeof(ConversationChoice), 143, choiceTexts + 1, 1, localizedGameText(120),
        conversationLast, shownA, selectedA, &stateB};
    const LocalizedText greetings[] = {{L"よく来た。", L"Glad you came."}, {L"また来たか。", L"You again."}};
    const ScriptTextReplacement greeting{sizeof(ScriptTextReplacement), 143, 100, greetings, 2, chooseVariant, nullptr};
    const LocalizedText choices602[] = {{L"武器を見る\n用は無い", L"See the weapons.\nI don't need anything right now."}};
    const ScriptTextReplacement shorter602{sizeof(ScriptTextReplacement), 143, 602, choices602, 1, nullptr, nullptr};
    check("Mod A's line accepted", addConversationChoice(modA, &fight));
    check("Mod B's line accepted", addConversationChoice(modB, &leave));
    check("replacements accepted", replaceScriptText(modB, &greeting) && replaceScriptText(modB, &shorter602));
    hostRegistry.registrationOpen = false;
}

// ---------------------------------------------------------------- checks

void checkScriptTexts() {
    const wchar_t* game = findScriptText(&ghostTable, 100);
    chosenVariant = 1;
    check("a replaced text shows the variant the Mod chose", std::wstring(readText(&ghostTable, 100)) == L"You again.");
    chosenVariant = -1;
    check("-1 shows the game's text", readText(&ghostTable, 100) == game);
    chosenVariant = 5;
    check("a variant past the list shows the game's text", readText(&ghostTable, 100) == game);
    check("another script's text is left alone", readText(&otherTable, 100) == game);
    check("an unreplaced text is left alone", readText(&ghostTable, 601) == findScriptText(&ghostTable, 601));
    chosenVariant = 0;
    Registers in{address(&ghostTable), 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, {14, 601}};
    const Registers out = callAt(0x41e650, in, 2);
    check("the text hook keeps ebx, esi, edi and ebp and pops its 2 arguments",
        out.ebx == 0x33 && out.esi == 0x44 && out.edi == 0x55 && out.ebp == 0x66);
}

void checkHiddenLine() {
    stateA.show = FALSE;
    windowsOpened = 0;
    openChoices(&ghostTable, 601);
    check("a hidden line leaves the game's choices as they are",
        stateA.shownCalls == 1 && stateA.lastChoiceText == 601 && openedText == findScriptText(&ghostTable, 601) &&
            requestOf(choiceMemory).targets[1] == 11 && active.choice == nullptr);
    check("the game decides its own line", decide(1) == choiceDecided && resultTarget() == 11);
    stateA.show = TRUE;
}

void checkAddedLine() {
    openChoices(&ghostTable, 601);
    check("the constructor hook keeps ebx, esi, edi and ebp and returns the object", constructRegistersKept);
    check("the Mod line goes before the game's last line",
        openedText == lines({L"I want to check which items I can take.", L"Fight the strong", L"I don't need anything right now."}));
    const ChoiceRequest& request = requestOf(choiceMemory);
    check("game lines keep their targets and the Mod line gets the closed target",
        request.targets[0] == 10 && request.targets[1] == closedTarget && request.targets[2] == 11 &&
            request.closedTarget == closedTarget);
    check("waiting returns 0", reinterpret_cast<ChoiceUpdate>(fake(0x419a50))(choiceMemory) == 0);
    check("the game's last line jumps to its own target", decide(2) == choiceDecided && resultTarget() == 11);

    // The Mod line, then a menu of the Mod
    openChoices(&ghostTable, 601);
    stateA.nextMenu = {{L"三勢力の強者と戦う", L"The factions"}, {L"町の強者と戦う", L"The town"}};
    choiceField<uint8_t>(choiceMemory, choiceWindowOpened) = 1;
    choiceField<int32_t>(choiceMemory, choiceIdleLeft) = 7;
    const unsigned selectedBefore = stateA.selectedCalls;
    check("choosing the Mod line keeps the object waiting", decide(1) == 0);
    check("the Mod got item 0 with the talk", stateA.selectedCalls == selectedBefore + 1 && stateA.lastItem == 0 &&
        stateA.lastSpeaker == 0x1234 && stateA.lastSession == active.session && stateA.lastSession != 0);
    check("the Mod's menu opens in a new window", stateA.menuAccepted && openedBalloons == balloons &&
        openedText == lines({L"The factions", L"The town"}));
    check("the object waits for the new window",
        resultTarget() == -1 && choiceField<uint8_t>(choiceMemory, choiceWindowOpened) == 0 &&
            choiceField<int32_t>(choiceMemory, choiceIdleLeft) == 300 && choiceField<uint32_t>(choiceMemory, choiceResult) == 0);

    // The second level has 6 items: 2 pages of 3 and Next
    stateA.nextMenu = {{L"一", L"One"}, {L"二", L"Two"}, {L"三", L"Three"}, {L"四", L"Four"}, {L"五", L"Five"},
        {L"六", L"Six"}};
    check("a menu item opens the next menu", decide(1) == 0 && stateA.lastItem == 1 &&
        openedText == lines({L"One", L"Two", L"Three", L"Next"}));
    check("Next shows the following page", decide(3) == 0 && openedText == lines({L"Four", L"Five", L"Six", L"Next"}));
    check("Next on the last page goes back to the first", decide(3) == 0 &&
        openedText == lines({L"One", L"Two", L"Three", L"Next"}));
    decide(3);
    const unsigned calls = stateA.selectedCalls;
    check("an item of the second page ends the talk as closed when the Mod ends it",
        decide(1) == choiceLeft && resultTarget() == closedTarget && stateA.selectedCalls == calls + 1 &&
            stateA.lastItem == 4 && active.choice == nullptr);
}

void checkCancel() {
    openChoices(&ghostTable, 601);
    stateA.nextMenu = {{L"はい", L"Yes"}, {L"いいえ", L"No"}};
    decide(1);
    const unsigned calls = stateA.selectedCalls;
    check("closing a Mod menu tells the Mod, and the game goes on as closed",
        closeWindow() == choiceLeft && resultTarget() == closedTarget && stateA.selectedCalls == calls + 1 &&
            stateA.lastItem == conversationCancelled && active.choice == nullptr);
    openChoices(&ghostTable, 601);
    const unsigned before = stateA.selectedCalls;
    check("closing the game's choices does not call the Mod", closeWindow() == choiceLeft && stateA.selectedCalls == before);
}

void checkMenuRefusals() {
    const LocalizedText items[] = {{L"はい", L"Yes"}};
    check("a menu outside a selected callback is refused",
        !showConversationMenu(modA, active.session, items, 1, selectedA, &stateA));
    openChoices(&ghostTable, 601);
    stateA.nextMenu = {};
    selectedCall.running = true;
    selectedCall.owner = modA;
    check("another Mod's menu is refused", !showConversationMenu(modB, active.session, items, 1, selectedA, &stateB));
    check("another talk's menu is refused", !showConversationMenu(modA, active.session + 1, items, 1, selectedA, &stateA));
    const LocalizedText broken[] = {{L"二\n行", L"Two\nlines"}};
    check("an item with a line break is refused", !showConversationMenu(modA, active.session, broken, 1, selectedA, &stateA));
    const std::vector<LocalizedText> many(17, items[0]);
    check("17 items are refused", !showConversationMenu(modA, active.session, many.data(), 17, selectedA, &stateA));
    selectedCall.running = false;
}

void checkTwoModsAndPaging() {
    // Text 602 is replaced by a shorter one; the lines are still recognized as 602's.
    openChoices(&ghostTable, 602);
    check("a replaced choice text still gets the Mods' lines, in the Mods' order and placement",
        openedText == lines({L"See the weapons.", L"Fight the strong", L"I don't need anything right now.", L"Leave"}));
    stateB.nextMenu = {};
    check("Mod B's line calls Mod B", decide(3) == choiceLeft && stateB.selectedCalls == 1);

    // 4 game lines and a Mod line: 3 lines a page and Next
    openChoices(&ghostTable, 604);
    check("5 lines are paged", openedText == lines({L"One.", L"Two.", L"Three.", L"Next"}) &&
        requestOf(choiceMemory).targets[3] == closedTarget);
    check("the second page shows the Mod line before the game's last", decide(3) == 0 &&
        openedText == lines({L"Fight the strong", L"Four.", L"Next"}));
    check("a game line of the second page jumps to its own target", decide(1) == choiceDecided && resultTarget() == 13);
}

void checkJapanese() {
    // The texts are resolved once, when the first talk opens; a Japanese game resolves again
    // only in a new process, so the probe resets that.
    conversationTextsResolved = false;
    field<uint32_t>(gameTextManagerObject, 0x4c) = 0;
    openChoices(&ghostTable, 601);
    check("a Japanese game shows the Japanese label",
        openedText == lines({L"I want to check which items I can take.", L"強者たちと戦う", L"I don't need anything right now."}));
    stateA.nextMenu = {{L"三勢力の強者と戦う", L"The factions"}, {L"町の強者と戦う", L"The town"}};
    decide(1);
    check("and the Japanese menu items", openedText == lines({L"三勢力の強者と戦う", L"町の強者と戦う"}));
}

}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);  // Keep every line if a hook crashes the probe
    checkMenuHelpers();

    fakeImage = static_cast<uint8_t*>(VirtualAlloc(nullptr, fakeEnd - fakeStart, MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE));
    if (!fakeImage) return 2;
    gameBase = reinterpret_cast<uintptr_t>(fakeImage) - (fakeStart - exeImageBase);
    hookOwner = GetModuleHandleW(nullptr);
    writeScriptTexts();
    writeFakeGame();
    registerMods();
    const MH_STATUS started = MH_Initialize();
    installConversationHooks();
    const bool installed = started == MH_OK &&
        MH_EnableHook(reinterpret_cast<void*>(gameAddress(0x4198e0))) == MH_ERROR_ENABLED &&
        MH_EnableHook(reinterpret_cast<void*>(gameAddress(0x419a50))) == MH_ERROR_ENABLED &&
        MH_EnableHook(reinterpret_cast<void*>(gameAddress(0x41e650))) == MH_ERROR_ENABLED;
    check("the 3 conversation hooks are installed", installed);
    if (!installed) {
        printf("RESULT %u passed, %u failed\n", tests - failures, failures);
        return 1;
    }
    checkScriptTexts();
    checkHiddenLine();
    checkAddedLine();
    checkCancel();
    checkMenuRefusals();
    checkTwoModsAndPaging();
    checkJapanese();

    check("hooks removed", MH_DisableHook(MH_ALL_HOOKS) == MH_OK && MH_Uninitialize() == MH_OK);
    bool restored = true;
    for (const HookSite& site : conversationSites) restored &= !memcmp(fake(site.address), site.expected, site.expectedLength);
    for (const HookSite& site : scriptTextSites) restored &= !memcmp(fake(site.address), site.expected, site.expectedLength);
    check("original bytes restored", restored);
    VirtualFree(fakeImage, 0, MEM_RELEASE);
    printf("RESULT %u passed, %u failed (synthetic code only; no game exe, no game process)\n", tests - failures, failures);
    return failures ? 1 : 0;
}
