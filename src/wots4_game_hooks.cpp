// The ModLoader's hooks into the game. Doing them once here lets several Mods
// add options pages and per-frame or options-saved callbacks without hooking
// the same code themselves.
//
// Options pages appear in two menus, which build their pages separately:
// - Pause menu: the builder 0x676cb0 makes a tree of 25 rows. Mod pages become
//   extra rows after the last options page (wots4_game_menu.h). The menu shows
//   6 rows around the selected one, so any number of rows scrolls.
// - Title screen: the options parent keeps 5 page pointers inline. Mod pages
//   live in titlePages, and the 6 places that read a page by index are redirected.
// Addresses are virtual addresses of the exe (ImageBase 0x400000).
#include "wots4_game_hooks.h"
#include "wots4_game_menu.h"
#include "wots4_mod_loader.h"
#include <MinHook.h>
#include <cstdio>
#include <memory>
#include <new>

namespace wots4::game {
namespace {

constexpr uintptr_t exeImageBase = 0x400000;
constexpr DWORD supportedExeTimestamp = 0x683d42eb;  // PE TimeDateStamp of the current Steam exe
static_assert(modTitleTextBase == 0x7fff0000, "rowTextHook and rowLabelHook compare with this value");

uintptr_t gameBase;
HMODULE hookOwner;

uintptr_t gameAddress(uint32_t virtualAddress) {
    return gameBase + virtualAddress - exeImageBase;
}

// ---------------------------------------------------------------- game functions and objects

// Import slots of the C runtime the game uses. Objects the game frees must come from them.
constexpr uint32_t gameNewSlot = 0x9052c8;          // operator new
constexpr uint32_t gameDeleteSlot = 0x9052c0;       // operator delete
constexpr uint32_t gameDeleteArraySlot = 0x9052e0;  // operator delete[]
constexpr uint32_t gameNewArray = 0x8c43fc;         // operator new[] (the builder allocates rows with it)

constexpr uint32_t optionsPageConstructor = 0x645050;  // stdcall(page); a 0xa0-byte page with an item vector
constexpr uint32_t optionsPageMethods = 0x99a918;      // vtable of that page, 26 entries
constexpr uint32_t optionsPageMethodCount = 26;
constexpr uint32_t optionItemConstructor = 0x6465b0;   // esi = item (0x30c bytes, a toggle)
constexpr uint32_t optionsPageAppend = 0x646480;       // eax = page, push item; returns the index or -1

constexpr uint32_t titleListAdd = 0x5c84f0;       // thiscall(list, index, text)
constexpr uint32_t titleListLayout = 0x5c87c0;    // eax = list; places the visible rows
constexpr uint32_t titleListMouse = 0x5c7e80;     // edi = list; rebuilds the mouse areas

// Page object: the game's 0xa0 bytes, then ours.
constexpr uint32_t pageBaseSize = 0xa0;
struct PageExtension {
    uint32_t definition;                // Index into hostRegistry.pages
    uint32_t built;                     // 1 once every item was appended
    uint8_t* items[maxOptionsItems];
};
constexpr uint32_t pageSize = pageBaseSize + sizeof(PageExtension);

// Page vtable slots this file replaces (byte offsets).
constexpr uint32_t buildItemsSlot = 0x3c;
constexpr uint32_t pageSpecificSlotA = 0x40;  // Used by the game's own pages; ours do nothing
constexpr uint32_t refreshItemsSlot = 0x44;
constexpr uint32_t pageSpecificSlotB = 0x48;
constexpr uint32_t itemChangedSlot = 0x58;
constexpr uint32_t initializeSlot = 0x04;
constexpr uint32_t updateSlot = 0x08;

// Option item fields
constexpr uint32_t itemSize = 0x30c;
constexpr uint32_t itemLabel = 0x004;       // wchar_t[128]
constexpr uint32_t itemKind = 0x104;
constexpr uint32_t itemOnText = 0x108;      // Toggle: wchar_t[128]
constexpr uint32_t itemOffText = 0x208;     // Toggle: wchar_t[128]
constexpr uint32_t itemChoices = 0x108;     // Choice: const wchar_t*[10]
constexpr uint32_t itemChoiceArea = 0x204;  // Choice: from itemChoices to the end of the item
constexpr uint32_t itemChoiceIndex = 0x130;
constexpr uint32_t itemToggleState = 0x308;
constexpr uint32_t itemTextChars = 128;

// Parents
constexpr uint32_t titleParentStarted = 0x2c;  // byte; set by the parent's initialize
constexpr uint32_t titleParentList = 0x60;
constexpr uint32_t pauseMenuRows = 0x90;
constexpr uint32_t pauseMenuRowCount = 0x94;
constexpr uint32_t pauseMenuCursor = 0x40;   // Set once the menu is in use; rows must not move then
constexpr uint32_t pauseMenuHistory = 0x44;

template <class T>
T& field(void* object, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

void* allocateGameObject(size_t size) {
    auto allocate = *reinterpret_cast<void*(__cdecl**)(size_t)>(gameAddress(gameNewSlot));
    return allocate(size);
}

void* allocateGameArray(size_t size) {
    return reinterpret_cast<void*(__cdecl*)(size_t)>(gameAddress(gameNewArray))(size);
}

void releaseGameObject(void* object) {
    (*reinterpret_cast<void(__cdecl**)(void*)>(gameAddress(gameDeleteSlot)))(object);
}

void releaseGameArray(void* array) {
    (*reinterpret_cast<void(__cdecl**)(void*)>(gameAddress(gameDeleteArraySlot)))(array);
}

uintptr_t virtualMethod(void* object, uint32_t slot) {
    return (*reinterpret_cast<uintptr_t**>(object))[slot / sizeof(uintptr_t)];
}

// vtable[0] is the scalar deleting destructor; 1 frees the memory too.
void destroyGameObject(void* object) {
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(virtualMethod(object, 0))(object, 1);
}

void initializeItem(void* item) {
    const uintptr_t constructor = gameAddress(optionItemConstructor);
    __asm {
        push esi
        mov esi, item
        call constructor
        pop esi
    }
}

int appendItem(void* page, void* item) {
    const uintptr_t append = gameAddress(optionsPageAppend);
    int index;
    __asm {
        mov eax, page
        push item
        call append
        mov index, eax
    }
    return index;
}

void layoutTitleList(void* list) {
    const uintptr_t layout = gameAddress(titleListLayout);
    const uintptr_t mouse = gameAddress(titleListMouse);
    __asm {
        mov eax, list
        call layout
        push edi
        mov edi, list
        call mouse
        pop edi
    }
}

void copyItemText(uint8_t* item, uint32_t offset, const OptionText& text) {
    wcsncpy_s(reinterpret_cast<wchar_t*>(item + offset), itemTextChars, text.text.c_str(), _TRUNCATE);
}

// ---------------------------------------------------------------- game texts

// GameTextUI: the text manager points to {count, pointer array}.
constexpr uint32_t gameTextManager = 0x9e9680;
constexpr uint32_t gameTextUiTable = 0x1c;

const wchar_t* gameTextString(uint32_t number) {
    auto manager = *reinterpret_cast<uint8_t**>(gameAddress(gameTextManager));
    if (!manager) return nullptr;
    auto table = field<const uint32_t*>(manager, gameTextUiTable);
    if (!table || number >= table[0]) return nullptr;
    auto strings = reinterpret_cast<const wchar_t* const*>(table[1]);
    return strings ? strings[number] : nullptr;
}

// Runs inside hooks, where no exception may escape; an allocation failure counts as missing.
bool copyGameText(OptionText& text) {
    if (!text.gameTextNumber) return true;
    const wchar_t* string = gameTextString(text.gameTextNumber);
    if (!string) return false;
    try {
        text.text = string;
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

// Fills every game text of every page, all at once, so that no pointer the
// game keeps is ever moved afterwards. False if the game texts are not loaded.
bool copyGameTexts() {
    if (hostRegistry.gameTextsCopied) return true;
    for (OptionsPageDefinition& page : hostRegistry.pages) {
        if (!copyGameText(page.title)) return false;
        for (OptionsItemDefinition& item : page.items) {
            if (!copyGameText(item.label)) return false;
            for (OptionText& choice : item.choices) {
                if (!copyGameText(choice)) return false;
            }
        }
    }
    for (OptionsPageDefinition& page : hostRegistry.pages) bindChoicePointers(page);
    hostRegistry.gameTextsCopied = true;
    return true;
}

// ---------------------------------------------------------------- Mod options pages

uintptr_t modPageMethods[optionsPageMethodCount];

PageExtension& extensionOf(void* page) {
    return field<PageExtension>(page, pageBaseSize);
}

const OptionsPageDefinition& definitionOf(void* page) {
    return hostRegistry.pages[extensionOf(page).definition];
}

uint8_t* buildItem(const OptionsItemDefinition& definition) {
    auto item = static_cast<uint8_t*>(allocateGameObject(itemSize));
    if (!item) return nullptr;
    initializeItem(item);
    copyItemText(item, itemLabel, definition.label);
    if (definition.kind == optionToggle) {
        copyItemText(item, itemOnText, definition.choices[0]);
        copyItemText(item, itemOffText, definition.choices[1]);
        return item;
    }
    // A choice item is the same object with kind 3 and a null-terminated list
    // of up to 10 choice pointers instead of the two toggle texts.
    memset(item + itemChoices, 0, itemChoiceArea);
    field<uint32_t>(item, itemKind) = optionChoice;
    for (size_t i = 0; i < definition.choicePointers.size(); ++i)
        field<const wchar_t*>(item, itemChoices + 4 * static_cast<uint32_t>(i)) = definition.choicePointers[i];
    return item;
}

// The page owns appended items and frees them with its vector.
bool buildItems(void* page) {
    PageExtension& extension = extensionOf(page);
    const auto& items = definitionOf(page).items;
    for (size_t i = 0; i < items.size(); ++i) {
        uint8_t* item = buildItem(items[i]);
        if (!item) return false;
        if (appendItem(page, item) != static_cast<int>(i)) {
            releaseGameObject(item);
            return false;
        }
        extension.items[i] = item;
    }
    extension.built = 1;
    return true;
}

void __fastcall buildItemsMethod(void* page, void*) {
    buildItems(page);
}

void __fastcall noPageWork(void*, void*) {}

void __fastcall refreshItemsMethod(void* page, void*) {
    const PageExtension& extension = extensionOf(page);
    const OptionsPageDefinition& definition = definitionOf(page);
    if (!extension.built) return;
    const auto count = static_cast<uint32_t>(definition.items.size());
    uint32_t values[maxOptionsItems] = {};
    definition.callbacks.read(definition.callbacks.context, values, count);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* item = extension.items[i];
        const OptionsItemDefinition& itemDefinition = definition.items[i];
        if (itemDefinition.kind == optionToggle) {
            item[itemToggleState] = values[i] ? 1 : 0;
            continue;
        }
        const auto choiceCount = static_cast<uint32_t>(itemDefinition.choices.size());
        field<uint32_t>(item, itemChoiceIndex) = values[i] < choiceCount ? values[i] : 0;
    }
}

uint32_t __fastcall itemChangedMethod(void* page, void*, void* item) {
    const PageExtension& extension = extensionOf(page);
    const OptionsPageDefinition& definition = definitionOf(page);
    for (uint32_t i = 0; i < definition.items.size(); ++i) {
        if (extension.items[i] != item) continue;
        const bool toggle = definition.items[i].kind == optionToggle;
        const uint32_t value = toggle ? extension.items[i][itemToggleState] : field<uint32_t>(item, itemChoiceIndex);
        definition.callbacks.changed(definition.callbacks.context, i, value);
        return 1;
    }
    return 0;
}

void prepareModPageMethods() {
    memcpy(modPageMethods, reinterpret_cast<void*>(gameAddress(optionsPageMethods)), sizeof(modPageMethods));
    auto replace = [](uint32_t slot, void* method) {
        modPageMethods[slot / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(method);
    };
    replace(buildItemsSlot, buildItemsMethod);
    replace(pageSpecificSlotA, noPageWork);
    replace(refreshItemsSlot, refreshItemsMethod);
    replace(pageSpecificSlotB, noPageWork);
    replace(itemChangedSlot, itemChangedMethod);
}

void* createModPage(uint32_t definition) {
    void* page = allocateGameObject(pageSize);
    if (!page) return nullptr;
    memset(page, 0, pageSize);
    reinterpret_cast<void*(__stdcall*)(void*)>(gameAddress(optionsPageConstructor))(page);
    field<uintptr_t*>(page, 0) = modPageMethods;
    extensionOf(page).definition = definition;
    return page;
}

// Logs a problem the first time it happens; the menus are built again and again.
void logOnce(bool& alreadyLogged, const char* message) {
    if (alreadyLogged) return;
    alreadyLogged = true;
    registryLog(nullptr, message);
}

// ---------------------------------------------------------------- pause menu

bool loggedPauseMenuAdded, loggedPauseMenuSkipped;

void destroyPages(void* const* pages, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) destroyGameObject(pages[i]);
}

// Called when the builder returns, before its caller uses the rows.
// Returns the new row table, or nullptr to keep the builder's.
GameMenuRow* __cdecl addPauseMenuPages(void* menu) {
    auto rows = field<GameMenuRow*>(menu, pauseMenuRows);
    const uint32_t count = field<uint32_t>(menu, pauseMenuRowCount);
    const auto pageCount = static_cast<uint32_t>(hostRegistry.pages.size());
    if (field<uintptr_t>(menu, pauseMenuCursor) || field<uintptr_t>(menu, pauseMenuHistory) ||
        !isUnchangedGameMenu(rows, count)) {
        logOnce(loggedPauseMenuSkipped, "pause menu: the options rows are not as expected; Mod options pages not added");
        return nullptr;
    }
    if (!copyGameTexts()) {
        logOnce(loggedPauseMenuSkipped, "pause menu: a game text of a Mod options page is missing; Mod options pages not added");
        return nullptr;
    }

    // No exception may leave a hook, so allocate this list without throwing.
    std::unique_ptr<void*[]> pages(new (std::nothrow) void*[pageCount]);
    if (!pages) {
        logOnce(loggedPauseMenuSkipped, "pause menu: could not allocate the list of Mod options pages; Mod options pages not added");
        return nullptr;
    }
    for (uint32_t i = 0; i < pageCount; ++i) {
        pages[i] = createModPage(i);
        if (pages[i]) continue;
        destroyPages(pages.get(), i);
        logOnce(loggedPauseMenuSkipped, "pause menu: could not allocate a Mod options page; Mod options pages not added");
        return nullptr;
    }
    auto target = static_cast<GameMenuRow*>(allocateGameArray((count + pageCount) * sizeof(GameMenuRow)));
    if (!target || !appendOptionsRows(rows, count, target, pages.get(), pageCount)) {
        if (target) releaseGameArray(target);
        destroyPages(pages.get(), pageCount);
        logOnce(loggedPauseMenuSkipped, "pause menu: could not extend the menu rows; Mod options pages not added");
        return nullptr;
    }
    // The game frees the rows with delete[] and each child page with its destructor,
    // looping over the row count, so the new rows and pages are freed with the menu.
    field<GameMenuRow*>(menu, pauseMenuRows) = target;
    field<uint32_t>(menu, pauseMenuRowCount) = count + pageCount;
    releaseGameArray(rows);
    logOnce(loggedPauseMenuAdded, "pause menu: Mod options pages added");
    return target;
}

const wchar_t* __fastcall modTitleText(uint32_t text) {
    const uint32_t page = text - modTitleTextBase;
    return page < hostRegistry.pages.size() ? hostRegistry.pages[page].title.text.c_str() : nullptr;
}

// ---------------------------------------------------------------- title screen options

TitlePageTable titlePages;
bool loggedTitleAdded, loggedTitleSkipped;

uintptr_t __fastcall titlePageAt(uintptr_t parent, uint32_t index) {
    return titlePages.pageAt(parent, index);
}

uint32_t __fastcall titlePageCount(uintptr_t parent) {
    return titlePages.pageCount(parent);
}

// A page whose items could not all be built is destroyed at once and not shown.
void* createInitializedTitlePage(uint32_t definition) {
    void* page = createModPage(definition);
    if (!page) return nullptr;
    reinterpret_cast<void(__thiscall*)(void*)>(virtualMethod(page, initializeSlot))(page);
    reinterpret_cast<void(__thiscall*)(void*)>(virtualMethod(page, updateSlot))(page);
    if (extensionOf(page).built) return page;
    destroyGameObject(page);
    return nullptr;
}

// Called after the parent's initialize, which builds its five pages and list once.
void __cdecl addTitlePages(void* parent) {
    void* list = field<void*>(parent, titleParentList);
    if (!field<uint8_t>(parent, titleParentStarted) || !list) return;
    if (!copyGameTexts()) {
        logOnce(loggedTitleSkipped, "title options: a game text of a Mod options page is missing; Mod options pages not added");
        return;
    }
    TitlePages* entry = titlePages.add(reinterpret_cast<uintptr_t>(parent));
    if (!entry) {
        logOnce(loggedTitleSkipped, "title options: too many options screens at once; Mod options pages not added");
        return;
    }
    // No exception may leave a hook: reserve once, then push_back never allocates.
    try {
        entry->pages.reserve(hostRegistry.pages.size());
    } catch (const std::bad_alloc&) {
        titlePages.remove(reinterpret_cast<uintptr_t>(parent));
        logOnce(loggedTitleSkipped, "title options: could not allocate the list of Mod options pages; Mod options pages not added");
        return;
    }
    const auto addPage = reinterpret_cast<void(__thiscall*)(void*, uint32_t, const wchar_t*)>(gameAddress(titleListAdd));
    for (uint32_t i = 0; i < hostRegistry.pages.size(); ++i) {
        void* page = createInitializedTitlePage(i);
        if (!page) {
            logOnce(loggedTitleSkipped, "title options: a Mod options page could not be built; it is not shown");
            continue;
        }
        // The list position is the page index that the redirected reads use.
        const auto listIndex = gameOptionsPageCount + static_cast<uint32_t>(entry->pages.size());
        entry->pages.push_back(reinterpret_cast<uintptr_t>(page));
        addPage(list, listIndex, hostRegistry.pages[i].title.text.c_str());
    }
    if (!entry->pages.empty()) {
        layoutTitleList(list);
        logOnce(loggedTitleAdded, "title options: Mod options pages added");
    }
}

// Called before the parent's destructor, which frees only its five pages.
void __cdecl releaseTitlePages(void* parent) {
    const TitlePages* entry = titlePages.find(reinterpret_cast<uintptr_t>(parent));
    if (!entry) return;
    for (uintptr_t page : entry->pages) destroyGameObject(reinterpret_cast<void*>(page));
    titlePages.remove(reinterpret_cast<uintptr_t>(parent));
}

// ---------------------------------------------------------------- game events

void __cdecl runFrameHandlers() {
    for (const GameEventHandler& handler : hostRegistry.frameHandlers) handler.callback(handler.context);
}

void __cdecl runOptionsSavedHandlers() {
    for (const GameEventHandler& handler : hostRegistry.optionsSavedHandlers) handler.callback(handler.context);
}

// ---------------------------------------------------------------- hook sites

// A place to hook: the original bytes to expect, and how many bytes the hook
// owns (whole instructions, at least the 5 of a jump). Operands that the
// loader relocates (ASLR) are left out of the bytes.
struct HookSite {
    uint32_t address;
    uint32_t length;
    uint8_t expected[12];
    uint32_t expectedLength;
    void* detour;
    void** original;  // Trampoline to the original code, or nullptr for mid-function hooks
};

// Resume addresses for the mid-function hooks, set by resolveResumeAddresses().
uintptr_t enterPageResume, pageInputResume, leavePageResume;
uintptr_t updatePageFound, updatePageMissing, drawIndexValid, drawIndexInvalid, drawPageResume;
uintptr_t rowTextGameResume, rowTextModResume, rowLabelGameResume, rowLabelModResume;
void* titleInitializeOriginal;
void* titleDestroyOriginal;
void* pauseMenuBuildOriginal;
void* frameOriginal;
void* optionsSavedOriginal;

// 0x67653d mov eax, [esi+eax*4+0x64]; mov edx, [eax]  (enter the selected page)
__declspec(naked) void enterPageHook() {
    __asm {
        mov ecx, esi
        mov edx, eax
        call titlePageAt
        mov edx, [eax]
        jmp dword ptr [enterPageResume]
    }
}

// 0x6765c1 mov ecx, [esi+ebx*4+0x64]; mov edx, [ecx]  (input inside the page; eax is in use)
__declspec(naked) void pageInputHook() {
    __asm {
        push eax
        mov ecx, esi
        mov edx, ebx
        call titlePageAt
        mov ecx, eax
        mov edx, [ecx]
        pop eax
        jmp dword ptr [pageInputResume]
    }
}

// 0x6765e4 mov ecx, [esi+ebx*4+0x64]; mov edx, [ecx]  (leave the page)
__declspec(naked) void leavePageHook() {
    __asm {
        push eax
        mov ecx, esi
        mov edx, ebx
        call titlePageAt
        mov ecx, eax
        mov edx, [ecx]
        pop eax
        jmp dword ptr [leavePageResume]
    }
}

// 0x67663e cmp dword ptr [esi+eax*4+0x64], 0; je 0x676652; mov eax, [esi+eax*4+0x64]
// (update the page if there is one)
__declspec(naked) void updatePageHook() {
    __asm {
        mov ecx, esi
        mov edx, eax
        call titlePageAt
        test eax, eax
        jz missing
        jmp dword ptr [updatePageFound]
    missing:
        jmp dword ptr [updatePageMissing]
    }
}

// 0x6766cb cmp edi, 4; ja 0x6766da  (the index to draw must be a page)
__declspec(naked) void drawIndexHook() {
    __asm {
        mov ecx, esi
        call titlePageCount
        cmp edi, eax
        jae invalid
        jmp dword ptr [drawIndexValid]
    invalid:
        jmp dword ptr [drawIndexInvalid]
    }
}

// 0x6766fe mov ecx, [esi+edi*4+0x64]; mov edx, [ecx]  (draw the page)
__declspec(naked) void drawPageHook() {
    __asm {
        push eax
        mov ecx, esi
        mov edx, edi
        call titlePageAt
        mov ecx, eax
        mov edx, [ecx]
        pop eax
        jmp dword ptr [drawPageResume]
    }
}

// 0x5c6357 mov ecx, [edi+0x18]; mov eax, [eax+0x1c]  (text of a menu row; eax = text manager).
// The game code after rowTextModResume uses ecx as the text and skips it if null.
__declspec(naked) void rowTextHook() {
    __asm {
        mov ecx, [edi + 0x18]
        cmp ecx, 0x7fff0000  ; modTitleTextBase
        jae modText
        mov eax, [eax + 0x1c]
        jmp dword ptr [rowTextGameResume]
    modText:
        push edx
        call modTitleText
        mov ecx, eax
        pop edx
        jmp dword ptr [rowTextModResume]
    }
}

// 0x5c6426: the same lookup for the selected row.
__declspec(naked) void rowLabelHook() {
    __asm {
        mov ecx, [edi + 0x18]
        cmp ecx, 0x7fff0000  ; modTitleTextBase
        jae modText
        mov eax, [eax + 0x1c]
        jmp dword ptr [rowLabelGameResume]
    modText:
        push edx
        call modTitleText
        mov ecx, eax
        pop edx
        jmp dword ptr [rowLabelModResume]
    }
}

// The function hooks below save every register, the flags and the x87/SSE state
// around the C++ code: the game's optimizer passes values in registers across
// calls, so the caller may rely on more than the usual calling convention.
// Stack after "push eax" (the fxsave pointer): +4 the 0x220-byte save area,
// +0x224 the pushad registers (ecx at +0x224+24, eax at +0x224+28), +0x244 the flags,
// +0x248 the ecx pushed before calling the original.

// 0x676120: the title options parent's initialize (thiscall, no arguments).
__declspec(naked) void titleInitializeHook() {
    __asm {
        push ecx
        call dword ptr [titleInitializeOriginal]
        pushfd
        pushad
        sub esp, 0x220
        lea eax, [esp + 15]
        and eax, 0xfffffff0
        fxsave [eax]
        push eax
        fninit
        push dword ptr [esp + 0x248]
        call addTitlePages
        add esp, 4
        pop eax
        fxrstor [eax]
        add esp, 0x220
        popad
        popfd
        lea esp, [esp + 4]
        ret
    }
}

// 0x676050: the title options parent's destructor body (thiscall, no arguments).
__declspec(naked) void titleDestroyHook() {
    __asm {
        pushfd
        pushad
        sub esp, 0x220
        lea eax, [esp + 15]
        and eax, 0xfffffff0
        fxsave [eax]
        push eax
        fninit
        push dword ptr [esp + 0x224 + 24]
        call releaseTitlePages
        add esp, 4
        pop eax
        fxrstor [eax]
        add esp, 0x220
        popad
        popfd
        jmp dword ptr [titleDestroyOriginal]
    }
}

// 0x676cb0: the pause menu builder (thiscall, no arguments, returns the rows in eax).
__declspec(naked) void pauseMenuBuildHook() {
    __asm {
        push ecx
        call dword ptr [pauseMenuBuildOriginal]
        pushfd
        pushad
        sub esp, 0x220
        lea eax, [esp + 15]
        and eax, 0xfffffff0
        fxsave [eax]
        push eax
        fninit
        push dword ptr [esp + 0x248]
        call addPauseMenuPages
        add esp, 4
        test eax, eax
        jz keepRows
        mov [esp + 0x224 + 28], eax
    keepRows:
        pop eax
        fxrstor [eax]
        add esp, 0x220
        popad
        popfd
        lea esp, [esp + 4]
        ret
    }
}

// 0x50e870: the game's per-frame update.
__declspec(naked) void frameHook() {
    __asm {
        call dword ptr [frameOriginal]
        pushfd
        pushad
        sub esp, 0x220
        lea eax, [esp + 15]
        and eax, 0xfffffff0
        fxsave [eax]
        push eax
        fninit
        call runFrameHandlers
        pop eax
        fxrstor [eax]
        add esp, 0x220
        popad
        popfd
        ret
    }
}

// 0x536ef0: copies the options to S4-SETTINGS.ini.
__declspec(naked) void optionsSavedHook() {
    __asm {
        call dword ptr [optionsSavedOriginal]
        pushfd
        pushad
        sub esp, 0x220
        lea eax, [esp + 15]
        and eax, 0xfffffff0
        fxsave [eax]
        push eax
        fninit
        call runOptionsSavedHandlers
        pop eax
        fxrstor [eax]
        add esp, 0x220
        popad
        popfd
        ret
    }
}

const HookSite frameSites[] = {
    {0x50e870, 6, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8}, 6, frameHook, &frameOriginal},
};
const HookSite optionsSavedSites[] = {
    {0x536ef0, 9, {0x55, 0x8b, 0xec, 0x81, 0xec, 0x04, 0x01, 0x00, 0x00}, 9, optionsSavedHook, &optionsSavedOriginal},
};
const HookSite optionsMenuSites[] = {
    {0x676120, 5, {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68}, 6, titleInitializeHook, &titleInitializeOriginal},
    {0x676050, 5, {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68}, 6, titleDestroyHook, &titleDestroyOriginal},
    {0x676cb0, 6, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec, 0xd4, 0x00, 0x00, 0x00}, 12,
        pauseMenuBuildHook, &pauseMenuBuildOriginal},
    {0x67653d, 6, {0x8b, 0x44, 0x86, 0x64, 0x8b, 0x10}, 6, enterPageHook, nullptr},
    {0x6765c1, 6, {0x8b, 0x4c, 0x9e, 0x64, 0x8b, 0x11}, 6, pageInputHook, nullptr},
    {0x6765e4, 6, {0x8b, 0x4c, 0x9e, 0x64, 0x8b, 0x11}, 6, leavePageHook, nullptr},
    {0x67663e, 11, {0x83, 0x7c, 0x86, 0x64, 0x00, 0x74, 0x0d, 0x8b, 0x44, 0x86, 0x64}, 11, updatePageHook, nullptr},
    {0x6766cb, 5, {0x83, 0xff, 0x04, 0x77, 0x0a}, 5, drawIndexHook, nullptr},
    {0x6766fe, 6, {0x8b, 0x4c, 0xbe, 0x64, 0x8b, 0x11}, 6, drawPageHook, nullptr},
    {0x5c6357, 6, {0x8b, 0x4f, 0x18, 0x8b, 0x40, 0x1c}, 6, rowTextHook, nullptr},
    {0x5c6426, 6, {0x8b, 0x4f, 0x18, 0x8b, 0x40, 0x1c}, 6, rowLabelHook, nullptr},
};

void resolveResumeAddresses() {
    enterPageResume = gameAddress(0x676543);
    pageInputResume = gameAddress(0x6765c7);
    leavePageResume = gameAddress(0x6765ea);
    updatePageFound = gameAddress(0x676649);
    updatePageMissing = gameAddress(0x676652);
    drawIndexValid = gameAddress(0x6766d0);
    drawIndexInvalid = gameAddress(0x6766da);
    drawPageResume = gameAddress(0x676704);
    rowTextGameResume = gameAddress(0x5c635d);
    rowTextModResume = gameAddress(0x5c636e);
    rowLabelGameResume = gameAddress(0x5c642c);
    rowLabelModResume = gameAddress(0x5c643d);
}

template <size_t N>
bool sitesMatch(const HookSite (&sites)[N]) {
    for (const HookSite& site : sites) {
        if (memcmp(reinterpret_cast<void*>(gameAddress(site.address)), site.expected, site.expectedLength)) return false;
    }
    return true;
}

template <size_t N>
bool claimSites(const HookSite (&sites)[N]) {
    for (const HookSite& site : sites) {
        if (!claimHook(hookOwner, gameAddress(site.address), site.length)) return false;
    }
    return true;
}

bool exeTimestampMatches(HMODULE game) {
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const uint8_t*>(game) + dos->e_lfanew);
    return dos->e_magic == IMAGE_DOS_SIGNATURE && nt->Signature == IMAGE_NT_SIGNATURE &&
        nt->FileHeader.TimeDateStamp == supportedExeTimestamp;
}

// Creates and enables every hook of a group. On failure, removes the ones it made.
template <size_t N>
bool installSites(const HookSite (&sites)[N], const char* groupName) {
    size_t created = 0;
    MH_STATUS status = MH_OK;
    for (; created < N; ++created) {
        const HookSite& site = sites[created];
        void* unusedTrampoline = nullptr;
        status = MH_CreateHook(reinterpret_cast<void*>(gameAddress(site.address)), site.detour,
            site.original ? site.original : &unusedTrampoline);
        if (status != MH_OK) break;
    }
    if (created == N) {
        for (const HookSite& site : sites) {
            status = MH_QueueEnableHook(reinterpret_cast<void*>(gameAddress(site.address)));
            if (status != MH_OK) break;
        }
        if (status == MH_OK) status = MH_ApplyQueued();
        if (status == MH_OK) return true;
    }
    for (size_t i = 0; i < created; ++i) MH_RemoveHook(reinterpret_cast<void*>(gameAddress(sites[i].address)));
    char line[160];
    sprintf_s(line, "could not install the %s hooks (MinHook: %s); that service is unavailable",
        groupName, MH_StatusToString(status));
    registryLog(nullptr, line);
    return false;
}

}  // namespace

void prepareGameHooks(HMODULE game, HMODULE owner) {
    gameBase = reinterpret_cast<uintptr_t>(game);
    hookOwner = owner;
    hostRegistry.supported = {};
    if (!exeTimestampMatches(game)) {
        registryLog(nullptr, "this game exe is not the supported version; options pages, onFrame and onOptionsSaved are unavailable");
        return;
    }
    hostRegistry.supported.options = sitesMatch(optionsMenuSites) && claimSites(optionsMenuSites);
    hostRegistry.supported.frame = sitesMatch(frameSites) && claimSites(frameSites);
    hostRegistry.supported.optionsSaved = sitesMatch(optionsSavedSites) && claimSites(optionsSavedSites);
    if (!hostRegistry.supported.options) registryLog(nullptr, "the options menu code is not as expected; options pages are unavailable");
    if (!hostRegistry.supported.frame) registryLog(nullptr, "the frame update code is not as expected; onFrame is unavailable");
    if (!hostRegistry.supported.optionsSaved) registryLog(nullptr, "the options save code is not as expected; onOptionsSaved is unavailable");
}

void installGameHooks() {
    const bool wantOptions = !hostRegistry.pages.empty();
    const bool wantFrame = !hostRegistry.frameHandlers.empty();
    const bool wantOptionsSaved = !hostRegistry.optionsSavedHandlers.empty();
    if (!wantOptions && !wantFrame && !wantOptionsSaved) return;
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        registryLog(nullptr, std::string("could not start MinHook (") + MH_StatusToString(status) +
            "); options pages, onFrame and onOptionsSaved are unavailable");
        return;
    }
    if (wantOptions) {
        resolveResumeAddresses();
        prepareModPageMethods();
        if (installSites(optionsMenuSites, "options menu")) {
            char line[96];
            sprintf_s(line, "options menu hooks installed for %u Mod page(s)", static_cast<unsigned>(hostRegistry.pages.size()));
            registryLog(nullptr, line);
        }
    }
    if (wantFrame && installSites(frameSites, "frame update")) registryLog(nullptr, "frame update hook installed");
    if (wantOptionsSaved && installSites(optionsSavedSites, "options save"))
        registryLog(nullptr, "options save hook installed");
}

}  // namespace wots4::game
