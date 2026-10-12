#pragma once
// Data structures of the game's two options menus, and the pure parts of adding
// Mod pages to them. No game code is called here, so tests can use plain memory.
#include <wots4/mod_api.h>
#include <cstring>

namespace wots4::game {

// ---------------------------------------------------------------- pause menu

// One row of the pause menu tree. The builder 0x676cb0 allocates 25 of them
// (0x676cf3) and links them; row 17 is "各種設定" (text 101) and its children
// are the five options pages (rows 18..22).
struct GameMenuRow {
    uintptr_t next, previous, parent, first, selected, child;
    uint32_t text;  // GameTextUI number, or a Mod title number (modTitleTextBase + page)
    int32_t state;  // -1 for every row the builder makes
};
static_assert(sizeof(GameMenuRow) == 32);

constexpr uint32_t gameMenuRowCount = 25;
constexpr uint32_t optionsMenuRow = 17;
constexpr uint32_t optionsMenuText = 101;      // "各種設定"
constexpr uint32_t firstOptionsPageText = 102;  // "各種設定" page, the first child of row 17
constexpr uint32_t lastOptionsPageText = 104;   // "表示設定" page, the last child of row 17
// Far above the number of GameTextUI entries, so a game text never uses it.
constexpr uint32_t modTitleTextBase = 0x7fff0000;

inline bool isModTitleText(uint32_t text) {
    return text >= modTitleTextBase && text < modTitleTextBase + maxOptionsPages;
}

// True if every link of the rows points to the start of a row in the table.
inline bool rowLinksAreInside(const GameMenuRow* rows, uint32_t count) {
    const auto begin = reinterpret_cast<uintptr_t>(rows);
    const auto end = begin + count * sizeof(GameMenuRow);
    for (uint32_t i = 0; i < count; ++i) {
        const uintptr_t links[] = {rows[i].next, rows[i].previous, rows[i].parent, rows[i].first, rows[i].selected};
        for (uintptr_t link : links) {
            if (link && (link < begin || link >= end || (link - begin) % sizeof(GameMenuRow))) return false;
        }
    }
    return true;
}

// The table the builder made, before anything else changed it.
inline bool isUnchangedGameMenu(const GameMenuRow* rows, uint32_t count) {
    if (!rows || count != gameMenuRowCount) return false;
    const GameMenuRow& options = rows[optionsMenuRow];
    if (options.text != optionsMenuText) return false;
    if (options.first != reinterpret_cast<uintptr_t>(&rows[optionsMenuRow + 1])) return false;
    if (rows[optionsMenuRow + 1].text != firstOptionsPageText) return false;
    return rowLinksAreInside(rows, count);
}

// Index of the last child of the options row, or -1.
inline int lastOptionsPageRow(const GameMenuRow* rows, uint32_t count) {
    auto row = reinterpret_cast<const GameMenuRow*>(rows[optionsMenuRow].first);
    for (uint32_t steps = 0; row && steps < count; ++steps) {
        if (!row->next) return row->text == lastOptionsPageText ? static_cast<int>(row - rows) : -1;
        row = reinterpret_cast<const GameMenuRow*>(row->next);
    }
    return -1;
}

// Copies the rows into target (room for count + pageCount rows), moves every
// link to target, and appends one row per page after the last options page.
// pages[i] becomes the child of the new row i. False leaves target unspecified.
inline bool appendOptionsRows(const GameMenuRow* source, uint32_t count, GameMenuRow* target,
    void* const* pages, uint32_t pageCount) {
    if (!target || target == source || !pages || pageCount == 0 || pageCount > maxOptionsPages) return false;
    if (!isUnchangedGameMenu(source, count)) return false;
    const int lastRow = lastOptionsPageRow(source, count);
    if (lastRow < 0) return false;

    const auto sourceBegin = reinterpret_cast<uintptr_t>(source);
    const auto targetBegin = reinterpret_cast<uintptr_t>(target);
    auto moved = [&](uintptr_t link) { return link ? targetBegin + (link - sourceBegin) : 0; };
    memcpy(target, source, count * sizeof(GameMenuRow));
    for (uint32_t i = 0; i < count; ++i) {
        GameMenuRow& row = target[i];
        row.next = moved(row.next);
        row.previous = moved(row.previous);
        row.parent = moved(row.parent);
        row.first = moved(row.first);
        row.selected = moved(row.selected);
    }

    const auto optionsRow = reinterpret_cast<uintptr_t>(&target[optionsMenuRow]);
    GameMenuRow* previous = &target[lastRow];
    for (uint32_t i = 0; i < pageCount; ++i) {
        GameMenuRow& row = target[count + i];
        row = {0, reinterpret_cast<uintptr_t>(previous), optionsRow, 0, 0,
            reinterpret_cast<uintptr_t>(pages[i]), modTitleTextBase + i, -1};
        previous->next = reinterpret_cast<uintptr_t>(&row);
        previous = &row;
    }
    return true;
}

// ---------------------------------------------------------------- title screen options

// The title screen's options parent (vtable 0x99bec0, 0x84 bytes) keeps five
// page pointers at +0x64. Pages after them live in this table, keyed by parent.
constexpr uint32_t titleParentPagesOffset = 0x64;
constexpr uint32_t gameOptionsPageCount = 5;
constexpr uint32_t maxTitleParents = 4;  // The game makes one at a time; a few spare slots

struct TitlePages {
    uintptr_t parent;  // 0 when the slot is free
    uint32_t count;
    uintptr_t pages[maxOptionsPages];
};

struct TitlePageTable {
    TitlePages parents[maxTitleParents];

    const TitlePages* find(uintptr_t parent) const {
        for (const TitlePages& entry : parents) {
            if (entry.parent == parent) return &entry;
        }
        return nullptr;
    }

    TitlePages* add(uintptr_t parent) {
        if (!parent || find(parent)) return nullptr;
        for (TitlePages& entry : parents) {
            if (!entry.parent) {
                entry = {parent, 0, {}};
                return &entry;
            }
        }
        return nullptr;
    }

    void remove(uintptr_t parent) {
        for (TitlePages& entry : parents) {
            if (entry.parent == parent) entry = {};
        }
    }

    // Called on every frame the options menu is open, so it stays a few loads.
    uint32_t pageCount(uintptr_t parent) const {
        const TitlePages* entry = find(parent);
        return gameOptionsPageCount + (entry ? entry->count : 0);
    }

    uintptr_t pageAt(uintptr_t parent, uint32_t index) const {
        if (index < gameOptionsPageCount)
            return reinterpret_cast<const uintptr_t*>(parent + titleParentPagesOffset)[index];
        const TitlePages* entry = find(parent);
        const uint32_t extra = index - gameOptionsPageCount;
        return entry && extra < entry->count ? entry->pages[extra] : 0;
    }
};

}  // namespace wots4::game
