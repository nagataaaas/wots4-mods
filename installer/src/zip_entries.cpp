#include "zip_entries.h"
#include "text_utils.h"
#include <optional>
#include <vector>

namespace wots4::installer {
namespace {

const std::string licensesFolder = "docs/licenses/";

struct EntryPath {
    std::string path;  // parts joined by "/"
    bool isFolder;
};

// Refuses names that could point outside the target folder: absolute paths, drive letters,
// "." and "..". Zips made by Windows PowerShell 5 use "\", so both separators are accepted.
std::optional<EntryPath> normalizeEntryName(std::string name) {
    for (char& character : name) {
        if (character == '\\') character = '/';
    }
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos) return std::nullopt;
    const bool isFolder = name.back() == '/';
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        const std::string part = name.substr(start, end - start);
        if (part == "." || part == "..") return std::nullopt;
        if (!part.empty()) parts.push_back(part);
        start = end + 1;
    }
    if (parts.empty()) return std::nullopt;
    std::string path = parts[0];
    for (size_t index = 1; index < parts.size(); ++index) path += "/" + parts[index];
    return EntryPath{path, isFolder};
}

}  // namespace

EntryTarget loaderEntryTarget(const std::string& entryName) {
    const auto entry = normalizeEntryName(entryName);
    if (!entry) return {EntryAction::reject, {}};
    if (entry->isFolder) return {EntryAction::skip, {}};
    const bool isLoader = equalsIgnoreCaseAscii(entry->path, "dinput8.dll");
    const bool isLicense = startsWithIgnoreCaseAscii(entry->path, licensesFolder) &&
        entry->path.find('/', licensesFolder.size()) == std::string::npos;
    if (isLoader || isLicense) return {EntryAction::extract, entry->path};
    return {EntryAction::skip, {}};
}

EntryTarget modEntryTarget(const std::string& entryName, const std::string& folderName) {
    const auto entry = normalizeEntryName(entryName);
    if (!entry) return {EntryAction::reject, {}};
    const std::string modFolder = "mods/" + folderName + "/";
    if (entry->isFolder) {
        // Folders on the way to the Mod's folder ("mods/", "mods/<name>/") and inside it are fine
        const std::string folder = entry->path + "/";
        const bool onTheWay = startsWithIgnoreCaseAscii(modFolder, folder);
        return {onTheWay || startsWithIgnoreCaseAscii(folder, modFolder) ? EntryAction::skip : EntryAction::reject, {}};
    }
    if (!startsWithIgnoreCaseAscii(entry->path, modFolder)) return {EntryAction::reject, {}};
    // Use the expected spelling of the folder so that the files land in the folder the ModLoader reads
    return {EntryAction::extract, modFolder + entry->path.substr(modFolder.size())};
}

}  // namespace wots4::installer
