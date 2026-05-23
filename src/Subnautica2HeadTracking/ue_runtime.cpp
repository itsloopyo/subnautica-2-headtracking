#include "ue_runtime.h"

#include <cctype>
#include <cstring>
#include <windows.h>

namespace Subnautica2HeadTracking::ue
{
    namespace
    {
        std::uintptr_t g_moduleBase = 0;
        std::uintptr_t g_moduleEnd  = 0;
    }

    void SetModuleRange(std::uintptr_t base, std::uintptr_t end)
    {
        g_moduleBase = base;
        g_moduleEnd  = end;
    }
    std::uintptr_t ModuleBase() { return g_moduleBase; }
    std::uintptr_t ModuleEnd()  { return g_moduleEnd; }

    bool SafeReadPtr(std::uintptr_t addr, std::uintptr_t& out)
    {
        __try {
            out = *reinterpret_cast<const std::uintptr_t*>(addr);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool SafeReadU32(std::uintptr_t addr, std::uint32_t& out)
    {
        __try { out = *reinterpret_cast<const std::uint32_t*>(addr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool SafeReadU16(std::uintptr_t addr, std::uint16_t& out)
    {
        __try { out = *reinterpret_cast<const std::uint16_t*>(addr); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool SafeReadFQuat(std::uintptr_t addr, FQuat4d& out)
    {
        __try {
            out = *reinterpret_cast<const FQuat4d*>(addr);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool SafeReadFVector(std::uintptr_t addr, FVector& out)
    {
        __try {
            out.X = *reinterpret_cast<const double*>(addr + 0);
            out.Y = *reinterpret_cast<const double*>(addr + 8);
            out.Z = *reinterpret_cast<const double*>(addr + 16);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool SafeWriteFQuat(std::uintptr_t addr, const FQuat4d& q)
    {
        __try {
            *reinterpret_cast<FQuat4d*>(addr) = q;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool SafeWriteFVector(std::uintptr_t addr, const FVector& v)
    {
        __try {
            *reinterpret_cast<double*>(addr + 0)  = v.X;
            *reinterpret_cast<double*>(addr + 8)  = v.Y;
            *reinterpret_cast<double*>(addr + 16) = v.Z;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool LooksLikePointer(std::uintptr_t v)
    {
        // Heap or .data in the user-mode 64-bit range, 8-byte aligned, not the
        // small immediate range that often appears in flag fields.
        if (v < 0x10000) return false;
        if (v > 0x7fffffffffffULL) return false;
        if ((v & 0x7) != 0) return false;
        return true;
    }

    // Layout confirmed in Ghidra: Blocks[] at pool+0x10, stride 2, entry header
    // uint16 (Len = header>>6, bIsWide = bit0), chars at +2.
    std::string ResolveFName(std::uint32_t id)
    {
        if (g_moduleBase == 0) return std::string();
        const std::uintptr_t blocks =
            g_moduleBase + Offsets::UObjectGlobals::kFNamePool
                         + Offsets::UObjectGlobals::kFNamePoolBlocks;
        std::uintptr_t blockPtr = 0;
        if (!SafeReadPtr(blocks + (static_cast<std::uintptr_t>(id >> 16) * 8), blockPtr))
            return std::string();
        if (!blockPtr) return std::string();
        const std::uintptr_t entry = blockPtr + (static_cast<std::uintptr_t>(id & 0xffff) * 2);
        std::uint16_t header = 0;
        if (!SafeReadU16(entry, header)) return std::string();
        const bool isWide = (header & 1) != 0;
        const int len = header >> 6;
        if (len <= 0 || len > 1024) return std::string();
        std::string out;
        out.reserve(len);
        if (!isWide) {
            for (int i = 0; i < len; ++i) {
                std::uint16_t b = 0;
                if (!SafeReadU16(entry + 2 + i, b)) break;  // overlapping read ok
                out.push_back(static_cast<char>(b & 0xff));
            }
        } else {
            for (int i = 0; i < len; ++i) {
                std::uint16_t w = 0;
                if (!SafeReadU16(entry + 2 + i * 2, w)) break;
                out.push_back(static_cast<char>(w & 0x7f));
            }
        }
        return out;
    }

    std::string ObjectName(std::uintptr_t obj)
    {
        std::uint32_t id = 0;
        if (!SafeReadU32(obj + Offsets::UObjectGlobals::kNamePrivate, id))
            return std::string();
        return ResolveFName(id);
    }

    std::string ClassName(std::uintptr_t obj)
    {
        std::uintptr_t cls = 0;
        if (!SafeReadPtr(obj + Offsets::UObjectGlobals::kClassPrivate, cls) || !cls)
            return std::string();
        return ObjectName(cls);
    }

    std::string OuterName(std::uintptr_t obj)
    {
        std::uintptr_t outer = 0;
        if (!SafeReadPtr(obj + Offsets::UObjectGlobals::kOuterPrivate, outer) || !outer)
            return std::string();
        return ObjectName(outer);
    }

    // Folds case in place rather than allocating two lowercased copies on every
    // call - this runs once per UObject across the ~244k-object enumeration in
    // reticle discovery. Semantics match lowercase(hay).find(lowercase(needle))
    // (empty needle -> true).
    bool ContainsCI(const std::string& hay, const char* needle)
    {
        const std::size_t nlen = std::strlen(needle);
        if (nlen == 0) return true;
        if (hay.size() < nlen) return false;
        auto lc = [](char c) {
            return static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        };
        const std::size_t last = hay.size() - nlen;
        for (std::size_t i = 0; i <= last; ++i) {
            std::size_t j = 0;
            for (; j < nlen; ++j) {
                if (lc(hay[i + j]) != lc(needle[j])) break;
            }
            if (j == nlen) return true;
        }
        return false;
    }

    std::uintptr_t FindLiveObject(const char* wantClass, const char* wantName,
                                  const char* wantOuter)
    {
        std::uintptr_t found = 0;
        ForEachUObject([&](std::uintptr_t obj) -> bool {
            if (ObjectName(obj) != wantName) return false;
            if (wantClass && ClassName(obj) != wantClass) return false;
            if (wantOuter && OuterName(obj) != wantOuter) return false;
            found = obj;
            return true;
        });
        return found;
    }
}
