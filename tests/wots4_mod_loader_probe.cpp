#include "wots4_mod_loader.h"
#include <cstdio>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;std::wstring base=argv[1];if(base.back()!=L'\\')base+=L"\\";
    unsigned tests=0,failures=0;
    auto check=[&](const char* name,bool ok){++tests;if(!ok)++failures;printf("%s %s\n",ok?"PASS":"FAIL",name);};
    auto candidates=wots4::discoverMods(base);
    check("one-level discovery selects mod.dll only",candidates.size()==5);
    check("stable alphabetical paths",candidates.size()==5&&
        candidates[0].path.find(L"a_first")!=std::wstring::npos&&candidates[4].path.find(L"z_last")!=std::wstring::npos);
    wots4::HostApi host{sizeof(host),1,nullptr,wots4::claimHook,wots4::releaseHookClaims};
    auto results=wots4::loadMods(base,GetModuleHandleW(nullptr),&host);
    unsigned accepted=0,refused=0,missingEntry=0,failed=0;
    for(auto& result:results) {
        accepted+=result.state==wots4::LoadState::accepted;refused+=result.state==wots4::LoadState::refused;
        missingEntry+=result.state==wots4::LoadState::missingEntry;failed+=result.state==wots4::LoadState::failed;
        if(result.state==wots4::LoadState::accepted||result.state==wots4::LoadState::refused) {
            HMODULE module=GetModuleHandleW(result.path.c_str());
            auto state=(LONG(WINAPI*)())GetProcAddress(module,"FixtureModState");
            check("each same-basename DLL gets own context and one initialization",state&&state()==0x101);
        }
        if(result.state==wots4::LoadState::failed)check("failed DLL has Windows error",result.error!=0);
    }
    check("two Mods accepted, one refuses, missing entry and invalid DLL reported",accepted==2&&refused==1&&missingEntry==1&&failed==1);
    check("unreferenced helper DLL not loaded",!GetModuleHandleW((base+L"mods\\a_first\\helper.dll").c_str()));
    check("dependency-only folder is not loaded",!GetModuleHandleW((base+L"mods\\dependency\\helper.dll").c_str()));
    check("nested DLL not loaded",!GetModuleHandleW((base+L"mods\\a_first\\nested\\ignored.dll").c_str()));
    auto first=(HMODULE)1,second=(HMODULE)2;
    check("first hook reservation",wots4::claimHook(first,0x1000,8));
    check("overlap from another Mod rejected",!wots4::claimHook(second,0x1004,8));
    check("own duplicate rejected",!wots4::claimHook(first,0x1000,8));
    check("adjacent range allowed",wots4::claimHook(second,0x1008,8));
    check("overflow refused",!wots4::claimHook(first,UINTPTR_MAX-2,8));
    wots4::releaseHookClaims(first);check("release keeps other owner",wots4::hookClaims.size()==1&&wots4::hookClaims[0].owner==second);
    check("released range can be reserved",wots4::claimHook(second,0x1000,8));
    wots4::releaseHookClaims(second);check("all test reservations removed",wots4::hookClaims.empty());
    printf("RESULT tests=%u failures=%u\n",tests,failures);return failures?1:0;
}
