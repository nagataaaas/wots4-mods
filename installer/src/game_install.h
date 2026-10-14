// Writing into the game folder.
#pragma once
#include "failure.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wots4::installer {

enum class LoaderFileState {
    missing,  // no dinput8.dll
    ours,     // this ModLoader (ProductName in its version information)
    foreign,  // another tool's dinput8.dll, or one without version information
};

LoaderFileState inspectLoaderFile(const std::filesystem::path& dinput8Path);

// The first of <path>.bak, <path>.bak2, <path>.bak3, ... that does not exist yet.
std::filesystem::path backupPathFor(const std::filesystem::path& path);

std::optional<Failure> renameFile(const std::filesystem::path& from, const std::filesystem::path& to);

// Copies stagingFolder/<path> to gameFolder/<path> for each path (relative, "/" separated).
// mods/<name>/config.ini keeps the user's values when the file already exists (config_merge.h).
// Files in the game folder that are not in paths are left alone.
std::optional<Failure> installStagedFiles(const std::filesystem::path& stagingFolder,
    const std::vector<std::string>& paths, const std::filesystem::path& gameFolder);

// True while WayOfTheSamurai4.exe at exactly this path is running.
bool isGameRunning(const std::filesystem::path& gameExe);

}  // namespace wots4::installer
