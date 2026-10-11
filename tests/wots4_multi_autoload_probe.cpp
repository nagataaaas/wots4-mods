// Runs inside the folder made by make_loader_tree.cmake. Imports dinput8.dll
// normally, so Windows picks the ModLoader next to the exe, as the game does.
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "wots4_fixture_state.h"
#include <string>
#include <cstdio>

static int fail(const char* what) {
    printf("FAIL %s\n", what);
    return 1;
}

int wmain() {
    wchar_t exe[32768];
    GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
    std::wstring base(exe);
    base.resize(base.find_last_of(L'\\') + 1);
    auto modModule = [&](const wchar_t* name) {
        return GetModuleHandleW((base + L"mods\\" + name + L"\\mod.dll").c_str());
    };
    const wchar_t* mods[] = {L"a_first", L"b_refused", L"z_last"};

    HMODULE proxy = GetModuleHandleW(L"dinput8.dll");
    if (!proxy) return fail("dinput8.dll is not loaded");
    wchar_t proxyPath[32768];
    GetModuleFileNameW(proxy, proxyPath, ARRAYSIZE(proxyPath));
    if (_wcsicmp(proxyPath, (base + L"dinput8.dll").c_str()) != 0)
        return fail("dinput8.dll is not the ModLoader next to the exe");
    puts("PASS automatic adjacent proxy import");

    for (auto name : mods) {
        if (modModule(name)) return fail("a Mod was loaded before DirectInput8Create (under DllMain)");
    }
    puts("PASS no Mod initialization under DllMain");

    IDirectInput8W* input = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), 0x800, IID_IDirectInput8W,
            reinterpret_cast<void**>(&input), nullptr)) || !input)
        return fail("DirectInput8Create did not return the system interface");
    input->Release();
    puts("PASS real DirectInput interface despite refused and invalid Mods");

    constexpr LONG expectedState = fixture::makeState(1, true, true);
    for (auto name : mods) {
        HMODULE module = modModule(name);
        auto state = module
            ? reinterpret_cast<fixture::StateFunction>(GetProcAddress(module, "FixtureModState"))
            : nullptr;
        if (!state) return fail("a fixture Mod was not loaded");
        if (state() != expectedState) {
            printf("  %ls: state 0x%05lx, expected 0x%05lx\n", name, state(), expectedState);
            return fail("fixture Mod was not initialized as expected");
        }
        puts("PASS independent Mod initialized once, correct directory, recursive DirectInput forwarded");
    }

    if (GetModuleHandleW((base + L"mods\\a_first\\helper.dll").c_str()))
        return fail("helper.dll was loaded though no Mod references it");
    puts("PASS unreferenced helper DLL remains unloaded");

    std::string log;
    if (FILE* file = _wfopen((base + L"mod_loader.log").c_str(), L"rb")) {
        char buffer[4096];
        for (size_t n; (n = fread(buffer, 1, sizeof(buffer), file)) > 0;) log.append(buffer, n);
        fclose(file);
    }
    const char* expectedLines[] = {"[ModLoader] mods\\a_first\\mod.dll: accepted\r\n",
        "[ModLoader] mods\\b_refused\\mod.dll: refused (Wots4ModInitialize returned FALSE)\r\n",
        "[ModLoader] mods\\invalid\\mod.dll: failed (could not load the DLL; Windows error 193",
        "[ModLoader] mods\\missing_entry\\mod.dll: missingEntry (mod.dll does not export Wots4ModInitialize)\r\n",
        "[a_first] fixture initializing\r\n", "[z_last] fixture initializing\r\n"};
    for (const char* expected : expectedLines) {
        if (log.find(expected) == std::string::npos) {
            printf("  missing in mod_loader.log: %s\n%s", expected, log.c_str());
            return fail("mod_loader.log does not name each Mod with its result and log lines");
        }
    }
    puts("PASS mod_loader.log names each Mod with its result and log lines");
    return 0;
}
