// Text and key menus in the console window.
#pragma once
#include <string>
#include <vector>

typedef void* HANDLE;

namespace wots4::installer {

struct ToggleItem {
    std::wstring label;
    std::wstring note;  // shown after the label, such as "installed"
    bool checked;
};

class Console {
public:
    Console();

    void writeLine(const std::wstring& text = {});

    // Up and down move, Enter chooses. Returns the index of the chosen option.
    size_t chooseOne(const std::vector<std::wstring>& options, size_t initial);

    // Up and down move, Space toggles, Enter confirms. Returns the checked state of each item.
    std::vector<bool> chooseMany(const std::vector<ToggleItem>& items);

    void waitForEnter();

private:
    void write(const std::wstring& text);
    short reserveRows(size_t count);
    void drawRow(short row, const std::wstring& text);
    void moveBelow(short firstRow, size_t count);
    void setCursorVisible(bool visible);
    unsigned short readKey();

    HANDLE output_;
    HANDLE input_;
};

}  // namespace wots4::installer
