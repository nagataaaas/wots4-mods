#include "game_install.h"
#include "config_merge.h"
#include "file_io.h"
#include "text_utils.h"
#include "version_text.h"
#include <windows.h>
#include <tlhelp32.h>
#include <memory>

namespace wots4::installer {
namespace {

struct HandleCloser {
    void operator()(HANDLE handle) const { CloseHandle(handle); }
};
using KernelHandle = std::unique_ptr<void, HandleCloser>;

struct LanguageAndCodePage {
    WORD language;
    WORD codePage;
};

bool hasLoaderProductName(const std::vector<BYTE>& versionInfo) {
    LanguageAndCodePage* translations = nullptr;
    UINT translationsSize = 0;
    if (!VerQueryValueW(versionInfo.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations),
            &translationsSize)) {
        return false;
    }
    for (UINT index = 0; index < translationsSize / sizeof(LanguageAndCodePage); ++index) {
        wchar_t query[64];
        swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\ProductName", translations[index].language,
            translations[index].codePage);
        wchar_t* productName = nullptr;
        UINT productNameLength = 0;
        if (VerQueryValueW(versionInfo.data(), query, reinterpret_cast<void**>(&productName), &productNameLength) &&
            productNameLength > 0 && wcscmp(productName, loaderProductName) == 0) {
            return true;
        }
    }
    return false;
}

// mods/<name>/config.ini
bool isModConfig(const std::string& path) {
    const size_t firstSlash = path.find('/');
    const size_t secondSlash = path.find('/', firstSlash + 1);
    return firstSlash != std::string::npos && secondSlash != std::string::npos &&
        equalsIgnoreCaseAscii(path.substr(0, firstSlash), "mods") &&
        equalsIgnoreCaseAscii(path.substr(secondSlash + 1), "config.ini");
}

std::optional<Failure> installFile(const std::filesystem::path& staged, const std::filesystem::path& target,
    bool mergeWithExisting) {
    std::string contents;
    if (auto failure = readWholeFile(staged, contents)) return failure;
    std::error_code error;
    if (mergeWithExisting && std::filesystem::exists(target, error)) {
        std::string oldContents;
        if (auto failure = readWholeFile(target, oldContents)) return failure;
        contents = mergeConfigIni(contents, oldContents);
    }
    return replaceFileContents(target, contents);
}

}  // namespace

LoaderFileState inspectLoaderFile(const std::filesystem::path& dinput8Path) {
    if (GetFileAttributesW(dinput8Path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? LoaderFileState::missing
                                                                              : LoaderFileState::foreign;
    }
    const DWORD size = GetFileVersionInfoSizeW(dinput8Path.c_str(), nullptr);
    if (size == 0) return LoaderFileState::foreign;
    std::vector<BYTE> versionInfo(size);
    if (!GetFileVersionInfoW(dinput8Path.c_str(), 0, size, versionInfo.data())) return LoaderFileState::foreign;
    return hasLoaderProductName(versionInfo) ? LoaderFileState::ours : LoaderFileState::foreign;
}

std::filesystem::path backupPathFor(const std::filesystem::path& path) {
    for (int number = 1;; ++number) {
        std::filesystem::path backup = path;
        backup += number == 1 ? L".bak" : L".bak" + std::to_wstring(number);
        if (GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES) return backup;
    }
}

std::optional<Failure> renameFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    if (MoveFileExW(from.c_str(), to.c_str(), 0)) return std::nullopt;
    return Failure{FailureKind::fileAccess, GetLastError(), from.wstring()};
}

std::optional<Failure> installStagedFiles(const std::filesystem::path& stagingFolder,
    const std::vector<std::string>& paths, const std::filesystem::path& gameFolder) {
    for (const std::string& path : paths) {
        const std::filesystem::path relative = fromUtf8(path);
        const auto failure = installFile(stagingFolder / relative, gameFolder / relative, isModConfig(path));
        if (failure) return failure;
    }
    return std::nullopt;
}

bool isGameRunning(const std::filesystem::path& gameExe) {
    KernelHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        snapshot.release();
        return false;
    }
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL found = Process32FirstW(snapshot.get(), &entry); found; found = Process32NextW(snapshot.get(), &entry)) {
        if (_wcsicmp(entry.szExeFile, gameExe.filename().c_str()) != 0) continue;
        KernelHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process) return true;  // A game we cannot inspect might be this one; closing it is safer
        wchar_t imagePath[MAX_PATH * 2];
        DWORD imagePathLength = ARRAYSIZE(imagePath);
        if (!QueryFullProcessImageNameW(process.get(), 0, imagePath, &imagePathLength)) return true;
        std::error_code error;
        if (std::filesystem::equivalent(imagePath, gameExe, error)) return true;
    }
    return false;
}

}  // namespace wots4::installer
