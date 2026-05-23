#pragma once
#include <cstdint>

// Per-build constants extracted from Subnautica2-Win64-Shipping.exe
// (UE 5.6.1) via scripts/ghidra/*.py. Re-derivation flow + how each
// value was found: see .lab/NOTES.md (Appendix: ghidra_offsets.h
// derivations). Refresh after every game patch.

namespace Subnautica2HeadTracking::Offsets
{
    inline constexpr std::uintptr_t kDefaultImageBase = 0x140000000ULL;

    // PE-header fingerprint of the build the RVAs below match.
    // ValidateRunningBuild compares against the live module; mismatch
    // disables the mod so a stale RVA can't crash the user.
    namespace BuildFingerprint
    {
        inline constexpr std::uint32_t kTimeDateStamp = 0xb727d315u;
        inline constexpr std::uint32_t kSizeOfImage   = 0x0ddcb000u;
        inline constexpr std::uint32_t kCheckSum      = 0x0d7d4cc3u;
    }

    // Static UClass* slots populated at engine boot. Deref (module_base + RVA).
    namespace ZRegInfo
    {
        inline constexpr std::uintptr_t kAUWEPlayerCameraManager         = 0x0D0F9C50ULL;
        inline constexpr std::uintptr_t kUWEPlayerCameraManagerSettings  = 0x0D0F9C70ULL;
        inline constexpr std::uintptr_t kAPlayerCameraManager            = 0x0CE64478ULL;
        inline constexpr std::uintptr_t kMinimalViewInfo                 = 0x0CE5E488ULL;
        inline constexpr std::uintptr_t kUWECameraPackage                = 0x0D0F9C28ULL;
    }

    namespace ZConstruct
    {
        inline constexpr std::uintptr_t kAUWEPlayerCameraManager = 0x06303F40ULL;
        inline constexpr std::uintptr_t kAPlayerCameraManager    = 0x03AD8BA0ULL;
        inline constexpr std::uintptr_t kMinimalViewInfo         = 0x03A78190ULL;
        inline constexpr std::uintptr_t kUWECameraPackage        = 0x06303C90ULL;
    }

    namespace UECodeGen
    {
        inline constexpr std::uintptr_t kConstructUClass_thunk = 0x0157A7D0ULL;
        inline constexpr std::uintptr_t kConstructUClass       = 0x016D82F0ULL;
        inline constexpr std::uintptr_t kConstructUPackage     = 0x016D8120ULL;
    }

    namespace UWEPlayerCameraManager
    {
        inline constexpr std::uint32_t  kInstanceSize_Bytes = 0x27E0;
        inline constexpr std::uint32_t  kClassFlags         = 0x1000000C;
        inline constexpr std::uintptr_t kStaticsRva         = 0x063040D0ULL;
    }

    // USceneComponent FTransform field offsets (96-byte layout).
    namespace USceneComponentLayout
    {
        inline constexpr std::size_t kComponentToWorldRotation    = 0x1f0;
        inline constexpr std::size_t kComponentToWorldTranslation = 0x210;
        inline constexpr std::size_t kComponentToWorldScale       = 0x230;
    }

    // ASN2PlayerCharacter component slots.
    namespace PawnSlots
    {
        inline constexpr std::size_t   kCapsule              = 0x340;
        inline constexpr std::size_t   kCapsuleAlias         = 0x1c0;
        inline constexpr std::size_t   kPrimaryMesh          = 0x330;
        inline constexpr std::size_t   kMeshArrayBegin       = 0x7c8;
        inline constexpr std::size_t   kMeshArrayStride      = 0x008;
        inline constexpr std::size_t   kMeshArrayCount       = 6;
        inline constexpr std::size_t   kCameraMountComponent = 0x858;
    }

    // APlayerController instance fields.
    namespace PlayerController
    {
        inline constexpr std::size_t   kShowMouseCursorOffset = 0x554;
        inline constexpr std::uint32_t kShowMouseCursorMask   = 0x1;
        inline constexpr std::size_t   kPawn                  = 0x2f0;
        inline constexpr std::size_t   kPlayerCameraManager   = 0x368;
    }

    // UE5 global object/name tables. RVAs into the EXE; add module_base.
    namespace UObjectGlobals
    {
        inline constexpr std::uintptr_t kObjObjects      = 0x0cd16500;
        inline constexpr std::size_t    kObjObjects_Num  = 0x14;
        inline constexpr std::size_t    kFUObjectItemSize = 0x18;
        inline constexpr std::size_t    kChunkNumElems   = 0x10000;

        inline constexpr std::uintptr_t kFNamePool       = 0x0cc32300;
        inline constexpr std::size_t    kFNamePoolBlocks = 0x10;

        inline constexpr std::size_t    kClassPrivate    = 0x10;
        inline constexpr std::size_t    kNamePrivate     = 0x18;
        inline constexpr std::size_t    kOuterPrivate    = 0x20;
    }

    // UObject vtable RVAs.
    namespace VTables
    {
        inline constexpr std::uintptr_t kCapsuleComponent      = 0x0a2a2748ULL;
        inline constexpr std::uintptr_t kSkeletalMeshComponent = 0x0a2e2a48ULL;
        inline constexpr std::uintptr_t kCameraMountComponent  = 0x0aeedfa8ULL;
    }
}
