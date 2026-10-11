// Test Mod. Records how it was initialized; read it back with FixtureModState().
// Built twice: FIXTURE_ACCEPT=1 accepts, FIXTURE_ACCEPT=0 refuses.
#include <wots4/mod_api.h>
#include "wots4_fixture_state.h"
#include <string>
#ifndef FIXTURE_ACCEPT
#define FIXTURE_ACCEPT 1
#endif

static LONG calls;
static bool directoryMatched, reentrantCallForwarded;

static bool isValidContext(const wots4::ModContext* context) {
    return context && context->size == sizeof(*context) && context->version == wots4::modApiVersion &&
        context->module && context->loader && context->game && context->directory && context->host &&
        context->host->version == wots4::modApiVersion;
}

extern "C" BOOL WINAPI FixtureModInitialize(const wots4::ModContext* context) {
    ++calls;
    if (!isValidContext(context)) return FALSE;

    wchar_t path[32768];
    GetModuleFileNameW(context->module, path, ARRAYSIZE(path));
    std::wstring directory(path);
    directory.resize(directory.find_last_of(L'\\') + 1);
    directoryMatched = directory == context->directory;

    // The loader is still initializing; this call must reach the system DirectInput.
    using Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    auto create = reinterpret_cast<Create>(GetProcAddress(context->loader, "DirectInput8Create"));
    IID emptyIid = {};
    void* output = nullptr;
    if (create && FAILED(create(context->game, 0x800, emptyIid, &output, nullptr))) reentrantCallForwarded = true;

    return FIXTURE_ACCEPT ? TRUE : FALSE;
}

extern "C" LONG WINAPI FixtureModState() {
    return fixture::makeState(calls, directoryMatched, reentrantCallForwarded);
}

BOOL WINAPI DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
