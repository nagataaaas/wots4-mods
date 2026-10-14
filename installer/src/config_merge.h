// Merging a Mod's config.ini when the Mod is updated.
#pragma once
#include <string>

namespace wots4::installer {

// Returns newText with the user's values from oldText. Comments, the order of sections and keys,
// line endings and the BOM come from newText. A key keeps its old value when the same section
// and key exist in oldText (names compared without case, as GetPrivateProfileString does).
// Keys only in oldText were dropped by the new version and are left out. Values are not checked.
std::string mergeConfigIni(const std::string& newText, const std::string& oldText);

}  // namespace wots4::installer
