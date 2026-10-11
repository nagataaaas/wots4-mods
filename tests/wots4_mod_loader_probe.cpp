// Calls the loader directly on the folder made by make_loader_tree.cmake,
// then checks hook claims. Usage: wots4_mod_loader_probe <tree folder>
#include "wots4_mod_loader.h"
#include "wots4_fixture_state.h"
#include <cstdio>

using wots4::LoadState;

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        puts("usage: wots4_mod_loader_probe <folder made by make_loader_tree.cmake>");
        return 2;
    }
    std::wstring base = argv[1];
    if (base.back() != L'\\') base += L'\\';

    unsigned tests = 0, failures = 0;
    auto check = [&](const char* name, bool ok) {
        ++tests;
        if (!ok) ++failures;
        printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    };
    auto isLoaded = [&](const wchar_t* relativePath) {
        return GetModuleHandleW((base + relativePath).c_str()) != nullptr;
    };

    // Discovery
    auto candidates = wots4::discoverMods(base);
    check("one-level discovery selects mod.dll only", candidates.size() == 5);
    check("stable alphabetical paths", candidates.size() == 5 &&
        candidates[0].path.find(L"a_first") != std::wstring::npos &&
        candidates[4].path.find(L"z_last") != std::wstring::npos);

    // Loading. The probe exe stands in for the loader and has no DirectInput8Create,
    // so the fixture makes no reentrant call.
    constexpr LONG expectedState = fixture::makeState(1, true, false);
    wots4::HostApi host{sizeof(host), wots4::modApiVersion, nullptr, wots4::claimHook, wots4::releaseHookClaims};
    auto results = wots4::loadMods(base, GetModuleHandleW(nullptr), &host);
    unsigned accepted = 0, refused = 0, missingEntry = 0, failed = 0;
    for (const auto& result : results) {
        printf("  %ls: %s", result.path.c_str() + base.size(), wots4::loadStateName(result.state));
        if (result.state == LoadState::failed) printf(" (Windows error %lu)", result.error);
        printf("\n");
        accepted += result.state == LoadState::accepted;
        refused += result.state == LoadState::refused;
        missingEntry += result.state == LoadState::missingEntry;
        failed += result.state == LoadState::failed;
        if (result.state == LoadState::accepted || result.state == LoadState::refused) {
            HMODULE module = GetModuleHandleW(result.path.c_str());
            auto state = reinterpret_cast<fixture::StateFunction>(GetProcAddress(module, "FixtureModState"));
            check("each same-basename DLL gets own context and one initialization",
                state && state() == expectedState);
        }
        if (result.state == LoadState::failed) check("failed DLL has Windows error", result.error != 0);
    }
    check("two Mods accepted, one refuses, missing entry and invalid DLL reported",
        accepted == 2 && refused == 1 && missingEntry == 1 && failed == 1);
    check("unreferenced helper DLL not loaded", !isLoaded(L"mods\\a_first\\helper.dll"));
    check("dependency-only folder is not loaded", !isLoaded(L"mods\\dependency\\helper.dll"));
    check("nested DLL not loaded", !isLoaded(L"mods\\a_first\\nested\\ignored.dll"));

    // Hook claims. The owners are fake handles; claims never dereference them.
    auto first = reinterpret_cast<HMODULE>(1), second = reinterpret_cast<HMODULE>(2);
    check("first hook reservation", wots4::claimHook(first, 0x1000, 8));
    check("overlap from another Mod rejected", !wots4::claimHook(second, 0x1004, 8));
    check("own duplicate rejected", !wots4::claimHook(first, 0x1000, 8));
    check("adjacent range allowed", wots4::claimHook(second, 0x1008, 8));
    check("overflow refused", !wots4::claimHook(first, UINTPTR_MAX - 2, 8));
    wots4::releaseHookClaims(first);
    check("release keeps other owner", wots4::hookClaims.size() == 1 && wots4::hookClaims[0].owner == second);
    check("released range can be reserved", wots4::claimHook(second, 0x1000, 8));
    wots4::releaseHookClaims(second);
    check("all test reservations removed", wots4::hookClaims.empty());

    printf("RESULT %u passed, %u failed\n", tests - failures, failures);
    return failures ? 1 : 0;
}
