// Checks the installer's parts that do not need the network or the game: config.ini merging,
// reading .gitmodules and libraryfolders.vdf, release names, zip entry checks, extraction and
// installing into a game folder made in a temporary folder.
#include "config_merge.h"
#include "file_io.h"
#include "game_install.h"
#include "game_location.h"
#include "release_sources.h"
#include "zip_archive.h"
#include <miniz.h>
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace wots4::installer;

namespace {

unsigned tests, failures;

void check(const char* name, bool ok) {
    ++tests;
    if (!ok) ++failures;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

std::string readText(const fs::path& path) {
    std::string contents;
    return readWholeFile(path, contents) ? std::string("<unreadable>") : contents;
}

void checkConfigMerge() {
    const std::string oldText =
        "; old comment\r\n"
        "[Antialiasing]\r\n"
        "method = smaa\r\n"
        "removed=1\r\n"
        "[motion_blur]\r\n"
        "enabled=1\r\n"
        "enabled=0\r\n"
        "[gone]\r\n"
        "key=x\r\n";
    const std::string newText =
        "; new comment\n"
        "[antialiasing]\n"
        "; Edge smoothing\n"
        "METHOD=off\n"
        "supersampling=off\n"
        "[motion_blur]\n"
        "enabled = 0\n"
        "strength=normal";
    const std::string expected =
        "; new comment\n"
        "[antialiasing]\n"
        "; Edge smoothing\n"
        "METHOD=smaa\n"
        "supersampling=off\n"
        "[motion_blur]\n"
        "enabled = 1\n"
        "strength=normal";
    const std::string merged = mergeConfigIni(newText, oldText);
    check("config: old values kept, new comments, keys and line endings used", merged == expected);
    if (merged != expected) printf("---- merged\n%s\n----\n", merged.c_str());

    const std::string bom = "\xEF\xBB\xBF";
    check("config: the new BOM is kept and the old BOM is ignored",
        mergeConfigIni(bom + "[a]\r\nk=new\r\n", bom + "[a]\r\nk=old\r\n") == bom + "[a]\r\nk=old\r\n");
    check("config: no old file keeps the new text", mergeConfigIni("[a]\nk=1\n", "") == "[a]\nk=1\n");
    check("config: an empty old value is kept", mergeConfigIni("[a]\nk=1\n", "[a]\nk=\n") == "[a]\nk=\n");
    check("config: keys before any section match", mergeConfigIni("k=1\n[a]\nk=2\n", "k=9\n") == "k=9\n[a]\nk=2\n");
}

void checkGitmodules() {
    const std::string text =
        "[submodule \"mods/debugger\"]\n"
        "\tpath = mods/debugger\n"
        "\turl = https://github.com/nagataaaas/wots4-debugger.git\n"
        "[submodule \"mods/post_fx\"]\r\n"
        "\tpath = mods/post_fx\r\n"
        "\turl = git@github.com:nagataaaas/wots4-post-fx\r\n"
        "[submodule \"tools/other\"]\n"
        "\tpath = tools/other\n"
        "\turl = https://github.com/someone/other.git\n"
        "[submodule \"mods/elsewhere\"]\n"
        "\tpath = mods/elsewhere\n"
        "\turl = https://gitlab.com/someone/elsewhere.git\n"
        "[submodule \"mods/bad\"]\n"
        "\tpath = mods/../bad\n"
        "\turl = https://github.com/someone/bad.git\n";
    const auto mods = parseGitmodules(text);
    check("gitmodules: only GitHub Mods under mods/ are listed", mods.size() == 2);
    if (mods.size() != 2) return;
    check("gitmodules: folder, owner and repository",
        mods[0].folderName == "debugger" && mods[0].repository.owner == "nagataaaas" &&
            mods[0].repository.name == "wots4-debugger");
    check("gitmodules: SSH URL without .git and CRLF lines",
        mods[1].folderName == "post_fx" && mods[1].repository.name == "wots4-post-fx");
}

void checkLibraryFolders() {
    const std::string vdf =
        "\"libraryfolders\"\n{\n"
        "\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n"
        "\t\t\"apps\"\n\t\t{\n\t\t\t\"228980\"\t\t\"1\"\n\t\t}\n\t}\n"
        "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t\t\"label\"\t\t\"\"\n"
        "\t\t\"apps\"\n\t\t{\n\t\t\t\"312780\"\t\t\"123\"\n\t\t}\n\t}\n"
        "}\n";
    const auto libraries = librariesWithApp(vdf, "312780");
    check("vdf: only the library with the game", libraries.size() == 1 && libraries[0] == "D:\\SteamLibrary");
    check("vdf: an AppID elsewhere is not taken for an app",
        librariesWithApp("\"libraryfolders\" { \"0\" { \"path\" \"E:\\\\x\" \"312780\" \"1\" } }", "312780").empty());
}

void checkReleaseNames() {
    check("tag: read from the tag page",
        tagFromReleaseUrl("https://github.com/o/r/releases/tag/v2026.10.14.3") == std::optional<std::string>("v2026.10.14.3"));
    check("tag: none when there is no release", !tagFromReleaseUrl("https://github.com/o/r/releases"));
    check("tag: unexpected characters are refused", !tagFromReleaseUrl("https://github.com/o/r/releases/tag/v1%2F.."));
    check("zip: ModLoader name", loaderZipName("v2026.10.14") == "wots4-modloader-v2026.10.14.zip");
    check("zip: Mod name uses - for _", modZipName("unofficial_patch", "v2026.10.14.5") ==
        "wots4-mod-unofficial-patch-v2026.10.14.5.zip");
    check("zip: download URL", releaseAssetUrl({"o", "r"}, "v1", "a.zip") == "https://github.com/o/r/releases/download/v1/a.zip");
    check("latest release URL", latestReleaseUrl({"o", "r"}) == "https://github.com/o/r/releases/latest");
}

void checkEntryTargets() {
    const auto mod = [](const std::string& name) { return modEntryTarget(name, "post_fx"); };
    check("mod entry: a file in the Mod folder", mod("mods/post_fx/mod.dll").action == EntryAction::extract &&
        mod("mods/post_fx/mod.dll").path == "mods/post_fx/mod.dll");
    check("mod entry: backslashes from PowerShell 5", mod("mods\\post_fx\\docs\\licenses\\a.txt").path ==
        "mods/post_fx/docs/licenses/a.txt");
    check("mod entry: folders on the way are skipped", mod("mods/").action == EntryAction::skip &&
        mod("mods/post_fx/").action == EntryAction::skip && mod("mods/post_fx/docs/").action == EntryAction::skip);
    check("mod entry: another Mod is refused", mod("mods/tweaks/mod.dll").action == EntryAction::reject &&
        mod("mods/post_fx_extra/mod.dll").action == EntryAction::reject && mod("mods/tweaks/").action == EntryAction::reject);
    check("mod entry: files outside mods are refused", mod("dinput8.dll").action == EntryAction::reject);
    check("mod entry: .. and absolute paths are refused", mod("mods/post_fx/../tweaks/mod.dll").action == EntryAction::reject &&
        mod("/mods/post_fx/mod.dll").action == EntryAction::reject && mod("C:/x/mod.dll").action == EntryAction::reject);
    check("loader entry: dinput8.dll and licenses are installed", loaderEntryTarget("dinput8.dll").action == EntryAction::extract &&
        loaderEntryTarget("docs/licenses/minhook.txt").path == "docs/licenses/minhook.txt");
    check("loader entry: README.md stays out", loaderEntryTarget("README.md").action == EntryAction::skip);
    check("loader entry: .. is refused", loaderEntryTarget("../dinput8.dll").action == EntryAction::reject);
}

bool writeZip(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& files) {
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) return false;
    for (const auto& [name, contents] : files) {
        mz_zip_writer_add_mem(&archive, name.c_str(), contents.data(), contents.size(), MZ_DEFAULT_COMPRESSION);
    }
    void* buffer = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size);
    const bool written = !writeWholeFile(path, std::string(static_cast<char*>(buffer), size));
    mz_zip_writer_end(&archive);
    return written;
}

void checkExtractAndInstall(const fs::path& work) {
    const auto modTarget = [](const std::string& name) { return modEntryTarget(name, "sample_mod"); };
    const fs::path zip = work / L"mod.zip";
    writeZip(zip, {{"mods/", ""}, {"mods/sample_mod/mod.dll", "new dll"},
                      {"mods/sample_mod/config.ini", "[a]\r\nk=default\r\nnew=1\r\n"}});
    const fs::path staging = work / L"staging";
    std::vector<std::string> paths;
    const auto failure = extractZip(zip, modTarget, staging, paths);
    check("extract: the Mod's files are extracted", !failure && paths.size() == 2 &&
        readText(staging / L"mods/sample_mod/mod.dll") == "new dll");

    const fs::path badZip = work / L"bad.zip";
    writeZip(badZip, {{"mods/sample_mod/mod.dll", "x"}, {"mods/other/mod.dll", "x"}});
    std::vector<std::string> badPaths;
    const auto badFailure = extractZip(badZip, modTarget, work / L"bad", badPaths);
    check("extract: a zip with another Mod's file writes nothing", badFailure &&
        badFailure->kind == FailureKind::zipUnexpectedEntry && badPaths.empty() && !fs::exists(work / L"bad"));

    const fs::path broken = work / L"broken.zip";
    writeWholeFile(broken, "not a zip");
    const auto brokenFailure = extractZip(broken, modTarget, work / L"broken", badPaths);
    check("extract: a broken zip fails", brokenFailure && brokenFailure->kind == FailureKind::zipUnreadable);

    const fs::path game = work / L"game";
    writeWholeFile(game / L"mods/sample_mod/mod.dll", "old dll");
    writeWholeFile(game / L"mods/sample_mod/config.ini", "[A]\r\nK=user\r\ngone=1\r\n");
    writeWholeFile(game / L"mods/sample_mod/user.log", "log");
    const auto installFailure = installStagedFiles(staging, paths, game);
    check("install: succeeds", !installFailure);
    check("install: mod.dll is replaced", readText(game / L"mods/sample_mod/mod.dll") == "new dll");
    check("install: config.ini keeps the user's value", readText(game / L"mods/sample_mod/config.ini") ==
        "[a]\r\nk=user\r\nnew=1\r\n");
    check("install: other files are left alone", readText(game / L"mods/sample_mod/user.log") == "log");
    check("install: no temporary file is left", !fs::exists(game / L"mods/sample_mod/config.ini.installer-new"));

    const fs::path freshGame = work / L"fresh";
    check("install: a new Mod gets the new config.ini", !installStagedFiles(staging, paths, freshGame) &&
        readText(freshGame / L"mods/sample_mod/config.ini") == "[a]\r\nk=default\r\nnew=1\r\n");
}

void checkLoaderFiles(const fs::path& work, const fs::path& builtLoader) {
    check("loader file: this ModLoader is ours", inspectLoaderFile(builtLoader) == LoaderFileState::ours);
    wchar_t systemFolder[MAX_PATH];
    GetSystemDirectoryW(systemFolder, MAX_PATH);
    check("loader file: Windows' dinput8.dll is foreign",
        inspectLoaderFile(fs::path(systemFolder) / L"dinput8.dll") == LoaderFileState::foreign);
    const fs::path noVersion = work / L"noversion/dinput8.dll";
    writeWholeFile(noVersion, "MZ but no version information");
    check("loader file: a file without version information is foreign", inspectLoaderFile(noVersion) == LoaderFileState::foreign);
    check("loader file: missing", inspectLoaderFile(work / L"none/dinput8.dll") == LoaderFileState::missing);

    check("backup: first .bak", backupPathFor(noVersion).filename() == L"dinput8.dll.bak");
    writeWholeFile(work / L"noversion/dinput8.dll.bak", "older");
    check("backup: .bak2 when .bak exists", backupPathFor(noVersion).filename() == L"dinput8.dll.bak2");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: wots4_installer_probe <path to the built dinput8.dll>\n");
        return 2;
    }
    const fs::path work = fs::temp_directory_path() / (L"wots4-installer-probe-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    fs::remove_all(work, error);

    checkConfigMerge();
    checkGitmodules();
    checkLibraryFolders();
    checkReleaseNames();
    checkEntryTargets();
    checkExtractAndInstall(work);
    checkLoaderFiles(work, argv[1]);

    fs::remove_all(work, error);
    printf("RESULT %u passed, %u failed\n", tests - failures, failures);
    return failures ? 1 : 0;
}
