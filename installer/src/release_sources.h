// Where the installer finds the published Mods and their releases on GitHub.
// Release zips are named by the Release workflows: wots4-modloader-<tag>.zip and
// wots4-mod-<name with "_" replaced by "-">-<tag>.zip.
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace wots4::installer {

struct GitHubRepository {
    std::string owner;
    std::string name;
};

// A Mod published as a submodule mods/<folderName> of the ModLoader repository.
struct PublishedMod {
    std::string folderName;
    GitHubRepository repository;
};

extern const GitHubRepository loaderRepository;

// .gitmodules on the default branch of the ModLoader repository. Read at run time so that
// a newly published Mod shows up without a new installer.
std::string publishedModListUrl();

// Mods under mods/ in a .gitmodules file. Entries that are not a GitHub repository under
// mods/ or whose names contain unexpected characters are left out.
std::vector<PublishedMod> parseGitmodules(const std::string& text);

// https://github.com/<owner>/<name>/releases/latest, which redirects to the newest release's tag page.
std::string latestReleaseUrl(const GitHubRepository& repository);

// The tag in a redirect target such as .../releases/tag/v2026.10.14.3. Empty when the target is
// not a tag page (a repository without releases redirects to .../releases).
std::optional<std::string> tagFromReleaseUrl(const std::string& url);

std::string loaderZipName(const std::string& tag);
std::string modZipName(const std::string& folderName, const std::string& tag);
std::string releaseAssetUrl(const GitHubRepository& repository, const std::string& tag, const std::string& assetName);

}  // namespace wots4::installer
