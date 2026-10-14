// File operations that report a Failure with the Win32 error.
#pragma once
#include "failure.h"
#include <filesystem>
#include <optional>
#include <string>

namespace wots4::installer {

std::optional<Failure> readWholeFile(const std::filesystem::path& path, std::string& contents);

// Creates the parent folders, then writes contents to path (replacing it).
std::optional<Failure> writeWholeFile(const std::filesystem::path& path, const std::string& contents);

// Writes contents next to path first and then moves it over path, so that a failure part way
// leaves the old file whole.
std::optional<Failure> replaceFileContents(const std::filesystem::path& path, const std::string& contents);

}  // namespace wots4::installer
