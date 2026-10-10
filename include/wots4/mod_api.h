#pragma once
#include <windows.h>
#include <cstdint>
// Shared x86 C ABI. Context strings live through initialization; copy them if
// needed later. Host and modules live to process exit. No STL crosses this ABI.
namespace wots4 {
constexpr uint32_t modApiVersion=1;
struct HostApi {
    uint32_t size,version;
    void (WINAPI* log)(HMODULE owner,const char* message);
    BOOL (WINAPI* claim)(HMODULE owner,uintptr_t address,uint32_t length);
    void (WINAPI* releaseClaims)(HMODULE owner);
};
struct ModContext {
    uint32_t size,version;
    HMODULE loader,module,game;
    const wchar_t* directory;
    const HostApi* host;
};
using Initialize=BOOL (WINAPI*)(const ModContext*);
}
