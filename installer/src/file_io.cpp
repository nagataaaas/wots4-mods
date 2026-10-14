#include "file_io.h"
#include <windows.h>
#include <memory>

namespace wots4::installer {
namespace {

const wchar_t temporarySuffix[] = L".installer-new";

struct HandleCloser {
    void operator()(HANDLE handle) const { CloseHandle(handle); }
};
using FileHandle = std::unique_ptr<void, HandleCloser>;

Failure accessFailure(const std::filesystem::path& path, DWORD error) {
    return {FailureKind::fileAccess, error, path.wstring()};
}

std::optional<Failure> createParentFolders(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return accessFailure(path.parent_path(), static_cast<DWORD>(error.value()));
    return std::nullopt;
}

}  // namespace

std::optional<Failure> readWholeFile(const std::filesystem::path& path, std::string& contents) {
    FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        file.release();
        return accessFailure(path, GetLastError());
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size)) return accessFailure(path, GetLastError());
    contents.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    if (!contents.empty() && (!ReadFile(file.get(), contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr) ||
        read != contents.size())) {
        return accessFailure(path, GetLastError());
    }
    return std::nullopt;
}

std::optional<Failure> writeWholeFile(const std::filesystem::path& path, const std::string& contents) {
    if (auto failure = createParentFolders(path)) return failure;
    FileHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        file.release();
        return accessFailure(path, GetLastError());
    }
    DWORD written = 0;
    if (!contents.empty() && (!WriteFile(file.get(), contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) ||
        written != contents.size())) {
        return accessFailure(path, GetLastError());
    }
    return std::nullopt;
}

std::optional<Failure> replaceFileContents(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::path temporary = path;
    temporary += temporarySuffix;
    if (auto failure = writeWholeFile(temporary, contents)) {
        DeleteFileW(temporary.c_str());
        return failure;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        DeleteFileW(temporary.c_str());
        return accessFailure(path, error);
    }
    return std::nullopt;
}

}  // namespace wots4::installer
