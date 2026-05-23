#include <windows.h>
#include "headtracking_mod.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Subnautica2HeadTracking::Initialize(hModule);
    } else if (reason == DLL_PROCESS_DETACH) {
        Subnautica2HeadTracking::Shutdown();
    }
    return TRUE;
}
