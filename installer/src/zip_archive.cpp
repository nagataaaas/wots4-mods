#include "zip_archive.h"
#include "file_io.h"
#include "text_utils.h"
#include <miniz.h>
#include <memory>

namespace wots4::installer {
namespace {

struct ZipReaderCloser {
    void operator()(mz_zip_archive* archive) const { mz_zip_reader_end(archive); }
};
using ZipReader = std::unique_ptr<mz_zip_archive, ZipReaderCloser>;

struct PlannedEntry {
    mz_uint index;
    std::string path;
};

Failure unreadable(const std::filesystem::path& zipFile) {
    return {FailureKind::zipUnreadable, 0, zipFile.filename().wstring()};
}

std::optional<std::string> entryName(mz_zip_archive* archive, mz_uint index) {
    const mz_uint size = mz_zip_reader_get_filename(archive, index, nullptr, 0);
    if (size == 0) return std::nullopt;
    std::string name(size, '\0');
    mz_zip_reader_get_filename(archive, index, name.data(), size);
    name.resize(size - 1);  // the size includes the terminating null
    return name;
}

}  // namespace

std::optional<Failure> extractZip(const std::filesystem::path& zipFile, const EntryTargetFunction& targetOf,
    const std::filesystem::path& destination, std::vector<std::string>& extractedPaths) {
    // Read the zip into memory: miniz opens files with narrow paths, which break on non-ASCII user folders
    std::string zipBytes;
    if (auto failure = readWholeFile(zipFile, zipBytes)) return failure;

    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zipBytes.data(), zipBytes.size(), 0)) return unreadable(zipFile);
    ZipReader reader(&archive);

    std::vector<PlannedEntry> planned;
    const mz_uint entryCount = mz_zip_reader_get_num_files(&archive);
    for (mz_uint index = 0; index < entryCount; ++index) {
        const auto name = entryName(&archive, index);
        if (!name) return unreadable(zipFile);
        const EntryTarget target = targetOf(*name);
        if (target.action == EntryAction::reject) {
            return Failure{FailureKind::zipUnexpectedEntry, 0, zipFile.filename().wstring(), fromUtf8(*name)};
        }
        if (target.action == EntryAction::skip || mz_zip_reader_is_file_a_directory(&archive, index)) continue;
        if (!mz_zip_reader_is_file_supported(&archive, index)) return unreadable(zipFile);
        planned.push_back({index, target.path});
    }

    for (const PlannedEntry& entry : planned) {
        size_t size = 0;
        void* contents = mz_zip_reader_extract_to_heap(&archive, entry.index, &size, 0);
        if (!contents) return unreadable(zipFile);
        const std::string bytes(static_cast<const char*>(contents), size);
        mz_free(contents);
        const auto failure = writeWholeFile(destination / fromUtf8(entry.path), bytes);
        if (failure) return failure;
        extractedPaths.push_back(entry.path);
    }
    return std::nullopt;
}

}  // namespace wots4::installer
