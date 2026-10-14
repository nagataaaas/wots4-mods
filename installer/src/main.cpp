// WayOfTheSamurai4ModInstaller.exe: installs or updates the ModLoader and the published Mods
// from their latest GitHub Releases. Everything is downloaded and extracted to a temporary
// folder before anything in the game folder is changed.
#include "console_ui.h"
#include "game_install.h"
#include "game_location.h"
#include "http_client.h"
#include "messages.h"
#include "release_sources.h"
#include "text_utils.h"
#include "version_text.h"
#include "zip_archive.h"
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;
using namespace wots4::installer;

namespace {

const wchar_t loaderDisplayName[] = L"ModLoader";
const wchar_t loaderFileName[] = L"dinput8.dll";

struct DownloadedRelease {
    std::string tag;
    fs::path stagingFolder;
    std::vector<std::string> paths;  // extracted files, relative to stagingFolder and to the game folder
};

struct ModDownload {
    PublishedMod mod;
    bool wasInstalled;
    DownloadedRelease release;
    std::optional<Failure> failure;
};

// Removes the temporary folder however the installer ends.
class WorkFolder {
public:
    WorkFolder() {
        std::error_code error;
        path_ = fs::temp_directory_path(error) / (L"wots4-installer-" + std::to_wstring(GetCurrentProcessId()));
        fs::remove_all(path_, error);
    }
    ~WorkFolder() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

Language chooseLanguage(Console& console) {
    console.writeLine(L"言語を選んでください / Choose a language  (↑↓, Enter)");
    const bool windowsIsJapanese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
    const size_t chosen = console.chooseOne({L"日本語", L"English"}, windowsIsJapanese ? 0 : 1);
    console.writeLine();
    return chosen == 0 ? Language::japanese : Language::english;
}

bool askYesNo(Console& console, const Messages& text, const std::wstring& question) {
    console.writeLine(question);
    const bool yes = console.chooseOne({text.yes, text.no}, 0) == 0;
    console.writeLine();
    return yes;
}

std::optional<fs::path> showGameExeDialog(const Messages& text) {
    wchar_t file[MAX_PATH * 2] = L"";
    // Two strings per filter, and the list ends with an extra null
    std::wstring filter = std::wstring(text.gameExeFilterName) + L'\0' + gameExeName + L'\0' + L'\0';
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = GetConsoleWindow();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = file;
    dialog.nMaxFile = ARRAYSIZE(file);
    dialog.lpstrTitle = text.gameExeDialogTitle;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return std::nullopt;
    return fs::path(file);
}

std::optional<fs::path> askForGameExe(Console& console, const Messages& text) {
    console.writeLine(text.chooseGameExe);
    for (;;) {
        const auto chosen = showGameExeDialog(text);
        if (!chosen) return std::nullopt;
        if (_wcsicmp(chosen->filename().c_str(), gameExeName) == 0) return chosen;
        console.writeLine(formatText(text.wrongExeChosen, {chosen->wstring()}));
    }
}

std::optional<fs::path> chooseGameExe(Console& console, const Messages& text) {
    const auto candidates = findGameExeCandidates();
    if (candidates.empty()) {
        console.writeLine(text.gameNotFound);
    } else if (askYesNo(console, text, formatText(text.gameFound, {candidates.front().wstring()}))) {
        return candidates.front();
    }
    return askForGameExe(console, text);
}

std::vector<PublishedMod> fetchPublishedMods(HttpClient& http, Console& console, const Messages& text) {
    console.writeLine(text.fetchingModList);
    std::string gitmodules;
    if (auto failure = http.fetchText(publishedModListUrl(), gitmodules)) {
        console.writeLine(formatText(text.modListFailed, {describeFailure(*failure, text)}));
        return {};
    }
    return parseGitmodules(gitmodules);
}

bool isModInstalled(const fs::path& gameFolder, const PublishedMod& mod) {
    std::error_code error;
    return fs::is_regular_file(gameFolder / L"mods" / fromUtf8(mod.folderName) / L"mod.dll", error);
}

// Mods the user checked. Installed Mods start checked; unchecking one only skips its update.
std::vector<ModDownload> chooseMods(Console& console, const Messages& text, const std::vector<PublishedMod>& mods,
    const fs::path& gameFolder) {
    if (mods.empty()) return {};
    std::vector<ToggleItem> items;
    for (const PublishedMod& mod : mods) {
        const bool installed = isModInstalled(gameFolder, mod);
        items.push_back({fromUtf8(mod.folderName), installed ? text.installedNote : L"", installed});
    }
    console.writeLine();
    console.writeLine(text.chooseMods);
    console.writeLine(text.toggleHelp);
    const std::vector<bool> checked = console.chooseMany(items);
    console.writeLine();
    std::vector<ModDownload> chosen;
    for (size_t index = 0; index < mods.size(); ++index) {
        if (checked[index]) chosen.push_back({mods[index], items[index].checked, {}, std::nullopt});
    }
    return chosen;
}

// Finds the latest tag, downloads the zip named zipNameOf(tag) and extracts the accepted entries.
template <typename ZipNameOf>
std::optional<Failure> downloadLatestRelease(HttpClient& http, const GitHubRepository& repository, ZipNameOf zipNameOf,
    const EntryTargetFunction& targetOf, const fs::path& stagingFolder, DownloadedRelease& release) {
    const std::string releasePage = latestReleaseUrl(repository);
    std::string location;
    if (auto failure = http.redirectTarget(releasePage, location)) return failure;
    const auto tag = tagFromReleaseUrl(location);
    if (!tag) return Failure{FailureKind::noRelease, 0, fromUtf8(releasePage)};

    const std::string zipName = zipNameOf(*tag);
    const fs::path zipFile = stagingFolder.parent_path() / fromUtf8(zipName);
    if (auto failure = http.download(releaseAssetUrl(repository, *tag, zipName), zipFile)) return failure;
    release.tag = *tag;
    release.stagingFolder = stagingFolder;
    return extractZip(zipFile, targetOf, stagingFolder, release.paths);
}

std::optional<Failure> downloadLoader(HttpClient& http, const fs::path& workFolder, DownloadedRelease& release) {
    return downloadLatestRelease(http, loaderRepository, loaderZipName, loaderEntryTarget, workFolder / L"loader", release);
}

std::optional<Failure> downloadMod(HttpClient& http, const fs::path& workFolder, ModDownload& download) {
    const std::string folderName = download.mod.folderName;
    return downloadLatestRelease(
        http, download.mod.repository, [&](const std::string& tag) { return modZipName(folderName, tag); },
        [&](const std::string& entry) { return modEntryTarget(entry, folderName); },
        workFolder / (L"mod-" + fromUtf8(folderName)), download.release);
}

void reportDownload(Console& console, const Messages& text, const std::wstring& name,
    const std::optional<Failure>& failure, const DownloadedRelease& release) {
    if (failure) {
        console.writeLine(formatText(text.downloadFailed, {name, describeFailure(*failure, text)}));
    } else {
        console.writeLine(formatText(text.downloaded, {name, fromUtf8(release.tag)}));
    }
}

std::wstring resultLine(const Messages& text, const std::wstring& name, const std::string& tag, bool wasInstalled,
    const std::optional<Failure>& failure) {
    if (failure) return formatText(text.resultFailed, {name, describeFailure(*failure, text)});
    return formatText(wasInstalled ? text.resultUpdated : text.resultInstalled, {name, fromUtf8(tag)});
}

void run(Console& console, const Messages& text) {
    const auto gameExe = chooseGameExe(console, text);
    if (!gameExe) {
        console.writeLine(text.noGameChosen);
        return;
    }
    const fs::path gameFolder = gameExe->parent_path();
    console.writeLine(formatText(text.installFolder, {gameFolder.wstring()}));
    if (isGameRunning(*gameExe)) {
        console.writeLine(text.gameRunning);
        return;
    }

    const fs::path loaderPath = gameFolder / loaderFileName;
    const LoaderFileState loaderState = inspectLoaderFile(loaderPath);
    const fs::path backupPath = backupPathFor(loaderPath);
    if (loaderState == LoaderFileState::foreign &&
        !askYesNo(console, text, formatText(text.foreignLoaderFound, {backupPath.filename().wstring()}))) {
        console.writeLine(text.foreignLoaderKept);
        return;
    }

    HttpClient http;
    std::vector<ModDownload> mods = chooseMods(console, text, fetchPublishedMods(http, console, text), gameFolder);

    WorkFolder workFolder;
    DownloadedRelease loader;
    console.writeLine(formatText(text.downloading, {loaderDisplayName}));
    const auto loaderFailure = downloadLoader(http, workFolder.path(), loader);
    reportDownload(console, text, loaderDisplayName, loaderFailure, loader);
    if (loaderFailure) {
        console.writeLine(text.loaderDownloadFailed);
        return;
    }
    for (ModDownload& mod : mods) {
        const std::wstring name = fromUtf8(mod.mod.folderName);
        console.writeLine(formatText(text.downloading, {name}));
        mod.failure = downloadMod(http, workFolder.path(), mod);
        reportDownload(console, text, name, mod.failure, mod.release);
    }

    console.writeLine();
    console.writeLine(text.writing);
    std::optional<Failure> loaderWriteFailure;
    if (loaderState == LoaderFileState::foreign) loaderWriteFailure = renameFile(loaderPath, backupPath);
    if (!loaderWriteFailure) loaderWriteFailure = installStagedFiles(loader.stagingFolder, loader.paths, gameFolder);
    if (loaderWriteFailure) {
        console.writeLine(formatText(text.loaderWriteFailed, {describeFailure(*loaderWriteFailure, text)}));
        return;
    }
    for (ModDownload& mod : mods) {
        if (!mod.failure) mod.failure = installStagedFiles(mod.release.stagingFolder, mod.release.paths, gameFolder);
    }

    console.writeLine();
    console.writeLine(text.resultHeader);
    console.writeLine(resultLine(text, loaderDisplayName, loader.tag, loaderState == LoaderFileState::ours, std::nullopt));
    if (loaderState == LoaderFileState::foreign) {
        console.writeLine(formatText(text.resultBackup, {backupPath.filename().wstring()}));
    }
    for (const ModDownload& mod : mods) {
        console.writeLine(resultLine(text, fromUtf8(mod.mod.folderName), mod.release.tag, mod.wasInstalled, mod.failure));
    }
}

}  // namespace

int wmain() {
    // The file dialog uses COM
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetConsoleTitleW(L"Way of the Samurai 4 Mod Installer");
    Console console;
    console.writeLine(std::wstring(L"Way of the Samurai 4 Mod Installer ") + installerVersion);
    console.writeLine();
    const Messages& text = messagesFor(chooseLanguage(console));
    run(console, text);
    console.writeLine();
    console.writeLine(text.pressEnterToClose);
    console.waitForEnter();
    CoUninitialize();
    return 0;
}
