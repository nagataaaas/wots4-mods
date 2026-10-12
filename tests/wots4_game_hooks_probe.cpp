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

// Covers every address the hooks use, from 0x4f0000 to 0x9f0000.
constexpr uint32_t fakeStart = 0x4f0000;
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

// ---- layout and animation slots, and the game's layout descriptors

constexpr uint32_t slotCountsOffset = 0x1e;  // u16 layout slots, then u16 animation slots at +0x20
constexpr uint32_t layoutSlotSize = 0x40;
constexpr uint32_t animationSlotSize = 0x30;
unsigned slotSetsMade, slotSetsReleased;

void __cdecl fakeMakeSlots(void* page, uint32_t array, uint32_t countOffset, uint32_t count, uint32_t slotSize) {
    ++slotSetsMade;
    auto slots = static_cast<uint8_t**>(malloc(count * sizeof(uint8_t*)));
    for (uint32_t i = 0; i < count; ++i) slots[i] = static_cast<uint8_t*>(calloc(1, slotSize));
    field<uint8_t**>(page, array) = slots;
    field<uint16_t>(page, countOffset) = static_cast<uint16_t>(count);
}

void __cdecl fakeFreeSlots(void* page, uint32_t array, uint32_t countOffset) {
    auto& slots = field<uint8_t**>(page, array);
    if (!slots) return;
    ++slotSetsReleased;
    for (uint32_t i = 0; i < field<uint16_t>(page, countOffset); ++i) free(slots[i]);
    free(slots);
    slots = nullptr;
    field<uint16_t>(page, countOffset) = 0;
}

void __cdecl fakeMakeLayoutSlots(void* page, uint32_t count) {
    fakeMakeSlots(page, pageLayoutSlots, slotCountsOffset, count, layoutSlotSize);
}
void __cdecl fakeMakeAnimationSlots(void* page, uint32_t count) {
    fakeMakeSlots(page, pageAnimationSlots, slotCountsOffset + 2, count, animationSlotSize);
}
void __cdecl fakeFreeLayoutSlots(void* page) {
    fakeFreeSlots(page, pageLayoutSlots, slotCountsOffset);
}
void __cdecl fakeFreeAnimationSlots(void* page) {
    fakeFreeSlots(page, pageAnimationSlots, slotCountsOffset + 2);
}

__declspec(naked) void fakeCreateLayoutSlots() {  // eax = count, esi = page
    __asm {
        push eax
        push esi
        call fakeMakeLayoutSlots
        add esp, 8
        ret
    }
}
__declspec(naked) void fakeCreateAnimationSlots() {  // eax = count, esi = page
    __asm {
        push eax
        push esi
        call fakeMakeAnimationSlots
        add esp, 8
        ret
    }
}
__declspec(naked) void fakeReleaseLayoutSlots() {  // esi = page
    __asm {
        push esi
        call fakeFreeLayoutSlots
        add esp, 4
        ret
    }
}
__declspec(naked) void fakeReleaseAnimationSlots() {  // esi = page
    __asm {
        push esi
        call fakeFreeAnimationSlots
        add esp, 4
        ret
    }
}

// Where the fake descriptors keep their entries; the descriptors are at the game's addresses.
constexpr uint32_t fakeBaseLayoutFiles = 0x96a558, fakeBaseAnimations = 0x96a5d8;
constexpr uint32_t fakeMappingLayoutFiles = 0x969be8, fakeMappingAnimations = 0x969ce8;

LayoutAnimation animationEntry(const char* name, int32_t layout) {
    LayoutAnimation animation{{1}, 1, {}, {}, layout};
    strcpy_s(animation.name, name);
    strcpy_s(animation.file, "fake.lyanim");
    return animation;
}

void writeLayoutDescriptor(uint32_t descriptor, uint32_t files, std::initializer_list<const char*> layouts,
    uint32_t animations, std::initializer_list<LayoutAnimation> entries) {
    auto file = reinterpret_cast<LayoutFile*>(fake(files));
    for (const char* layout : layouts) strcpy_s((file++)->name, layout);
    memcpy(fake(animations), entries.begin(), entries.size() * sizeof(LayoutAnimation));
    const LayoutDescriptor written{{0x10, 9}, reinterpret_cast<const LayoutFile*>(fake(files)),
        static_cast<uint32_t>(layouts.size()), reinterpret_cast<const LayoutAnimation*>(fake(animations)),
        static_cast<uint32_t>(entries.size())};
    memcpy(fake(descriptor), &written, sizeof(written));
}

void writeLayoutDescriptors() {
    writeLayoutDescriptor(optionsPageLayouts, fakeBaseLayoutFiles, {"u_st_op_kamera.layout", "u_st_op_mpl.layout"},
        fakeBaseAnimations, {animationEntry("st_fade_in", 0), animationEntry("st_fade_out", 0),
        animationEntry("pl_00", 1), animationEntry("pl_01", 1)});
    writeLayoutDescriptor(controlMappingLayouts, fakeMappingLayoutFiles,
        {"u_st_op_kamera.layout", "u_ps_yousi01.layout", "u_sign.layout", "u_sign.layout"}, fakeMappingAnimations,
        {animationEntry("st_fade_in", 0), animationEntry("st_fade_out", 0), animationEntry("sign_white", 2),
        animationEntry("sign_up", 2), animationEntry("sign_white", 3), animationEntry("sign_up", 3)});
}

// ---- animations, arrows, pointer, keys and sounds

std::vector<uint32_t> animationsPlayed;
unsigned animationsAdvanced, layoutsUpdated, layoutsDrawn, soundsPlayed;

// Like the game: nothing before the layouts are loaded; playing shows the animation's layout.
void __cdecl fakeShowAnimationLayout(void* page, uint32_t animation, bool play) {
    if (!field<uint8_t>(page, pageLayoutsLoaded)) return;
    if (play) animationsPlayed.push_back(animation);
    const auto descriptor = field<const LayoutDescriptor*>(page, pageLayoutDescriptor);
    layoutSlot(page, descriptor->animations[animation].layout)[layoutShown] = play ? 1 : 0;
}

__declspec(naked) void fakePlayAnimation() {  // ecx = animation, edi = page
    __asm {
        push 1
        push ecx
        push edi
        call fakeShowAnimationLayout
        add esp, 12
        ret
    }
}
__declspec(naked) void fakeStopAnimation() {  // edi = animation, esi = page
    __asm {
        push 0
        push edi
        push esi
        call fakeShowAnimationLayout
        add esp, 12
        ret
    }
}
__declspec(naked) void fakeAdvanceAnimation() {  // esi = animation slot
    __asm {
        inc animationsAdvanced
        ret
    }
}
__declspec(naked) void fakeUpdateLayout() {  // esi = layout slot
    __asm {
        inc layoutsUpdated
        ret
    }
}
void __fastcall fakeDrawLayout(void*, void*, uint32_t) {
    ++layoutsDrawn;
}

bool upKey, downKey;
int32_t pointerX, pointerY;
uint8_t inputObject[0xd0];
uint8_t pointerObject[0x1c20];
uint8_t screenObject[0x18];

__declspec(naked) void fakeUpPressed() {  // eax = input service
    __asm {
        movzx eax, upKey
        ret
    }
}
__declspec(naked) void fakeDownPressed() {
    __asm {
        movzx eax, downKey
        ret
    }
}
void __cdecl fakeStorePointer(int32_t* x, int32_t* y) {
    *x = pointerX;
    *y = pointerY;
}
__declspec(naked) void fakeReadPointer() {  // ecx = 0, edi = &x, [esp+4] = &y; al = known
    __asm {
        push dword ptr [esp + 4]
        push edi
        call fakeStorePointer
        add esp, 8
        mov al, 1
        ret
    }
}
void __cdecl fakePlaySound(float) {  // ecx = 0, edx = sound
    ++soundsPlayed;
}

void clickAt(MenuPoint point) {
    pointerX = static_cast<int32_t>(point.x);
    pointerY = static_cast<int32_t>(point.y);
    pointerObject[pointerPressed] = 1;
    field<int32_t>(pointerObject, pointerPressTime) = 0;
}

void releasePointer() {
    pointerObject[pointerPressed] = 0;
}

// ---- the base page

uintptr_t fakePageMethods[optionsPageMethodCount];

void* __stdcall fakePageConstructor(void* page) {
    field<uintptr_t*>(page, 0) = fakePageMethods;
    auto items = static_cast<void**>(malloc(itemCapacity * sizeof(void*)));
    field<void**>(page, itemVector) = items;
    field<void**>(page, itemVector + 4) = items;
    field<void**>(page, itemVector + 8) = items + itemCapacity;
    field<int32_t>(page, pageCursor) = noCursor;
    // Like the game's constructor: its own descriptor, 2 layout slots, an animation slot per animation.
    field<const void*>(page, pageLayoutDescriptor) = fake(optionsPageLayouts);
    fakeMakeLayoutSlots(page, 2);
    fakeMakeAnimationSlots(page, 4);
    return page;
}

void __fastcall fakePageDestroy(void* page, void*, uint32_t flags) {
    ++pagesDestroyed;
    auto begin = field<void**>(page, itemVector), end = field<void**>(page, itemVector + 4);
    for (void** item = begin; item != end; ++item) fakeDelete(*item);
    free(begin);
    fakeFreeLayoutSlots(page);
    fakeFreeAnimationSlots(page);
    if (flags & 1) fakeDelete(page);
}

// Like the game: initialize builds the items, then shows the current values, then loads the layouts.
void __fastcall fakePageInitialize(void* page, void*) {
    reinterpret_cast<void(__thiscall*)(void*)>(virtualMethod(page, buildItemsSlot))(page);
    reinterpret_cast<void(__thiscall*)(void*)>(virtualMethod(page, refreshItemsSlot))(page);
    field<uint8_t>(page, pageLayoutsLoaded) = 1;
}

unsigned baseUpdates;
uint32_t __fastcall fakePageUpdate(void*, void*) {
    ++baseUpdates;
    return 1;
}

// What the base draw and input were shown.
struct ShownRows {
    void** begin;
    uint32_t count;
    int32_t cursor;
};
ShownRows lastDraw, lastInput;

ShownRows shownRows(void* page) {
    auto begin = field<void**>(page, itemVector);
    return {begin, static_cast<uint32_t>(field<void**>(page, itemVector + 4) - begin), field<int32_t>(page, pageCursor)};
}

uint32_t __fastcall fakePageDraw(void* page, void*, uint32_t) {
    lastDraw = shownRows(page);
    return 1;
}

// Moves the cursor like the game's input (0x645f90): up from the first item gives no
// item, up from no item the last; down from the last item gives no item.
uint8_t __fastcall fakePageInput(void* page, void*, uint32_t, uint32_t) {
    lastInput = shownRows(page);
    int32_t& cursor = field<int32_t>(page, pageCursor);
    const auto count = static_cast<int32_t>(lastInput.count);
    if (upKey && --cursor < noCursor) cursor = count - 1;
    if (downKey && ++cursor >= count) cursor = noCursor;
    return 1;
}

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
    writeJump(createLayoutSlots, fakeCreateLayoutSlots);
    writeJump(releaseLayoutSlots, fakeReleaseLayoutSlots);
    writeJump(createAnimationSlots, fakeCreateAnimationSlots);
    writeJump(releaseAnimationSlots, fakeReleaseAnimationSlots);
    writeJump(playAnimation, fakePlayAnimation);
    writeJump(stopAnimation, fakeStopAnimation);
    writeJump(advanceAnimation, fakeAdvanceAnimation);
    writeJump(updateLayout, fakeUpdateLayout);
    writeJump(drawLayout, fakeDrawLayout);
    writeJump(upPressed, fakeUpPressed);
    writeJump(downPressed, fakeDownPressed);
    writeJump(readPointer, fakeReadPointer);
    writeJump(playSound, fakePlaySound);
    field<void*>(inputObject, inputButtons) = inputObject;
    writeSlot(inputService, inputObject);
    writeSlot(pointerService, pointerObject);
    field<int32_t>(screenObject, screenWidth) = 1280;
    field<int32_t>(screenObject, screenHeight) = 720;
    writeSlot(screenService, screenObject);
    writeLayoutDescriptors();

    for (uintptr_t& method : fakePageMethods) method = reinterpret_cast<uintptr_t>(fakeUnexpectedMethod);
    fakePageMethods[0] = reinterpret_cast<uintptr_t>(fakePageDestroy);
    fakePageMethods[1] = reinterpret_cast<uintptr_t>(fakePageInitialize);
    fakePageMethods[updateSlot / 4] = reinterpret_cast<uintptr_t>(fakePageUpdate);
    fakePageMethods[drawSlot / 4] = reinterpret_cast<uintptr_t>(fakePageDraw);
    fakePageMethods[inputSlot / 4] = reinterpret_cast<uintptr_t>(fakePageInput);
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
    uint32_t values[16];
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
    uint8_t* toggle = itemsOf(page)[0];
    uint8_t* choice = itemsOf(page)[1];
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
    uint8_t* choiceA = itemsOf(pageA)[1];
    field<uint32_t>(choiceA, itemChoiceIndex) = 3;
    check("changed: choice reports its index", changed(pageA, choiceA) == 1 && modA.changes == 1 &&
        modA.lastItem == 1 && modA.lastValue == 3);
    uint8_t* toggleA = itemsOf(pageA)[0];
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

// ---------------------------------------------------------------- scrolling page

constexpr uint32_t scrollingItemCount = 12;  // 8 rows, 4 more below
ModState modC{{1, 0, 1}};

// A page added after the others, as a Mod would register it.
uint32_t addScrollingPageDefinition() {
    const LocalizedText onOff[] = {{L"はい", L"Yes"}, {L"いいえ", L"No"}};
    const std::vector<OptionItem> items(scrollingItemCount, OptionItem{optionToggle, {L"項目", L"Item"}, onOff, 2});
    const OptionsPageCallbacks callbacks{&modC, readValues, valueChanged};
    const LocalizedText title{L"ページC", L"Page C"};
    hostRegistry.registrationOpen = true;
    addOptionsPage(reinterpret_cast<HMODULE>(0x10000), &title, items.data(), scrollingItemCount, &callbacks);
    hostRegistry.registrationOpen = false;
    hostRegistry.textsResolved = false;
    resolveTexts();
    return static_cast<uint32_t>(hostRegistry.pages.size() - 1);
}

void updatePage(void* page) {
    reinterpret_cast<uint32_t(__thiscall*)(void*)>(virtualMethod(page, updateSlot))(page);
}

void drawPage(void* page) {
    reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t)>(virtualMethod(page, drawSlot))(page, 0);
}

void pressKey(void* page, bool up, bool down) {
    upKey = up;
    downKey = down;
    reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(virtualMethod(page, inputSlot))(page, 0, 0);
    upKey = downKey = false;
}

void pressKeyTimes(void* page, bool up, unsigned times) {
    for (unsigned i = 0; i < times; ++i) pressKey(page, up, !up);
}

bool rowsAre(void* page, uint32_t first, int32_t cursor) {
    return extensionOf(page).firstVisibleItem == first && cursorOf(page) == cursor;
}

bool arrowsShown(void* page, bool up, bool down) {
    return (layoutSlot(page, upArrowLayout)[layoutShown] != 0) == up &&
        (layoutSlot(page, downArrowLayout)[layoutShown] != 0) == down;
}

void checkScrollingLayouts() {
    check("scrolling: the game's descriptors match", scrollingLayoutsMatch());
    check("scrolling: the descriptor has the base layouts and the two arrows",
        scrollingLayouts.layoutCount == 4 && scrollingLayouts.animationCount == 8 &&
        !strcmp(scrollingLayoutFiles[1].name, "u_st_op_mpl.layout") &&
        !strcmp(scrollingLayoutFiles[upArrowLayout].name, "u_sign.layout") &&
        !strcmp(scrollingAnimations[3].name, "pl_01") && scrollingAnimations[3].layout == 1 &&
        !strcmp(scrollingAnimations[upArrowShown].name, "sign_white") && scrollingAnimations[upArrowShown].layout == 2 &&
        !strcmp(scrollingAnimations[downArrowMoved].name, "sign_up") && scrollingAnimations[downArrowMoved].layout == 3);
    auto mapping = reinterpret_cast<LayoutDescriptor*>(fake(controlMappingLayouts));
    mapping->layoutCount = 3;
    check("scrolling: another control mapping descriptor does not match", !scrollingLayoutsMatch());
    mapping->layoutCount = 4;
}

void checkScrollingPage() {
    checkScrollingLayouts();
    const uint32_t definition = addScrollingPageDefinition();
    const unsigned slotsMadeBefore = slotSetsMade, slotsReleasedBefore = slotSetsReleased;
    void* page = createModPage(definition);
    check("scrolling: a page of 12 items gets the scrolling methods", page &&
        field<uintptr_t*>(page, 0) == scrollingPageMethods && extensionOf(page).itemCount == scrollingItemCount);
    if (!page) return;
    check("scrolling: the page's slots are made again for the scrolling descriptor",
        field<const LayoutDescriptor*>(page, pageLayoutDescriptor) == &scrollingLayouts &&
        field<uint16_t>(page, slotCountsOffset) == 4 && field<uint16_t>(page, slotCountsOffset + 2) == 8 &&
        slotSetsReleased == slotsReleasedBefore + 2 && slotSetsMade == slotsMadeBefore + 2 + 2);  // Constructor, then ours
    uint8_t* downArrow = layoutSlot(page, downArrowLayout);
    check("scrolling: the arrows are placed from the screen centre, the down one turned",
        field<float>(layoutSlot(page, upArrowLayout), layoutX) == 755 - 640 &&
        field<float>(layoutSlot(page, upArrowLayout), layoutY) == 164 - 360 &&
        field<float>(downArrow, layoutY) == 548 - 360 && field<float>(downArrow, layoutRotation) == 180);

    const unsigned baseUpdatesBefore = baseUpdates;
    updatePage(page);
    check("scrolling: before the layouts are loaded, update is the base one only",
        baseUpdates == baseUpdatesBefore + 1 && animationsPlayed.empty() && animationsAdvanced == 0);

    reinterpret_cast<void(__thiscall*)(void*)>(virtualMethod(page, initializeSlot))(page);
    const auto vectorBegin = field<void**>(page, itemVector);
    check("scrolling: all 12 items are built in a vector made larger", extensionOf(page).built &&
        field<void**>(page, itemVector + 4) - vectorBegin == 12 && field<void**>(page, itemVector + 8) - vectorBegin == 12 &&
        vectorBegin[11] == itemsOf(page)[11]);
    check("scrolling: the values of all 12 items are read", modC.reads == 1 && itemsOf(page)[2][itemToggleState] == 1 &&
        itemsOf(page)[11][itemToggleState] == 0);

    updatePage(page);
    check("scrolling: update shows only the down arrow and runs the arrows",
        arrowsShown(page, false, true) && baseUpdates == baseUpdatesBefore + 2 && animationsAdvanced == 4 &&
        layoutsUpdated == 2);
    drawPage(page);
    check("scrolling: draw shows the base draw 8 rows from the first item, then the down arrow",
        lastDraw.begin == vectorBegin && lastDraw.count == 8 && lastDraw.cursor == noCursor && layoutsDrawn == 1 &&
        field<void**>(page, itemVector + 4) - field<void**>(page, itemVector) == 12);

    pressKeyTimes(page, false, 8);
    check("scrolling: down 8 times from no item reaches the last row", rowsAre(page, 0, 7));
    animationsPlayed.clear();
    pressKey(page, false, true);
    check("scrolling: down from the last row moves the rows, and the cursor to item 8",
        rowsAre(page, 1, 8) && lastInput.begin == vectorBegin + 1 && lastInput.count == 8 && lastInput.cursor == 6 &&
        animationsPlayed.size() == 1 && animationsPlayed[0] == downArrowMoved);
    updatePage(page);
    check("scrolling: with items above and below, both arrows show", arrowsShown(page, true, true));
    pressKeyTimes(page, false, 3);
    check("scrolling: down to the last item", rowsAre(page, 4, 11));
    updatePage(page);
    check("scrolling: at the end, only the up arrow shows", arrowsShown(page, true, false));
    pressKey(page, false, true);
    check("scrolling: down from the last item gives no item", rowsAre(page, 4, noCursor));
    pressKey(page, false, true);
    check("scrolling: down from no item goes to the first item", rowsAre(page, 0, 0));
    pressKey(page, true, false);
    check("scrolling: up from the first item gives no item", rowsAre(page, 0, noCursor));
    pressKey(page, true, false);
    check("scrolling: up from no item goes to the last item", rowsAre(page, 4, 11));
    pressKeyTimes(page, true, 8);
    check("scrolling: up from the top row moves the rows", rowsAre(page, 3, 3));

    const unsigned soundsBefore = soundsPlayed;
    clickAt(downArrowCentre);
    pressKey(page, false, false);
    check("scrolling: a click on the down arrow turns the rows to the end, the cursor 8 items down",
        rowsAre(page, 4, 11) && soundsPlayed == soundsBefore + 1);
    clickAt(upArrowCentre);
    pressKey(page, false, false);
    check("scrolling: a click on the up arrow turns a screen of rows up", rowsAre(page, 0, 3) &&
        soundsPlayed == soundsBefore + 2);
    pressKey(page, false, false);
    check("scrolling: the up arrow does nothing at the top", rowsAre(page, 0, 3) && soundsPlayed == soundsBefore + 2);
    clickAt({upArrowCentre.x + 30, downArrowCentre.y});
    pressKey(page, false, false);
    check("scrolling: a click beside the arrow does nothing", rowsAre(page, 0, 3));
    releasePointer();

    cursorOf(page) = 10;
    drawPage(page);
    check("scrolling: draw first brings a cursor set elsewhere into the rows",
        rowsAre(page, 3, 10) && lastDraw.begin == vectorBegin + 3 && lastDraw.cursor == 7);

    const auto changed = reinterpret_cast<uint32_t(__thiscall*)(void*, void*)>(virtualMethod(page, itemChangedSlot));
    itemsOf(page)[10][itemToggleState] = 1;
    check("scrolling: a change of item 10 is reported as item 10",
        changed(page, itemsOf(page)[10]) == 1 && modC.lastItem == 10 && modC.lastValue == 1);

    const unsigned destroyedBefore = pagesDestroyed;
    destroyGameObject(page);
    check("scrolling: the page is destroyed with its slots", pagesDestroyed == destroyedBefore + 1 &&
        slotSetsReleased == slotSetsMade);
    check("no unexpected page method was called by the scrolling page", unexpectedMethodCalls == 0);
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
    checkScrollingPage();
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
