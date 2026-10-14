#include "config_merge.h"
#include "text_utils.h"
#include <map>
#include <utility>
#include <vector>

namespace wots4::installer {
namespace {

const std::string utf8Bom = "\xEF\xBB\xBF";

struct IniLine {
    std::string text;
    std::string ending;  // "\r\n", "\n" or empty for a last line without one
};

// (section, key) in lower case -> value
using IniValues = std::map<std::pair<std::string, std::string>, std::string>;

std::vector<IniLine> splitLines(const std::string& text) {
    std::vector<IniLine> lines;
    size_t start = 0;
    while (start < text.size()) {
        const size_t newline = text.find('\n', start);
        if (newline == std::string::npos) {
            lines.push_back({text.substr(start), {}});
            break;
        }
        const bool hasCarriageReturn = newline > start && text[newline - 1] == '\r';
        const size_t textEnd = hasCarriageReturn ? newline - 1 : newline;
        lines.push_back({text.substr(start, textEnd - start), hasCarriageReturn ? "\r\n" : "\n"});
        start = newline + 1;
    }
    return lines;
}

std::string withoutBom(const std::string& text) {
    return text.compare(0, utf8Bom.size(), utf8Bom) == 0 ? text.substr(utf8Bom.size()) : text;
}

// "[name]" -> name. Returns false for any other line.
bool readSectionName(const std::string& trimmedLine, std::string& name) {
    if (trimmedLine.size() < 2 || trimmedLine.front() != '[') return false;
    const size_t close = trimmedLine.find(']');
    if (close == std::string::npos) return false;
    name = toLowerAscii(trimBlanks(trimmedLine.substr(1, close - 1)));
    return true;
}

bool isCommentOrBlank(const std::string& trimmedLine) {
    return trimmedLine.empty() || trimmedLine.front() == ';' || trimmedLine.front() == '#';
}

IniValues readValues(const std::string& text) {
    IniValues values;
    std::string section;
    for (const IniLine& line : splitLines(withoutBom(text))) {
        const std::string trimmed = trimBlanks(line.text);
        if (readSectionName(trimmed, section) || isCommentOrBlank(trimmed)) continue;
        const size_t equals = line.text.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = toLowerAscii(trimBlanks(line.text.substr(0, equals)));
        // emplace keeps the first one, the value GetPrivateProfileString would read
        values.emplace(std::make_pair(section, key), trimBlanks(line.text.substr(equals + 1)));
    }
    return values;
}

}  // namespace

std::string mergeConfigIni(const std::string& newText, const std::string& oldText) {
    const IniValues oldValues = readValues(oldText);
    const bool hasBom = newText.compare(0, utf8Bom.size(), utf8Bom) == 0;
    std::string merged = hasBom ? utf8Bom : std::string();
    std::string section;
    for (const IniLine& line : splitLines(withoutBom(newText))) {
        const std::string trimmed = trimBlanks(line.text);
        const size_t equals = line.text.find('=');
        if (readSectionName(trimmed, section) || isCommentOrBlank(trimmed) || equals == std::string::npos) {
            merged += line.text + line.ending;
            continue;
        }
        const std::string key = toLowerAscii(trimBlanks(line.text.substr(0, equals)));
        const auto oldValue = oldValues.find(std::make_pair(section, key));
        if (oldValue == oldValues.end()) {
            merged += line.text + line.ending;
            continue;
        }
        // Keep the new line's spacing around "=" and replace only the value
        const size_t valueStart = line.text.find_first_not_of(" \t", equals + 1);
        const size_t keepLength = valueStart == std::string::npos ? line.text.size() : valueStart;
        merged += line.text.substr(0, keepLength) + oldValue->second + line.ending;
    }
    return merged;
}

}  // namespace wots4::installer
