// Which files of a release zip go into the game folder.
#pragma once
#include <string>

namespace wots4::installer {

enum class EntryAction {
    skip,     // not installed (a folder entry, or a file the game folder does not need)
    extract,  // installed at EntryTarget::path
    reject,   // the whole zip is refused
};

struct EntryTarget {
    EntryAction action;
    std::string path;  // relative to the game folder, separated by "/"
};

// ModLoader zip: dinput8.dll and docs/licenses/*. Other files (README.md) stay out of the game folder.
EntryTarget loaderEntryTarget(const std::string& entryName);

// Mod zip: only files under mods/<folderName>/, so that a zip cannot change another Mod.
EntryTarget modEntryTarget(const std::string& entryName, const std::string& folderName);

}  // namespace wots4::installer
