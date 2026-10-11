// dinput8.dll proxy. Every export forwards to the system dinput8.dll; the first
// DirectInput8Create call also loads the Mods (see wots4_mod_loader.h).
#include "wots4_mod_loader.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cstdio>
#include <string>

namespace {

constexpr DWORD maxLongPathChars = 32768;  // Enough for any path, including \\?\ paths.

HMODULE selfModule;
std::wstring loaderDirectory;  // Folder of this DLL, ending with a backslash. Set once.
std::wstring logPath;          // <loaderDirectory>mod_loader.log. Empty until set.
volatile LONG modLoadStarted;  // 1 once the first DirectInput8Create began loading Mods.

// ---------------------------------------------------------------- mod_loader.log

std::string toUtf8(const wchar_t* text, int length) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
    return result;
}

std::string toUtf8(const std::wstring& text) {
    return toUtf8(text.c_str(), static_cast<int>(text.size()));
}

bool startsWithIgnoreCase(const std::wstring& text, const std::wstring& prefix) {
    return text.size() >= prefix.size() &&
        CompareStringOrdinal(text.c_str(), static_cast<int>(prefix.size()),
            prefix.c_str(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

// "C:\Game\mods\a\mod.dll" -> "mods\a\mod.dll". Paths outside the game folder stay as they are.
std::wstring relativeToLoader(const std::wstring& path) {
    return startsWithIgnoreCase(path, loaderDirectory) ? path.substr(loaderDirectory.size()) : path;
}

// The name in brackets: "ModLoader", the folder name of mods\<name>\mod.dll,
// or the file name of any other module.
std::string ownerName(HMODULE owner) {
    if (!owner || owner == selfModule) return "ModLoader";
    wchar_t file[1024];  // Small: Mod threads may log, and their stacks may be small.
    DWORD length = GetModuleFileNameW(owner, file, ARRAYSIZE(file));
    if (!length || length >= ARRAYSIZE(file)) {
        char address[32];
        sprintf_s(address, "module %p", static_cast<void*>(owner));
        return address;
    }
    std::wstring path(file, length);
    const std::wstring modsDirectory = loaderDirectory + L"mods\\";
    if (startsWithIgnoreCase(path, modsDirectory)) {
        size_t nameEnd = path.find(L'\\', modsDirectory.size());
        return toUtf8(path.substr(modsDirectory.size(), nameEnd - modsDirectory.size()));
    }
    return toUtf8(path.substr(path.find_last_of(L'\\') + 1));
}

// Appends "2026-10-11 12:34:56.789 [<source>] <message>". One WriteFile per line
// keeps lines from different threads whole. The file is opened per line so
// the log survives a crash and can be read while the game runs.
void writeLog(const char* source, const std::string& message) {
    if (logPath.empty()) return;
    SYSTEMTIME now;
    GetLocalTime(&now);
    char timestamp[32];
    sprintf_s(timestamp, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    std::string line = timestamp;
    line += '[';
    line += source;
    line += "] ";
    line += message;
    line += "\r\n";

    HANDLE file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

// HostApi::log
void WINAPI hostLog(HMODULE owner, const char* message) {
    if (!message) return;
    writeLog(ownerName(owner).c_str(), message);
}

// "Windows のエラー 193: %1 は有効な Win32 アプリケーションではありません。"
std::string windowsErrorText(DWORD error) {
    char number[48];
    sprintf_s(number, "Windows のエラー %lu", error);
    std::string result = number;
    wchar_t* text = nullptr;
    DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    while (length && (text[length - 1] == L'\r' || text[length - 1] == L'\n' || text[length - 1] == L' '))
        --length;
    if (length) result += ": " + toUtf8(text, static_cast<int>(length));
    LocalFree(text);
    return result;
}

// Why a Mod was not accepted, in the words of docs/MOD_API.md.
const char* notAcceptedReason(wots4::LoadState state) {
    switch (state) {
    case wots4::LoadState::refused: return "Wots4ModInitialize が FALSE を返しました";
    case wots4::LoadState::missingEntry: return "mod.dll に Wots4ModInitialize がありません";
    case wots4::LoadState::failed: return "DLL を読み込めませんでした";
    case wots4::LoadState::duplicate: return "同じモジュールがすでに初期化されていました";
    default: return nullptr;
    }
}

// "mods\a\mod.dll: accepted"
// "mods\b\mod.dll: failed（DLL を読み込めませんでした。Windows のエラー 193: ...）"
void logLoadResult(const wots4::LoadResult& result) {
    std::string line = toUtf8(relativeToLoader(result.path)) + ": " + wots4::loadStateName(result.state);
    if (const char* reason = notAcceptedReason(result.state)) {
        line += "（";
        line += reason;
        if (result.state == wots4::LoadState::failed) line += "。" + windowsErrorText(result.error);
        line += "）";
    }
    writeLog("ModLoader", line);
}

const wots4::HostApi hostApi{sizeof(wots4::HostApi), wots4::modApiVersion,
    hostLog, wots4::claimHook, wots4::releaseHookClaims};

// ---------------------------------------------------------------- Mods

// Loads the Mods on the first call only. Reentrant calls (DirectInput used by a
// Mod's initialization) and concurrent calls return at once without waiting,
// so the caller still forwards to the system DirectInput.
void loadModsOnce() {
    if (InterlockedCompareExchange(&modLoadStarted, 1, 0) != 0) return;
    wchar_t file[maxLongPathChars];
    DWORD length = GetModuleFileNameW(selfModule, file, maxLongPathChars);
    if (!length || length >= maxLongPathChars) return;
    std::wstring path(file, length);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    loaderDirectory = path.substr(0, slash + 1);
    logPath = loaderDirectory + L"mod_loader.log";
    wots4::pinModule(selfModule);

    writeLog("ModLoader", "Mod を読み込みます: " + toUtf8(loaderDirectory) + "mods\\");
    std::vector<wots4::LoadResult> results = wots4::loadMods(loaderDirectory, selfModule, &hostApi);
    if (results.empty()) writeLog("ModLoader", "mods\\<名前>\\mod.dll が1つもありません");
    for (const wots4::LoadResult& result : results) logLoadResult(result);
}

// ---------------------------------------------------------------- system dinput8.dll

// Indices into realExports, in the order of dinput8_proxy.def.
enum RealExport {
    directInput8Create,
    dllCanUnloadNow,
    dllGetClassObject,
    dllRegisterServer,
    dllUnregisterServer,
    getdfDIJoystick,
    realExportCount
};
constexpr const char* realExportNames[realExportCount] = {"DirectInput8Create", "DllCanUnloadNow",
    "DllGetClassObject", "DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick"};

INIT_ONCE realExportsOnce = INIT_ONCE_STATIC_INIT;
FARPROC realExports[realExportCount];

// On failure realExports stay null, and each proxy returns the error the
// original would return. Never retried.
BOOL CALLBACK loadRealExports(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t directory[MAX_PATH];
    UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return TRUE;
    std::wstring path(directory, length);
    path += L"\\dinput8.dll";
    HMODULE real = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!real || real == selfModule) return TRUE;
    for (int i = 0; i < realExportCount; ++i) realExports[i] = GetProcAddress(real, realExportNames[i]);
    return TRUE;
}

// The system dinput8.dll export as Function, or nullptr if it is unavailable.
template <class Function>
Function realExport(RealExport which) {
    InitOnceExecuteOnce(&realExportsOnce, loadRealExports, nullptr, nullptr);
    return reinterpret_cast<Function>(realExports[which]);
}

}  // namespace

// ---------------------------------------------------------------- exports (dinput8_proxy.def)

extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance, DWORD version,
    REFIID iid, LPVOID* output, LPUNKNOWN outer) {
    using Function = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto real = realExport<Function>(directInput8Create);
    if (!real) {
        if (output) *output = nullptr;
        return E_FAIL;
    }
    loadModsOnce();
    return real(instance, version, iid, output, outer);
}

extern "C" HRESULT WINAPI ProxyDllCanUnloadNow() {
    auto real = realExport<HRESULT(WINAPI*)()>(dllCanUnloadNow);
    // Once Mods started loading, they may have installed callbacks even if they failed.
    if (modLoadStarted) return S_FALSE;
    return real ? real() : S_FALSE;
}

extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* output) {
    auto real = realExport<HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*)>(dllGetClassObject);
    if (!real) {
        if (output) *output = nullptr;
        return E_FAIL;
    }
    return real(clsid, iid, output);
}

extern "C" HRESULT WINAPI ProxyDllRegisterServer() {
    auto real = realExport<HRESULT(WINAPI*)()>(dllRegisterServer);
    return real ? real() : E_FAIL;
}

extern "C" HRESULT WINAPI ProxyDllUnregisterServer() {
    auto real = realExport<HRESULT(WINAPI*)()>(dllUnregisterServer);
    return real ? real() : E_FAIL;
}

extern "C" LPCDIDATAFORMAT WINAPI ProxyGetdfDIJoystick() {
    auto real = realExport<LPCDIDATAFORMAT(WINAPI*)()>(getdfDIJoystick);
    return real ? real() : nullptr;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) selfModule = module;
    return TRUE;  // No load, thread creation, I/O or hooks under the loader lock.
}
