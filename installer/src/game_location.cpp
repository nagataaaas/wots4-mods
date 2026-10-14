#include "game_location.h"
#include "text_utils.h"
#include <windows.h>
#include <fstream>
#include <iterator>
#include <optional>

namespace wots4::installer {

const wchar_t gameExeName[] = L"WayOfTheSamurai4.exe";

namespace {

const char gameAppId[] = "312780";  // Steam AppID of Way of the Samurai 4
const wchar_t gameFolderInLibrary[] = L"steamapps\\common\\Way of the Samurai 4";
const wchar_t defaultSteamFolder[] = L"C:\\Program Files (x86)\\Steam";

// Tokens of Valve's KeyValues text: quoted strings, "{" and "}".
class VdfTokenizer {
public:
    explicit VdfTokenizer(const std::string& text) : text_(text) {}

    // Returns false at the end of the text.
    bool next(std::string& token, bool& isString) {
        skipBlanksAndComments();
        if (position_ >= text_.size()) return false;
        const char first = text_[position_++];
        isString = first == '"';
        if (!isString) {
            token.assign(1, first);
            return true;
        }
        token.clear();
        while (position_ < text_.size() && text_[position_] != '"') {
            char character = text_[position_++];
            if (character == '\\' && position_ < text_.size()) character = text_[position_++];
            token += character;
        }
        ++position_;  // closing quote
        return true;
    }

private:
    void skipBlanksAndComments() {
        while (position_ < text_.size()) {
            const char character = text_[position_];
            if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
                ++position_;
            } else if (text_.compare(position_, 2, "//") == 0) {
                const size_t lineEnd = text_.find('\n', position_);
                position_ = lineEnd == std::string::npos ? text_.size() : lineEnd;
            } else {
                return;
            }
        }
    }

    const std::string& text_;
    size_t position_ = 0;
};

std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::optional<std::wstring> steamFolderFromRegistry() {
    wchar_t path[MAX_PATH];
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, path, &size) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    std::wstring folder = path;
    for (wchar_t& character : folder) {
        if (character == L'/') character = L'\\';  // Steam writes forward slashes
    }
    return folder;
}

void addIfExists(const std::filesystem::path& exe, std::vector<std::filesystem::path>& candidates) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(exe, error)) return;
    for (const auto& known : candidates) {
        if (std::filesystem::equivalent(known, exe, error)) return;
    }
    candidates.push_back(exe);
}

std::filesystem::path gameExeInLibrary(const std::filesystem::path& library) {
    return library / gameFolderInLibrary / gameExeName;
}

}  // namespace

std::vector<std::string> librariesWithApp(const std::string& libraryFoldersVdf, const std::string& appId) {
    // Layout: "libraryfolders" { "<n>" { "path" "<folder>" ... "apps" { "<appid>" "<size>" ... } } ... }
    constexpr size_t libraryDepth = 2;  // inside "libraryfolders" { "<n>" {
    std::vector<std::string> libraries;
    std::vector<std::string> openKeys;
    std::string pendingKey, libraryPath, token;
    bool hasPendingKey = false, libraryHasApp = false, isString = false;
    VdfTokenizer tokenizer(libraryFoldersVdf);
    while (tokenizer.next(token, isString)) {
        if (!isString && token == "{") {
            openKeys.push_back(hasPendingKey ? toLowerAscii(pendingKey) : std::string());
            hasPendingKey = false;
            if (openKeys.size() == libraryDepth) {
                libraryPath.clear();
                libraryHasApp = false;
            }
        } else if (!isString && token == "}") {
            if (openKeys.size() == libraryDepth && libraryHasApp && !libraryPath.empty()) libraries.push_back(libraryPath);
            if (!openKeys.empty()) openKeys.pop_back();
            hasPendingKey = false;
        } else if (!hasPendingKey) {
            pendingKey = token;
            hasPendingKey = true;
        } else {
            const bool inLibrary = openKeys.size() == libraryDepth;
            const bool inApps = openKeys.size() == libraryDepth + 1 && openKeys.back() == "apps";
            if (inLibrary && equalsIgnoreCaseAscii(pendingKey, "path")) libraryPath = token;
            if (inApps && pendingKey == appId) libraryHasApp = true;
            hasPendingKey = false;
        }
    }
    return libraries;
}

std::vector<std::filesystem::path> findGameExeCandidates() {
    std::vector<std::filesystem::path> candidates;
    const std::wstring steamFolder = steamFolderFromRegistry().value_or(defaultSteamFolder);
    const auto vdf = readFile(std::filesystem::path(steamFolder) / L"steamapps\\libraryfolders.vdf");
    if (vdf) {
        for (const std::string& library : librariesWithApp(*vdf, gameAppId)) {
            addIfExists(gameExeInLibrary(fromUtf8(library)), candidates);
        }
    }
    addIfExists(gameExeInLibrary(defaultSteamFolder), candidates);
    return candidates;
}

}  // namespace wots4::installer
