#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <string>
#include <cstdio>
int wmain() {
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);
    std::wstring base(exe);base.resize(base.find_last_of(L"\\")+1);
    HMODULE proxy=GetModuleHandleW(L"dinput8.dll");if(!proxy)return 1;
    wchar_t path[32768];GetModuleFileNameW(proxy,path,32768);
    if(_wcsicmp(path,(base+L"dinput8.dll").c_str()))return 1;
    puts("PASS automatic adjacent proxy import");
    const wchar_t* mods[]={L"a_first",L"b_refused",L"z_last"};
    for(auto name:mods)if(GetModuleHandleW((base+L"mods\\"+name+L"\\mod.dll").c_str()))return 1;
    puts("PASS no Mod initialization under DllMain");
    IDirectInput8W* input=nullptr;
    if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),0x800,IID_IDirectInput8W,(void**)&input,nullptr))||!input)return 1;
    input->Release();puts("PASS real DirectInput interface despite refused and invalid Mods");
    for(auto name:mods) {
        HMODULE module=GetModuleHandleW((base+L"mods\\"+name+L"\\mod.dll").c_str());
        auto state=module?(LONG(WINAPI*)())GetProcAddress(module,"FixtureModState"):nullptr;
        if(!state||state()!=0x10101)return 1;
        puts("PASS independent Mod initialized once, correct directory, recursive DirectInput forwarded");
    }
    if(GetModuleHandleW((base+L"mods\\a_first\\helper.dll").c_str()))return 1;
    puts("PASS unreferenced helper DLL remains unloaded");
    return 0;
}
