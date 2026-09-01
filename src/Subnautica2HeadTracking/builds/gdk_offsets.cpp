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
    extern const BuildProfile kGdkProfile_20260820;
    extern const BuildProfile kGdkProfile_20260901;
    extern const BuildProfile kGdkProfile_20260714;
    extern const BuildProfile kGdkProfile_20260710;
    extern const BuildProfile kGdkProfile_20260602;
    extern const BuildProfile kGdkProfile_20260524;

    // ---- Xbox/GDK package 0.12.7034.0 (PE TS 0x7d471cdc) ----
    //
    // RVAs only. Every struct offset below was re-read from this build via
    // scripts/derive_struct_offsets.py against the dump, not carried over, and
    // every value matches both gdk-wingdk-20260820 and the same-day Steam
    // profile: PlayerCameraManager 0x378, Pawn 0x300, RootComponent 0x1c0,
    // CapsuleComponent 0x350, bShowMouseCursor 0x564 mask 0x1. The control
    // passed, PlayerCameraManager being the displacement GPV itself
    // dereferences.
    //
    // Do NOT route this build by package version. Get-AppxPackage reported the
    // previous 0.12.3362.0 for the whole of the update AND for a freshly
    // reinstalled copy, across three different EXE sizes. The PE fingerprint
    // was the only field that told the truth at every step.
    const BuildProfile kGdkProfile_20260901 = {
        /* Name        */ "gdk-wingdk-20260901",
        /* Fingerprint */ { 0x7d471cdcu, 0x0cbb0000u, 0x0c607b37u },
        /* Offsets     */ {
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
            /* USceneComponentLayout */ {
                /* kComponentToWorldRotation    */ 0x1f0,
                /* kComponentToWorldTranslation */ 0x210,
                /* kComponentToWorldScale       */ 0x230,
            },
            // kCapsule is ACharacter::CapsuleComponent and kCapsuleAlias is
            // AActor::RootComponent, both read off the property table. Nothing
            // in this group is read at runtime any more (mask compensation
            // discovers components structurally), so the rest are carried.
            /* PawnSlots */ {
                /* kCapsule              */ 0x350,
                /* kCapsuleAlias         */ 0x1c0,
                /* kPrimaryMesh          */ 0x340,
                /* kMeshArrayBegin       */ 0x7d8,
                /* kMeshArrayStride      */ 0x008,
                /* kMeshArrayCount       */ 6,
                /* kCameraMountComponent */ 0x868,
            },
            /* PlayerController */ {
                /* kShowMouseCursorOffset */ 0x564,
                /* kShowMouseCursorMask   */ 0x1u,
                /* kPawn                  */ 0x300,
                /* kPlayerCameraManager   */ 0x378,
            },
            // Relocated via scripts/derive_globals.py against the dump:
            // allocator sig unique hit (fn 0x01502ae0), the `mov [rip],rax` @
            // fn+0x18e. FName decoder pair at 0x0128f560/0x0128f5d0 agree on
            // the pool and pool - init_flag == 0x267 holds.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0bbc4d00ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0bae0a80ULL,
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
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
            // Relocated for GDK package 0.12.7034.0 via scripts/derive_rvas.py
            // against a scripts/dump-running-exe.ps1 -RebaseTo 0 dump. GPV
            // anchored on the relocation-free prologue signature, 1 hit.
            /* kGetPlayerViewPointRva */ 0x0417fd40ULL,
            /* kKnownCallerRvas */ {{
                0,
                // 1: render caller. Containing fn 0x03f03d30 is the
                // FMinimalViewInfo builder - the only `call [reg+0x830]` site
                // in the image with the builder window around it:
                // `call [rax+0x800]` (PCM FOV vfn) -> `movss [r14],xmm0`
                // (FOV store, r14 = base+0x30) -> `lea r8,[rdi+0x18]`
                // (out_Rotation) -> `call [rax+0x830]` (GPV).
                0x03f03f77ULL,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
            }},
        },
    };

    // ---- Xbox/GDK package 0.12.3362.0 (PE TS 0x0adc0a4a) ----
    // Same game patch as steam-win64-20260820, packaged separately. Derived
    // from a runtime dump (scripts/dump-running-exe.ps1 -RebaseTo 0, which
    // preserves the PE CheckSum) fed to scripts/derive_rvas.py +
    // derive_globals.py.
    //
    // This patch moved struct field offsets as well as RVAs - AActor grew
    // 0x10 bytes after RootComponent, so every field past that point in
    // AActor and its subclasses shifts by +0x10. Each one was re-derived
    // against the GDK dump independently rather than copied from the Steam
    // profile, and all of them agree with it: Pawn 0x300,
    // PlayerCameraManager 0x378 (read out of this build's own GPV),
    // bShowMouseCursor 0x564 (its generated SetBitFunc is
    // `or dword ptr [rcx+0x564], 1`), CapsuleComponent 0x350,
    // RootComponent unmoved at 0x1c0, and ComponentToWorld unmoved at 0x1f0.
    const BuildProfile kGdkProfile_20260820 = {
        /* Name        */ "gdk-wingdk-20260820",
        /* Fingerprint */ { 0x0adc0a4au, 0x0cbab000u, 0x0c5f9b4cu },
        /* Offsets     */ {
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
            // Unchanged. GetComponentTransform (RVA 0x03bea210 this build)
            // still reads the FTransform at [this+0x1f0].
            /* USceneComponentLayout */ {
                /* kComponentToWorldRotation    */ 0x1f0,
                /* kComponentToWorldTranslation */ 0x210,
                /* kComponentToWorldScale       */ 0x230,
            },
            // Rebased by the AActor +0x10 delta; see the header comment.
            /* PawnSlots */ {
                /* kCapsule              */ 0x350,
                /* kCapsuleAlias         */ 0x1c0,
                /* kPrimaryMesh          */ 0x340,
                /* kMeshArrayBegin       */ 0x7d8,
                /* kMeshArrayStride      */ 0x008,
                /* kMeshArrayCount       */ 6,
                /* kCameraMountComponent */ 0x868,
            },
            /* PlayerController */ {
                /* kShowMouseCursorOffset */ 0x564,
                /* kShowMouseCursorMask   */ 0x1u,
                /* kPawn                  */ 0x300,
                /* kPlayerCameraManager   */ 0x378,
            },
            // Relocated via scripts/derive_globals.py against the dump:
            // allocator sig unique hit (fn 0x01502520), FName decoder pair at
            // 0x0128ef00/0x0128ef70 agreeing on the pool with
            // pool - init_flag == 0x267.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0bbbfb80ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0badb900ULL,
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
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
            // GPV anchored on the relocation-free prologue signature, 1 hit.
            // The GPV vtable slot moved with this patch on GDK too
            // (0x828 -> 0x830, and the PCM FOV vfn 0x7f8 -> 0x800), so the
            // render-caller search has to run against the new displacements.
            /* kGetPlayerViewPointRva */ 0x0417fc40ULL,
            /* kKnownCallerRvas */ {{
                0,
                // 1: render caller. Containing fn 0x03f03c30 is the
                // FMinimalViewInfo builder - the only `call [reg+0x830]` site
                // in the image with the builder window around it:
                // `call [rax+0x800]` -> `movss [r14],xmm0` (r14 = base+0x30)
                // -> `lea r8,[rdi+0x18]` -> `call [rax+0x830]`.
                0x03f03e77ULL,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
            }},
        },
    };

    // ---- Xbox/GDK package 0.12.1347.0 (PE TS 0x7f8917fa) ----
    // Derived from a runtime dump (scripts/dump-running-exe.ps1 -RebaseTo 0,
    // which preserves the PE CheckSum) fed to scripts/derive_rvas.py +
    // derive_globals.py. SizeOfImage is identical to 0.11.9503.0 and the
    // .data globals (ObjObjects/FNamePool) did not move; only the code RVAs
    // shifted.
    const BuildProfile kGdkProfile_20260714 = {
        /* Name        */ "gdk-wingdk-20260714",
        /* Fingerprint */ { 0x7f8917fau, 0x0cb09000u, 0x0c55b965u },
        /* Offsets     */ {
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
            // ObjObjects: allocator sig unique hit (fn 0x014fe850), the
            // `mov [rip],rax` @ fn+0x18e. FNamePool: both decoder variants
            // (0x0128b2d0 / 0x0128b340) agree, pool - init_flag == 0x267.
            // Both globals unchanged from gdk-wingdk-20260710.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0bb2e500ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0ba4a280ULL,
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
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
            // GPV: 32-byte prologue signature, unique hit (-0x160 from 20260710).
            /* kGetPlayerViewPointRva */ 0x0417c5e0ULL,
            /* kKnownCallerRvas */ {{
                0,
                // Render caller: fn 0x03f005c0's call [pcm+0x828] retRVA. Of 3
                // PCM:YES candidates only this one shows the FMinimalViewInfo
                // builder window anchored at the +0x7f8 call: call [rax+0x7f8]
                // (PCM FOV vfn) -> movss [r14],xmm0 (FOV store) ->
                // lea r8,[rdi+0x18] (out_Rotation = out_Location +
                // kRotationStride) -> call [rax+0x828] (GPV). The other two
                // lack a +0x7f8 call before the GPV call entirely.
                0x03f00807ULL,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
            }},
        },
    };

    // ---- Xbox/GDK package 0.11.9503.0 (PE TS 0x17f2983d) ----
    // Derived from a runtime dump (scripts/dump-running-exe.ps1 -RebaseTo 0,
    // which preserves the PE CheckSum) fed to scripts/derive_rvas.py +
    // derive_globals.py. Same Steam-derived signatures hit the GDK binary 1:1
    // (same UE5.6.1 codegen), so no Ghidra or runtime caller-capture needed.
    const BuildProfile kGdkProfile_20260710 = {
        /* Name        */ "gdk-wingdk-20260710",
        /* Fingerprint */ { 0x17f2983du, 0x0cb09000u, 0x0c563577u },
        /* Offsets     */ {
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
            // ObjObjects: allocator sig unique hit (fn 0x014fe850), the
            // `mov [rip],rax` @ fn+0x18e. FNamePool: both decoder variants
            // (0x0128b270 / 0x0128b2e0) agree, pool - init_flag == 0x267.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0bb2e500ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0ba4a280ULL,
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
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
            // GPV: 32-byte prologue signature, unique hit.
            /* kGetPlayerViewPointRva */ 0x0417c740ULL,
            /* kKnownCallerRvas */ {{
                0,
                // Render caller: fn 0x03f00760's call [pcm+0x828] retRVA.
                // The FMinimalViewInfo builder - call [pcm+0x7f8] (FOV vfn) ->
                // movss [r14],xmm0 (FOV store) -> lea r8,[rdi+0x18]
                // (out_Rotation = out_Location + kRotationStride) ->
                // call [pcm+0x828] (GPV). The other two PCM-deref candidates
                // lack the +0x7f8-then-+0x828 window.
                0x03f009a7ULL,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
            }},
        },
    };

    // ---- Xbox/GDK package 0.11.5506.0 (PE TS 0x0bf5cd18) ----
    // RVAs derived from a runtime dump (scripts/dump-running-exe.ps1) fed to
    // scripts/derive_rvas.py + derive_globals.py (deterministic PE signature
    // scan - see steam_offsets.cpp kSteamProfile_20260601 for the method).
    const BuildProfile kGdkProfile_20260602 = {
        /* Name        */ "gdk-wingdk-20260602",
        /* Fingerprint */ { 0x0bf5cd18u, 0x0ccd6000u, 0x0c72fe83u },
        /* Offsets     */ {
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
            // ObjObjects: allocator sig unique hit (fn 0x015031c0), the
            // `mov [rip],rax` @ fn+0x18e. FNamePool: both decoder variants
            // (0x0128ffc0 / 0x01290030) agree, pool - init_flag == 0x267.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0bcf0c00ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0bc0c980ULL,
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
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
            // GPV: 32-byte prologue signature, unique hit (+0xb0 from 20260524).
            /* kGetPlayerViewPointRva */ 0x04183300ULL,
            /* kKnownCallerRvas */ {{
                0,
                // Render caller: fn 0x03f07330's call [pcm+0x828] retRVA.
                // Confirmed three ways: structural double-vfn match (2nd of 4
                // candidates, PCM[+0x368] deref), +0xd0 from the 20260524
                // render fn 0x03f07260, and that old fn's 32-byte prologue
                // signature hits the new dump exactly once at 0x03f07330.
                0x03f07577ULL,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
            }},
        },
    };

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
            // Same UE5.6.1 FMinimalViewInfo ABI as Steam. The GDK render caller
            // (FUN_143f07260) runs the identical 0x7f8-then-0x828 double-vfn
            // sequence, so it writes FOV@+0x30 before the GPV call too.
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
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
