#include "text_utils.h"
#include <windows.h>
#include <vector>

namespace wots4::installer {

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring fromUtf8(const std::string& text) {
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), length, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), length, result.data(), size);
    return result;
}

std::string toLowerAscii(std::string text) {
    for (char& character : text) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
    return text;
}

bool equalsIgnoreCaseAscii(const std::string& left, const std::string& right) {
    return toLowerAscii(left) == toLowerAscii(right);
}

bool startsWithIgnoreCaseAscii(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && equalsIgnoreCaseAscii(text.substr(0, prefix.size()), prefix);
}

std::string trimBlanks(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

std::wstring formatText(const std::wstring& pattern, std::initializer_list<std::wstring> arguments) {
    const std::vector<std::wstring> values(arguments);
    std::wstring result;
    for (size_t index = 0; index < pattern.size(); ++index) {
        const bool isPlaceholder = pattern[index] == L'{' && index + 2 < pattern.size() &&
            pattern[index + 1] >= L'0' && pattern[index + 1] <= L'9' && pattern[index + 2] == L'}';
        if (!isPlaceholder) {
            result += pattern[index];
            continue;
        }
        const size_t argument = pattern[index + 1] - L'0';
        if (argument < values.size()) result += values[argument];
        index += 2;
    }
    return result;
}

}  // namespace wots4::installer
