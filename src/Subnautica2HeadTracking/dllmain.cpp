#include <windows.h>
#include "headtracking_mod.h"

// dxgi.dll proxy. Each export here is a forwarder to the matching symbol in
// dxgi_orig.dll - a copy of the user's C:\Windows\System32\dxgi.dll planted
// next to the game exe by install.cmd. The Windows loader resolves forwarders
// at LoadLibrary time, so the game (and our own kiero/ImGui DXGI imports)
// transparently reach the real DXGI through us.
//
// Re-derive the export list with `dumpbin /EXPORTS %SystemRoot%\System32\dxgi.dll`
// after a Windows feature update if a new symbol appears; missing forwarders
// would crash the game on first use of that symbol.

#pragma comment(linker, "/export:ApplyCompatResolutionQuirking=dxgi_orig.ApplyCompatResolutionQuirking")
#pragma comment(linker, "/export:CompatString=dxgi_orig.CompatString")
#pragma comment(linker, "/export:CompatValue=dxgi_orig.CompatValue")
#pragma comment(linker, "/export:CreateDXGIFactory=dxgi_orig.CreateDXGIFactory")
#pragma comment(linker, "/export:CreateDXGIFactory1=dxgi_orig.CreateDXGIFactory1")
#pragma comment(linker, "/export:CreateDXGIFactory2=dxgi_orig.CreateDXGIFactory2")
#pragma comment(linker, "/export:DXGID3D10CreateDevice=dxgi_orig.DXGID3D10CreateDevice")
#pragma comment(linker, "/export:DXGID3D10CreateLayeredDevice=dxgi_orig.DXGID3D10CreateLayeredDevice")
#pragma comment(linker, "/export:DXGID3D10GetLayeredDeviceSize=dxgi_orig.DXGID3D10GetLayeredDeviceSize")
#pragma comment(linker, "/export:DXGID3D10RegisterLayers=dxgi_orig.DXGID3D10RegisterLayers")
#pragma comment(linker, "/export:DXGIDeclareAdapterRemovalSupport=dxgi_orig.DXGIDeclareAdapterRemovalSupport")
#pragma comment(linker, "/export:DXGIDisableVBlankVirtualization=dxgi_orig.DXGIDisableVBlankVirtualization")
#pragma comment(linker, "/export:DXGIDumpJournal=dxgi_orig.DXGIDumpJournal")
#pragma comment(linker, "/export:DXGIGetDebugInterface1=dxgi_orig.DXGIGetDebugInterface1")
#pragma comment(linker, "/export:DXGIReportAdapterConfiguration=dxgi_orig.DXGIReportAdapterConfiguration")
#pragma comment(linker, "/export:PIXBeginCapture=dxgi_orig.PIXBeginCapture")
#pragma comment(linker, "/export:PIXEndCapture=dxgi_orig.PIXEndCapture")
#pragma comment(linker, "/export:PIXGetCaptureState=dxgi_orig.PIXGetCaptureState")
#pragma comment(linker, "/export:SetAppCompatStringPointer=dxgi_orig.SetAppCompatStringPointer")
#pragma comment(linker, "/export:UpdateHMDEmulationStatus=dxgi_orig.UpdateHMDEmulationStatus")

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
