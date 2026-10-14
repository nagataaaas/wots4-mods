// Finding WayOfTheSamurai4.exe in the user's Steam libraries.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace wots4::installer {

extern const wchar_t gameExeName[];

// Library folders in libraryfolders.vdf (UTF-8 text) whose "apps" list has appId.
std::vector<std::string> librariesWithApp(const std::string& libraryFoldersVdf, const std::string& appId);

// Existing WayOfTheSamurai4.exe files, best guess first: the Steam libraries that have the game
// (from the registry and libraryfolders.vdf), then the default Steam folder.
std::vector<std::filesystem::path> findGameExeCandidates();

}  // namespace wots4::installer
