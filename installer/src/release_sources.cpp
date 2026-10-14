#include "release_sources.h"
#include "text_utils.h"
#include <algorithm>

namespace wots4::installer {

const GitHubRepository loaderRepository{"nagataaaas", "wots4-mods"};

namespace {

const std::string gitHubHttpsPrefix = "https://github.com/";
const std::string gitHubSshPrefix = "git@github.com:";
const std::string modsFolderPrefix = "mods/";
const std::string releaseTagMarker = "/releases/tag/";

// Names end up in file paths and URLs, so only plain characters are accepted.
bool isPlainName(const std::string& name, bool allowDot) {
    if (name.empty() || name == "." || name == "..") return false;
    return std::all_of(name.begin(), name.end(), [allowDot](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' || character == '-' ||
            (allowDot && character == '.');
    });
}

std::optional<GitHubRepository> parseGitHubUrl(std::string url) {
    if (startsWithIgnoreCaseAscii(url, gitHubHttpsPrefix)) {
        url = url.substr(gitHubHttpsPrefix.size());
    } else if (startsWithIgnoreCaseAscii(url, gitHubSshPrefix)) {
        url = url.substr(gitHubSshPrefix.size());
    } else {
        return std::nullopt;
    }
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.size() > 4 && equalsIgnoreCaseAscii(url.substr(url.size() - 4), ".git")) url.resize(url.size() - 4);
    const size_t slash = url.find('/');
    if (slash == std::string::npos) return std::nullopt;
    GitHubRepository repository{url.substr(0, slash), url.substr(slash + 1)};
    if (!isPlainName(repository.owner, false) || !isPlainName(repository.name, true)) return std::nullopt;
    return repository;
}

struct SubmoduleEntry {
    std::string path;
    std::string url;
};

void addIfPublishedMod(const SubmoduleEntry& entry, std::vector<PublishedMod>& mods) {
    if (!startsWithIgnoreCaseAscii(entry.path, modsFolderPrefix)) return;
    const std::string folderName = entry.path.substr(modsFolderPrefix.size());
    if (!isPlainName(folderName, false)) return;
    const auto repository = parseGitHubUrl(entry.url);
    if (!repository) return;
    mods.push_back({folderName, *repository});
}

}  // namespace

std::string publishedModListUrl() {
    return "https://raw.githubusercontent.com/" + loaderRepository.owner + "/" + loaderRepository.name +
        "/master/.gitmodules";
}

std::vector<PublishedMod> parseGitmodules(const std::string& text) {
    std::vector<PublishedMod> mods;
    std::optional<SubmoduleEntry> entry;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lineStart = lineEnd + 1;

        const std::string trimmed = trimBlanks(line);
        if (!trimmed.empty() && trimmed.front() == '[') {
            if (entry) addIfPublishedMod(*entry, mods);
            entry = startsWithIgnoreCaseAscii(trimmed, "[submodule") ? std::optional<SubmoduleEntry>(SubmoduleEntry{})
                                                                    : std::nullopt;
            continue;
        }
        const size_t equals = trimmed.find('=');
        if (!entry || equals == std::string::npos) continue;
        const std::string key = toLowerAscii(trimBlanks(trimmed.substr(0, equals)));
        const std::string value = trimBlanks(trimmed.substr(equals + 1));
        if (key == "path") entry->path = value;
        if (key == "url") entry->url = value;
    }
    if (entry) addIfPublishedMod(*entry, mods);
    return mods;
}

std::string latestReleaseUrl(const GitHubRepository& repository) {
    return gitHubHttpsPrefix + repository.owner + "/" + repository.name + "/releases/latest";
}

std::optional<std::string> tagFromReleaseUrl(const std::string& url) {
    const size_t marker = url.find(releaseTagMarker);
    if (marker == std::string::npos) return std::nullopt;
    const size_t tagStart = marker + releaseTagMarker.size();
    const size_t tagEnd = url.find_first_of("/?#", tagStart);
    const std::string tag = url.substr(tagStart, tagEnd == std::string::npos ? std::string::npos : tagEnd - tagStart);
    if (!isPlainName(tag, true)) return std::nullopt;
    return tag;
}

std::string loaderZipName(const std::string& tag) {
    return "wots4-modloader-" + tag + ".zip";
}

std::string modZipName(const std::string& folderName, const std::string& tag) {
    std::string zipName = folderName;
    std::replace(zipName.begin(), zipName.end(), '_', '-');
    return "wots4-mod-" + zipName + "-" + tag + ".zip";
}

std::string releaseAssetUrl(const GitHubRepository& repository, const std::string& tag, const std::string& assetName) {
    return gitHubHttpsPrefix + repository.owner + "/" + repository.name + "/releases/download/" + tag + "/" + assetName;
}

}  // namespace wots4::installer
