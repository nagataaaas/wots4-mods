#pragma once
#include <wots4/mod_api.h>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>
namespace wots4 {
struct Candidate {std::wstring path,directory;HMODULE module=nullptr;};
enum class LoadState { failed,missingEntry,accepted,refused,duplicate };
struct LoadResult {std::wstring path;LoadState state;DWORD error;};
inline bool stableLess(const std::wstring& a,const std::wstring& b) {
    auto order=CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE);
    return order==CSTR_LESS_THAN||(order==CSTR_EQUAL&&a<b);
}
inline std::vector<Candidate> discoverMods(const std::wstring& base) {
    std::vector<Candidate> result;WIN32_FIND_DATAW folder;
    std::wstring root=base+L"mods\\";
    HANDLE folders=FindFirstFileW((root+L"*").c_str(),&folder);
    if(folders==INVALID_HANDLE_VALUE)return result;
    do {
        if(!(folder.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||
            (folder.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||
            !wcscmp(folder.cFileName,L".")||!wcscmp(folder.cFileName,L".."))continue;
        std::wstring directory=root+folder.cFileName+L"\\",path=directory+L"mod.dll";
        DWORD attributes=GetFileAttributesW(path.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&
            !(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
            result.push_back({path,directory});
    } while(FindNextFileW(folders,&folder));
    FindClose(folders);
    std::sort(result.begin(),result.end(),[](const Candidate& a,const Candidate& b){return stableLess(a.path,b.path);});
    return result;
}
inline std::vector<LoadResult> loadMods(const std::wstring& base,HMODULE loader,const HostApi* host) {
    auto candidates=discoverMods(base);std::vector<LoadResult> results;
    // Load every module first. Explicit initialization starts after discovery
    // and loading, so a later Mod is not initialized recursively by a dependency.
    for(auto& candidate:candidates) {
        // A broken mod.dll must be reported in the log, not block the game with
        // a system error dialog. Only this thread's mode changes, and only here.
        DWORD previousMode=0;
        BOOL modeChanged=SetThreadErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX,&previousMode);
        candidate.module=LoadLibraryExW(candidate.path.c_str(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        DWORD error=candidate.module?0:GetLastError();
        if(modeChanged)SetThreadErrorMode(previousMode,nullptr);
        results.push_back({candidate.path,LoadState::failed,error});
        if(candidate.module) {
            HMODULE pinned;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(candidate.module),&pinned);
        }
    }
    std::vector<HMODULE> initialized;
    for(size_t i=0;i<candidates.size();++i) {
        auto& candidate=candidates[i];if(!candidate.module)continue;
        auto initialize=reinterpret_cast<Initialize>(GetProcAddress(candidate.module,"Wots4ModInitialize"));
        if(!initialize) {results[i].state=LoadState::missingEntry;continue;}
        if(std::find(initialized.begin(),initialized.end(),candidate.module)!=initialized.end()) {
            results[i].state=LoadState::duplicate;continue;
        }
        initialized.push_back(candidate.module);
        ModContext context{sizeof(context),modApiVersion,loader,candidate.module,GetModuleHandleW(nullptr),candidate.directory.c_str(),host};
        results[i].state=initialize(&context)?LoadState::accepted:LoadState::refused;
    }
    return results;
}
struct HookClaim {HMODULE owner;uintptr_t address;uint32_t length;};
inline std::vector<HookClaim> hookClaims;
inline BOOL WINAPI claimHook(HMODULE owner,uintptr_t address,uint32_t length) {
    if(!owner||!address||!length||address>UINTPTR_MAX-length)return FALSE;
    for(auto& claim:hookClaims)if(address<claim.address+claim.length&&claim.address<address+length)return FALSE;
    hookClaims.push_back({owner,address,length});return TRUE;
}
inline void WINAPI releaseHookClaims(HMODULE owner) {
    hookClaims.erase(std::remove_if(hookClaims.begin(),hookClaims.end(),[&](const HookClaim& claim){return claim.owner==owner;}),hookClaims.end());
}
// Claims run on the startup/game thread. This is reservation/ownership only;
// it does not yet offer callback chaining or install machine-code hooks.
}
