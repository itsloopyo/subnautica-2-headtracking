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
    extern const BuildProfile kSteamProfile_20260710;
    extern const BuildProfile kSteamProfile_20260714;
    extern const BuildProfile kSteamProfile_20260820;

    // ---- Steam Win64 build released ~2026-08-20 (buildid 24418064, PE TS 0xae73a557) ----
    //
    // First patch that moved STRUCT FIELD OFFSETS, not just RVAs. AActor grew
    // 0x10 bytes somewhere after RootComponent, so every field past that point
    // in AActor and in each of its subclasses shifts by +0x10. Confirmed three
    // ways off the UECodeGen property tables in the EXE, with a negative
    // control: AController::Pawn 0x2f0 -> 0x300, ACharacter::CapsuleComponent
    // 0x340 -> 0x350, APlayerController::bShowMouseCursor 0x554 -> 0x564,
    // while AActor::RootComponent stays at 0x1c0. USceneComponent is on the
    // UActorComponent branch and did NOT move (verified against the
    // K2_GetComponentToWorld accessor, which still reads [this+0x1f0]).
    // Carrying the previous build's PlayerController offsets forward would
    // have read garbage at every one of them.
    const BuildProfile kSteamProfile_20260820 = {
        /* Name        */ "steam-win64-20260820",
        /* Fingerprint */ { 0xae73a557u, 0x0dc93000u, 0x0d69a7d0u },
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
            // Unchanged: USceneComponent sits on the UActorComponent branch,
            // which this patch did not touch. GetComponentTransform still
            // reads the FTransform at [this+0x1f0] (Rotation 0x1f0,
            // Translation 0x210, Scale 0x230).
            /* USceneComponentLayout */ {
                /* kComponentToWorldRotation    */ 0x1f0,
                /* kComponentToWorldTranslation */ 0x210,
                /* kComponentToWorldScale       */ 0x230,
            },
            // Rebased by the AActor +0x10 delta. kCapsule is
            // ACharacter::CapsuleComponent, read straight off the property
            // table. kCapsuleAlias is AActor::RootComponent, which sits before
            // the insertion point and stays put. Nothing in this group is read
            // at runtime any more (mask compensation discovers components
            // structurally), so the rest carry the delta rather than a fresh
            // probe run.
            /* PawnSlots */ {
                /* kCapsule              */ 0x350,
                /* kCapsuleAlias         */ 0x1c0,
                /* kPrimaryMesh          */ 0x340,
                /* kMeshArrayBegin       */ 0x7d8,
                /* kMeshArrayStride      */ 0x008,
                /* kMeshArrayCount       */ 6,
                /* kCameraMountComponent */ 0x868,
            },
            // All three moved +0x10 this build - see the header comment.
            // kPlayerCameraManager is the offset GetPlayerViewPoint itself
            // dereferences (`mov rcx,[rbx+0x378]` before the CameraCache
            // TimeStamp test), so it is confirmed by the hook target's own
            // code, not just the property table. kShowMouseCursorOffset/Mask
            // come from the property's generated SetBitFunc, which is literally
            // `or dword ptr [rcx+0x564], 1`.
            /* PlayerController */ {
                /* kShowMouseCursorOffset */ 0x564,
                /* kShowMouseCursorMask   */ 0x1u,
                /* kPawn                  */ 0x300,
                /* kPlayerCameraManager   */ 0x378,
            },
            // Relocated via scripts/derive_globals.py: allocator sig unique hit
            // (fn 0x016eadf0), the `mov [rip],rax` @ fn+0x18e. FName decoder
            // pair at 0x01479bc0/0x01479c30 agree on the pool and
            // pool - init_flag == 0x267 holds.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0cbd8680ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0caf4480ULL,
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
            // Unchanged, and re-confirmed in the builder: it does
            // `lea r14,[rdi+0x30]` (FOV) and `lea r8,[rdi+0x18]` (Rotation)
            // off the same FMinimalViewInfo base it passes to GPV.
            /* MinimalViewInfoLayout */ {
                /* kFovOffset      */ 0x30,
                /* kRotationStride */ 0x18,
            },
            // Relocated for the 2026-08-20 build (buildid 24418064) via
            // scripts/derive_rvas.py. GPV anchored on the relocation-free
            // prologue signature, 1 hit.
            //
            // The GPV VTABLE SLOT also moved this build: 0x828 -> 0x830 (one
            // virtual inserted before it), and the PlayerCameraManager FOV vfn
            // alongside it, 0x7f8 -> 0x800. derive_rvas.py hardcodes the old
            // pair, so its render-caller pass returned zero candidates until
            // rerun against the new displacements. Recover the slot by finding
            // the .rdata qwords pointing at the GPV RVA and walking back to the
            // vtable base (the RTTI locator at base-8 stops the walk).
            /* kGetPlayerViewPointRva */ 0x043eaa00ULL,
            /* kKnownCallerRvas */ {{
                0,
                // 1: render caller. Containing fn 0x0416eba0 is the
                // FMinimalViewInfo builder - the only `call [reg+0x830]` site
                // in the image with the builder window around it:
                // `call [rax+0x800]` (PCM FOV vfn) -> `movss [r14],xmm0`
                // (FOV store, r14 = base+0x30) -> `lea r8,[rdi+0x18]`
                // (out_Rotation) -> `call [rax+0x830]` (GPV).
                0x0416ede7ULL,
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

    // ---- Steam Win64 build released ~2026-07-14 (buildid 24153994, PE TS 0xa3d114c1) ----
    const BuildProfile kSteamProfile_20260714 = {
        /* Name        */ "steam-win64-20260714",
        /* Fingerprint */ { 0xa3d114c1u, 0x0dbcf000u, 0x0d5d8938u },
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
            // Struct layouts are engine-ABI-bound (UE5.6.1), not packaging-bound;
            // carried over from steam-win64-20260710 unchanged.
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
            // Relocated via scripts/derive_globals.py: allocator sig unique hit
            // (fn 0x016e7070), the `mov [rip],rax` @ fn+0x18e. FName decoder
            // pair at 0x01475e40/0x01475eb0 agree on the pool and
            // pool - init_flag == 0x267 holds.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0cb26200ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0ca42000ULL,
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
            // Relocated for the 2026-07-14 build (buildid 24153994) via
            // scripts/derive_rvas.py. GPV anchored on the relocation-free
            // prologue signature, 1 hit. SizeOfImage shrank 0x5000 again, so
            // RVAs moved down: GPV -0x1a60, render caller -0x1a10.
            /* kGetPlayerViewPointRva */ 0x043e7640ULL,
            /* kKnownCallerRvas */ {{
                0,
                // 1: render caller. Containing fn 0x0416b7c0 is the
                // FMinimalViewInfo builder - of 3 PCM:YES candidates only this
                // one shows the builder window anchored at the +0x7f8 call:
                // `call [rax+0x7f8]` (PCM FOV vfn) -> `movss [r14],xmm0`
                // (FOV store) -> `lea r8,[rdi+0x18]` (out_Rotation =
                // out_Location + kRotationStride) -> `call [rax+0x828]` (GPV).
                // The other two candidates lack a +0x7f8 call before the GPV
                // call entirely.
                0x0416ba07ULL,
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

    // ---- Steam Win64 build released ~2026-07-10 (PE TS 0x1308b6a1) ----
    const BuildProfile kSteamProfile_20260710 = {
        /* Name        */ "steam-win64-20260710",
        /* Fingerprint */ { 0x1308b6a1u, 0x0dbd4000u, 0x0d5e0a9bu },
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
            // Struct layouts are engine-ABI-bound (UE5.6.1), not packaging-bound;
            // carried over from steam-win64-20260601 unchanged.
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
            // Relocated via scripts/derive_globals.py. The FName decoder
            // signature hits twice this build (two adjacent decoders at
            // 0x01475e20/0x01475e90); both reference the same pool and both
            // satisfy pool - init_flag == 0x267, so the pool is unambiguous.
            /* UObjectGlobals */ {
                /* kObjObjects       */ 0x0cb2a200ULL,
                /* kObjObjects_Num   */ 0x14,
                /* kFUObjectItemSize */ 0x18,
                /* kChunkNumElems    */ 0x10000,
                /* kFNamePool        */ 0x0ca46000ULL,
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
            // Relocated for the 2026-07-10 build via scripts/derive_rvas.py.
            // GPV anchored on the old build's relocation-free prologue signature
            // (the checkf string stays stripped); 1 hit. SizeOfImage shrank this
            // build, so RVAs moved down: GPV -0x5380, render caller -0x5410.
            /* kGetPlayerViewPointRva */ 0x043e90a0ULL,
            /* kKnownCallerRvas */ {{
                0,
                // 1: render caller. Containing fn 0x0416d1d0 is the
                // FMinimalViewInfo builder: `call [rax+0x7f8]` (PCM FOV vfn) ->
                // `movss [r14],xmm0` (FOV store) -> `lea r8,[rdi+0x18]`
                // (out_Rotation = out_Location + kRotationStride) ->
                // `call [rax+0x828]` (GPV). Two other candidates issue the same
                // double-vfn pair but store FOV to unrelated fields.
                0x0416d417ULL,
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
