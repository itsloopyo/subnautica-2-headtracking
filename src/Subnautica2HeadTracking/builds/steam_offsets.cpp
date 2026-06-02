#include "build_profile.h"

// Steam Win64 build of Subnautica 2 (Subnautica2-Win64-Shipping.exe, UE 5.6.1).
// Values derived in Ghidra via scripts/ghidra/*.py. The full re-derivation flow
// for each field is documented in .lab/NOTES.md (Appendix: ghidra_offsets.h
// derivations).
//
// To add support for a new Steam build: do not edit kSteamProfile_<existing
// date> in place. Instead, append a new `extern const BuildProfile
// kSteamProfile_YYYYMMDD = { ... };` definition below, register it in
// build_registry.cpp's kKnownProfiles array, and keep the older profiles for
// users who haven't updated yet. The PE fingerprint routes each user to the
// right profile automatically.

namespace Subnautica2HeadTracking::builds
{
    extern const BuildProfile kSteamProfile_20260522;
    extern const BuildProfile kSteamProfile_20260601;

    // ---- Steam Win64 build released ~2026-06-01 (PE TS 0x72247abc) ----
    const BuildProfile kSteamProfile_20260601 = {
        /* Name        */ "steam-win64-20260601",
        /* Fingerprint */ { 0x72247abcu, 0x0ddda000u, 0x0d7ecf66u },
        /* Offsets     */ {
            // ZRegInfo/ZConstruct/UECodeGen/UWEPlayerCameraManager/VTables are
            // not read at runtime; left zeroed for this build (same as the GDK
            // profile). Re-derive on demand if mask/camera-manager discovery
            // work resumes.
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
            // Struct layouts are engine-ABI-bound (UE5.6.1), not packaging-bound;
            // carried over from steam-win64-20260522 unchanged.
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
            // ObjObjects + FNamePool relocated via scripts/derive_globals.py:
            // allocator's unique `mov [rip],rax` @ fn+0x18e gives ObjObjects;
            // FName decoder's `lea r8,[rip+pool]` gives FNamePool, cross-checked
            // by pool - init_flag == 0x267 (holds, same as 20260522).
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0cd23980ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0cc3f780ULL,
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
            // Relocated for the 2026-06-01 build via scripts/derive_rvas.py
            // (deterministic PE signature scan; the verbose GPV checkf string
            // was stripped this build, so GPV is anchored on the old function's
            // 32-byte relocation-free prologue signature). GPV fn start shifted
            // +0xd30 from steam-win64-20260522 (0x043ed6f0 -> 0x043ee420).
            /* kGetPlayerViewPointRva */ 0x043ee420ULL,
            /* kKnownCallerRvas */ {{
                0,
                0x04172827ULL,  // 1: render caller (FMinimalViewInfo builder, fn 0x041725e0); call [pcm+0x828] retRVA
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

    // ---- Steam Win64 build released ~2026-05-22 (PE TS 0xb727d315) ----
    const BuildProfile kSteamProfile_20260522 = {
        /* Name        */ "steam-win64-20260522",
        /* Fingerprint */ { 0xb727d315u, 0x0ddcb000u, 0x0d7d4cc3u },
        /* Offsets     */ {
            /* ZRegInfo */ {
                /* kAUWEPlayerCameraManager        */ 0x0D0F9C50ULL,
                /* kUWEPlayerCameraManagerSettings */ 0x0D0F9C70ULL,
                /* kAPlayerCameraManager           */ 0x0CE64478ULL,
                /* kMinimalViewInfo                */ 0x0CE5E488ULL,
                /* kUWECameraPackage               */ 0x0D0F9C28ULL,
            },
            /* ZConstruct */ {
                /* kAUWEPlayerCameraManager */ 0x06303F40ULL,
                /* kAPlayerCameraManager    */ 0x03AD8BA0ULL,
                /* kMinimalViewInfo         */ 0x03A78190ULL,
                /* kUWECameraPackage        */ 0x06303C90ULL,
            },
            /* UECodeGen */ {
                /* kConstructUClass_thunk */ 0x0157A7D0ULL,
                /* kConstructUClass       */ 0x016D82F0ULL,
                /* kConstructUPackage     */ 0x016D8120ULL,
            },
            /* UWEPlayerCameraManager */ {
                /* kInstanceSize_Bytes */ 0x27E0u,
                /* kClassFlags         */ 0x1000000Cu,
                /* kStaticsRva         */ 0x063040D0ULL,
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
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0cd16500ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0cc32300ULL,
                /* kFNamePoolBlocks  */ 0x10,
                /* kClassPrivate     */ 0x10,
                /* kNamePrivate      */ 0x18,
                /* kOuterPrivate     */ 0x20,
            },
            /* VTables */ {
                /* kCapsuleComponent      */ 0x0a2a2748ULL,
                /* kSkeletalMeshComponent */ 0x0a2e2a48ULL,
                /* kCameraMountComponent  */ 0x0aeedfa8ULL,
            },
            // Confirmed in Ghidra: FUN_1441718b0 (the FMinimalViewInfo builder)
            // copies Location@+0x00, Rotation@+0x18, FOV@+0x30 from the camera
            // cache, and writes FOV before the GPV vfn call on the render path.
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
            /* kGetPlayerViewPointRva */ 0x043ed6f0ULL,
            /* kKnownCallerRvas */ {{
                0x06329c08ULL,  // 1: ~22/frame, dominant
                0x04171af7ULL,  // 2: ~4/frame  <- render (FMinimalViewInfo builder)
                0x043eae77ULL,  // 3: ~2/frame
                0x00000000ULL,  // 4: unconfirmed
                0x03fc993cULL,  // 5: ~1/frame
                0x04177e2dULL,  // 6: ~1/frame
                0x02b5de88ULL,  // 7: ~1/frame
                0x051149d1ULL,  // 8: ~1/frame
                0x063e82d9ULL,  // 9: ~1/frame
                0x05106f00ULL,  // 10: one-shot at startup
                0x068d91a5ULL,  // 11: rare
            }},
        },
    };
}
