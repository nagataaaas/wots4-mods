// Checks what HostApi accepts for options pages and game events, and the pure
// menu helpers (rows of the pause menu, pages of the title screen options).
// No game code runs; the menus are plain memory.
#include "wots4_host_registry.h"
#include "wots4_game_menu.h"
#include <cstdio>

using namespace wots4;
using namespace wots4::game;

namespace {

unsigned tests, failures;

void check(const char* name, bool ok) {
    ++tests;
    if (!ok) ++failures;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

void WINAPI readNothing(void*, uint32_t*, uint32_t) {}
void WINAPI ignoreChange(void*, uint32_t, uint32_t) {}
void WINAPI ignoreEvent(void*) {}

const OptionsPageCallbacks callbacks{nullptr, readNothing, ignoreChange};
const wchar_t* const onOff[] = {L"On", L"Off"};
const wchar_t* const levels[] = {L"Easy", L"Normal", L"Hard", L"Any"};
const OptionItem toggle{optionToggle, L"Connect", onOff, 2};
const OptionItem level{optionChoice, L"Level", levels, 4};

HMODULE fakeModule(uintptr_t value) {
    return reinterpret_cast<HMODULE>(value);
}

void resetRegistry(bool open) {
    hostRegistry = {};
    hostRegistry.registrationOpen = open;
    hostRegistry.supported = {true, true, true};
}

void checkPageLimits() {
    const auto mod = fakeModule(0x10000);
    resetRegistry(true);
    const OptionItem items[] = {toggle, level};
    check("a valid page is accepted", addOptionsPage(mod, L"Online", items, 2, &callbacks));
    check("the page is copied", hostRegistry.pages.size() == 1 && hostRegistry.pages[0].title.text == L"Online" &&
        hostRegistry.pages[0].items.size() == 2 && hostRegistry.pages[0].items[1].choices[3].text == L"Any");
    const auto& copied = hostRegistry.pages[0].items[1];
    check("choice pointers point into the copy", copied.choicePointers.size() == 4 &&
        copied.choicePointers[2] == copied.choices[2].text.c_str());

    check("no items refused", !addOptionsPage(mod, L"Empty", items, 0, &callbacks));
    OptionItem nine[9] = {toggle, toggle, toggle, toggle, toggle, toggle, toggle, toggle, toggle};
    check("8 items accepted", addOptionsPage(mod, L"Eight", nine, 8, &callbacks));
    check("9 items refused", !addOptionsPage(mod, L"Nine", nine, 9, &callbacks));

    std::wstring longTitle(64, L'x');
    check("63-character title accepted", addOptionsPage(mod, longTitle.substr(0, 63).c_str(), items, 1, &callbacks));
    check("64-character title refused", !addOptionsPage(mod, longTitle.c_str(), items, 1, &callbacks));
    check("empty title refused", !addOptionsPage(mod, L"", items, 1, &callbacks));

    std::wstring longLabel(128, L'y');
    OptionItem labelled = toggle;
    labelled.label = longLabel.c_str();
    check("128-character label refused", !addOptionsPage(mod, L"Label", &labelled, 1, &callbacks));
    longLabel.resize(127);
    labelled.label = longLabel.c_str();
    check("127-character label accepted", addOptionsPage(mod, L"Label", &labelled, 1, &callbacks));

    OptionItem threeWayToggle{optionToggle, L"Toggle", levels, 3};
    check("toggle with 3 choices refused", !addOptionsPage(mod, L"T", &threeWayToggle, 1, &callbacks));
    const wchar_t* const eleven[] = {L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"10", L"11"};
    OptionItem tenChoices{optionChoice, L"Ten", eleven, 10};
    OptionItem elevenChoices{optionChoice, L"Eleven", eleven, 11};
    OptionItem oneChoice{optionChoice, L"One", eleven, 1};
    check("10 choices accepted", addOptionsPage(mod, L"C10", &tenChoices, 1, &callbacks));
    check("11 choices refused", !addOptionsPage(mod, L"C11", &elevenChoices, 1, &callbacks));
    check("1 choice refused", !addOptionsPage(mod, L"C1", &oneChoice, 1, &callbacks));
    OptionItem slider{static_cast<OptionKind>(1), L"Slider", onOff, 2};
    check("unknown item kind refused", !addOptionsPage(mod, L"K", &slider, 1, &callbacks));
    const wchar_t* const withNull[] = {L"On", nullptr};
    OptionItem nullChoice{optionToggle, L"Null", withNull, 2};
    check("null choice refused", !addOptionsPage(mod, L"N", &nullChoice, 1, &callbacks));
    OptionsPageCallbacks noRead{nullptr, nullptr, ignoreChange};
    check("missing read callback refused", !addOptionsPage(mod, L"R", items, 1, &noRead));

    bool allAccepted = true;
    while (hostRegistry.pages.size() < 100) allAccepted &= addOptionsPage(mod, L"Fill", items, 2, &callbacks) != FALSE;
    check("100 pages accepted; the number of pages has no limit", allAccepted);
    bool pointersFollow = true;
    for (const OptionsPageDefinition& page : hostRegistry.pages) {
        for (const OptionsItemDefinition& item : page.items) {
            for (size_t i = 0; i < item.choices.size(); ++i) pointersFollow &= item.choicePointers[i] == item.choices[i].text.c_str();
        }
    }
    check("choice pointers follow every page the vector moved", pointersFollow);
}

void checkGameTexts() {
    const auto mod = fakeModule(0x10000);
    resetRegistry(true);
    const wchar_t* const gameChoices[] = {gameText(211), gameText(212)};
    const OptionItem item{optionToggle, gameText(502), gameChoices, 2};
    check("game text numbers are accepted for every text", addOptionsPage(mod, gameText(503), &item, 1, &callbacks));
    const OptionsPageDefinition& page = hostRegistry.pages[0];
    check("game text numbers are kept for later", page.title.gameTextNumber == 503 && page.title.text.empty() &&
        page.items[0].label.gameTextNumber == 502 && page.items[0].choices[1].gameTextNumber == 212);
    check("a string keeps number 0", copyText(L"Text").gameTextNumber == 0);
    check("number 0 is no text", !isGameText(gameText(0)) && !isValidText(gameText(0), maxOptionTextChars));
    check("the largest number is a game text", isGameText(gameText(maxGameTextNumber)));
}

void checkRegistrationWindow() {
    const auto mod = fakeModule(0x10000);
    const OptionItem items[] = {toggle};
    resetRegistry(false);
    check("page refused outside initialization", !addOptionsPage(mod, L"Late", items, 1, &callbacks));
    check("onFrame refused outside initialization", !onFrame(mod, ignoreEvent, nullptr));
    resetRegistry(true);
    hostRegistry.supported = {};
    check("page refused on an unsupported exe", !addOptionsPage(mod, L"Old", items, 1, &callbacks));
    check("onOptionsSaved refused on an unsupported exe", !onOptionsSaved(mod, ignoreEvent, nullptr));
}

void checkGameEvents() {
    const auto first = fakeModule(0x10000), second = fakeModule(0x20000);
    resetRegistry(true);
    check("onFrame accepted", onFrame(first, ignoreEvent, nullptr));
    check("null callback refused", !onFrame(first, nullptr, nullptr));
    for (uint32_t i = 1; i < maxGameEventHandlers; ++i) onFrame(second, ignoreEvent, nullptr);
    check("33rd frame callback refused", !onFrame(first, ignoreEvent, nullptr));
    check("onOptionsSaved accepted", onOptionsSaved(first, ignoreEvent, nullptr));
}

void checkRefusedModIsForgotten() {
    const auto kept = fakeModule(0x10000), refused = fakeModule(0x20000);
    resetRegistry(true);
    const OptionItem items[] = {level};
    addOptionsPage(refused, L"Gone", items, 1, &callbacks);
    addOptionsPage(kept, L"Kept", items, 1, &callbacks);
    onFrame(refused, ignoreEvent, nullptr);
    onOptionsSaved(kept, ignoreEvent, nullptr);
    removeRegistrations(refused);
    const auto& pages = hostRegistry.pages;
    check("refused Mod's page removed", pages.size() == 1 && pages[0].title.text == L"Kept");
    check("choice pointers follow the moved page",
        pages.size() == 1 && pages[0].items[0].choicePointers[0] == pages[0].items[0].choices[0].text.c_str());
    check("refused Mod's callbacks removed, others kept",
        hostRegistry.frameHandlers.empty() && hostRegistry.optionsSavedHandlers.size() == 1);
}

// 25 rows linked like the builder links the options branch: row 17 with children 18..22.
void makeGameMenu(GameMenuRow* rows) {
    memset(rows, 0, gameMenuRowCount * sizeof(GameMenuRow));
    for (uint32_t i = 0; i < gameMenuRowCount; ++i) {
        rows[i].text = 1000 + i;
        rows[i].state = -1;
    }
    rows[0].next = reinterpret_cast<uintptr_t>(&rows[1]);
    rows[1].previous = reinterpret_cast<uintptr_t>(&rows[0]);
    rows[optionsMenuRow].text = optionsMenuText;
    rows[optionsMenuRow].first = reinterpret_cast<uintptr_t>(&rows[18]);
    for (uint32_t i = 18; i <= 22; ++i) {
        rows[i].parent = reinterpret_cast<uintptr_t>(&rows[optionsMenuRow]);
        rows[i].child = 0x5000 + i;  // Pages are not rows; they must not be relinked
        if (i > 18) rows[i].previous = reinterpret_cast<uintptr_t>(&rows[i - 1]);
        if (i < 22) rows[i].next = reinterpret_cast<uintptr_t>(&rows[i + 1]);
    }
    rows[18].text = firstOptionsPageText;
    rows[22].text = lastOptionsPageText;
}

void checkPauseMenuRows() {
    constexpr uint32_t manyPages = 100;
    GameMenuRow source[gameMenuRowCount];
    std::vector<GameMenuRow> targetRows(gameMenuRowCount + manyPages);
    GameMenuRow* target = targetRows.data();
    makeGameMenu(source);
    void* pages[manyPages];
    for (uint32_t i = 0; i < manyPages; ++i) pages[i] = reinterpret_cast<void*>(0x9000 + i);

    check("the builder's table is recognized", isUnchangedGameMenu(source, gameMenuRowCount));
    check("3 rows appended", appendOptionsRows(source, gameMenuRowCount, target, pages, 3));
    const auto at = [&](uint32_t i) { return reinterpret_cast<uintptr_t>(&target[i]); };
    check("links moved to the new table", target[0].next == at(1) && target[optionsMenuRow].first == at(18) &&
        target[20].parent == at(optionsMenuRow) && target[21].previous == at(20));
    check("child pages kept", target[19].child == 0x5013);
    check("new rows follow the last options page", target[22].next == at(25) && target[25].next == at(26) &&
        target[26].next == at(27) && target[27].next == 0);
    check("new rows link back", target[25].previous == at(22) && target[27].previous == at(26));
    check("new rows belong to the options row", target[25].parent == at(optionsMenuRow) &&
        target[27].parent == at(optionsMenuRow));
    check("new rows carry page and title number", target[25].child == 0x9000 && target[27].child == 0x9002 &&
        target[25].text == modTitleTextBase && target[27].text == modTitleTextBase + 2 && target[26].state == -1);
    check("the new table is recognized as changed", !isUnchangedGameMenu(target, gameMenuRowCount + 3));

    check("100 rows appended", appendOptionsRows(source, gameMenuRowCount, target, pages, manyPages));
    const uint32_t lastRow = gameMenuRowCount + manyPages - 1;
    check("the 100th row ends the options branch with its page and number",
        target[lastRow].next == 0 && target[lastRow].previous == at(lastRow - 1) &&
        target[lastRow].child == 0x9000 + manyPages - 1 && target[lastRow].text == modTitleTextBase + manyPages - 1);
    check("zero rows refused", !appendOptionsRows(source, gameMenuRowCount, target, pages, 0));

    GameMenuRow changed[gameMenuRowCount];
    makeGameMenu(changed);
    changed[22].next = reinterpret_cast<uintptr_t>(&changed[0]);
    check("an options branch that does not end at text 104 is refused",
        !appendOptionsRows(changed, gameMenuRowCount, target, pages, 1));
    makeGameMenu(changed);
    changed[3].next = 0x1234;
    check("a link outside the table is refused", !appendOptionsRows(changed, gameMenuRowCount, target, pages, 1));
    check("a table with another row count is refused", !appendOptionsRows(source, 24, target, pages, 1));
    check("mod title numbers", isModTitleText(modTitleTextBase) && isModTitleText(UINT32_MAX) &&
        !isModTitleText(modTitleTextBase - 1) && !isModTitleText(104));
}

void checkTitlePages() {
    uintptr_t parentMemory[0x84 / 4] = {};
    const auto parent = reinterpret_cast<uintptr_t>(parentMemory);
    for (uint32_t i = 0; i < gameOptionsPageCount; ++i) parentMemory[titleParentPagesOffset / 4 + i] = 0x7000 + i;

    TitlePageTable table{};
    check("a parent without Mod pages has 5 pages", table.pageCount(parent) == 5);
    check("game pages come from the parent", table.pageAt(parent, 0) == 0x7000 && table.pageAt(parent, 4) == 0x7004);
    check("no page after the game's", table.pageAt(parent, 5) == 0);

    TitlePages* entry = table.add(parent);
    check("a parent is added once", entry && !table.add(parent));
    if (!entry) return;
    entry->pages = {0x8000, 0x8001};
    check("count includes Mod pages", table.pageCount(parent) == 7);
    check("Mod pages come from the table", table.pageAt(parent, 5) == 0x8000 && table.pageAt(parent, 6) == 0x8001);
    check("past the Mod pages is null", table.pageAt(parent, 7) == 0);
    for (uintptr_t page = 0x8002; page < 0x8000 + 100; ++page) entry->pages.push_back(page);
    check("100 Mod pages: count and the last page", table.pageCount(parent) == 105 &&
        table.pageAt(parent, 104) == 0x8063 && table.pageAt(parent, 105) == 0);

    uintptr_t others[maxTitleParents][0x84 / 4] = {};
    for (uint32_t i = 1; i < maxTitleParents; ++i) table.add(reinterpret_cast<uintptr_t>(others[i]));
    check("the table holds 4 parents", !table.add(reinterpret_cast<uintptr_t>(others[0])));
    table.remove(parent);
    check("a removed parent has 5 pages again", table.pageCount(parent) == 5 && table.pageAt(parent, 5) == 0);
    check("a freed slot is reused", table.add(reinterpret_cast<uintptr_t>(others[0])) != nullptr);
}

}  // namespace

int main() {
    checkPageLimits();
    checkGameTexts();
    checkRegistrationWindow();
    checkGameEvents();
    checkRefusedModIsForgotten();
    checkPauseMenuRows();
    checkTitlePages();
    printf("RESULT %u passed, %u failed\n", tests - failures, failures);
    return failures ? 1 : 0;
}
