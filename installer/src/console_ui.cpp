#include "console_ui.h"
#include "text_utils.h"
#include <windows.h>

namespace wots4::installer {
namespace {

const wchar_t selectedMarker[] = L"> ";
const wchar_t unselectedMarker[] = L"  ";

}  // namespace

Console::Console() : output_(GetStdHandle(STD_OUTPUT_HANDLE)), input_(GetStdHandle(STD_INPUT_HANDLE)) {}

// WriteConsoleW shows Japanese whatever the console code page is. When the output is not a
// console (redirected to a file), write UTF-8 instead.
void Console::write(const std::wstring& text) {
    DWORD written = 0;
    if (WriteConsoleW(output_, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr)) return;
    const std::string utf8 = toUtf8(text);
    WriteFile(output_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void Console::writeLine(const std::wstring& text) {
    write(text + L"\n");
}

// Prints count empty lines (scrolling if needed) and returns the row of the first one.
short Console::reserveRows(size_t count) {
    for (size_t index = 0; index < count; ++index) write(L"\n");
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(output_, &info)) return 0;
    return static_cast<short>(info.dwCursorPosition.Y - static_cast<short>(count));
}

void Console::drawRow(short row, const std::wstring& text) {
    SetConsoleCursorPosition(output_, {0, row});
    write(text);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(output_, &info)) return;
    // Clear what is left of a longer previous text. Japanese characters take two columns, so
    // clear from where the cursor ended up rather than from text.size().
    const DWORD rest = info.dwSize.X > info.dwCursorPosition.X ? info.dwSize.X - info.dwCursorPosition.X : 0;
    DWORD filled = 0;
    FillConsoleOutputCharacterW(output_, L' ', rest, info.dwCursorPosition, &filled);
}

void Console::moveBelow(short firstRow, size_t count) {
    SetConsoleCursorPosition(output_, {0, static_cast<short>(firstRow + count)});
}

void Console::setCursorVisible(bool visible) {
    CONSOLE_CURSOR_INFO info;
    if (!GetConsoleCursorInfo(output_, &info)) return;
    info.bVisible = visible;
    SetConsoleCursorInfo(output_, &info);
}

// Returns VK_RETURN when there is no console to read from, so that menus end with their defaults.
unsigned short Console::readKey() {
    INPUT_RECORD record;
    DWORD count = 0;
    while (ReadConsoleInputW(input_, &record, 1, &count)) {
        if (count == 1 && record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
            return record.Event.KeyEvent.wVirtualKeyCode;
        }
    }
    return VK_RETURN;
}

size_t Console::chooseOne(const std::vector<std::wstring>& options, size_t initial) {
    FlushConsoleInputBuffer(input_);
    setCursorVisible(false);
    const short firstRow = reserveRows(options.size());
    size_t current = initial;
    for (;;) {
        for (size_t index = 0; index < options.size(); ++index) {
            drawRow(static_cast<short>(firstRow + index), (index == current ? selectedMarker : unselectedMarker) + options[index]);
        }
        const unsigned short key = readKey();
        if (key == VK_RETURN) break;
        if (key == VK_UP && current > 0) --current;
        if (key == VK_DOWN && current + 1 < options.size()) ++current;
    }
    moveBelow(firstRow, options.size());
    setCursorVisible(true);
    return current;
}

std::vector<bool> Console::chooseMany(const std::vector<ToggleItem>& items) {
    std::vector<bool> checked;
    for (const ToggleItem& item : items) checked.push_back(item.checked);
    if (items.empty()) return checked;
    FlushConsoleInputBuffer(input_);
    setCursorVisible(false);
    const short firstRow = reserveRows(items.size());
    size_t current = 0;
    for (;;) {
        for (size_t index = 0; index < items.size(); ++index) {
            std::wstring row = index == current ? selectedMarker : unselectedMarker;
            row += checked[index] ? L"[x] " : L"[ ] ";
            row += items[index].label;
            if (!items[index].note.empty()) row += L"  (" + items[index].note + L")";
            drawRow(static_cast<short>(firstRow + index), row);
        }
        const unsigned short key = readKey();
        if (key == VK_RETURN) break;
        if (key == VK_SPACE) checked[current] = !checked[current];
        if (key == VK_UP && current > 0) --current;
        if (key == VK_DOWN && current + 1 < items.size()) ++current;
    }
    moveBelow(firstRow, items.size());
    setCursorVisible(true);
    return checked;
}

void Console::waitForEnter() {
    FlushConsoleInputBuffer(input_);
    while (readKey() != VK_RETURN) {
    }
}

}  // namespace wots4::installer
