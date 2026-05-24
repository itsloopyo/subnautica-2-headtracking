#include "build_profile.h"

// Xbox / Microsoft Store (GDK) build of Subnautica 2
// (Subnautica2-WinGDK-Shipping.exe). The on-disk EXE sits behind a Process
// Trust ACE (S-1-19-512-4096) that blocks normal file read, so the values
// below were derived from a runtime memory dump produced by
// scripts/dump-running-exe.ps1 (rebased back to 0x140000000), then fed to
// Ghidra via:
//
//   pixi run ghidra-script rederive_all \
//     -ProgramExe scratch/Subnautica2-WinGDK-Shipping-dumped.exe \
//     -ProjectName "Subnautica2-GDK"
//
// The Steam and GDK builds are produced from the same UE5.6.1 source tree,
// so the per-instance struct offsets (UObject/USceneComponent/APlayerController/
// FName) carry across 1:1. Only RVAs and the fingerprint move.
//
// To add support for a new GDK build: do not edit kGdkProfile_<existing date>
// in place. Append a new `extern const BuildProfile kGdkProfile_YYYYMMDD = {
// ... };` definition below, register it in build_registry.cpp's
// kKnownProfiles array, and keep the older profiles for users who haven't
// updated yet. The PE fingerprint routes each user to the right profile
// automatically.

namespace Subnautica2HeadTracking::builds
{
    extern const BuildProfile kGdkProfile_20260524;

    // ---- Xbox/GDK package 0.11.4707.0 (PE TS 0xb6cdc688) ----
    const BuildProfile kGdkProfile_20260524 = {
        /* Name        */ "gdk-wingdk-20260524",
        /* Fingerprint */ { 0xb6cdc688u, 0x0cccd000u, 0x0c727056u },
        /* Offsets     */ {
            // Filled in once rederive_all.py finishes against the dump.
            // All zeros below = "not yet discovered". The mod will refuse
            // to install hooks against a profile whose kGetPlayerViewPointRva
            // is zero (see build_registry.cpp), so this file safely compiles
            // and ships with placeholders until discovery is complete.
            /* ZRegInfo */ {
                /* kAUWEPlayerCameraManager        */ 0,
                /* kUWEPlayerCameraManagerSettings */ 0,
                /* kAPlayerCameraManager           */ 0,
                /* kMinimalViewInfo                */ 0,
                /* kUWECameraPackage               */ 0,
            },
            /* ZConstruct */ {
                /* kAUWEPlayerCameraManager */ 0,
                /* kAPlayerCameraManager    */ 0,
                /* kMinimalViewInfo         */ 0,
                /* kUWECameraPackage        */ 0,
            },
            /* UECodeGen */ {
                /* kConstructUClass_thunk */ 0,
                /* kConstructUClass       */ 0,
                /* kConstructUPackage     */ 0,
            },
            /* UWEPlayerCameraManager */ {
                /* kInstanceSize_Bytes */ 0,
                /* kClassFlags         */ 0,
                /* kStaticsRva         */ 0,
            },
            // Struct layouts carry across from Steam (same UE5.6.1 ABI).
            /* USceneComponentLayout */ {
                /* kComponentToWorldRotation    */ 0x1f0,
                /* kComponentToWorldTranslation */ 0x210,
                /* kComponentToWorldScale       */ 0x230,
            },
            /* PawnSlots */ {
                /* kCapsule              */ 0x340,
                /* kCapsuleAlias         */ 0x1c0,
                /* kPrimaryMesh          */ 0x330,
                /* kMeshArrayBegin       */ 0x7c8,
                /* kMeshArrayStride      */ 0x008,
                /* kMeshArrayCount       */ 6,
                /* kCameraMountComponent */ 0x858,
            },
            /* PlayerController */ {
                /* kShowMouseCursorOffset */ 0x554,
                /* kShowMouseCursorMask   */ 0x1u,
                /* kPawn                  */ 0x2f0,
                /* kPlayerCameraManager   */ 0x368,
            },
            // GUObjectArray base identified by F3 sweep probe over the
            // allocator's write cluster: candidate 0x0bce8780 reads chunks
            // as a heap pointer and NumElements@+0x14=227875 - matches the
            // expected UE5 UObject count for SN2. Confirmed live on
            // 2026-05-24. Steam scanner had picked a false positive in a
            // hash-table candidate at similar RVA.
            // FNamePool base picked by Blocks[] having 8 consecutive valid
            // heap pointers in the 0x7ff... runtime range.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0bce8780ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                // FNamePool identified by find_fnamepool2.py signature scan
                // (SHR ,0x10 + SHR ,6 + .data global access). The decoder
                // function pattern `ADD reg, qword ptr [R8 + RDX*8 + 0x10]`
                // with R8 loaded from `LEA [0x14bc04500]` is the
                // `Blocks[id>>16]` access in ResolveFName. Confirmed on
                // 2026-05-24 - prior shape-based scanner pick (0x0b9afea8)
                // was a false positive in an unrelated pointer array.
                /* kFNamePool        */ 0x0bc04500ULL,
                /* kFNamePoolBlocks  */ 0x10,
                /* kClassPrivate     */ 0x10,
                /* kNamePrivate      */ 0x18,
                /* kOuterPrivate     */ 0x20,
            },
            /* VTables */ {
                /* kCapsuleComponent      */ 0,
                /* kSkeletalMeshComponent */ 0,
                /* kCameraMountComponent  */ 0,
            },
            // Discovered via rederive_all.py against the rebased dump on
            // 2026-05-24.
            /* kGetPlayerViewPointRva */ 0x04183250ULL,
            // Captured via F2 (inject-mode 0) + caller-summary dump from a
            // ~60s Xbox gameplay session on 2026-05-24, then mapped to
            // containing functions via identify_gpv_callers_gdk.py.
            // Slot [1] is the render caller used by inject-mode 2 (default):
            // 0x03f074a7 sits in FUN_143f07260, which dereferences the
            // PlayerCameraManager pointer at [controller + 0x368] and
            // invokes two camera vfns in sequence (slot 0x7f8 then 0x828
            // = GPV). Confirmed by toggling F7: only this slot decouples
            // view-rotation from aim during Xbox gameplay.
            /* kKnownCallerRvas */ {{
                0x06131bf8ULL,  // 0: dominant per-actor (~93/frame)
                0x03f074a7ULL,  // 1: <- RENDER (CameraManager double-vfn caller)
                0x041809d7ULL,  // 2: mid (~8/frame, FMinimalViewInfo-shape pattern but NOT the active render path)
                0x068555b5ULL,  // 3: mid (~8/frame)
                0x028f81d8ULL,  // 4: per-frame
                0x061f02b9ULL,  // 5: per-frame
                0x04e99361ULL,  // 6: per-frame
                0x03f0d7ddULL,  // 7: per-frame
                0x03d5f36cULL,  // 8: per-frame
                0x066ddfe5ULL,  // 9: rare (~1/frame)
                0x04e8b890ULL,  // 10: very rare / one-shot (154 hits in 60s)
            }},
        },
    };
}
