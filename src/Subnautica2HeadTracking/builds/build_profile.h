#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// One BuildProfile describes a single packaging/build of Subnautica 2: the
// PE-header fingerprint that uniquely identifies it, and every per-build
// constant (RVAs into the EXE, UObject/USceneComponent field offsets) the
// mod needs to operate. The registry holds one profile per supported build
// (Steam Win64 today; WinGDK / Xbox planned). At Initialize() time the mod
// fingerprints the live module and selects the matching profile; no match
// leaves the mod dormant via the existing fail-safe path.
//
// Field names mirror the namespaces in the previous ghidra_offsets.h so
// call-site diffs are mechanical: `Offsets::ZRegInfo::kFoo` becomes
// `Offsets().ZRegInfo.kFoo`. Struct layouts (UObject/USceneComponent/FName)
// are engine-version-bound, not packaging-bound, so most fields will match
// across Steam and WinGDK profiles; only the RVAs differ.

namespace Subnautica2HeadTracking
{
    struct PeFingerprint
    {
        std::uint32_t TimeDateStamp;
        std::uint32_t SizeOfImage;
        std::uint32_t CheckSum;
    };

    struct OffsetTable
    {
        struct {
            std::uintptr_t kAUWEPlayerCameraManager;
            std::uintptr_t kUWEPlayerCameraManagerSettings;
            std::uintptr_t kAPlayerCameraManager;
            std::uintptr_t kMinimalViewInfo;
            std::uintptr_t kUWECameraPackage;
        } ZRegInfo;

        struct {
            std::uintptr_t kAUWEPlayerCameraManager;
            std::uintptr_t kAPlayerCameraManager;
            std::uintptr_t kMinimalViewInfo;
            std::uintptr_t kUWECameraPackage;
        } ZConstruct;

        struct {
            std::uintptr_t kConstructUClass_thunk;
            std::uintptr_t kConstructUClass;
            std::uintptr_t kConstructUPackage;
        } UECodeGen;

        struct {
            std::uint32_t  kInstanceSize_Bytes;
            std::uint32_t  kClassFlags;
            std::uintptr_t kStaticsRva;
        } UWEPlayerCameraManager;

        struct {
            std::size_t kComponentToWorldRotation;
            std::size_t kComponentToWorldTranslation;
            std::size_t kComponentToWorldScale;
        } USceneComponentLayout;

        struct {
            std::size_t kCapsule;
            std::size_t kCapsuleAlias;
            std::size_t kPrimaryMesh;
            std::size_t kMeshArrayBegin;
            std::size_t kMeshArrayStride;
            std::size_t kMeshArrayCount;
            std::size_t kCameraMountComponent;
        } PawnSlots;

        struct {
            std::size_t   kShowMouseCursorOffset;
            std::uint32_t kShowMouseCursorMask;
            std::size_t   kPawn;
            std::size_t   kPlayerCameraManager;
        } PlayerController;

        struct {
            std::uintptr_t kObjObjects;
            std::size_t    kObjObjects_Num;
            std::size_t    kFUObjectItemSize;
            std::size_t    kChunkNumElems;
            std::uintptr_t kFNamePool;
            std::size_t    kFNamePoolBlocks;
            std::size_t    kClassPrivate;
            std::size_t    kNamePrivate;
            std::size_t    kOuterPrivate;
        } UObjectGlobals;

        struct {
            std::uintptr_t kCapsuleComponent;
            std::uintptr_t kSkeletalMeshComponent;
            std::uintptr_t kCameraMountComponent;
        } VTables;

        // Hook target: APlayerController::GetPlayerViewPoint.
        std::uintptr_t kGetPlayerViewPointRva;

        // Inject-mode caller-RVA table. Entry [1] is the production caller
        // (FMinimalViewInfo builder). 0-valued entries are placeholders for
        // call sites that did not fire during the most recent re-bisection.
        std::array<std::uintptr_t, 11> kKnownCallerRvas;
    };

    struct BuildProfile
    {
        const char*    Name;
        PeFingerprint  Fingerprint;
        OffsetTable    Offsets;
    };
}
