#pragma once
// Finds <base>mods\<name>\mod.dll, loads every one, then calls their
// Wots4ModInitialize. Header-only so that tests can drive it without the proxy.
#include <wots4/mod_api.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

namespace wots4 {

constexpr char modEntryName[] = "Wots4ModInitialize";

struct Candidate {
    std::wstring path;       // <base>mods\<name>\mod.dll
    std::wstring directory;  // <base>mods\<name>\ (ends with a backslash)
    HMODULE module = nullptr;
};

enum class LoadState { failed, missingEntry, accepted, refused, duplicate };

struct LoadResult {
    std::wstring path;
    LoadState state;
    DWORD error;  // GetLastError() of LoadLibraryExW when state is failed, else 0
};

// The words used in mod_loader.log and in the table of docs/MOD_API.md.
constexpr const char* loadStateName(LoadState state) {
    switch (state) {
    case LoadState::failed: return "failed";
    case LoadState::missingEntry: return "missingEntry";
    case LoadState::accepted: return "accepted";
    case LoadState::refused: return "refused";
    case LoadState::duplicate: return "duplicate";
    }
    return "unknown";
}

// Keeps the module loaded until the process exits, whatever FreeLibrary calls follow.
inline void pinModule(HMODULE module) {
    HMODULE pinned;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(module), &pinned);
}

// Case-insensitive order. Paths that differ only in case fall back to the
// case-sensitive order, so the result never depends on the discovery order.
inline bool modPathLess(const std::wstring& a, const std::wstring& b) {
    int order = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
    return order == CSTR_LESS_THAN || (order == CSTR_EQUAL && a < b);
}

// A real subfolder: not ".", "..", a symbolic link or a junction.
inline bool isPlainDirectory(const WIN32_FIND_DATAW& entry) {
    if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    return wcscmp(entry.cFileName, L".") != 0 && wcscmp(entry.cFileName, L"..") != 0;
}

inline bool isPlainFile(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

// Lists <base>mods\*\mod.dll, one level deep only, sorted by path.
inline std::vector<Candidate> discoverMods(const std::wstring& base) {
    std::vector<Candidate> candidates;
    const std::wstring modsDirectory = base + L"mods\\";
    WIN32_FIND_DATAW entry;
    HANDLE search = FindFirstFileW((modsDirectory + L"*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return candidates;
    do {
        if (!isPlainDirectory(entry)) continue;
        std::wstring directory = modsDirectory + entry.cFileName + L"\\";
        std::wstring path = directory + L"mod.dll";
        if (isPlainFile(path)) candidates.push_back({path, directory});
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return modPathLess(a.path, b.path); });
    return candidates;
}

// Loads and pins one mod.dll. On failure returns nullptr and sets error.
inline HMODULE loadModModule(const std::wstring& path, DWORD& error) {
    // A broken mod.dll must be reported in the log, not block the game with
    // a system error dialog. Only this thread's mode changes, and only here.
    DWORD previousMode = 0;
    BOOL modeChanged = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previousMode);
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    error = module ? 0 : GetLastError();
    if (modeChanged) SetThreadErrorMode(previousMode, nullptr);
    if (module) pinModule(module);
    return module;
}

// Returns one result per discovered mod.dll, in the order of discoverMods().
inline std::vector<LoadResult> loadMods(const std::wstring& base, HMODULE loader, const HostApi* host) {
    std::vector<Candidate> candidates = discoverMods(base);
    std::vector<LoadResult> results;
    results.reserve(candidates.size());

    // Load every module first. Explicit initialization starts after discovery
    // and loading, so a later Mod is not initialized recursively by a dependency.
    for (Candidate& candidate : candidates) {
        DWORD error = 0;
        candidate.module = loadModModule(candidate.path, error);
        results.push_back({candidate.path, LoadState::failed, error});
    }

    const HMODULE game = GetModuleHandleW(nullptr);
    std::vector<HMODULE> initialized;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& candidate = candidates[i];
        LoadState& state = results[i].state;
        if (!candidate.module) continue;

        auto initialize = reinterpret_cast<Initialize>(GetProcAddress(candidate.module, modEntryName));
        if (!initialize) {
            state = LoadState::missingEntry;
            continue;
        }
        // Two candidates can resolve to one loaded module; initialize it only once.
        if (std::find(initialized.begin(), initialized.end(), candidate.module) != initialized.end()) {
            state = LoadState::duplicate;
            continue;
        }
        initialized.push_back(candidate.module);

        ModContext context{sizeof(context), modApiVersion, loader, candidate.module, game,
            candidate.directory.c_str(), host};
        state = initialize(&context) ? LoadState::accepted : LoadState::refused;
    }
    return results;
}

// Byte ranges that Mods are going to overwrite with hooks. Claims run on the
// startup/game thread. This records ownership only; it neither chains
// callbacks nor writes machine code.
struct HookClaim {
    HMODULE owner;
    uintptr_t address;
    uint32_t length;
};
inline std::vector<HookClaim> hookClaims;

inline bool rangesOverlap(uintptr_t a, uint32_t aLength, uintptr_t b, uint32_t bLength) {
    return a < b + bLength && b < a + aLength;
}

inline BOOL WINAPI claimHook(HMODULE owner, uintptr_t address, uint32_t length) {
    const bool valid = owner && address && length && address <= UINTPTR_MAX - length;
    if (!valid) return FALSE;
    for (const HookClaim& claim : hookClaims) {
        if (rangesOverlap(address, length, claim.address, claim.length)) return FALSE;
    }
    hookClaims.push_back({owner, address, length});
    return TRUE;
}

inline void WINAPI releaseHookClaims(HMODULE owner) {
    hookClaims.erase(std::remove_if(hookClaims.begin(), hookClaims.end(),
        [&](const HookClaim& claim) { return claim.owner == owner; }), hookClaims.end());
}

}  // namespace wots4
