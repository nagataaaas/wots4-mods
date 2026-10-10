#include <wots4/mod_api.h>
#include <string>
#ifndef FIXTURE_ACCEPT
#define FIXTURE_ACCEPT 1
#endif
static LONG calls,valid,recursive;
extern "C" BOOL WINAPI FixtureModInitialize(const wots4::ModContext* context) {
    ++calls;
    if(!context||context->size!=sizeof(*context)||context->version!=wots4::modApiVersion||
        !context->module||!context->loader||!context->game||!context->directory||!context->host||
        context->host->version!=wots4::modApiVersion)return FALSE;
    wchar_t path[32768];GetModuleFileNameW(context->module,path,32768);
    std::wstring directory(path);directory.resize(directory.find_last_of(L"\\")+1);
    valid=directory==context->directory;
    using Create=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,IUnknown*);
    auto create=(Create)GetProcAddress(context->loader,"DirectInput8Create");
    IID unknown={};void* output=nullptr;
    if(create&&FAILED(create(context->game,0x800,unknown,&output,nullptr)))recursive=1;
    return FIXTURE_ACCEPT?TRUE:FALSE;
}
extern "C" LONG WINAPI FixtureModState(){return calls|(valid<<8)|(recursive<<16);}
BOOL WINAPI DllMain(HMODULE,DWORD,LPVOID){return TRUE;}
