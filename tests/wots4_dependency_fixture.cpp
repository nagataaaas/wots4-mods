#include <windows.h>
extern "C" __declspec(dllexport) unsigned DependencyOnly(){return 17;}
BOOL WINAPI DllMain(HMODULE,DWORD,LPVOID){return TRUE;}
