// Runs the ModLoader's game hooks against synthetic code. A block of memory
// stands in for the game: each hook site holds its original bytes, the code it
// resumes to records where it arrived, and the game functions the hooks call
// jump to small fakes below. No game exe is used.
#include "wots4_game_hooks.cpp"
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <vector>

using namespace wots4;
using namespace wots4::game;

namespace {

unsigned tests, failures;

void check(const char* name, bool ok) {
    ++tests;
    if (!ok) ++failures;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

// ---------------------------------------------------------------- fake game memory

// Covers every address the hooks use, from 0x500000 to 0x9f0000.
constexpr uint32_t fakeStart = 0x500000;
constexpr uint32_t fakeEnd = 0x9f0000;
uint8_t* fakeImage;

uint8_t* fake(uint32_t virtualAddress) {
    return fakeImage + (virtualAddress - fakeStart);
}

void writeBytes(uint32_t virtualAddress, std::initializer_list<uint8_t> bytes) {
    memcpy(fake(virtualAddress), bytes.begin(), bytes.size());
}

void writeJump(uint32_t virtualAddress, const void* target) {
    uint8_t* at = fake(virtualAddress);
    at[0] = 0xe9;  // jmp rel32
    const auto relative = reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(at + 5);
    memcpy(at + 1, &relative, 4);
}

// mov ebx, marker; ret
void writeArrival(uint32_t virtualAddress, uint8_t marker) {
    writeBytes(virtualAddress, {0xbb, marker, 0, 0, 0, 0xc3});
}

void writeSlot(uint32_t virtualAddress, const void* function) {
    memcpy(fake(virtualAddress), &function, 4);
}

// ---------------------------------------------------------------- fake game functions

unsigned objectsAllocated, objectsReleased, arraysReleased;
unsigned pagesDestroyed, layoutCalls, mouseCalls, unexpectedMethodCalls;

void* __cdecl fakeNew(size_t size) {
    ++objectsAllocated;
    return malloc(size);
}

void __cdecl fakeDelete(void* object) {
    ++objectsReleased;
    free(object);
}

void* __cdecl fakeNewArray(size_t size) {
    return malloc(size);
}

void __cdecl fakeDeleteArray(void* array) {
    ++arraysReleased;
    free(array);
}

// The page's item vector (begin, end, capacity end), like the game's 10 slots.
constexpr uint32_t itemVector = 0x4c;
constexpr uint32_t itemCapacity = 10;

uintptr_t fakePageMethods[optionsPageMethodCount];

void* __stdcall fakePageConstructor(void* page) {
    field<uintptr_t*>(page, 0) = fakePageMethods;
    auto items = static_cast<void**>(malloc(itemCapacity * sizeof(void*)));
    field<void**>(page, itemVector) = items;
    field<void**>(page, itemVector + 4) = items;
    field<void**>(page, itemVector + 8) = items + itemCapacity;
    return page;
}

void __fastcall fakePageDestroy(void* page, void*, uint32_t flags) {
    ++pagesDestroyed;
    auto begin = field<void**>(page, itemVector), end = field<void**>(page, itemVector + 4);
    for (void** item = begin; item != end; ++item) fakeDelete(*item);
    free(begin);
    if (flags & 1) fakeDelete(page);
}

// Like the game: initialize builds the items, then shows the current values.
void __fastcall fakePageInitialize(void* page, void*) {
    reinterpret_cast<void(__thiscall*)(void*)>(virtualMethod(page, buildItemsSlot))(page);
    reinterpret_cast<void(__thiscall*)(void*)>(virtualMethod(page, refreshItemsSlot))(page);
}

void __fastcall fakePageUpdate(void*, void*) {}

void __fastcall fakeUnexpectedMethod(void*, void*) {
    ++unexpectedMethodCalls;
}

void __cdecl fakeItemConstruct(uint8_t* item) {
    memset(item, 0, itemSize);
}

__declspec(naked) void fakeItemConstructor() {  // esi = item
    __asm {
        push esi
        call fakeItemConstruct
        add esp, 4
        mov eax, esi
        ret
    }
}

int __cdecl fakeAppend(void* page, void* item) {
    auto& end = field<void**>(page, itemVector + 4);
    if (end == field<void**>(page, itemVector + 8)) return -1;
    *end++ = item;
    return static_cast<int>(end - field<void**>(page, itemVector)) - 1;
}

__declspec(naked) void fakeAppendEntry() {  // eax = page, [esp+4] = item, ret 4
    __asm {
        push dword ptr [esp + 4]
        push eax
        call fakeAppend
        add esp, 8
        ret 4
    }
}

struct ListEntry {
    uint32_t index;
    std::wstring text;
};
std::vector<ListEntry> listEntries;

void __fastcall fakeListAdd(void*, void*, uint32_t index, const wchar_t* text) {
    listEntries.push_back({index, text});
}

__declspec(naked) void fakeListLayout() {  // eax = list
    __asm {
        inc layoutCalls
        ret
    }
}

__declspec(naked) void fakeListMouse() {  // edi = list
    __asm {
        inc mouseCalls
        ret
    }
}

void writeFakeGame() {
    writeSlot(gameNewSlot, fakeNew);
    writeSlot(gameDeleteSlot, fakeDelete);
    writeSlot(gameDeleteArraySlot, fakeDeleteArray);
    writeJump(gameNewArray, fakeNewArray);
    writeJump(optionsPageConstructor, fakePageConstructor);
    writeJump(optionItemConstructor, fakeItemConstructor);
    writeJump(optionsPageAppend, fakeAppendEntry);
    writeJump(titleListAdd, fakeListAdd);
    writeJump(titleListLayout, fakeListLayout);
    writeJump(titleListMouse, fakeListMouse);

    for (uintptr_t& method : fakePageMethods) method = reinterpret_cast<uintptr_t>(fakeUnexpectedMethod);
    fakePageMethods[0] = reinterpret_cast<uintptr_t>(fakePageDestroy);
    fakePageMethods[1] = reinterpret_cast<uintptr_t>(fakePageInitialize);
    fakePageMethods[2] = reinterpret_cast<uintptr_t>(fakePageUpdate);
    memcpy(fake(optionsPageMethods), fakePageMethods, sizeof(fakePageMethods));
}

// Each site gets its original bytes. Function sites continue into a body that
// returns; mid-function sites resume at code that records the arrival in ebx.
void writeHookSites() {
    auto writeSites = [](const HookSite* sites, size_t count) {
        for (size_t i = 0; i < count; ++i) memcpy(fake(sites[i].address), sites[i].expected, sites[i].expectedLength);
    };
    writeSites(optionsMenuSites, std::size(optionsMenuSites));
    writeSites(frameSites, std::size(frameSites));
    writeSites(optionsSavedSites, std::size(optionsSavedSites));

    // The operand of "push imm32", then mov esp, ebp; pop ebp; ret
    writeBytes(0x676126, {0x00, 0x00, 0x00, 0x00, 0x8b, 0xe5, 0x5d, 0xc3});
    writeBytes(0x676056, {0x00, 0x00, 0x00, 0x00, 0x8b, 0xe5, 0x5d, 0xc3});
    // mov eax, [ecx+0x90]; mov esp, ebp; pop ebp; ret (the builder returns its rows)
    writeBytes(0x676cbc, {0x8b, 0x81, 0x90, 0x00, 0x00, 0x00, 0x8b, 0xe5, 0x5d, 0xc3});
    writeBytes(0x50e876, {0x8b, 0xe5, 0x5d, 0xc3});
    writeBytes(0x536ef9, {0x8b, 0xe5, 0x5d, 0xc3});

    writeBytes(0x676543, {0xc3});
    writeBytes(0x6765c7, {0xc3});
    writeBytes(0x6765ea, {0xc3});
    writeBytes(0x676704, {0xc3});
    writeArrival(0x676649, 1);
    writeArrival(0x676652, 2);
    writeArrival(0x6766d0, 1);
    writeArrival(0x6766da, 2);
    writeArrival(0x5c635d, 1);
    writeArrival(0x5c636e, 2);
    writeArrival(0x5c642c, 1);
    writeArrival(0x5c643d, 2);
}

// ---------------------------------------------------------------- calling with registers

struct Registers {
    uint32_t eax, ecx, edx, ebx, esi, edi;
};
Registers* activeRegisters;
uintptr_t callTarget;

__declspec(naked) void callWithActiveRegisters() {
    __asm {
        pushad
        mov eax, activeRegisters
        mov ecx, [eax + 4]
        mov edx, [eax + 8]
        mov ebx, [eax + 12]
        mov esi, [eax + 16]
        mov edi, [eax + 20]
        mov eax, [eax]
        call dword ptr [callTarget]
        push eax
        mov eax, activeRegisters
        mov [eax + 4], ecx
        mov [eax + 8], edx
        mov [eax + 12], ebx
        mov [eax + 16], esi
        mov [eax + 20], edi
        pop dword ptr [eax]
        popad
        ret
    }
}

Registers callAt(uint32_t virtualAddress, Registers registers) {
    activeRegisters = &registers;
    callTarget = reinterpret_cast<uintptr_t>(fake(virtualAddress));
    callWithActiveRegisters();
    return registers;
}

uint32_t address(const void* pointer) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
}

// ---------------------------------------------------------------- Mod side

struct ModState {
    uint32_t values[maxOptionsItems];
    unsigned reads, changes;
    uint32_t lastItem, lastValue;
};
ModState modA{{1, 2}}, modB{{0, 9}};  // 9 is out of range for B's choice item
unsigned frames, optionsSaved;

void WINAPI readValues(void* context, uint32_t* values, uint32_t count) {
    auto state = static_cast<ModState*>(context);
    ++state->reads;
    memcpy(values, state->values, count * sizeof(uint32_t));
}

void WINAPI valueChanged(void* context, uint32_t item, uint32_t value) {
    auto state = static_cast<ModState*>(context);
    ++state->changes;
    state->lastItem = item;
    state->lastValue = value;
}

// GameTextUI: the manager's +0x1c points to {count, strings}; +0x4c is the menu language.
constexpr uint32_t gameTextCount = 1000;
const wchar_t* gameTextStrings[gameTextCount];
uint32_t gameTextTable[2] = {gameTextCount, reinterpret_cast<uint32_t>(gameTextStrings)};
uint8_t gameTextManagerObject[0x50];
constexpr uint32_t englishLanguage = 1;

// The game in English, as most checks expect.
void loadGameTexts() {
    gameTextStrings[502] = L"Connect";
    gameTextStrings[503] = L"Page B";
    field<uint32_t*>(gameTextManagerObject, gameTextUiTable) = gameTextTable;
    field<uint32_t>(gameTextManagerObject, gameTextLanguage) = englishLanguage;
    writeSlot(gameTextManager, gameTextManagerObject);
}

void WINAPI countFrame(void*) { ++frames; }
void WINAPI countOptionsSaved(void*) { ++optionsSaved; }

void registerMods() {
    hostRegistry = {};
    hostRegistry.supported = {true, true, true};
    hostRegistry.registrationOpen = true;
    const auto mod = reinterpret_cast<HMODULE>(0x10000);
    const LocalizedText onOff[] = {{L"はい", L"Yes"}, {L"いいえ", L"No"}};
    const LocalizedText levels[] = {{L"低", L"Low"}, {L"中", L"Mid"}, {L"高", L"High"}, {L"全", L"Any"}};
    const OptionItem itemsA[] = {
        {optionToggle, localizedGameText(502), onOff, 2}, {optionChoice, {L"段階", L"Level"}, levels, 4}};
    const OptionItem itemsB[] = {
        {optionToggle, {L"表示", L"Show"}, onOff, 2}, {optionChoice, {L"大きさ", L"Size"}, levels, 3}};
    const OptionsPageCallbacks callbacksA{&modA, readValues, valueChanged};
    const OptionsPageCallbacks callbacksB{&modB, readValues, valueChanged};
    const LocalizedText titleA{L"ページA", L"Page A"};
    const LocalizedText titleB = localizedGameText(503);
    addOptionsPage(mod, &titleA, itemsA, 2, &callbacksA);
    addOptionsPage(mod, &titleB, itemsB, 2, &callbacksB);
    onFrame(mod, countFrame, nullptr);
    onOptionsSaved(mod, countOptionsSaved, nullptr);
    hostRegistry.registrationOpen = false;
}

// ---------------------------------------------------------------- checks

const wchar_t* itemText(void* item, uint32_t offset) {
    return reinterpret_cast<const wchar_t*>(static_cast<uint8_t*>(item) + offset);
}

void checkModPage(void* page, const char* name, bool expectA) {
    char label[96];
    const PageExtension& extension = extensionOf(page);
    sprintf_s(label, "%s: page uses the Mod page methods and is built", name);
    check(label, field<uintptr_t*>(page, 0) == modPageMethods && extension.built == 1);
    uint8_t* toggle = extension.items[0];
    uint8_t* choice = extension.items[1];
    sprintf_s(label, "%s: toggle texts and state from read", name);
    check(label, wcscmp(itemText(toggle, itemLabel), expectA ? L"Connect" : L"Show") == 0 &&
        wcscmp(itemText(toggle, itemOnText), L"Yes") == 0 && wcscmp(itemText(toggle, itemOffText), L"No") == 0 &&
        toggle[itemToggleState] == (expectA ? 1 : 0) && field<uint32_t>(toggle, itemKind) == optionToggle);
    sprintf_s(label, "%s: choice kind, pointers and index from read", name);
    const auto choices = reinterpret_cast<const wchar_t* const*>(choice + itemChoices);
    const uint32_t count = expectA ? 4 : 3;
    check(label, field<uint32_t>(choice, itemKind) == optionChoice && wcscmp(choices[0], L"Low") == 0 &&
        choices[count - 1] && !choices[count] &&
        field<uint32_t>(choice, itemChoiceIndex) == (expectA ? 2u : 0u));
}

void checkTitleScreen() {
    uintptr_t parentMemory[0x84 / 4] = {};
    uint8_t list[0x6c] = {};
    auto parent = reinterpret_cast<uint8_t*>(parentMemory);
    parent[titleParentStarted] = 1;
    field<void*>(parent, titleParentList) = list;
    uintptr_t gamePages[gameOptionsPageCount][1] = {{0x11}, {0x22}, {0x33}, {0x44}, {0x55}};
    for (uint32_t i = 0; i < gameOptionsPageCount; ++i)
        parentMemory[titleParentPagesOffset / 4 + i] = reinterpret_cast<uintptr_t>(gamePages[i]);

    callAt(0x676120, {0, address(parent)});
    check("title: no pages while the game texts are not loaded",
        listEntries.empty() && !titlePages.find(reinterpret_cast<uintptr_t>(parent)) && !hostRegistry.textsResolved);
    loadGameTexts();

    callAt(0x676120, {0, address(parent)});
    const TitlePages* entry = titlePages.find(reinterpret_cast<uintptr_t>(parent));
    check("title: the English texts are resolved once the game texts are loaded", hostRegistry.textsResolved &&
        hostRegistry.pages[1].title.text == L"Page B" && hostRegistry.pages[0].items[0].label.text == L"Connect");
    check("title: two Mod pages recorded for the parent", entry && entry->pages.size() == 2);
    check("title: list rows 5 and 6 carry the titles", listEntries.size() == 2 && listEntries[0].index == 5 &&
        listEntries[0].text == L"Page A" && listEntries[1].index == 6 && listEntries[1].text == L"Page B");
    check("title: the list is laid out again once", layoutCalls == 1 && mouseCalls == 1);
    check("title: each page read its values once", modA.reads == 1 && modB.reads == 1);
    if (!entry || entry->pages.size() != 2) return;
    auto pageA = reinterpret_cast<void*>(entry->pages[0]);
    auto pageB = reinterpret_cast<void*>(entry->pages[1]);
    checkModPage(pageA, "page A", true);
    checkModPage(pageB, "page B (out-of-range value shows the first choice)", false);
    check("no unexpected page method was called", unexpectedMethodCalls == 0);

    callAt(0x676120, {0, address(parent)});
    check("title: a second initialize adds nothing", listEntries.size() == 2);

    Registers r = callAt(0x67653d, {5, 0, 0, 0, address(parent)});
    check("enter: index 5 is page A", r.eax == address(pageA) && r.edx == address(modPageMethods));
    r = callAt(0x67653d, {2, 0, 0, 0, address(parent)});
    check("enter: index 2 is the game's page", r.eax == address(gamePages[2]) && r.edx == 0x33);
    r = callAt(0x6765c1, {0xabcd, 0, 0, 6, address(parent)});
    check("input: index 6 is page B, eax kept", r.ecx == address(pageB) && r.eax == 0xabcd &&
        r.edx == address(modPageMethods));
    r = callAt(0x6765e4, {0xabcd, 0, 0, 0, address(parent)});
    check("leave: index 0 is the game's page, eax kept", r.ecx == address(gamePages[0]) && r.eax == 0xabcd);
    r = callAt(0x67663e, {6, 0, 0, 0, address(parent)});
    check("update: a page resumes after the reload, with the page in eax", r.ebx == 1 && r.eax == address(pageB));
    r = callAt(0x67663e, {7, 0, 0, 0, address(parent)});
    check("update: no page skips", r.ebx == 2);
    r = callAt(0x6766cb, {0, 0, 0, 0, address(parent), 6});
    check("draw index 6 is valid", r.ebx == 1 && r.edi == 6);
    r = callAt(0x6766cb, {0, 0, 0, 0, address(parent), 7});
    check("draw index 7 is invalid", r.ebx == 2);
    r = callAt(0x6766fe, {0xabcd, 0, 0, 0, address(parent), 5});
    check("draw: index 5 is page A, eax kept", r.ecx == address(pageA) && r.eax == 0xabcd);

    const auto changed = reinterpret_cast<uint32_t(__thiscall*)(void*, void*)>(virtualMethod(pageA, itemChangedSlot));
    uint8_t* choiceA = extensionOf(pageA).items[1];
    field<uint32_t>(choiceA, itemChoiceIndex) = 3;
    check("changed: choice reports its index", changed(pageA, choiceA) == 1 && modA.changes == 1 &&
        modA.lastItem == 1 && modA.lastValue == 3);
    uint8_t* toggleA = extensionOf(pageA).items[0];
    toggleA[itemToggleState] = 0;
    check("changed: toggle reports 0 or 1", changed(pageA, toggleA) == 1 && modA.lastItem == 0 && modA.lastValue == 0);
    uint8_t stranger[itemSize] = {};
    check("changed: another item is not reported", changed(pageA, stranger) == 0 && modA.changes == 2);

    const unsigned destroyedBefore = pagesDestroyed;
    callAt(0x676050, {0, address(parent)});
    check("destroy: both Mod pages destroyed with the parent", pagesDestroyed == destroyedBefore + 2);
    check("destroy: the parent is forgotten", !titlePages.find(reinterpret_cast<uintptr_t>(parent)));
    r = callAt(0x6766cb, {0, 0, 0, 0, address(parent), 5});
    check("draw index 5 is invalid once the Mod pages are gone", r.ebx == 2);
}

void makeGameMenuRows(GameMenuRow* rows) {
    memset(rows, 0, gameMenuRowCount * sizeof(GameMenuRow));
    for (uint32_t i = 0; i < gameMenuRowCount; ++i) rows[i].state = -1;
    rows[optionsMenuRow].text = optionsMenuText;
    rows[optionsMenuRow].first = reinterpret_cast<uintptr_t>(&rows[18]);
    for (uint32_t i = 18; i <= 22; ++i) {
        rows[i].text = 200 + i;
        rows[i].parent = reinterpret_cast<uintptr_t>(&rows[optionsMenuRow]);
        if (i > 18) rows[i].previous = reinterpret_cast<uintptr_t>(&rows[i - 1]);
        if (i < 22) rows[i].next = reinterpret_cast<uintptr_t>(&rows[i + 1]);
    }
    rows[18].text = firstOptionsPageText;
    rows[22].text = lastOptionsPageText;
}

void checkPauseMenu() {
    uint8_t menu[0x98] = {};
    auto rows = static_cast<GameMenuRow*>(fakeNewArray(gameMenuRowCount * sizeof(GameMenuRow)));
    makeGameMenuRows(rows);
    field<GameMenuRow*>(menu, pauseMenuRows) = rows;
    field<uint32_t>(menu, pauseMenuRowCount) = gameMenuRowCount;

    const unsigned arraysBefore = arraysReleased;
    Registers r = callAt(0x676cb0, {0, address(menu)});
    auto table = field<GameMenuRow*>(menu, pauseMenuRows);
    check("pause menu: the builder returns the new rows", r.eax == address(table) && table != rows);
    check("pause menu: 27 rows, old rows freed", field<uint32_t>(menu, pauseMenuRowCount) == 27 &&
        arraysReleased == arraysBefore + 1);
    check("pause menu: Mod rows after the last options page", table[22].next == address(&table[25]) &&
        table[25].next == address(&table[26]) && table[26].next == 0 && table[25].parent == address(&table[17]));
    check("pause menu: Mod rows carry their pages", table[25].text == modTitleTextBase &&
        table[26].text == modTitleTextBase + 1 && extensionOf(reinterpret_cast<void*>(table[25].child)).definition == 0 &&
        extensionOf(reinterpret_cast<void*>(table[26].child)).definition == 1);

    field<uintptr_t>(menu, pauseMenuCursor) = 1;
    r = callAt(0x676cb0, {0, address(menu)});
    check("pause menu: rows in use are not changed", r.eax == address(table) &&
        field<uint32_t>(menu, pauseMenuRowCount) == 27);

    // Free like the game: each row's child page, then the rows.
    const unsigned destroyedBefore = pagesDestroyed;
    for (uint32_t i = 0; i < 27; ++i)
        if (table[i].child) destroyGameObject(reinterpret_cast<void*>(table[i].child));
    fakeDeleteArray(table);
    check("pause menu: the game frees the Mod pages", pagesDestroyed == destroyedBefore + 2);

    uint8_t textManager[0x20] = {};
    field<uint32_t>(textManager, 0x1c) = 0x5678;
    GameMenuRow modRow{}, gameRow{}, unknownRow{};
    modRow.text = modTitleTextBase + 1;
    gameRow.text = 102;
    unknownRow.text = modTitleTextBase + 5;
    r = callAt(0x5c6357, {address(textManager), 0, 0x77, 0, 0, address(&modRow)});
    check("row text: a Mod title gives the page title, edx kept", r.ebx == 2 &&
        r.ecx == address(hostRegistry.pages[1].title.text.c_str()) && r.edx == 0x77);
    r = callAt(0x5c6357, {address(textManager), 0, 0, 0, 0, address(&gameRow)});
    check("row text: a game text continues to the table", r.ebx == 1 && r.ecx == 102 && r.eax == 0x5678);
    r = callAt(0x5c6426, {address(textManager), 0, 0, 0, 0, address(&unknownRow)});
    check("row label: an unknown Mod title gives no text", r.ebx == 2 && r.ecx == 0);
    r = callAt(0x5c6426, {address(textManager), 0, 0, 0, 0, address(&modRow)});
    check("row label: a Mod title gives the page title", r.ebx == 2 &&
        r.ecx == address(hostRegistry.pages[1].title.text.c_str()));
}

void checkGameEvents() {
    callAt(0x50e870, {});
    callAt(0x50e870, {});
    check("frame: callback after each update", frames == 2);
    callAt(0x536ef0, {});
    check("options saved: callback after the save", optionsSaved == 1);
}

}  // namespace

// The texts are resolved once per process; this resolves them again, after every other check.
void checkJapaneseTexts() {
    field<uint32_t>(gameTextManagerObject, gameTextLanguage) = 0;
    gameTextStrings[502] = L"接続";
    hostRegistry.textsResolved = false;
    check("Japanese game: the Japanese strings and the game texts are used", resolveTexts() &&
        hostRegistry.pages[0].title.text == L"ページA" && hostRegistry.pages[0].items[0].label.text == L"接続" &&
        hostRegistry.pages[1].items[1].choicePointers[2] == hostRegistry.pages[1].items[1].choices[2].text.c_str() &&
        wcscmp(hostRegistry.pages[1].items[1].choicePointers[2], L"高") == 0);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);  // Keep every line if a hook crashes the probe
    fakeImage = static_cast<uint8_t*>(VirtualAlloc(nullptr, fakeEnd - fakeStart, MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE));
    if (!fakeImage) return 2;
    gameBase = reinterpret_cast<uintptr_t>(fakeImage) - (fakeStart - exeImageBase);
    writeFakeGame();
    writeHookSites();
    registerMods();

    installGameHooks();
    const bool installed = MH_EnableHook(reinterpret_cast<void*>(gameAddress(0x676120))) == MH_ERROR_ENABLED;
    check("every hook group installed", installed && modPageMethods[0] == reinterpret_cast<uintptr_t>(fakePageDestroy));
    if (!installed) {
        printf("RESULT %u passed, %u failed\n", tests - failures, failures);
        return 1;
    }
    checkTitleScreen();
    checkPauseMenu();
    checkGameEvents();
    checkJapaneseTexts();
    check("every object the hooks allocated was released", objectsAllocated == objectsReleased);

    check("hooks removed", MH_DisableHook(MH_ALL_HOOKS) == MH_OK && MH_Uninitialize() == MH_OK);
    for (const HookSite& site : optionsMenuSites) {
        if (memcmp(fake(site.address), site.expected, site.expectedLength)) check("original bytes restored", false);
    }
    VirtualFree(fakeImage, 0, MEM_RELEASE);
    printf("RESULT %u passed, %u failed (synthetic code only; no game exe, no game process)\n",
        tests - failures, failures);
    return failures ? 1 : 0;
}
