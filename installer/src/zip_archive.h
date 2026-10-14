// Extracting a release zip into a staging folder.
#pragma once
#include "failure.h"
#include "zip_entries.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wots4::installer {

using EntryTargetFunction = std::function<EntryTarget(const std::string& entryName)>;

// Extracts the entries that targetOf accepts into destination/<EntryTarget::path> and appends
// those paths to extractedPaths. Every entry is checked before any file is written, so a zip
// with a rejected entry writes nothing.
std::optional<Failure> extractZip(const std::filesystem::path& zipFile, const EntryTargetFunction& targetOf,
    const std::filesystem::path& destination, std::vector<std::string>& extractedPaths);

}  // namespace wots4::installer
