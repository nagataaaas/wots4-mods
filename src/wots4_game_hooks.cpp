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
// A page itself shows 8 items; one with more scrolls (see "scrolling pages").
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

// Page object: the game's 0xa0 bytes, then ours: the extension, one item pointer
// per item, then one value per item. They are allocated with the page, so the
// game frees them with it.
constexpr uint32_t pageBaseSize = 0xa0;
struct PageExtension {
    uint32_t definition;        // Index into hostRegistry.pages
    uint32_t built;             // 1 once every item was appended
    uint32_t itemCount;
    uint32_t firstVisibleItem;  // Scrolling pages: the item in the top row
};

size_t pageSize(uint32_t itemCount) {
    return pageBaseSize + sizeof(PageExtension) + itemCount * (sizeof(uint8_t*) + sizeof(uint32_t));
}

// Page fields. The page is an InfoArea (base 0x636410), which loads the layouts
// and animations its descriptor names into slots.
constexpr uint32_t pageLayoutDescriptor = 0x08;
constexpr uint32_t pageLayoutSlots = 0x0c;     // Array, one slot per layout of the descriptor
constexpr uint32_t pageAnimationSlots = 0x10;  // Array, one slot per animation
// Copied into each slot when the slots are made; the loader (0x5b5f40) looks the
// files up only in these archives. Each page's constructor writes its own values.
constexpr uint32_t pageArchives = 0x24;  // Bit set of the game's resource archives
constexpr uint32_t pageSlotValue = 0x28;
constexpr uint32_t pageLayoutsLoaded = 0x2c;   // byte
constexpr uint32_t pageItemsBegin = 0x4c;      // Item vector: begin, end, end of the capacity
constexpr uint32_t pageItemsEnd = 0x50;
constexpr uint32_t pageItemsCapacityEnd = 0x54;
constexpr uint32_t pageCursor = 0x58;          // int32: index into the item vector
constexpr int32_t noCursor = -1;               // No item is selected

// Page vtable slots this file replaces (byte offsets).
constexpr uint32_t buildItemsSlot = 0x3c;
constexpr uint32_t resetItemsSlot = 0x40;    // The 初期設定に戻す button; refreshItemsSlot follows
constexpr uint32_t refreshItemsSlot = 0x44;
constexpr uint32_t pageSpecificSlotB = 0x48;  // Used by the game's own pages when leaving; ours do nothing
constexpr uint32_t itemChangedSlot = 0x58;
constexpr uint32_t initializeSlot = 0x04;
constexpr uint32_t updateSlot = 0x08;
constexpr uint32_t drawSlot = 0x10;   // Scrolling pages
constexpr uint32_t inputSlot = 0x34;  // Scrolling pages

// Layout slot fields (0x40 bytes, made by 0x5b9d20)
constexpr uint32_t layoutShown = 0x1a;      // byte
constexpr uint32_t layoutDrawValue = 0x24;  // Passed to drawLayout
constexpr uint32_t layoutX = 0x2c;          // float, from the centre of the screen
constexpr uint32_t layoutY = 0x30;          // float, from the centre of the screen
constexpr uint32_t layoutRotation = 0x34;   // float, degrees

// InfoArea functions; they take their arguments in registers.
constexpr uint32_t createLayoutSlots = 0x5b9d20;      // eax = count, esi = page
constexpr uint32_t releaseLayoutSlots = 0x5b9e50;     // esi = page
constexpr uint32_t createAnimationSlots = 0x5b9ec0;   // eax = count, esi = page
constexpr uint32_t releaseAnimationSlots = 0x5b9fb0;  // esi = page
constexpr uint32_t playAnimation = 0x5ba380;          // ecx = animation, edi = page; shows its layout
constexpr uint32_t stopAnimation = 0x5ba7e0;          // edi = animation, esi = page; hides its layout
constexpr uint32_t advanceAnimation = 0x5b8670;       // esi = animation slot
constexpr uint32_t updateLayout = 0x5bb9d0;           // esi = layout slot
constexpr uint32_t drawLayout = 0x5bbd70;             // thiscall(layout slot, [slot + layoutDrawValue])

// Input, pointer, screen and sound, as the options pages use them
constexpr uint32_t inputService = 0x9ebb8c;     // Pointer; the direction queries read its buttons
constexpr uint32_t inputButtons = 0xc8;         // Pointer the queries dereference
constexpr uint32_t upPressed = 0x680340;        // eax = input service; al
constexpr uint32_t downPressed = 0x680280;      // eax = input service; al
constexpr uint32_t pointerService = 0x9ecf30;   // Pointer
constexpr uint32_t pointerInUse = 0x44c;        // byte; the game sets it before reading the pointer
constexpr uint32_t pointerPressed = 0x1c00;     // byte
constexpr uint32_t pointerPressTime = 0x1c1c;   // int32; the game's buttons count a click below 100
constexpr int32_t clickTimeLimit = 100;
constexpr uint32_t readPointer = 0x4f4960;      // ecx = 0, edi = &x, push &y (the caller pops); al = known
constexpr uint32_t screenService = 0x9ecfb4;    // Pointer
constexpr uint32_t screenWidth = 0x10;          // int32
constexpr uint32_t screenHeight = 0x14;         // int32
constexpr uint32_t playSound = 0x5a0050;        // ecx = 0, edx = sound, push 0.0f (the caller pops)
constexpr uint32_t cursorMovedSound = 0x61;     // The options pages play it when the cursor moves

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
// byte. Kind 4 (a row to read, which the input passes over) draws its toggle texts
// unless this is set (0x645deb); the game's 各種設定 page sets it on such a row.
constexpr uint32_t itemValueHidden = 0x309;
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

void createSlots(uint32_t function, void* page, uint32_t count) {
    const uintptr_t create = gameAddress(function);
    __asm {
        push esi
        mov esi, page
        mov eax, count
        call create
        pop esi
    }
}

void releaseSlots(uint32_t function, void* page) {
    const uintptr_t release = gameAddress(function);
    __asm {
        push esi
        mov esi, page
        call release
        pop esi
    }
}

void playPageAnimation(void* page, uint32_t animation) {
    const uintptr_t play = gameAddress(playAnimation);
    __asm {
        push edi
        mov edi, page
        mov ecx, animation
        call play
        pop edi
    }
}

void stopPageAnimation(void* page, uint32_t animation) {
    const uintptr_t stop = gameAddress(stopAnimation);
    __asm {
        push esi
        push edi
        mov esi, page
        mov edi, animation
        call stop
        pop edi
        pop esi
    }
}

// advanceAnimation and updateLayout: esi = slot
void runOnSlot(uint32_t function, void* slot) {
    const uintptr_t run = gameAddress(function);
    __asm {
        push esi
        mov esi, slot
        call run
        pop esi
    }
}

void drawLayoutSlot(uint8_t* slot) {
    reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(gameAddress(drawLayout))(slot, field<uint32_t>(slot, layoutDrawValue));
}

bool directionPressed(uint32_t query) {
    void* input = *reinterpret_cast<void**>(gameAddress(inputService));
    if (!input || !field<void*>(input, inputButtons)) return false;
    const uintptr_t ask = gameAddress(query);
    uint8_t pressed;
    __asm {
        mov eax, input
        call ask
        mov pressed, al
    }
    return pressed != 0;
}

// The pointer's position in the menu coordinates, if the game knows it.
bool readPointerPosition(int32_t& x, int32_t& y) {
    auto service = *reinterpret_cast<uint8_t**>(gameAddress(pointerService));
    if (!service) return false;
    service[pointerInUse] = 1;
    const uintptr_t read = gameAddress(readPointer);
    int32_t pointerX = 0, pointerY = 0;
    uint8_t known;
    __asm {
        push edi
        lea edi, pointerX
        lea eax, pointerY
        push eax
        xor ecx, ecx
        call read
        add esp, 4
        mov known, al
        pop edi
    }
    x = pointerX;
    y = pointerY;
    return known != 0;
}

bool pointerClicked() {
    auto service = *reinterpret_cast<uint8_t**>(gameAddress(pointerService));
    return service && service[pointerPressed] && field<int32_t>(service, pointerPressTime) < clickTimeLimit;
}

void playMenuSound(uint32_t sound) {
    const uintptr_t play = gameAddress(playSound);
    __asm {
        push 0  ; 0.0f, as every menu passes
        xor ecx, ecx
        mov edx, sound
        call play
        add esp, 4
    }
}

void copyItemText(uint8_t* item, uint32_t offset, const OptionText& text) {
    wcsncpy_s(reinterpret_cast<wchar_t*>(item + offset), itemTextChars, text.text.c_str(), _TRUNCATE);
}

// ---------------------------------------------------------------- game texts

// GameTextUI: the text manager points to {count, pointer array}.
constexpr uint32_t gameTextManager = 0x9e9680;
constexpr uint32_t gameTextUiTable = 0x1c;
// The menu language, set at startup from S4-SETTINGS.ini [Language] (0x537b28). A change in
// the game takes effect after a restart. 0 is Japanese; every other language uses the
// English menu texts (US/), the only other ones the game has.
constexpr uint32_t gameTextLanguage = 0x4c;
constexpr uint32_t japaneseLanguage = 0;

bool menusInJapanese() {
    auto manager = *reinterpret_cast<uint8_t**>(gameAddress(gameTextManager));
    return manager && field<uint32_t>(manager, gameTextLanguage) == japaneseLanguage;
}

const wchar_t* gameTextString(uint32_t number) {
    auto manager = *reinterpret_cast<uint8_t**>(gameAddress(gameTextManager));
    if (!manager) return nullptr;
    auto table = field<const uint32_t*>(manager, gameTextUiTable);
    if (!table || number >= table[0]) return nullptr;
    auto strings = reinterpret_cast<const wchar_t* const*>(table[1]);
    return strings ? strings[number] : nullptr;
}

// Sets text from the source of the game's language. Runs inside hooks, where no
// exception may escape; an allocation failure counts as missing.
bool resolveText(OptionText& text, bool japanese) {
    const TextSource& source = japanese ? text.japanese : text.english;
    const wchar_t* string = source.gameTextNumber ? gameTextString(source.gameTextNumber) : source.string.c_str();
    if (!string) return false;
    try {
        text.text = string;
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

bool addHeaderBrackets(OptionText& text, bool japanese) {
    const HeaderBrackets& brackets = japanese ? japaneseHeaderBrackets : englishHeaderBrackets;
    try {
        text.text.insert(0, brackets.open);
        text.text.append(brackets.close);
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

// Resolves every text of every page, all at once, so that no pointer the game
// keeps is ever moved afterwards. False if the game texts are not loaded.
bool resolveTexts() {
    if (hostRegistry.textsResolved) return true;
    const bool japanese = menusInJapanese();
    for (OptionsPageDefinition& page : hostRegistry.pages) {
        if (!resolveText(page.title, japanese)) return false;
        for (OptionsItemDefinition& item : page.items) {
            if (!resolveText(item.label, japanese)) return false;
            if (item.kind == optionHeader && !addHeaderBrackets(item.label, japanese)) return false;
            for (OptionText& choice : item.choices) {
                if (!resolveText(choice, japanese)) return false;
            }
        }
    }
    for (OptionsPageDefinition& page : hostRegistry.pages) bindChoicePointers(page);
    hostRegistry.textsResolved = true;
    registryLog(nullptr, japanese ? "Mod options pages use the Japanese texts (game language 0)"
                                  : "Mod options pages use the English texts (game language other than 0)");
    return true;
}

// ---------------------------------------------------------------- Mod options pages

uintptr_t modPageMethods[optionsPageMethodCount];

PageExtension& extensionOf(void* page) {
    return field<PageExtension>(page, pageBaseSize);
}

uint8_t** itemsOf(void* page) {
    return reinterpret_cast<uint8_t**>(&extensionOf(page) + 1);
}

uint32_t* valuesOf(void* page) {
    return reinterpret_cast<uint32_t*>(itemsOf(page) + extensionOf(page).itemCount);
}

const OptionsPageDefinition& definitionOf(void* page) {
    return hostRegistry.pages[extensionOf(page).definition];
}

uint8_t* buildItem(const OptionsItemDefinition& definition) {
    auto item = static_cast<uint8_t*>(allocateGameObject(itemSize));
    if (!item) return nullptr;
    initializeItem(item);
    copyItemText(item, itemLabel, definition.label);
    // The game draws a header without the cursor's highlight, and its input and
    // pointer pass over it (0x645b0a, 0x646199, 0x64611e).
    if (definition.kind == optionHeader) {
        field<uint32_t>(item, itemKind) = optionHeader;
        item[itemValueHidden] = 1;
        return item;
    }
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

// The constructor gives the item vector 10 places, and appending never grows it.
// The page's destructor frees the vector with the game's operator delete.
bool reserveItemPlaces(void* page, uint32_t count) {
    auto& begin = field<void**>(page, pageItemsBegin);
    auto& end = field<void**>(page, pageItemsEnd);
    auto& capacityEnd = field<void**>(page, pageItemsCapacityEnd);
    if (static_cast<uint32_t>(capacityEnd - begin) >= count) return true;
    auto places = static_cast<void**>(allocateGameObject(count * sizeof(void*)));
    if (!places) return false;
    const auto used = static_cast<uint32_t>(end - begin);
    if (used) memcpy(places, begin, used * sizeof(void*));
    if (begin) releaseGameObject(begin);
    begin = places;
    end = places + used;
    capacityEnd = places + count;
    return true;
}

// The page owns appended items and frees them with its vector.
bool buildItems(void* page) {
    PageExtension& extension = extensionOf(page);
    const auto& items = definitionOf(page).items;
    if (!reserveItemPlaces(page, extension.itemCount)) return false;
    for (uint32_t i = 0; i < extension.itemCount; ++i) {
        uint8_t* item = buildItem(items[i]);
        if (!item) return false;
        if (appendItem(page, item) != static_cast<int>(i)) {
            releaseGameObject(item);
            return false;
        }
        itemsOf(page)[i] = item;
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
    const uint32_t count = extension.itemCount;
    uint32_t* values = valuesOf(page);
    memset(values, 0, count * sizeof(uint32_t));
    definition.callbacks.read(definition.callbacks.context, values, count);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* item = itemsOf(page)[i];
        const OptionsItemDefinition& itemDefinition = definition.items[i];
        if (itemDefinition.kind == optionHeader) continue;
        if (itemDefinition.kind == optionToggle) {
            item[itemToggleState] = values[i] ? 1 : 0;
            continue;
        }
        const auto choiceCount = static_cast<uint32_t>(itemDefinition.choices.size());
        field<uint32_t>(item, itemChoiceIndex) = values[i] < choiceCount ? values[i] : 0;
    }
}

// The value the item shows, as read and changed take it.
uint32_t shownValue(uint8_t* item, OptionKind kind) {
    if (kind == optionToggle) return item[itemToggleState];
    return field<uint32_t>(item, itemChoiceIndex);
}

uint32_t __fastcall itemChangedMethod(void* page, void*, void* item) {
    const PageExtension& extension = extensionOf(page);
    const OptionsPageDefinition& definition = definitionOf(page);
    uint8_t* const* items = itemsOf(page);
    for (uint32_t i = 0; i < extension.itemCount; ++i) {
        if (items[i] != item) continue;
        definition.callbacks.changed(definition.callbacks.context, i, shownValue(items[i], definition.items[i].kind));
        return 1;
    }
    return 0;
}

// The game's own pages write their defaults to the settings here. The Mod gets
// each item that is not at its default as a change; the refresh that follows
// reads the values back from the Mod.
void __fastcall resetItemsMethod(void* page, void*) {
    const PageExtension& extension = extensionOf(page);
    const OptionsPageDefinition& definition = definitionOf(page);
    if (!extension.built) return;
    uint8_t* const* items = itemsOf(page);
    for (uint32_t i = 0; i < extension.itemCount; ++i) {
        const OptionsItemDefinition& item = definition.items[i];
        if (item.kind == optionHeader || shownValue(items[i], item.kind) == item.defaultValue) continue;
        definition.callbacks.changed(definition.callbacks.context, i, item.defaultValue);
    }
}

// ---------------------------------------------------------------- scrolling pages
//
// A page with more items than optionsItemsPerScreen shows that many rows and
// scrolls, like the game's control mapping page (0x64a7b0): the rows follow the
// cursor, arrows above and below show that more items are there, and a click on
// an arrow turns a screen of rows. The game's own page methods do the rest; they
// draw and hit-test items by their index in the vector, so while they run the
// page shows them only the items of its rows.

// The layouts and animations a page loads (InfoArea +8). The game's descriptors
// hold absolute pointers, relocated with the exe.
struct LayoutFile {
    char name[0x40];
};
struct LayoutAnimation {
    uint8_t flags[4];
    uint32_t unknown;  // 1 in every options page
    char name[0x20];
    char file[0x40];
    int32_t layout;  // The layout it plays on
};
static_assert(sizeof(LayoutAnimation) == 0x6c, "the game's animation entries are 0x6c bytes");
struct LayoutDescriptor {
    uint32_t archives;   // The page constructor writes the same values to pageArchives
    uint32_t slotValue;  // and pageSlotValue
    const LayoutFile* layouts;
    uint32_t layoutCount;
    const LayoutAnimation* animations;
    uint32_t animationCount;
};

constexpr uint32_t optionsPageLayouts = 0x96a788;     // Base options page: frame, rows
constexpr uint32_t controlMappingLayouts = 0x969f70;  // Control mapping page: frame, paper, two arrows
constexpr uint32_t optionsPageLayoutCount = 2;        // The base update and draw run these only
constexpr uint32_t optionsPageAnimationCount = 4;
constexpr uint32_t controlMappingLayoutCount = 4;
constexpr uint32_t controlMappingAnimationCount = 6;
constexpr uint32_t controlMappingFirstArrowAnimation = 2;

// The scrolling page: the base page's layouts and animations, then the control
// mapping page's arrows with their animations (the same layout numbers).
constexpr uint32_t upArrowLayout = 2;
constexpr uint32_t downArrowLayout = 3;
constexpr uint32_t scrollingLayoutCount = 4;
constexpr uint32_t upArrowShown = 4;  // sign_white
constexpr uint32_t upArrowMoved = 5;  // sign_up
constexpr uint32_t downArrowShown = 6;
constexpr uint32_t downArrowMoved = 7;
constexpr uint32_t scrollingAnimationCount = 8;

LayoutFile scrollingLayoutFiles[scrollingLayoutCount];
LayoutAnimation scrollingAnimations[scrollingAnimationCount];
LayoutDescriptor scrollingLayouts;

// Menu coordinates (those of the pointer and of the layouts). The rows of the
// base page take y 194 to 519 (8 rows from 0xc2, 0x29 apart); the arrows go just
// above and below them, clear of the page's button at x 800 to 1170, y 550 to 600.
struct MenuPoint {
    float x, y;
};
constexpr MenuPoint upArrowCentre{755, 164};
constexpr MenuPoint downArrowCentre{755, 548};
constexpr float downArrowRotation = 180;  // u_sign points up
constexpr int32_t arrowClickHalfWidth = 20;  // The control mapping page's click areas are 40 by 50
constexpr int32_t arrowClickHalfHeight = 25;

const LayoutDescriptor& gameLayouts(uint32_t descriptor) {
    return *reinterpret_cast<const LayoutDescriptor*>(gameAddress(descriptor));
}

bool isArrowAnimation(const LayoutAnimation& animation, const char* name, int32_t layout) {
    return !strcmp(animation.name, name) && animation.layout == layout;
}

// The descriptors the scrolling page is made of are as expected.
bool scrollingLayoutsMatch() {
    const LayoutDescriptor& base = gameLayouts(optionsPageLayouts);
    const LayoutDescriptor& mapping = gameLayouts(controlMappingLayouts);
    if (base.layoutCount != optionsPageLayoutCount || base.animationCount != optionsPageAnimationCount ||
        mapping.layoutCount != controlMappingLayoutCount || mapping.animationCount != controlMappingAnimationCount)
        return false;
    const LayoutAnimation* arrows = mapping.animations + controlMappingFirstArrowAnimation;
    return !strcmp(mapping.layouts[upArrowLayout].name, "u_sign.layout") &&
        !strcmp(mapping.layouts[downArrowLayout].name, "u_sign.layout") &&
        isArrowAnimation(arrows[0], "sign_white", upArrowLayout) && isArrowAnimation(arrows[1], "sign_up", upArrowLayout) &&
        isArrowAnimation(arrows[2], "sign_white", downArrowLayout) && isArrowAnimation(arrows[3], "sign_up", downArrowLayout);
}

void prepareScrollingLayouts() {
    const LayoutDescriptor& base = gameLayouts(optionsPageLayouts);
    const LayoutDescriptor& mapping = gameLayouts(controlMappingLayouts);
    memcpy(scrollingLayoutFiles, base.layouts, optionsPageLayoutCount * sizeof(LayoutFile));
    memcpy(scrollingLayoutFiles + upArrowLayout, mapping.layouts + upArrowLayout, 2 * sizeof(LayoutFile));
    memcpy(scrollingAnimations, base.animations, optionsPageAnimationCount * sizeof(LayoutAnimation));
    memcpy(scrollingAnimations + upArrowShown, mapping.animations + controlMappingFirstArrowAnimation,
        4 * sizeof(LayoutAnimation));
    // The arrows' files are not in the base page's archives (0x10), but in the
    // control mapping page's (0x31); without them, loading stops at the first arrow
    // and the base initialize plays an animation that was never loaded.
    scrollingLayouts = {base.archives | mapping.archives, base.slotValue, scrollingLayoutFiles, scrollingLayoutCount,
        scrollingAnimations, scrollingAnimationCount};
}

uint8_t* layoutSlot(void* page, uint32_t layout) {
    return field<uint8_t**>(page, pageLayoutSlots)[layout];
}

void placeArrow(uint8_t* slot, MenuPoint centre, float rotation) {
    auto screen = *reinterpret_cast<uint8_t**>(gameAddress(screenService));
    const float halfWidth = screen ? field<int32_t>(screen, screenWidth) * 0.5f : 0;
    const float halfHeight = screen ? field<int32_t>(screen, screenHeight) * 0.5f : 0;
    field<float>(slot, layoutX) = centre.x - halfWidth;
    field<float>(slot, layoutY) = centre.y - halfHeight;
    field<float>(slot, layoutRotation) = rotation;
}

bool slotsMade(void* page, uint32_t array, uint32_t count) {
    auto slots = field<void**>(page, array);
    if (!slots) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!slots[i]) return false;
    }
    return true;
}

// The base constructor made slots for its own descriptor and archives, 2 layout
// slots fixed in its code; the page's initialize loads as many as the descriptor names.
bool useScrollingLayouts(void* page) {
    releaseSlots(releaseLayoutSlots, page);
    releaseSlots(releaseAnimationSlots, page);
    field<const LayoutDescriptor*>(page, pageLayoutDescriptor) = &scrollingLayouts;
    field<uint32_t>(page, pageArchives) = scrollingLayouts.archives;
    field<uint32_t>(page, pageSlotValue) = scrollingLayouts.slotValue;
    createSlots(createLayoutSlots, page, scrollingLayoutCount);
    createSlots(createAnimationSlots, page, scrollingAnimationCount);
    if (!slotsMade(page, pageLayoutSlots, scrollingLayoutCount) ||
        !slotsMade(page, pageAnimationSlots, scrollingAnimationCount))
        return false;
    placeArrow(layoutSlot(page, upArrowLayout), upArrowCentre, 0);
    placeArrow(layoutSlot(page, downArrowLayout), downArrowCentre, downArrowRotation);
    return true;
}

int32_t& cursorOf(void* page) {
    return field<int32_t>(page, pageCursor);
}

uint32_t lastFirstVisibleItem(const PageExtension& extension) {
    return extension.itemCount - optionsItemsPerScreen;
}

bool hasItemsAbove(const PageExtension& extension) {
    return extension.firstVisibleItem > 0;
}

bool hasItemsBelow(const PageExtension& extension) {
    return extension.firstVisibleItem < lastFirstVisibleItem(extension);
}

bool isHeader(void* page, int32_t item) {
    return definitionOf(page).items[item].kind == optionHeader;
}

// Moves the rows as little as needed to show item (noCursor shows nothing).
void bringIntoRows(PageExtension& extension, int32_t item) {
    uint32_t& first = extension.firstVisibleItem;
    if (item >= 0 && static_cast<uint32_t>(item) < extension.itemCount) {
        const auto shown = static_cast<uint32_t>(item);
        if (shown < first) first = shown;
        else if (shown >= first + optionsItemsPerScreen) first = shown - optionsItemsPerScreen + 1;
    }
    if (first > lastFirstVisibleItem(extension)) first = lastFirstVisibleItem(extension);
}

void keepCursorVisible(void* page) {
    bringIntoRows(extensionOf(page), cursorOf(page));
}

void playScrollAnimation(void* page, uint32_t firstBefore) {
    const uint32_t first = extensionOf(page).firstVisibleItem;
    if (first != firstBefore) playPageAnimation(page, first < firstBefore ? upArrowMoved : downArrowMoved);
}

// Where the game's input (0x645f90) moves the cursor among all items: one item
// up or down, one more past a header, and from the first or last item to no item.
int32_t nextCursorItem(void* page, int32_t cursor, bool up) {
    const auto count = static_cast<int32_t>(extensionOf(page).itemCount);
    const int32_t step = up ? -1 : 1;
    int32_t next = cursor + step;
    if (next >= 0 && next < count && isHeader(page, next)) next += step;
    return next >= 0 && next < count ? next : noCursor;
}

// The game's input moves the cursor within the rows it is shown, and from the
// last row to no item. Showing the item it goes to first turns that step into a
// step among all items. From no item it goes to the first or the last row, so
// the rows go to the top or the bottom.
void moveRowsForDirectionKeys(void* page) {
    const bool up = directionPressed(upPressed);
    const bool down = directionPressed(downPressed);
    if (up == down) return;
    PageExtension& extension = extensionOf(page);
    const int32_t cursor = cursorOf(page);
    const uint32_t firstBefore = extension.firstVisibleItem;
    if (cursor == noCursor) extension.firstVisibleItem = up ? lastFirstVisibleItem(extension) : 0;
    else bringIntoRows(extension, nextCursorItem(page, cursor, up));
    playScrollAnimation(page, firstBefore);
}

bool pointerOnArrow(int32_t x, int32_t y, MenuPoint centre) {
    const auto centreX = static_cast<int32_t>(centre.x);
    const auto centreY = static_cast<int32_t>(centre.y);
    return x >= centreX - arrowClickHalfWidth && x <= centreX + arrowClickHalfWidth &&
        y >= centreY - arrowClickHalfHeight && y <= centreY + arrowClickHalfHeight;
}

// Moves the rows and the cursor by rows (negative is up), within the items.
void turnRows(void* page, int32_t rows) {
    PageExtension& extension = extensionOf(page);
    const uint32_t firstBefore = extension.firstVisibleItem;
    const auto lastFirst = static_cast<int32_t>(lastFirstVisibleItem(extension));
    const int32_t first = static_cast<int32_t>(firstBefore) + rows;
    extension.firstVisibleItem = static_cast<uint32_t>(first < 0 ? 0 : first > lastFirst ? lastFirst : first);
    int32_t& cursor = cursorOf(page);
    if (cursor != noCursor) {
        const int32_t lastItem = static_cast<int32_t>(extension.itemCount) - 1;
        const int32_t moved = cursor + rows;
        cursor = moved < 0 ? 0 : moved > lastItem ? lastItem : moved;
        // Headers are never next to each other, so the item beside one is not a header.
        if (isHeader(page, cursor)) {
            const int32_t step = rows < 0 ? -1 : 1;
            cursor = cursor + step >= 0 && cursor + step <= lastItem ? cursor + step : cursor - step;
        }
        bringIntoRows(extension, cursor);
    }
    playScrollAnimation(page, firstBefore);
    playMenuSound(cursorMovedSound);
}

void turnRowsForArrowClick(void* page) {
    if (!pointerClicked()) return;
    int32_t x, y;
    if (!readPointerPosition(x, y)) return;
    const PageExtension& extension = extensionOf(page);
    const auto screenRows = static_cast<int32_t>(optionsItemsPerScreen);
    if (hasItemsAbove(extension) && pointerOnArrow(x, y, upArrowCentre)) turnRows(page, -screenRows);
    else if (hasItemsBelow(extension) && pointerOnArrow(x, y, downArrowCentre)) turnRows(page, screenRows);
}

// While it lives, the page's item vector and cursor cover only the items of its
// rows. Expects the cursor in the rows or at no item.
class VisibleRows {
public:
    explicit VisibleRows(void* page)
        : page_(page), begin_(field<void**>(page, pageItemsBegin)), end_(field<void**>(page, pageItemsEnd)),
          first_(extensionOf(page).firstVisibleItem) {
        field<void**>(page_, pageItemsBegin) = begin_ + first_;
        field<void**>(page_, pageItemsEnd) = begin_ + first_ + optionsItemsPerScreen;
        int32_t& cursor = cursorOf(page_);
        if (cursor != noCursor) cursor -= static_cast<int32_t>(first_);
    }
    ~VisibleRows() {
        field<void**>(page_, pageItemsBegin) = begin_;
        field<void**>(page_, pageItemsEnd) = end_;
        int32_t& cursor = cursorOf(page_);
        if (cursor != noCursor) cursor += static_cast<int32_t>(first_);
    }
    VisibleRows(const VisibleRows&) = delete;
    VisibleRows& operator=(const VisibleRows&) = delete;

private:
    void* page_;
    void** begin_;
    void** end_;
    uint32_t first_;
};

// Like the control mapping page's update: each arrow shows while items are on its side.
void showArrows(void* page) {
    const PageExtension& extension = extensionOf(page);
    stopPageAnimation(page, upArrowShown);
    if (hasItemsAbove(extension)) playPageAnimation(page, upArrowShown);
    stopPageAnimation(page, downArrowShown);
    if (hasItemsBelow(extension)) playPageAnimation(page, downArrowShown);
}

// The base update runs only the base page's animations and layouts.
void advanceArrows(void* page) {
    auto animations = field<void**>(page, pageAnimationSlots);
    for (uint32_t i = optionsPageAnimationCount; i < scrollingAnimationCount; ++i) runOnSlot(advanceAnimation, animations[i]);
    runOnSlot(updateLayout, layoutSlot(page, upArrowLayout));
    runOnSlot(updateLayout, layoutSlot(page, downArrowLayout));
}

void drawArrows(void* page) {
    for (uint32_t layout : {upArrowLayout, downArrowLayout}) {
        uint8_t* slot = layoutSlot(page, layout);
        if (slot[layoutShown]) drawLayoutSlot(slot);
    }
}

using PageUpdate = uint32_t(__thiscall*)(void* page);
using PageDraw = uint32_t(__thiscall*)(void* page, uint32_t target);
using PageInput = uint8_t(__thiscall*)(void* page, uint32_t buttons, uint32_t state);
uintptr_t scrollingPageMethods[optionsPageMethodCount];
PageUpdate baseUpdate;
PageDraw baseDraw;
PageInput baseInput;

// Until every item is built and the layouts are loaded, a scrolling page acts as the base page.
bool readyToScroll(void* page) {
    return extensionOf(page).built && field<uint8_t>(page, pageLayoutsLoaded);
}

uint32_t __fastcall scrollingUpdateMethod(void* page, void*) {
    if (!readyToScroll(page)) return baseUpdate(page);
    showArrows(page);
    const uint32_t result = baseUpdate(page);
    advanceArrows(page);
    return result;
}

uint32_t __fastcall scrollingDrawMethod(void* page, void*, uint32_t target) {
    if (!readyToScroll(page)) return baseDraw(page, target);
    keepCursorVisible(page);
    uint32_t result;
    {
        VisibleRows rows(page);
        result = baseDraw(page, target);
    }
    drawArrows(page);
    return result;
}

uint8_t __fastcall scrollingInputMethod(void* page, void*, uint32_t buttons, uint32_t state) {
    if (!readyToScroll(page)) return baseInput(page, buttons, state);
    keepCursorVisible(page);
    turnRowsForArrowClick(page);
    moveRowsForDirectionKeys(page);
    uint8_t handled;
    {
        VisibleRows rows(page);
        handled = baseInput(page, buttons, state);
    }
    keepCursorVisible(page);
    return handled;
}

// ---------------------------------------------------------------- page objects

void replaceMethod(uintptr_t* methods, uint32_t slot, void* method) {
    methods[slot / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(method);
}

void prepareModPageMethods() {
    memcpy(modPageMethods, reinterpret_cast<void*>(gameAddress(optionsPageMethods)), sizeof(modPageMethods));
    baseUpdate = reinterpret_cast<PageUpdate>(modPageMethods[updateSlot / sizeof(uintptr_t)]);
    baseDraw = reinterpret_cast<PageDraw>(modPageMethods[drawSlot / sizeof(uintptr_t)]);
    baseInput = reinterpret_cast<PageInput>(modPageMethods[inputSlot / sizeof(uintptr_t)]);
    replaceMethod(modPageMethods, buildItemsSlot, buildItemsMethod);
    replaceMethod(modPageMethods, resetItemsSlot, resetItemsMethod);
    replaceMethod(modPageMethods, refreshItemsSlot, refreshItemsMethod);
    replaceMethod(modPageMethods, pageSpecificSlotB, noPageWork);
    replaceMethod(modPageMethods, itemChangedSlot, itemChangedMethod);

    memcpy(scrollingPageMethods, modPageMethods, sizeof(scrollingPageMethods));
    replaceMethod(scrollingPageMethods, updateSlot, scrollingUpdateMethod);
    replaceMethod(scrollingPageMethods, drawSlot, scrollingDrawMethod);
    replaceMethod(scrollingPageMethods, inputSlot, scrollingInputMethod);
    prepareScrollingLayouts();
}

void* createModPage(uint32_t definition) {
    const auto itemCount = static_cast<uint32_t>(hostRegistry.pages[definition].items.size());
    const size_t size = pageSize(itemCount);
    void* page = allocateGameObject(size);
    if (!page) return nullptr;
    memset(page, 0, size);
    reinterpret_cast<void*(__stdcall*)(void*)>(gameAddress(optionsPageConstructor))(page);
    PageExtension& extension = extensionOf(page);
    extension.definition = definition;
    extension.itemCount = itemCount;
    if (itemCount <= optionsItemsPerScreen) {
        field<uintptr_t*>(page, 0) = modPageMethods;
        return page;
    }
    field<uintptr_t*>(page, 0) = scrollingPageMethods;
    if (useScrollingLayouts(page)) return page;
    destroyGameObject(page);
    return nullptr;
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
    if (!resolveTexts()) {
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
    if (!resolveTexts()) {
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

// mov eax,[0x9e9680] (0x537b1d) ... mov [eax+0x4c],ebx (0x537b28): where the game stores the
// menu language that menusInJapanese reads.
bool languageCodeMatches() {
    const uint8_t readManager = 0xa1;
    const uint8_t storeLanguage[] = {0x89, 0x58, gameTextLanguage};
    return *reinterpret_cast<const uint8_t*>(gameAddress(0x537b1d)) == readManager &&
        *reinterpret_cast<const uint32_t*>(gameAddress(0x537b1e)) == gameAddress(gameTextManager) &&
        !memcmp(reinterpret_cast<void*>(gameAddress(0x537b28)), storeLanguage, sizeof(storeLanguage));
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
        registryLog(nullptr, "this game exe is not the supported version; options pages, onFrame, onOptionsSaved and findGameText are unavailable");
        return;
    }
    hostRegistry.supported.gameTexts = languageCodeMatches();
    if (!hostRegistry.supported.gameTexts) registryLog(nullptr, "the game text language code is not as expected; findGameText is unavailable");
    hostRegistry.supported.options =
        sitesMatch(optionsMenuSites) && hostRegistry.supported.gameTexts && scrollingLayoutsMatch() &&
        claimSites(optionsMenuSites);
    hostRegistry.supported.frame = sitesMatch(frameSites) && claimSites(frameSites);
    hostRegistry.supported.optionsSaved = sitesMatch(optionsSavedSites) && claimSites(optionsSavedSites);
    if (!hostRegistry.supported.options) registryLog(nullptr, "the options menu code is not as expected; options pages are unavailable");
    if (!hostRegistry.supported.frame) registryLog(nullptr, "the frame update code is not as expected; onFrame is unavailable");
    if (!hostRegistry.supported.optionsSaved) registryLog(nullptr, "the options save code is not as expected; onOptionsSaved is unavailable");
}

const wchar_t* WINAPI findGameText(uint32_t number) {
    if (!hostRegistry.supported.gameTexts) return nullptr;
    __try {
        return gameTextString(number);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
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
