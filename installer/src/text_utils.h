// Small string helpers shared by the installer.
#pragma once
#include <initializer_list>
#include <string>

namespace wots4::installer {

std::string toUtf8(const std::wstring& text);
std::wstring fromUtf8(const std::string& text);

// ASCII only: the texts compared here (INI keys, file names in zips, URLs) are ASCII.
std::string toLowerAscii(std::string text);
bool equalsIgnoreCaseAscii(const std::string& left, const std::string& right);
bool startsWithIgnoreCaseAscii(const std::string& text, const std::string& prefix);

// Removes spaces and tabs at both ends.
std::string trimBlanks(const std::string& text);

// Replaces {0}, {1}, ... in pattern with the arguments.
std::wstring formatText(const std::wstring& pattern, std::initializer_list<std::wstring> arguments);

}  // namespace wots4::installer
