#pragma once
#include <cstdint>
#include <string>

#include "builds/build_registry.h"
#include "ue_math.h"

// Runtime access to the live UE5 process: fault-guarded memory reads/writes,
// the loaded module's address range, and FName/UObject reflection used to
// resolve class and object names by walking GUObjectArray.
namespace Subnautica2HeadTracking::ue
{
    // Set once from InstallCameraHook with the game module's base/end.
    void SetModuleRange(std::uintptr_t base, std::uintptr_t end);
    std::uintptr_t ModuleBase();
    std::uintptr_t ModuleEnd();

    // Fault-guarded reads/writes (SEH __try). Return false on access violation.
    bool SafeReadPtr(std::uintptr_t addr, std::uintptr_t& out);
    bool SafeReadU32(std::uintptr_t addr, std::uint32_t& out);
    bool SafeReadU16(std::uintptr_t addr, std::uint16_t& out);
    bool SafeReadFQuat(std::uintptr_t addr, FQuat4d& out);
    bool SafeReadFVector(std::uintptr_t addr, FVector& out);
    bool SafeWriteFQuat(std::uintptr_t addr, const FQuat4d& q);
    bool SafeWriteFVector(std::uintptr_t addr, const FVector& v);

    // Heap or .data in the user-mode 64-bit range, 8-byte aligned.
    bool LooksLikePointer(std::uintptr_t v);

    // Resolve an FName ComparisonIndex to its string via the FNamePool.
    std::string ResolveFName(std::uint32_t id);
    std::string ObjectName(std::uintptr_t obj);
    std::string ClassName(std::uintptr_t obj);
    std::string OuterName(std::uintptr_t obj);

    // Case-insensitive substring test.
    bool ContainsCI(const std::string& hay, const char* needle);

    // Visit every live UObject. visit(obj) returns true to stop early.
    template <typename Fn>
    void ForEachUObject(Fn&& visit)
    {
        if (ModuleBase() == 0) return;
        const auto& off = Offsets().UObjectGlobals;
        const std::uintptr_t objArr = ModuleBase() + off.kObjObjects;
        std::uintptr_t chunks = 0;
        std::uint32_t num = 0;
        if (!SafeReadPtr(objArr, chunks) || !chunks) return;
        if (!SafeReadU32(objArr + off.kObjObjects_Num, num)) return;
        if (num == 0 || num > 0x4000000) return;
        for (std::uint32_t i = 0; i < num; ++i) {
            std::uintptr_t chunk = 0;
            if (!SafeReadPtr(chunks + (static_cast<std::uintptr_t>(
                    i / off.kChunkNumElems) * 8), chunk) || !chunk)
                continue;
            const std::uintptr_t item = chunk +
                static_cast<std::uintptr_t>(i % off.kChunkNumElems)
                    * off.kFUObjectItemSize;
            std::uintptr_t obj = 0;
            if (!SafeReadPtr(item, obj) || !obj) continue;
            if (visit(obj)) return;
        }
    }

    // First object whose name (and optional class/outer) match.
    std::uintptr_t FindLiveObject(const char* wantClass, const char* wantName,
                                  const char* wantOuter);
}
