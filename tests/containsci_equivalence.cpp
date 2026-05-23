// Behaviour-lock + microbenchmark for the ContainsCI optimisation in
// src/Subnautica2HeadTracking/headtracking_mod.cpp.
//
// ContainsCI runs once per UObject across the ~244k-object enumeration in
// CollectReticleWidgets (every 2s on the collector thread). The original
// implementation allocated two lowercased std::string copies on every call.
// The optimised version folds case in place. This test asserts the optimised
// result is byte-identical to the original across an exhaustive case set, then
// times both over a workload that mirrors the live scan.
//
// Build (standalone, no game deps):
//   g++ -O2 -std=c++17 tests/containsci_equivalence.cpp -o /tmp/cci && /tmp/cci

#include <cctype>
#include <chrono>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include <cstdio>

// --- ORIGINAL (allocating) -------------------------------------------------
static bool ContainsCI_old(const std::string& hay, const char* needle) {
    auto lower = [](std::string s) {
        for (char& c : s)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    return lower(hay).find(lower(needle)) != std::string::npos;
}

// --- OPTIMISED (in-place case fold) ----------------------------------------
static bool ContainsCI_new(const std::string& hay, const char* needle) {
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

int main() {
    // ---- 1. Behavioural equivalence ----
    const std::vector<std::string> hays = {
        "", "a", "A", "Default__BP_HUD", "default__", "DEFAULT__C",
        "InteractionIcon", "ReticleOverlay", "WBP_HUD_C", "hud",
        "HUD", "xHUDx", "AbC", "aaa", "AAAA", "ArrowTexture",
        "Default", "fault__", "__", "Crosshair_Reticle_0",
    };
    const std::vector<const char*> needles = {
        "", "a", "A", "Default__", "default__", "HUD", "WBP_HUD",
        "icon", "ICON", "overlay", "x", "AbC", "aaaa", "reticle",
        "crosshair", "_", "__", "zzz", "Default", "fault",
    };
    long mismatches = 0, total = 0;
    for (const auto& h : hays) {
        for (const char* n : needles) {
            ++total;
            if (ContainsCI_old(h, n) != ContainsCI_new(h, n)) {
                ++mismatches;
                std::printf("MISMATCH hay=\"%s\" needle=\"%s\" old=%d new=%d\n",
                    h.c_str(), n, ContainsCI_old(h, n), ContainsCI_new(h, n));
            }
        }
    }

    // Randomised fuzz over mixed-case ASCII.
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> lenD(0, 24);
    std::uniform_int_distribution<int> chD(32, 126);
    auto randstr = [&](int n){ std::string s; for (int i=0;i<n;++i) s.push_back((char)chD(rng)); return s; };
    for (int it = 0; it < 200000; ++it) {
        std::string h = randstr(lenD(rng));
        std::string n = randstr(lenD(rng));
        ++total;
        if (ContainsCI_old(h, n.c_str()) != ContainsCI_new(h, n.c_str())) {
            ++mismatches;
            if (mismatches < 10)
                std::printf("FUZZ MISMATCH hay=\"%s\" needle=\"%s\"\n", h.c_str(), n.c_str());
        }
    }
    std::printf("equivalence: %ld cases, %ld mismatches\n", total, mismatches);
    if (mismatches != 0) { std::printf("FAIL\n"); return 1; }

    // ---- 2. Microbenchmark mirroring the live scan ----
    // ~244k object names per pass; the hot needle is the "Default__" CDO skip
    // that ContainsCI is called with for every object.
    std::vector<std::string> names;
    names.reserve(244000);
    const char* samples[] = {
        "BP_Something_C", "Default__BP_Pawn_C", "InteractionIcon",
        "WBP_HUD_Reticle", "SkeletalMeshComponent0", "Texture2D_2048",
        "MaterialInstanceConstant_44", "Default__WidgetTree",
    };
    for (int i = 0; i < 244000; ++i)
        names.emplace_back(samples[i % 8]);

    auto bench = [&](bool useNew) {
        volatile long sink = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int pass = 0; pass < 20; ++pass) {
            for (const auto& nm : names) {
                bool r = useNew ? ContainsCI_new(nm, "Default__")
                                : ContainsCI_old(nm, "Default__");
                sink += r ? 1 : 0;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        (void)sink;
        return std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    };

    // warm
    bench(false); bench(true);
    long oldUs = bench(false);
    long newUs = bench(true);
    std::printf("bench (20 passes x 244k calls):\n");
    std::printf("  old (allocating): %ld us  (%.1f ns/call)\n", oldUs, oldUs * 1000.0 / (20.0*244000));
    std::printf("  new (in-place):   %ld us  (%.1f ns/call)\n", newUs, newUs * 1000.0 / (20.0*244000));
    std::printf("  speedup: %.2fx\n", (double)oldUs / (double)newUs);
    std::printf("PASS\n");
    return 0;
}
