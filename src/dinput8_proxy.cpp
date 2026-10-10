#include "wots4_mod_loader.h"
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <string>
#include <cstdio>
#include <cstring>

static HMODULE selfModule, realModule;
static INIT_ONCE realOnce = INIT_ONCE_STATIC_INIT;
static FARPROC exports[6];
static volatile LONG coreAttempt;
static std::wstring loaderDirectory;
static void WINAPI loaderLog(HMODULE owner,const char* message) {
    if(!message||loaderDirectory.empty())return;
    HANDLE file=CreateFileW((loaderDirectory+L"mod_loader.log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    char prefix[48];sprintf_s(prefix,"module=%p ",owner);DWORD done;
    WriteFile(file,prefix,(DWORD)strlen(prefix),&done,nullptr);
    WriteFile(file,message,(DWORD)strlen(message),&done,nullptr);
    WriteFile(file,"\r\n",2,&done,nullptr);CloseHandle(file);
}
static const wots4::HostApi hostApi{sizeof(wots4::HostApi),wots4::modApiVersion,loaderLog,wots4::claimHook,wots4::releaseHookClaims};

static BOOL CALLBACK loadReal(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t directory[MAX_PATH];
    auto n = GetSystemDirectoryW(directory, MAX_PATH);
    if (!n || n >= MAX_PATH) return TRUE; // Terminal failure; original API returns error.
    std::wstring path(directory, n);
    path += L"\\dinput8.dll";
    realModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!realModule || realModule == selfModule) return TRUE;
    const char* names[] = {"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject",
        "DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick"};
    for (unsigned i = 0; i < 6; ++i) exports[i] = GetProcAddress(realModule, names[i]);
    return TRUE;
}
static void ensureReal() {
    InitOnceExecuteOnce(&realOnce, loadReal, nullptr, nullptr);
}
static void tryCore() {
    // Reentrant DirectInput calls from core initialization must still forward.
    // One caller initializes; other callers do not wait for core initialization.
    if (InterlockedCompareExchange(&coreAttempt, 1, 0)) return;
    wchar_t file[32768];
    DWORD n = GetModuleFileNameW(selfModule, file, 32768);
    if (!n || n >= 32768) return;
    std::wstring path(file, n);
    auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    path.resize(slash + 1);
    loaderDirectory=path;
    HMODULE pinned;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(selfModule), &pinned);
    auto results=wots4::loadMods(loaderDirectory,selfModule,&hostApi);
    for(auto& result:results) {
        int count=WideCharToMultiByte(CP_UTF8,0,result.path.c_str(),-1,nullptr,0,nullptr,nullptr);
        if(count>0) {
            std::string name(count,0);
            WideCharToMultiByte(CP_UTF8,0,result.path.c_str(),-1,name.data(),count,nullptr,nullptr);
            loaderLog(nullptr,name.c_str());
        }
        char line[80];sprintf_s(line,"load state=%u error=%lu",(unsigned)result.state,result.error);
        loaderLog(nullptr,line);
    }
}
extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE instance, DWORD version,
    REFIID iid, LPVOID* output, LPUNKNOWN outer) {
    ensureReal();
    if (!exports[0]) { if (output) *output = nullptr; return E_FAIL; }
    tryCore();
    using Function = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    return reinterpret_cast<Function>(exports[0])(instance, version, iid, output, outer);
}
extern "C" HRESULT WINAPI ProxyDllCanUnloadNow() {
    ensureReal();
    if (coreAttempt) return S_FALSE; // Entry may have installed callbacks even on failure.
    using Function = HRESULT (WINAPI*)();
    return exports[1] ? reinterpret_cast<Function>(exports[1])() : S_FALSE;
}
extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* output) {
    ensureReal();
    using Function = HRESULT (WINAPI*)(REFCLSID, REFIID, LPVOID*);
    if (!exports[2]) { if (output) *output = nullptr; return E_FAIL; }
    return reinterpret_cast<Function>(exports[2])(clsid, iid, output);
}
extern "C" HRESULT WINAPI ProxyDllRegisterServer() {
    ensureReal();
    using Function = HRESULT (WINAPI*)();
    return exports[3] ? reinterpret_cast<Function>(exports[3])() : E_FAIL;
}
extern "C" HRESULT WINAPI ProxyDllUnregisterServer() {
    ensureReal();
    using Function = HRESULT (WINAPI*)();
    return exports[4] ? reinterpret_cast<Function>(exports[4])() : E_FAIL;
}
extern "C" LPCDIDATAFORMAT WINAPI ProxyGetdfDIJoystick() {
    ensureReal();
    using Function = LPCDIDATAFORMAT (WINAPI*)();
    return exports[5] ? reinterpret_cast<Function>(exports[5])() : nullptr;
}
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) selfModule = module;
    return TRUE; // No load, thread creation, I/O or hooks under the loader lock.
}
