#include "headtracking_mod.h"
#include "logging.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <psapi.h>

#include "ghidra_offsets.h"
#include "reticle_overlay.h"
#include "ue_math.h"
#include "ue_runtime.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/processing/pose_interpolator.h"
#include "cameraunlock/processing/position_interpolator.h"
#include "cameraunlock/processing/position_processor.h"
#include "cameraunlock/data/position_data.h"
#include "cameraunlock/math/vec3.h"
#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/smoothing_utils.h"

// Runtime discovery/tuning hotkeys (mask isolator + comp, UObject/HUD dumps,
// inject-mode cycling, standalone position toggle). Off in shipping builds:
// the production render path runs without them (inject mode defaults to 2,
// mask compensation auto-enables from the persisted marks file). Set to 1 to
// re-arm the F6-F12 / ScrollLock / Insert / Delete debug controls.
#ifndef SN2HT_DEV_HOTKEYS
#define SN2HT_DEV_HOTKEYS 1
#endif

namespace Subnautica2HeadTracking
{
    namespace
    {
        constexpr std::uintptr_t kGetPlayerViewPointRva = 0x043ed6f0;

        // UE math types, fault-guarded memory access, and UObject reflection
        // live in the ue namespace (ue_math.h / ue_runtime.h). Pull the names
        // into scope so the call sites below read unqualified.
        using ue::FQuat4d;
        using ue::FRotator;
        using ue::FVector;
        using ue::ClassName;
        using ue::ContainsCI;
        using ue::FindLiveObject;
        using ue::ForEachUObject;
        using ue::LooksLikePointer;
        using ue::ObjectName;
        using ue::OuterName;
        using ue::QuatFromEulerDeg;
        using ue::QuatInv;
        using ue::QuatMul;
        using ue::QuatRotateVec;
        using ue::QuatToRotator;
        using ue::ResolveFName;
        using ue::SafeReadFQuat;
        using ue::SafeReadFVector;
        using ue::SafeReadPtr;
        using ue::SafeReadU16;
        using ue::SafeReadU32;
        using ue::SafeWriteFQuat;
        using ue::SafeWriteFVector;

        using GetPlayerViewPoint_t = void(__fastcall*)(void* self, FVector* outLocation, FRotator* outRotation);

        std::unique_ptr<cameraunlock::UdpReceiver> g_receiver;
        std::unique_ptr<cameraunlock::input::HotkeyPoller> g_hotkeys;
        std::atomic<bool> g_trackingEnabled{true};

        // True while both Ctrl (either side) and Shift (either side) are held.
        // GetAsyncKeyState(VK_CONTROL/VK_SHIFT) reports the merged left+right
        // state, so this covers all four modifier keys. The chord letter's own
        // edge is detected by the HotkeyPoller entry it is registered against;
        // this just gates the callback so the bare letter does nothing in-game.
        bool ChordHeld()
        {
            return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
                && (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
        }

        constexpr float kMaxFrameDt = 0.1f;  // clamp huge gaps (alt-tab etc.)

        // Per-pipeline frame-delta clock. Returns seconds since the previous
        // Tick(), clamped to [0, kMaxFrameDt]. Each pipeline owns an instance so
        // their dt streams stay independent; the QPC frequency is shared (it is
        // constant for the lifetime of the process).
        class FrameClock {
        public:
            float Tick() {
                if (!initialized_) {
                    QueryPerformanceCounter(&last_);
                    initialized_ = true;
                }
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                float dt = static_cast<float>(
                    static_cast<double>(now.QuadPart - last_.QuadPart) /
                    static_cast<double>(Freq().QuadPart));
                last_ = now;
                if (dt < 0.0f) dt = 0.0f;
                if (dt > kMaxFrameDt) dt = kMaxFrameDt;
                return dt;
            }
        private:
            static const LARGE_INTEGER& Freq() {
                static LARGE_INTEGER f = [] {
                    LARGE_INTEGER q;
                    QueryPerformanceFrequency(&q);
                    return q;
                }();
                return f;
            }
            LARGE_INTEGER last_{};
            bool initialized_ = false;
        };

        // Pipeline state. The hook fires on the render thread only, so all
        // of this is single-thread access - no locks needed.
        cameraunlock::PoseInterpolator g_interp;
        std::int64_t g_lastSampleTs = 0;
        FrameClock g_rotClock;
        float g_smoothedYaw = 0.0f, g_smoothedPitch = 0.0f, g_smoothedRoll = 0.0f;
        bool  g_hasSmoothed = false;
        // User smoothing setting. Floor of kBaselineSmoothing (0.15) applies
        // unconditionally in GetEffectiveSmoothing - that's what makes the
        // 30Hz tracker not look like 30fps on a 60+Hz display.
        float g_userSmoothing = 0.0f;

        // Positional (6DOF) pipeline. Same single-render-thread access as the
        // rotation pipeline - no locks. Defaults (sensitivity 1.0, limits
        // 0.30/0.20/0.40/0.10m, smoothing 0.15) come from PositionSettings,
        // matching HeadTracking.ini's [Position] section.
        cameraunlock::PositionProcessor   g_posProcessor;
        cameraunlock::PositionInterpolator g_posInterp;
        std::atomic<bool> g_positionEnabled{true};

        // Rotational head-tracking gate, independent of the master toggle. The
        // tracking-mode cycle (Page Up / Ctrl+Shift+G) drives this together
        // with g_positionEnabled through three states; the master toggle (End /
        // Ctrl+Shift+Y) still gates everything above both.
        std::atomic<bool> g_rotationEnabled{true};
        // Tracking-mode cycle index: 0 = both, 1 = rotation only (position off),
        // 2 = position only (rotation off). Advancing past 2 wraps to 0.
        std::atomic<int> g_trackingMode{0};
        FrameClock g_posClock;
        // Set on first sample and on every Home press: the next sample becomes
        // the position center, so the player starts the session (and re-centers)
        // at zero head offset rather than wherever the tracker happens to read.
        std::atomic<bool> g_posCenterPending{true};

        bool GetProcessedRotation(float& outYaw, float& outPitch, float& outRoll) {
            float rawYaw = 0.0f, rawPitch = 0.0f, rawRoll = 0.0f;
            if (!g_receiver->GetRotation(rawYaw, rawPitch, rawRoll)) {
                return false;
            }

            const float dt = g_rotClock.Tick();

            const std::int64_t ts = g_receiver->GetLastReceiveTimestamp();
            const bool isNew = (ts != g_lastSampleTs);
            g_lastSampleTs = ts;

            auto interp = g_interp.Update(rawYaw, rawPitch, rawRoll, isNew, dt);

            const float eff = static_cast<float>(
                cameraunlock::math::GetEffectiveSmoothing(g_userSmoothing));
            if (!g_hasSmoothed) {
                g_smoothedYaw = interp.yaw;
                g_smoothedPitch = interp.pitch;
                g_smoothedRoll = interp.roll;
                g_hasSmoothed = true;
            } else {
                g_smoothedYaw   = cameraunlock::math::Smooth(g_smoothedYaw,   interp.yaw,   eff, dt);
                g_smoothedPitch = cameraunlock::math::Smooth(g_smoothedPitch, interp.pitch, eff, dt);
                g_smoothedRoll  = cameraunlock::math::Smooth(g_smoothedRoll,  interp.roll,  eff, dt);
            }
            outYaw   = g_smoothedYaw;
            outPitch = g_smoothedPitch;
            outRoll  = g_smoothedRoll;
            return true;
        }

        GetPlayerViewPoint_t g_origGetPlayerViewPoint = nullptr;
        std::atomic<std::uint64_t> g_hookCallCount{0};

        std::mutex g_callerMutex;
        std::unordered_map<std::uintptr_t, std::uint64_t> g_callerCounts;
        std::uint64_t g_callerLastSummaryCount = 0;

        constexpr std::uint64_t kCallerSummaryEvery = 1800;  // every ~30s at 60fps

        // Tracks the Pawn pointer the harvest last ran against; harvest
        // re-runs whenever this changes (e.g. pre-game default pawn ->
        // in-game pawn, or level-load reallocates the actor).
        std::atomic<std::uintptr_t> g_lastHarvestedPawn{0};


        // ---- Mask isolator -----------------------------------------------
        // Hotkey-driven cycling through every SceneComponent-shape UObject
        // reachable from the Pawn or Controller. For the selected candidate
        // we write an amplified head-tracker quaternion into its
        // ComponentToWorld.Rotation; the component that visibly tilts on
        // screen with head movement is the mask. Candidates are discovered
        // at runtime so we are not limited to a hardcoded slot table.
        struct MaskCandidate {
            std::uintptr_t srcObj;     // Pawn or Controller pointer
            std::size_t    srcOff;     // offset within srcObj
            std::uintptr_t comp;       // resolved component pointer (cached for log only)
            char           src[64];    // src path, e.g. "pawn", "r1<pawn+0x858>"
        };
        // Stable mask member identifier: source-path string + offset within
        // that source. Survives across game launches and pawn changes,
        // unlike the harvest-order slot index which is allocation-dependent.
        struct MaskMark {
            std::string src;
            std::size_t srcOff;
        };

        std::mutex                 g_maskMutex;
        std::vector<MaskCandidate> g_maskCandidates;
        std::atomic<int>           g_maskSlot{-1};
        std::atomic<std::uintptr_t> g_currentPawn{0};

        // Heuristic: a SceneComponent-shape UObject has a normalized FQuat4d
        // at +0x1f0 and an FVector3d at +0x210 plausibly near the player.
        bool LooksLikeSceneComponent(std::uintptr_t comp,
                                     double playerX, double playerY, double playerZ)
        {
            double qx = 0, qy = 0, qz = 0, qw = 0, tx = 0, ty = 0, tz = 0;
            __try {
                qx = *reinterpret_cast<const double*>(comp + 0x1f0);
                qy = *reinterpret_cast<const double*>(comp + 0x1f8);
                qz = *reinterpret_cast<const double*>(comp + 0x200);
                qw = *reinterpret_cast<const double*>(comp + 0x208);
                tx = *reinterpret_cast<const double*>(comp + 0x210);
                ty = *reinterpret_cast<const double*>(comp + 0x218);
                tz = *reinterpret_cast<const double*>(comp + 0x220);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            if (std::isnan(qx) || std::isnan(qy) || std::isnan(qz) || std::isnan(qw))
                return false;
            if (std::isnan(tx) || std::isnan(ty) || std::isnan(tz)) return false;
            const double qm = qx*qx + qy*qy + qz*qz + qw*qw;
            if (qm < 0.99 || qm > 1.01) return false;
            // Loc within 5km of player on each axis - excludes garbage triples.
            const double r = 500000.0;
            if (std::abs(tx - playerX) > r) return false;
            if (std::abs(ty - playerY) > r) return false;
            if (std::abs(tz - playerZ) > r) return false;
            return true;
        }

        // Walk a source object (Pawn or Controller) for any 8-byte aligned
        // pointer whose deref is a SceneComponent-shape UObject, and add it
        // to g_maskCandidates. Dedupes by resolved component pointer.
        void HarvestMaskCandidates(const char* src, std::uintptr_t obj,
                                   std::size_t range,
                                   double px, double py, double pz);

        // After a primary harvest pass, recurse into every freshly-added
        // candidate and look for SUB-components stored inside it (e.g. a
        // CameraComponent containing a Mesh1P pointer). Multiple passes
        // surface deeply nested components (mask top parts that live a
        // few levels down in the FP rig).
        void HarvestMaskCandidatesRecursive(int passes,
                                            std::size_t range,
                                            double px, double py, double pz)
        {
            for (int pass = 0; pass < passes; ++pass) {
                std::vector<MaskCandidate> seeds;
                {
                    std::lock_guard<std::mutex> lk(g_maskMutex);
                    seeds = g_maskCandidates;
                }
                const std::size_t before = seeds.size();
                for (const auto& seed : seeds) {
                    char label[64];
                    std::snprintf(label, sizeof(label), "r%d<%s+0x%zx>",
                        pass, seed.src, seed.srcOff);
                    HarvestMaskCandidates(label, seed.comp, range, px, py, pz);
                }
                std::size_t after = 0;
                {
                    std::lock_guard<std::mutex> lk(g_maskMutex);
                    after = g_maskCandidates.size();
                }
                Log::Line("mask-harvest pass %d: %zu -> %zu (+%zu)",
                    pass, before, after, after - before);
                if (after == before) break;  // converged
            }
        }

        void HarvestMaskCandidates(const char* src, std::uintptr_t obj,
                                   std::size_t range,
                                   double px, double py, double pz)
        {
            if (!obj) return;
            std::size_t before = 0;
            {
                std::lock_guard<std::mutex> lk(g_maskMutex);
                before = g_maskCandidates.size();
            }
            for (std::size_t off = 0; off < range; off += 8) {
                std::uintptr_t comp = 0;
                if (!SafeReadPtr(obj + off, comp)) continue;
                if (!LooksLikePointer(comp)) continue;
                // First qword must be a vtable in module range (i.e. a UObject).
                std::uintptr_t vt = 0;
                if (!SafeReadPtr(comp, vt)) continue;
                if (ue::ModuleBase() == 0 || vt < ue::ModuleBase() || vt >= ue::ModuleEnd())
                    continue;
                if (!LooksLikeSceneComponent(comp, px, py, pz)) continue;

                std::lock_guard<std::mutex> lk(g_maskMutex);
                bool dup = false;
                for (const auto& c : g_maskCandidates) {
                    if (c.comp == comp) { dup = true; break; }
                }
                if (dup) continue;
                MaskCandidate mc{};
                mc.srcObj = obj;
                mc.srcOff = off;
                mc.comp   = comp;
                std::snprintf(mc.src, sizeof(mc.src), "%s", src);
                g_maskCandidates.push_back(mc);
            }
            std::size_t after = 0;
            {
                std::lock_guard<std::mutex> lk(g_maskMutex);
                after = g_maskCandidates.size();
            }
            Log::Line("mask-harvest %s obj=0x%llx range=0x%zx  added %zu (total %zu)",
                src, static_cast<unsigned long long>(obj), range,
                after - before, after);
        }

        std::string DescribeMaskSlot(int slot) {
            if (slot < 0) return "OFF";
            std::lock_guard<std::mutex> lk(g_maskMutex);
            if (slot >= static_cast<int>(g_maskCandidates.size())) return "<out of range>";
            const auto& c = g_maskCandidates[slot];
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s+0x%zx -> 0x%llx",
                c.src, c.srcOff, static_cast<unsigned long long>(c.comp));
            return std::string(buf);
        }

        std::uintptr_t ResolveMaskSlotTarget(int slot) {
            if (slot < 0) return 0;
            std::lock_guard<std::mutex> lk(g_maskMutex);
            if (slot >= static_cast<int>(g_maskCandidates.size())) return 0;
            // Re-deref the source slot in case the underlying pointer changed.
            std::uintptr_t comp = 0;
            SafeReadPtr(g_maskCandidates[slot].srcObj + g_maskCandidates[slot].srcOff, comp);
            return comp;
        }


        // Mask group compensation. Slots 23..27 (in the harvest order from
        // the in-game pawn) were identified as the 5 components that make
        // up the dive mask. Toggle Mode lets the user verify the order /
        // composition empirically.
        std::atomic<bool>     g_maskCompEnabled{false};
        std::atomic<int>      g_maskCompMode{0};  // 0=H*old, 1=old*H, 2=Hinv*old, 3=old*Hinv
        std::mutex            g_maskMarksMutex;
        // Populated from the marks file at startup. Empty if no file exists.
        std::vector<MaskMark> g_maskMarks;
        std::wstring          g_marksFilePath;
        std::atomic<std::uint64_t> g_maskCompLogCount{0};

        // Per-slot snapshot of (rotation, translation) we last wrote. The
        // render-caller hook fires multiple times per frame (~4x); without
        // gating, each call composes H onto the previous result and the mask
        // ends up rotating by H^N. We remember what we wrote and skip if
        // the memory still reads back the same value (engine hasn't updated
        // since our last write).
        struct LastWritten { FQuat4d rot; FVector loc; bool valid; };
        std::unordered_map<int, LastWritten> g_lastWrittenBySlot;
        std::mutex g_lastWrittenMutex;

        // Last CLEAN engine value (oldRot/oldLoc) we accepted per slot, used to
        // reject transient garbage reads. The mask's ComponentToWorld is written
        // many times per frame by different render contexts; our GPV read almost
        // always catches the stable main-view value, but rarely coincides with a
        // transient other-context write. Comping that garbage produces the
        // occasional single-frame "weird position". Guarded by g_lastWrittenMutex.
        std::unordered_map<int, std::pair<FQuat4d, FVector>> g_lastCleanE;
        // Per-slot consecutive-reject counter. Real transient writes only last
        // a frame; a legitimate big move (load screen, fast travel, vehicle hop,
        // leviathan grab, cutscene snap) looks like garbage forever because
        // g_lastCleanE never updates on the reject path - so the mask welds to
        // the pre-move world position and disappears off-screen. After kMaxRejects
        // in a row, force-accept the read to let g_lastCleanE catch up.
        std::unordered_map<int, int> g_rejectStreakBySlot;
        constexpr int kMaxRejects = 3;
        std::atomic<std::uint64_t> g_maskAnomalyCount{0};
        std::atomic<std::uint64_t> g_maskForceAcceptCount{0};

        // Mask measurement mode. When ON, we DO NOT write the mask - we read
        // each marked slot's engine-driven ComponentToWorld.Rotation per frame
        // and log its delta from a baseline captured when the mode was armed,
        // alongside the head delta H. The per-axis ratio mask-delta/head-delta
        // reveals how the engine itself moves the mask with the head, which is
        // what tells us the correct compensation (none of the canned H/Hinv
        // modes converged, so the relationship isn't a clean full-H).
        std::atomic<bool> g_maskDiagEnabled{false};
        std::atomic<bool> g_maskDiagArm{false};
        std::mutex g_maskDiagMutex;
        std::unordered_map<int, FQuat4d> g_maskDiagBaseline;
        std::atomic<std::uint64_t> g_maskDiagLogCount{0};

#if SN2HT_DEV_HOTKEYS
        std::string DescribeMaskMembers() {
            std::lock_guard<std::mutex> lk(g_maskMarksMutex);
            std::string out;
            for (std::size_t i = 0; i < g_maskMarks.size(); ++i) {
                char b[64];
                std::snprintf(b, sizeof(b), "%s%s+0x%zx",
                    i ? "," : "",
                    g_maskMarks[i].src.c_str(),
                    g_maskMarks[i].srcOff);
                out += b;
            }
            if (out.empty()) out = "(none)";
            return out;
        }
#endif

        // Resolve every saved mark to a current slot index. Returns indices
        // into g_maskCandidates that match the saved (src, srcOff) tuples.
        std::vector<int> ResolveMaskMarkSlots() {
            std::vector<MaskMark>       marks;
            std::vector<MaskCandidate>  cands;
            {
                std::lock_guard<std::mutex> lk(g_maskMarksMutex);
                marks = g_maskMarks;
            }
            {
                std::lock_guard<std::mutex> lk(g_maskMutex);
                cands = g_maskCandidates;
            }
            std::vector<int> out;
            out.reserve(marks.size());
            for (const auto& m : marks) {
                for (int i = 0; i < static_cast<int>(cands.size()); ++i) {
                    if (cands[i].srcOff != m.srcOff) continue;
                    if (m.src != cands[i].src) continue;
                    out.push_back(i);
                    break;
                }
            }
            return out;
        }


        // true  = world-space yaw (default; horizon-locked, FRotator additive
        // path). Yaw rotates around world up regardless of pitch, so "up" is a
        // constant - looking at the floor and yawing still pans across it.
        // false = camera-local yaw (head rotates in camera-local frame, so
        // pitched-up turns produce leaning, like a real neck).
        // Initialized from [Tracking] WorldSpaceYaw at startup; flipped live by
        // Page Down / Ctrl+Shift+H.
        std::atomic<bool> g_worldSpaceYaw{true};


        void ApplyMaskGroupCompensation(double yawDeg, double pitchDeg, double rollDeg,
                                        const FVector& cameraPos,
                                        const FVector& posOffset,
                                        const FQuat4d& H) {
            if (!g_maskCompEnabled.load(std::memory_order_relaxed)) return;
            // H is the world-space rotation the hook actually applied to the
            // view (computed by the caller from base/final quats, so it is
            // correct whether the hook used world-yaw FRotator addition or
            // local-yaw quaternion composition).
            const FQuat4d Hinv = QuatInv(H);
            const int     mode = g_maskCompMode.load(std::memory_order_relaxed);

            std::vector<MaskCandidate> snap;
            {
                std::lock_guard<std::mutex> lk(g_maskMutex);
                snap = g_maskCandidates;
            }
            const int total = static_cast<int>(snap.size());
            std::vector<int> members = ResolveMaskMarkSlots();

            int wrote = 0;
            for (int i : members) {
                if (i < 0 || i >= total) continue;
                std::uintptr_t comp = 0;
                SafeReadPtr(snap[i].srcObj + snap[i].srcOff, comp);
                if (!comp) continue;
                const std::uintptr_t rotTarget =
                    comp + Offsets::USceneComponentLayout::kComponentToWorldRotation;
                const std::uintptr_t locTarget =
                    comp + Offsets::USceneComponentLayout::kComponentToWorldTranslation;
                FQuat4d oldRot{0, 0, 0, 1};
                FVector oldLoc{};
                if (!SafeReadFQuat(rotTarget, oldRot)) continue;
                if (!SafeReadFVector(locTarget, oldLoc)) continue;

                // Same-frame de-dup. If what we read matches what we last
                // wrote for this slot, the engine hasn't refreshed the
                // transform since - skip to avoid H-stacking per frame.
                {
                    std::lock_guard<std::mutex> lk(g_lastWrittenMutex);
                    auto it = g_lastWrittenBySlot.find(i);
                    if (it != g_lastWrittenBySlot.end() && it->second.valid) {
                        const auto& w = it->second;
                        const double drq =
                            std::abs(w.rot.X - oldRot.X) +
                            std::abs(w.rot.Y - oldRot.Y) +
                            std::abs(w.rot.Z - oldRot.Z) +
                            std::abs(w.rot.W - oldRot.W);
                        const double dlv =
                            std::abs(w.loc.X - oldLoc.X) +
                            std::abs(w.loc.Y - oldLoc.Y) +
                            std::abs(w.loc.Z - oldLoc.Z);
                        if (drq < 1e-6 && dlv < 1e-3) continue;
                    }
                }

                // Reject transient garbage reads. A real player can't rotate the
                // clean view >90 deg or move >1m in one frame, so a read that
                // jumps that far from last frame's accepted value is a transient
                // other-context write landing in ComponentToWorld. Comping it
                // gives the rare single-frame "weird position"; instead re-assert
                // last frame's good output and skip.
                {
                    std::lock_guard<std::mutex> lk(g_lastWrittenMutex);
                    auto git = g_lastCleanE.find(i);
                    if (git != g_lastCleanE.end()) {
                        const FQuat4d& pg = git->second.first;
                        const FVector& pl = git->second.second;
                        const double dot = std::abs(oldRot.X*pg.X + oldRot.Y*pg.Y
                                                  + oldRot.Z*pg.Z + oldRot.W*pg.W);
                        const double posJump = std::abs(oldLoc.X - pl.X)
                                             + std::abs(oldLoc.Y - pl.Y)
                                             + std::abs(oldLoc.Z - pl.Z);
                        if (dot < 0.707 || posJump > 100.0) {
                            const int streak = ++g_rejectStreakBySlot[i];
                            if (streak <= kMaxRejects) {
                                auto wit = g_lastWrittenBySlot.find(i);
                                if (wit != g_lastWrittenBySlot.end() && wit->second.valid) {
                                    SafeWriteFQuat(rotTarget, wit->second.rot);
                                    SafeWriteFVector(locTarget, wit->second.loc);
                                }
                                const auto an = g_maskAnomalyCount.fetch_add(1, std::memory_order_relaxed) + 1;
                                Log::Line("mask-anomaly #%llu slot=%d streak=%d dot=%.3f posJump=%.1f  badE=(%.2f,%.2f,%.2f) lastE=(%.2f,%.2f,%.2f) - reasserted",
                                    static_cast<unsigned long long>(an), i, streak, dot, posJump,
                                    oldLoc.X, oldLoc.Y, oldLoc.Z, pl.X, pl.Y, pl.Z);
                                continue;
                            }
                            // Streak exhausted: the engine has genuinely moved
                            // the player (load, teleport, vehicle hop) - accept
                            // the new read and let g_lastCleanE catch up, else
                            // the mask stays welded to the pre-move position
                            // and renders off-screen.
                            const auto fa = g_maskForceAcceptCount.fetch_add(1, std::memory_order_relaxed) + 1;
                            Log::Line("mask-force-accept #%llu slot=%d streak=%d dot=%.3f posJump=%.1f - re-anchoring",
                                static_cast<unsigned long long>(fa), i, streak, dot, posJump);
                        }
                    }
                    g_lastCleanE[i] = {oldRot, oldLoc};
                    g_rejectStreakBySlot[i] = 0;
                }

                // Pick the rotation we'll apply this iteration.
                FQuat4d newRot{};
                FQuat4d pivotQ{};
                switch (mode) {
                    case 0: newRot = QuatMul(H,    oldRot); pivotQ = H;    break;
                    case 1: newRot = QuatMul(oldRot, H);    pivotQ = H;    break;
                    case 2: newRot = QuatMul(Hinv, oldRot); pivotQ = Hinv; break;
                    case 3: newRot = QuatMul(oldRot, Hinv); pivotQ = Hinv; break;
                }

                // Rotate the world position around the CLEAN camera position
                // by pivotQ so the mask piece swings through space to stay in
                // front of the eyes - not just spinning around its own origin -
                // then add the 6DOF head-sway offset so the mask tracks the
                // camera's translation too (otherwise the position offset
                // parallax-swings the mask far more than the head rotated).
                FVector rel{
                    oldLoc.X - cameraPos.X,
                    oldLoc.Y - cameraPos.Y,
                    oldLoc.Z - cameraPos.Z};
                FVector newRel = QuatRotateVec(pivotQ, rel);
                FVector newLoc{
                    cameraPos.X + newRel.X + posOffset.X,
                    cameraPos.Y + newRel.Y + posOffset.Y,
                    cameraPos.Z + newRel.Z + posOffset.Z};

                bool ok = SafeWriteFQuat(rotTarget, newRot)
                      && SafeWriteFVector(locTarget, newLoc);
                if (ok) {
                    ++wrote;
                    std::lock_guard<std::mutex> lk(g_lastWrittenMutex);
                    g_lastWrittenBySlot[i] = LastWritten{newRot, newLoc, true};
                }
            }

            const auto n = g_maskCompLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n % 120) == 0) {
                Log::Line("mask-comp on  mode=%d  wrote=%d/%zu  H=(P=%.2f Y=%.2f R=%.2f)  pivot=(%.1f, %.1f, %.1f)",
                    mode, wrote, members.size(),
                    pitchDeg, yawDeg, rollDeg,
                    cameraPos.X, cameraPos.Y, cameraPos.Z);
            }
        }

        // Read-only mask measurement. Logs, per marked slot, the engine-driven
        // rotation delta (current vs baseline) in YPR degrees next to the head
        // delta H in YPR degrees. Never writes - this measures the engine's own
        // behaviour so we can derive the right comp. Arm with F6 (recaptures
        // baseline); move your head and read the ratios from the log.
        void MeasureMaskGroup(const FQuat4d& H) {
            if (g_maskDiagArm.exchange(false)) {
                std::lock_guard<std::mutex> lk(g_maskDiagMutex);
                g_maskDiagBaseline.clear();
            }

            std::vector<MaskCandidate> snap;
            {
                std::lock_guard<std::mutex> lk(g_maskMutex);
                snap = g_maskCandidates;
            }
            const int total = static_cast<int>(snap.size());
            const std::vector<int> members = ResolveMaskMarkSlots();

            const FRotator hRot = QuatToRotator(H);
            const auto n = g_maskDiagLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
            const bool doLog = (n == 1 || (n % 30) == 0);

            for (int i : members) {
                if (i < 0 || i >= total) continue;
                std::uintptr_t comp = 0;
                SafeReadPtr(snap[i].srcObj + snap[i].srcOff, comp);
                if (!comp) continue;
                const std::uintptr_t rotTarget =
                    comp + Offsets::USceneComponentLayout::kComponentToWorldRotation;
                FQuat4d cur{0, 0, 0, 1};
                if (!SafeReadFQuat(rotTarget, cur)) continue;

                FQuat4d baseline{};
                bool haveBaseline = false;
                {
                    std::lock_guard<std::mutex> lk(g_maskDiagMutex);
                    auto it = g_maskDiagBaseline.find(i);
                    if (it == g_maskDiagBaseline.end()) {
                        g_maskDiagBaseline[i] = cur;  // first read after arm
                    } else {
                        baseline = it->second;
                        haveBaseline = true;
                    }
                }
                if (!haveBaseline) continue;

                // delta = cur * baseline^-1 : the engine's rotation of the mask
                // since the baseline was captured (i.e. since the head was at
                // rest / recentered).
                const FQuat4d delta = QuatMul(cur, QuatInv(baseline));
                const FRotator dRot = QuatToRotator(delta);
                if (doLog) {
                    Log::Line("mask-diag slot=%d (%s) headH=(Y=%.2f P=%.2f R=%.2f) maskDelta=(Y=%.2f P=%.2f R=%.2f)",
                        i, DescribeMaskSlot(i).c_str(),
                        hRot.Yaw, hRot.Pitch, hRot.Roll,
                        dRot.Yaw, dRot.Pitch, dRot.Roll);
                }
            }
            if (doLog && members.empty()) {
                Log::Line("mask-diag: no marked slots (mark the mask with Ins+F11 first)");
            }
        }

        std::atomic<std::uint64_t> g_maskWriteLogCount{0};
        // Persisted between calls so we can detect engine stomp: write Q,
        // read back next call, log the diff if the engine overwrote it.
        FQuat4d g_lastWrittenQ{0, 0, 0, 1};
        std::uintptr_t g_lastWrittenTarget = 0;

        void ApplyMaskIsolator(double yawDeg, double pitchDeg, double rollDeg) {
            const int slot = g_maskSlot.load(std::memory_order_relaxed);
            if (slot < 0) return;
            const std::uintptr_t comp = ResolveMaskSlotTarget(slot);
            if (!comp) return;

            const std::uintptr_t target =
                comp + Offsets::USceneComponentLayout::kComponentToWorldRotation;

            // Read what's there BEFORE we overwrite. If this is our previously
            // written Q, the engine left it alone last frame; if it's
            // something else, the engine stomped us.
            FQuat4d beforeWrite{0, 0, 0, 0};
            SafeReadFQuat(target, beforeWrite);

            // Static large rotation (90deg yaw) so the visible distortion
            // doesn't depend on the user moving their head - this is purely
            // diagnostic of "is the engine using our write at all".
            const FQuat4d q = QuatFromEulerDeg(0.0, 90.0, 0.0);
            const bool ok = SafeWriteFQuat(target, q);
            g_lastWrittenQ      = q;
            g_lastWrittenTarget = target;
            (void)yawDeg; (void)pitchDeg; (void)rollDeg;

            const auto n = g_maskWriteLogCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n % 60) == 0) {
                // Compare beforeWrite vs the Q we wrote LAST call. If they
                // match closely, our write persisted across the frame.
                const double dq =
                    std::abs(beforeWrite.X - q.X) +
                    std::abs(beforeWrite.Y - q.Y) +
                    std::abs(beforeWrite.Z - q.Z) +
                    std::abs(beforeWrite.W - q.W);
                Log::Line("mask-isolator slot=%d (%s) comp=0x%llx  "
                          "wrote Q=(%.4f,%.4f,%.4f|w=%.4f) ok=%d  "
                          "preWrite=(%.4f,%.4f,%.4f|w=%.4f)  persisted=%s  diff=%.4f",
                    slot, DescribeMaskSlot(slot).c_str(),
                    static_cast<unsigned long long>(comp),
                    q.X, q.Y, q.Z, q.W, ok ? 1 : 0,
                    beforeWrite.X, beforeWrite.Y, beforeWrite.Z, beforeWrite.W,
                    dq < 0.01 ? "YES" : "NO (engine stomped)",
                    dq);
            }
        }

        // ---- ProcessEvent-based UMG reticle mover --------------------------
        using ProcessEvent_t = void(__fastcall*)(void* self, void* func, void* params);
        // UObject::ProcessEvent is resolved from the UObject vtable slot at
        // runtime, not a hardcoded RVA. The slot is an engine-ABI constant
        // (stable across same-engine content patches), whereas the RVA moves
        // on every relink - which is exactly what broke this mod on the
        // 2026-05-22 patch.
        //
        // MUST resolve from the same kind of object we call it on - a UWidget.
        // AActor OVERRIDES slot 76 with its RPC-aware ProcessEvent (net-mode /
        // authority checks, then delegates to the base). Resolving off the
        // controller picked up that override; called on a widget its world/net
        // derefs fault and SafeProcessEvent swallows it, so the stock reticle
        // never hid. A widget does not override slot 76, so its slot gives the
        // base UObject::ProcessEvent we actually want.
        constexpr std::size_t kProcessEventVtableSlot = 76;
        ProcessEvent_t g_processEvent = nullptr;
        std::atomic<bool> g_peResolved{false};

        // Resolve ProcessEvent from a UWidget's vtable slot (NOT an actor - see
        // above). One-shot: logs a slot window so the slot can be re-pinned
        // after an engine update.
        void ResolveProcessEvent(std::uintptr_t obj) {
            if (g_peResolved.exchange(true)) return;
            std::uintptr_t vtbl = 0;
            if (!SafeReadPtr(obj, vtbl) || !vtbl) { g_peResolved.store(false); return; }
            for (std::size_t s = kProcessEventVtableSlot - 4; s <= kProcessEventVtableSlot + 4; ++s) {
                std::uintptr_t fn = 0;
                if (SafeReadPtr(vtbl + s * 8, fn) && fn >= ue::ModuleBase())
                    Log::Line("PE-probe vt[%zu] RVA 0x%08llx", s,
                        static_cast<unsigned long long>(fn - ue::ModuleBase()));
            }
            std::uintptr_t pe = 0;
            if (SafeReadPtr(vtbl + kProcessEventVtableSlot * 8, pe) && pe >= ue::ModuleBase()) {
                g_processEvent = reinterpret_cast<ProcessEvent_t>(pe);
                Log::Line("ProcessEvent resolved via vt[%zu] -> RVA 0x%08llx",
                    kProcessEventVtableSlot,
                    static_cast<unsigned long long>(pe - ue::ModuleBase()));
            }
        }


        // Separate non-unwinding fn so __try is legal (callers hold std::string).
        bool SafeProcessEvent(void* self, void* fn, void* params) {
            __try {
                g_processEvent(self, fn, params);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        // A collected widget plus the class pointer it had at collect time.
        // Loading a save frees/recreates widgets, so a held pointer can become
        // dangling or be reused for a different object. Before any ProcessEvent
        // we re-read +0x10 on the game thread and require it still equals Cls -
        // a freed/reused slot won't match and is skipped. This is what keeps
        // save-load from crashing in the script VM.
        struct ReticleWidget { std::uintptr_t Obj; std::uintptr_t Cls; };

        std::mutex                  g_reticleMutex;
        std::vector<ReticleWidget>  g_reticleWidgets;
        std::atomic<std::uintptr_t> g_getOpacityFn{0};
        std::atomic<std::uintptr_t> g_setOpacityFn{0};
        std::atomic<std::uintptr_t> g_getVisFn{0};
        std::atomic<std::uintptr_t> g_setVisFn{0};
        std::atomic<bool>           g_reticleMoveOn{true};

        // The interaction tooltip ("hover an interactable") is the UMG Overlay
        // named "InteractionPromptContainer" (found via the keyword dump). The
        // game anchors it to screen centre; we push the body-forward reticle
        // offset into its RenderTransform.Translation each gameplay frame so the
        // prompt sits where the body is actually aiming. Two live instances
        // (archetype + painted), same pattern as the reticle - we move both; the
        // unrendered one is harmless. setRenderTranslation takes an FVector2D of
        // two floats in this build (see project_sn2_reticle_widget memory).
        std::vector<ReticleWidget>  g_tooltipWidgets;
        std::atomic<std::uintptr_t> g_setRenderTranslationFn{0};
        std::atomic<bool>           g_tooltipMoveOn{true};
        // Backbuffer pixels -> UMG slate units. 1.0 holds at viewport DPI scale
        // 1.0; tune via [Tooltip] FollowScale if the prompt over/undershoots the
        // reticle at the user's resolution.
        std::atomic<float>          g_tooltipFollowScale{1.0f};

        // Re-validate a collected widget on the game thread immediately before
        // calling into it: the held pointer must still carry the class it had
        // at collect time, else it was freed/reused (save load) and is unsafe.
        bool ReticleWidgetLive(const ReticleWidget& w) {
            std::uintptr_t cls = 0;
            return SafeReadPtr(w.Obj + Offsets::UObjectGlobals::kClassPrivate, cls)
                && cls == w.Cls;
        }

        // Collect the reticle-texture widgets by name. The ref-scan dump showed
        // the on-screen reticle is drawn by these UImage widgets (UMG keeps
        // archetype copies alongside the painted instance, all live UObjects;
        // we can't tell which is on-screen by name, so we zero RenderOpacity on
        // every instance and the painted one is necessarily among them). Name
        // matching is cheap - the byte-scan variant froze the game thread.
        void CollectReticleWidgets() {
            static const char* kReticleNames[] = {
                "InteractionIcon", "Decor", "ArrowTexture", "ReticleOverlay",
            };
            std::vector<ReticleWidget> found;
            std::vector<ReticleWidget> tips;
            ForEachUObject([&](std::uintptr_t obj) -> bool {
                const std::string on = ObjectName(obj);
                if (ContainsCI(on, "Default__")) return false;  // skip CDOs
                // World-interaction tooltip: instances of
                // /Game/Blueprints/UI/HUD/WorldObjectHint/WBP_HoverTargetInfo
                // are the per-prompt controller widget. The instance name is
                // just the class name ("WBP_HoverTargetInfo_C") so match by
                // class substring rather than exact instance name. Targeting
                // the controller (not its hint children) avoids the cascading
                // double-translation that sent the prompt rows flying when we
                // moved the row widgets directly.
                {
                    const std::string cn = ClassName(obj);
                    if (ContainsCI(cn, "HoverTargetInfo")) {
                        std::uintptr_t cls = 0;
                        if (SafeReadPtr(obj + Offsets::UObjectGlobals::kClassPrivate, cls) && cls)
                            tips.push_back({obj, cls});
#if SN2HT_DEV_HOTKEYS
                        // Discriminator hunt: button-bar tooltips (eg air bladder
                        // "Inhale / Ascend") share the HoverTargetInfo class with
                        // world-target prompts, so they get moved too. Log the
                        // Outer chain (5 levels) once per instance so we can spot
                        // the parent that pins the button-bar variant in place,
                        // and add it as a skip-filter here.
                        {
                            static std::mutex chainMutex;
                            static std::unordered_set<std::uintptr_t> chainLogged;
                            std::lock_guard<std::mutex> lk(chainMutex);
                            if (chainLogged.insert(obj).second) {
                                Log::Line("tooltip-chain: HoverTargetInfo 0x%llx class=%s name=%s",
                                    static_cast<unsigned long long>(obj),
                                    cn.c_str(), on.c_str());
                                std::uintptr_t cur = obj;
                                for (int depth = 0; depth < 5; ++depth) {
                                    std::uintptr_t outer = 0;
                                    if (!SafeReadPtr(cur + Offsets::UObjectGlobals::kOuterPrivate, outer) || !outer)
                                        break;
                                    Log::Line("  outer[%d] 0x%llx class=%s name=%s",
                                        depth,
                                        static_cast<unsigned long long>(outer),
                                        ClassName(outer).c_str(),
                                        ObjectName(outer).c_str());
                                    cur = outer;
                                }
                            }
                        }
#endif
                        return false;
                    }
                }
                for (const char* nm : kReticleNames) {
                    if (on == nm) {
                        std::uintptr_t cls = 0;
                        if (SafeReadPtr(obj + Offsets::UObjectGlobals::kClassPrivate, cls) && cls)
                            found.push_back({obj, cls});
                        return false;
                    }
                }
                return false;
            });
            {
                std::lock_guard<std::mutex> lk(g_reticleMutex);
                g_reticleWidgets = found;
                g_tooltipWidgets = tips;
            }
            // These are engine UFunction objects (UWidget methods); they are
            // created once and live for the process lifetime, so resolve each
            // exactly once. Re-scanning every 2s cost four full ~244k-object
            // enumerations per call for a result that never changes. Each
            // FindLiveObject below only runs while its target is still
            // unresolved (during warmup, before the widget classes exist).
            if (!g_getOpacityFn.load())
                g_getOpacityFn.store(FindLiveObject("Function", "GetRenderOpacity", "Widget"));
            if (!g_setOpacityFn.load())
                g_setOpacityFn.store(FindLiveObject("Function", "SetRenderOpacity", "Widget"));
            if (!g_getVisFn.load())
                g_getVisFn.store(FindLiveObject("Function", "GetVisibility", "Widget"));
            if (!g_setVisFn.load())
                g_setVisFn.store(FindLiveObject("Function", "SetVisibility", "Widget"));
            if (!g_setRenderTranslationFn.load())
                g_setRenderTranslationFn.store(FindLiveObject("Function", "SetRenderTranslation", "Widget"));
            static std::size_t lastCount = SIZE_MAX;
            if (found.size() != lastCount) {
                lastCount = found.size();
                Log::Line("reticle: collected %zu reticle-texture + %zu tooltip widget(s)  setOpacity=0x%llx setVis=0x%llx setRenderTr=0x%llx",
                    found.size(), tips.size(),
                    static_cast<unsigned long long>(g_setOpacityFn.load()),
                    static_cast<unsigned long long>(g_setVisFn.load()),
                    static_cast<unsigned long long>(g_setRenderTranslationFn.load()));
            }
        }


        // Discovery: find the live HUD object(s) and dump every child-widget
        // pointer they hold, with the child's class/name/visibility/opacity, so
        // we can spot the on-screen reticle child (it isn't a free-standing
        // named widget - the SN2 HUD Blueprint owns it). Press in gameplay.
#if SN2HT_DEV_HOTKEYS
        void DumpHudWidgets() {
            const std::uintptr_t getVis = g_getVisFn.load();
            const std::uintptr_t getOp  = g_getOpacityFn.load();
            std::vector<std::uintptr_t> huds;
            ForEachUObject([&](std::uintptr_t obj) -> bool {
                const std::string on = ObjectName(obj);
                if (ContainsCI(on, "Default__")) return false;
                const std::string cn = ClassName(obj);
                if (ContainsCI(cn, "HUD") || ContainsCI(cn, "WBP_HUD")) {
                    huds.push_back(obj);
                    Log::Line("hud-obj 0x%llx class=%s name=%s",
                        static_cast<unsigned long long>(obj), cn.c_str(),
                        on.c_str());
                }
                return false;
            });
            for (std::uintptr_t hud : huds) {
                const std::string hudName = ObjectName(hud);
                for (std::size_t off = 0x28; off <= 0x2000; off += 8) {
                    std::uintptr_t child = 0;
                    if (!SafeReadPtr(hud + off, child) || !child) continue;
                    const std::string ccn = ClassName(child);
                    if (ccn.empty()) continue;
                    // Real UWidget paint types only - skip classes, trees,
                    // slots, animations, view models (getters on those are UB).
                    static const char* kSkip[] = {
                        "Slot", "Tree", "Class", "Animation", "ViewModel",
                        "Function", "Package", "Blueprint",
                    };
                    bool skip = false;
                    for (const char* k : kSkip)
                        if (ContainsCI(ccn, k)) { skip = true; break; }
                    if (skip) continue;
                    static const char* kWidgetish[] = {
                        "Image", "Overlay", "Text", "Border", "Canvas",
                        "SizeBox", "Button", "Widget", "Reticle", "Crosshair",
                        "Scale", "Spacer", "Panel",
                    };
                    bool widgetish = false;
                    for (const char* k : kWidgetish)
                        if (ContainsCI(ccn, k)) { widgetish = true; break; }
                    if (!widgetish) continue;
                    std::uint8_t vis = 0xff;
                    if (getVis) SafeProcessEvent(reinterpret_cast<void*>(child),
                        reinterpret_cast<void*>(getVis), &vis);
                    if (vis == 1 || vis == 2 || vis == 0xff) continue;  // visible only
                    float op[4] = {0};
                    if (getOp) SafeProcessEvent(reinterpret_cast<void*>(child),
                        reinterpret_cast<void*>(getOp), op);
                    Log::Line("  [%s] +0x%zx 0x%llx class=%s name=%s vis=%u op=%.2f",
                        hudName.c_str(), off, static_cast<unsigned long long>(child),
                        ccn.c_str(), ObjectName(child).c_str(), vis, op[0]);
                }
            }
            Log::Line("hud-dump: %zu HUD object(s)", huds.size());
        }

        // Discovery for the interaction tooltip ("hover an interactable" prompt,
        // text sourced from ST_WorldObjectHint). Scans every live UObject for
        // names/classes/outers carrying an interaction keyword and logs each,
        // plus - for the widget-shaped ones - live ESlateVisibility, opacity and
        // the RenderTransform.Translation at +0x90 (two floats). Press WHILE
        // hovering an interactable so the prompt is on screen: the widget(s) that
        // flip to a drawn visibility are the prompt; their shared OuterName is
        // the container to move. Press again NOT hovering to see what collapses.
        void DumpInteractionWidgets() {
            const std::uintptr_t getVis = g_getVisFn.load();
            const std::uintptr_t getOp  = g_getOpacityFn.load();
            static const char* kKeywords[] = {
                "Interact", "Prompt", "Hint", "Tooltip", "ObjectName",
                "WorldObject", "UseText", "HoverText", "Reticle",
            };
            static const char* kWidgetish[] = {
                "Image", "Overlay", "Text", "Border", "Canvas", "SizeBox",
                "Button", "Widget", "Scale", "Spacer", "Panel", "Box",
            };
            std::size_t hits = 0;
            ForEachUObject([&](std::uintptr_t obj) -> bool {
                const std::string on = ObjectName(obj);
                if (on.empty() || ContainsCI(on, "Default__")) return false;
                const std::string cn = ClassName(obj);
                const std::string ou = OuterName(obj);
                bool match = false;
                for (const char* k : kKeywords)
                    if (ContainsCI(on, k) || ContainsCI(cn, k) || ContainsCI(ou, k)) {
                        match = true; break;
                    }
                if (!match) return false;
                ++hits;
                bool widgetish = false;
                for (const char* k : kWidgetish)
                    if (ContainsCI(cn, k)) { widgetish = true; break; }
                std::uint8_t vis = 0xff;
                float op[4] = {0};
                float tx = 0.0f, ty = 0.0f;
                if (widgetish) {
                    if (getVis) SafeProcessEvent(reinterpret_cast<void*>(obj),
                        reinterpret_cast<void*>(getVis), &vis);
                    if (getOp) SafeProcessEvent(reinterpret_cast<void*>(obj),
                        reinterpret_cast<void*>(getOp), op);
                    std::uint32_t bx = 0, by = 0;
                    if (SafeReadU32(obj + 0x90, bx)) std::memcpy(&tx, &bx, 4);
                    if (SafeReadU32(obj + 0x94, by)) std::memcpy(&ty, &by, 4);
                }
                Log::Line("  interact-cand 0x%llx class=%s name=%s outer=%s%s vis=%u op=%.2f rt=(%.1f,%.1f)",
                    static_cast<unsigned long long>(obj), cn.c_str(), on.c_str(),
                    ou.c_str(), widgetish ? "" : " [non-widget]",
                    vis, op[0], tx, ty);
                return false;
            });
            Log::Line("interact-dump: %zu keyword match(es)  (vis 0/3/4 = drawn, 1=collapsed, 2=hidden)", hits);
        }

        // Discovery for held-item / hand-attached UWidgetComponents (the 3D-
        // rendered Slate widgets used for context prompts like the air-bladder
        // Inhale/Ascend tooltip). They sit at a fixed world position near the
        // player's hand, so our head-tracked view projects them to a different
        // screen location than the clean view - the same drift family as the
        // mask. To fix it we need the address + parent chain so we can extend
        // the GPV-time comp set (see project_sn2_updatecomponenttoworld memory)
        // to inverse-rotate their ComponentToWorld.
        //
        // Dumps every UWidgetComponent in the live UObject table with its
        // world position (ComponentToWorld.Translation @+0x210, 3 doubles per
        // USceneComponentLayout) and a 5-level Outer chain. Press while the
        // air-bladder is equipped AND its prompt is visible.
        void DumpWidgetComponents() {
            using Offsets::USceneComponentLayout::kComponentToWorldTranslation;
            using Offsets::UObjectGlobals::kOuterPrivate;
            std::size_t hits = 0;
            ForEachUObject([&](std::uintptr_t obj) -> bool {
                const std::string cn = ClassName(obj);
                if (!ContainsCI(cn, "WidgetComponent")) return false;
                const std::string on = ObjectName(obj);
                if (ContainsCI(on, "Default__")) return false;
                ++hits;
                double tx = 0.0, ty = 0.0, tz = 0.0;
                std::uintptr_t qw = 0;
                if (SafeReadPtr(obj + kComponentToWorldTranslation + 0,  qw)) std::memcpy(&tx, &qw, 8);
                if (SafeReadPtr(obj + kComponentToWorldTranslation + 8,  qw)) std::memcpy(&ty, &qw, 8);
                if (SafeReadPtr(obj + kComponentToWorldTranslation + 16, qw)) std::memcpy(&tz, &qw, 8);
                Log::Line("widget-comp 0x%llx class=%s name=%s  worldPos=(%.1f,%.1f,%.1f)",
                    static_cast<unsigned long long>(obj), cn.c_str(), on.c_str(),
                    tx, ty, tz);
                std::uintptr_t cur = obj;
                for (int depth = 0; depth < 5; ++depth) {
                    std::uintptr_t outer = 0;
                    if (!SafeReadPtr(cur + kOuterPrivate, outer) || !outer) break;
                    Log::Line("  outer[%d] 0x%llx class=%s name=%s",
                        depth,
                        static_cast<unsigned long long>(outer),
                        ClassName(outer).c_str(),
                        ObjectName(outer).c_str());
                    cur = outer;
                }
                return false;
            });
            Log::Line("widget-comp dump: %zu instance(s)", hits);
        }
#endif

        // True during gameplay (head-tracking reticle should be drawn), false
        // in UI contexts: main menu, PDA, pause. Reads APlayerController::
        // bShowMouseCursor directly off the hook's `self` - the cursor is shown
        // in every UI mode and hidden during play. This sidesteps the
        // widget-visibility approach, which couldn't distinguish the painted
        // on-screen reticle from always-"Visible" UMG widget-tree templates.
        bool InGameplay(std::uintptr_t controller) {
            std::uint32_t v = 0;
            if (!SafeReadU32(controller + Offsets::PlayerController::kShowMouseCursorOffset, v))
                return false;
            return (v & Offsets::PlayerController::kShowMouseCursorMask) == 0;
        }

        // Per render frame: force the InteractionIcon reticle to opacity 0 via
        // SetRenderOpacity (single float, confirmed). Driven from many GPV
        // callers per frame to win against any per-frame reset. If the on-screen
        // reticle vanishes, this is definitively the widget AND the setter
        // propagates to Slate.
        void DriveReticleMove() {
            if (!g_reticleMoveOn.load(std::memory_order_relaxed)) return;
            const std::uintptr_t setOp = g_setOpacityFn.load();
            if (!setOp) return;
            std::vector<ReticleWidget> widgets;
            {
                std::lock_guard<std::mutex> lk(g_reticleMutex);
                widgets = g_reticleWidgets;
            }
            if (widgets.empty()) return;
            // Resolve ProcessEvent off a widget (not the controller) - see the
            // note on kProcessEventVtableSlot. Done here, lazily, because this
            // is the first place a confirmed UWidget pointer is in hand.
            if (!g_processEvent) {
                for (const ReticleWidget& w : widgets) {
                    if (ReticleWidgetLive(w)) { ResolveProcessEvent(w.Obj); break; }
                }
                if (!g_processEvent) return;
            }
            struct { float Opacity; char pad[28]; } op{};
            op.Opacity = 0.0f;
            for (const ReticleWidget& w : widgets) {
                if (!ReticleWidgetLive(w)) continue;
                SafeProcessEvent(reinterpret_cast<void*>(w.Obj),
                                 reinterpret_cast<void*>(setOp), &op);
            }
        }

        // Per gameplay frame: drag the interaction tooltip to the body-forward
        // reticle by writing its RenderTransform.Translation. The reticle overlay
        // publishes the offset (backbuffer px from centre) it already projects
        // each frame; we scale it to slate units and push it through
        // SetRenderTranslation. When no reticle is drawn (tracking off / aim
        // behind view) the offset is (0,0), recentring the prompt - so toggling
        // tracking off mid-gameplay snaps the tooltip cleanly back to centre.
        void DriveTooltipMove() {
            if (!g_tooltipMoveOn.load(std::memory_order_relaxed)) return;
            const std::uintptr_t setTr = g_setRenderTranslationFn.load();
            if (!setTr) return;
            std::vector<ReticleWidget> widgets;
            {
                std::lock_guard<std::mutex> lk(g_reticleMutex);
                widgets = g_tooltipWidgets;
            }
            if (widgets.empty()) return;
            if (!g_processEvent) {
                for (const ReticleWidget& w : widgets) {
                    if (ReticleWidgetLive(w)) { ResolveProcessEvent(w.Obj); break; }
                }
                if (!g_processEvent) return;
            }
            float dx = 0.0f, dy = 0.0f;
            const bool haveOffset = ReticleOverlay::GetScreenOffset(dx, dy);
            float rawX = dx, rawY = dy;
            if (haveOffset) {
                const float s = g_tooltipFollowScale.load(std::memory_order_relaxed);
                dx *= s;
                dy *= s;
                // No hard pixel clamp here - the NDC clamp in DrawReticle already
                // bounds the raw offset to the visible viewport (no projection
                // runaway at extreme head angles), so widget motion is bounded by
                // viewport_half_px * FollowScale. A separate px clamp on top broke
                // FollowScale linearity at moderate head turns, making live tuning
                // impossible to converge.
            }
            // UE5 LWC: FVector2D = 2 doubles, NOT 2 floats. Passing floats
            // here makes the engine read bytes 0-7 as a single double (the two
            // float bit-patterns concatenated decode to ~1e17), then write that
            // garbage into RenderTransform.Translation - which is exactly the
            // "widget snaps thousands of pixels off-screen on the tiniest head
            // turn" symptom. The byte-verbatim readback at +0x90 as a float
            // shows our original value either way and is misleading.
            struct { double X; double Y; char pad[16]; } tr{};
            tr.X = static_cast<double>(dx);
            tr.Y = static_cast<double>(dy);
            static std::atomic<std::uint64_t> n{0};
            const bool logThis = (n.fetch_add(1) % 240) == 0;
            if (logThis)
                Log::Line("tooltip-move: haveOffset=%d raw=(%.1f,%.1f) off=(%.1f,%.1f) widgets=%zu scale=%.2f",
                    haveOffset ? 1 : 0, rawX, rawY, dx, dy, widgets.size(),
                    g_tooltipFollowScale.load());
            for (const ReticleWidget& w : widgets) {
                if (!ReticleWidgetLive(w)) continue;
                const bool peOk = SafeProcessEvent(reinterpret_cast<void*>(w.Obj),
                                                   reinterpret_cast<void*>(setTr), &tr);
                if (logThis) {
                    // Read +0x90/+0x94 as floats AND +0x90/+0x98 as doubles so
                    // we can tell whether RenderTransform.Translation is laid
                    // out as 2 floats or 2 doubles. Float-pair fits in the
                    // first 8 bytes; double-pair occupies 16. Whichever set
                    // sanely matches the values we wrote is the real layout.
                    float fX = 0.0f, fY = 0.0f;
                    double dX = 0.0, dY = 0.0;
                    std::uint32_t b32 = 0;
                    std::uintptr_t b64 = 0;
                    if (SafeReadU32(w.Obj + 0x90, b32)) std::memcpy(&fX, &b32, 4);
                    if (SafeReadU32(w.Obj + 0x94, b32)) std::memcpy(&fY, &b32, 4);
                    if (SafeReadPtr(w.Obj + 0x90, b64)) std::memcpy(&dX, &b64, 8);
                    if (SafeReadPtr(w.Obj + 0x98, b64)) std::memcpy(&dY, &b64, 8);
                    Log::Line("  ttip 0x%llx name=%s peOk=%d rb f@(0x90,0x94)=(%.2f,%.2f) d@(0x90,0x98)=(%.2f,%.2f)",
                        static_cast<unsigned long long>(w.Obj),
                        ObjectName(w.Obj).c_str(),
                        peOk ? 1 : 0, fX, fY, dX, dY);
                }
            }
        }

        // Re-bisected from a runtime caller-summary after the 2026-05-19 patch
        // (GPV is virtual; callers can't be relocated statically). Mapped 1:1
        // to the pre-patch list by RVA proximity + per-frame hit count. Re-run
        // with inject-mode 0 and re-derive from caller-summary after any patch.
        constexpr std::array<std::uintptr_t, 11> kKnownCallerRvas = {
            0x06329c08,  // 1: ~22/frame, dominant
            0x04171af7,  // 2: ~4/frame  <- render (FMinimalViewInfo builder, fn 0x41718b0)
            0x043eae77,  // 3: ~2/frame
            0x00000000,  // 4: intermittent; did not fire during re-bisection (unconfirmed)
            0x03fc993c,  // 5: ~1/frame
            0x04177e2d,  // 6: ~1/frame
            0x02b5de88,  // 7: ~1/frame
            0x051149d1,  // 8: ~1/frame
            0x063e82d9,  // 9: ~1/frame
            0x05106f00,  // 10: one-shot at startup
            0x068d91a5,  // 11: rare
        };

        // Injection mode controls which call-sites get head-tracking applied.
        // 0  = all callers (entangles aim with view - diagnostic only)
        // 1..11 = inject only for kKnownCallerRvas[mode-1]
        // 12 = no caller (head tracking disabled at the hook)
        // 13 = all per-frame callers (modes 5-9 grouped)
        // 14 = all tier 1+2 callers (modes 1-4 grouped)
        //
        // Default is mode 2: only caller [2] at RVA 0x04171af7 receives
        // head-tracking injection. That call site is inside fn 0x41718b0
        // which builds an FMinimalViewInfo for the renderer; isolating it
        // leaves CameraCachePOV clean for weapons/AI/raycasts (aim
        // decoupling) while the rendered view still rotates with the head.
        std::atomic<int> g_injectMode{2};
        constexpr int kNumInjectModes = 15;

#if SN2HT_DEV_HOTKEYS
        const char* InjectModeName(int mode) {
            switch (mode) {
                case 0:  return "ALL (current)";
                case 12: return "NONE";
                case 13: return "all PER-FRAME callers (5-9)";
                case 14: return "all TIER1+2 callers (1-4)";
                default: return "single caller";
            }
        }
#endif

        bool ShouldInjectForCaller(std::uintptr_t retRva, int mode) {
            if (mode == 0) return true;
            if (mode == 12) return false;
            if (mode >= 1 && mode <= 11) {
                return retRva == kKnownCallerRvas[mode - 1];
            }
            if (mode == 13) {
                for (int i = 4; i <= 8; ++i) {
                    if (retRva == kKnownCallerRvas[i]) return true;
                }
                return false;
            }
            if (mode == 14) {
                for (int i = 0; i <= 3; ++i) {
                    if (retRva == kKnownCallerRvas[i]) return true;
                }
                return false;
            }
            return true;
        }

        void DumpCallerSummary(std::uint64_t totalCalls)
        {
            std::lock_guard<std::mutex> lock(g_callerMutex);
            Log::Line("caller-summary @%llu calls: %zu unique return RVAs:",
                static_cast<unsigned long long>(totalCalls), g_callerCounts.size());
            for (const auto& kv : g_callerCounts) {
                Log::Line("  ret RVA 0x%08llx  count=%llu",
                    static_cast<unsigned long long>(kv.first),
                    static_cast<unsigned long long>(kv.second));
            }
        }

        // Run the positional pipeline and produce a world-space offset (UE
        // units = cm) to add to the camera location. The offset is built in
        // the CLEAN-camera frame (baseQ, mouse/controller orientation) so head
        // sway follows the body, not the head-rotated view - per the position
        // tracking rule in the camera-system doc. yaw/pitch/roll are the
        // already-processed head angles, fed to the processor for tracker-pivot
        // compensation. Returns false when position is disabled or no data.
        bool GetProcessedPositionOffset(const FQuat4d& baseQ,
                                        float yaw, float pitch, float roll,
                                        FVector& outOffsetUE) {
            if (!g_positionEnabled.load(std::memory_order_relaxed)) return false;
            float px = 0.0f, py = 0.0f, pz = 0.0f;
            if (!g_receiver->GetPosition(px, py, pz)) return false;

            const float dt = g_posClock.Tick();

            const std::int64_t ts = g_receiver->GetLastReceiveTimestamp();
            cameraunlock::PositionData raw(px, py, pz, ts);

            if (g_posCenterPending.exchange(false)) {
                g_posProcessor.SetCenter(raw);
                g_posProcessor.ResetSmoothing();
                g_posInterp.Reset();
            }

            const cameraunlock::PositionData interp = g_posInterp.Update(raw, dt);
            const cameraunlock::math::Quat4 headQ =
                cameraunlock::math::Quat4::FromYawPitchRoll(yaw, pitch, roll);
            // Clamped offset in tracker axes, meters: x=sway(right),
            // y=heave(up), z=surge(forward).
            const cameraunlock::math::Vec3 off =
                g_posProcessor.Process(interp, headQ, dt);

            // Clean-camera basis (UE: X=forward, Y=right, Z=up).
            const FVector camFwd   = QuatRotateVec(baseQ, FVector{1.0, 0.0, 0.0});
            const FVector camRight = QuatRotateVec(baseQ, FVector{0.0, 1.0, 0.0});
            const FVector camUp    = QuatRotateVec(baseQ, FVector{0.0, 0.0, 1.0});
            constexpr double kMetersToUE = 100.0;
            const double s = static_cast<double>(off.z) * kMetersToUE;  // surge -> forward
            const double r = static_cast<double>(off.x) * kMetersToUE;  // sway  -> right
            const double u = static_cast<double>(off.y) * kMetersToUE;  // heave -> up
            outOffsetUE.X = camFwd.X * s + camRight.X * r + camUp.X * u;
            outOffsetUE.Y = camFwd.Y * s + camRight.Y * r + camUp.Y * u;
            outOffsetUE.Z = camFwd.Z * s + camRight.Z * r + camUp.Z * u;
            return true;
        }

        void __fastcall GetPlayerViewPoint_Hook(void* self, FVector* outLocation, FRotator* outRotation)
        {
            const void* retAddr = _ReturnAddress();
            const std::uintptr_t retRva = ue::ModuleBase() != 0
                ? reinterpret_cast<std::uintptr_t>(retAddr) - ue::ModuleBase()
                : reinterpret_cast<std::uintptr_t>(retAddr);

            const bool inGameplay = InGameplay(reinterpret_cast<std::uintptr_t>(self));

            // Suppress the game's own crosshair only during gameplay - never
            // touch widget opacity in menus/PDA where the reticle isn't ours
            // to hide.
            if (inGameplay) {
                DriveReticleMove();
                DriveTooltipMove();
            }

            // Cache the live Pawn pointer for the mask isolator + comp.
            {
                std::uintptr_t pawnNow = 0;
                SafeReadPtr(reinterpret_cast<std::uintptr_t>(self)
                                + Offsets::PlayerController::kPawn, pawnNow);
                if (pawnNow) g_currentPawn.store(pawnNow);
            }

            const FVector preLoc = *outLocation;
            g_origGetPlayerViewPoint(self, outLocation, outRotation);
            const FVector postLoc = *outLocation;
            const FRotator postOrig = *outRotation;

            // Defer the D3D12 overlay install until the game has fully
            // spun up. Doing it from the bootstrap thread caught
            // kiero::init returning a failure (no usable D3D12 device
            // yet). Same call-count gate as the harvest works.
            static std::atomic<bool> s_overlayTried{false};
            if (g_hookCallCount.load(std::memory_order_relaxed) > 500
                && !s_overlayTried.exchange(true))
            {
                ReticleOverlay::Install();
            }

            // Mask candidate (re-)harvest. Runs once we have a stable
            // post-spawn camera loc, then again whenever the Pawn pointer
            // changes (pre-game default -> in-game, or level reload
            // reallocates the actor).
            if (g_hookCallCount.load(std::memory_order_relaxed) > 500
                && (std::abs(postLoc.X) + std::abs(postLoc.Y) + std::abs(postLoc.Z)) > 10.0)
            {
                const auto selfA = reinterpret_cast<std::uintptr_t>(self);
                std::uintptr_t pawn = 0;
                SafeReadPtr(selfA + Offsets::PlayerController::kPawn, pawn);
                if (pawn && pawn != g_lastHarvestedPawn.load()) {
                    g_lastHarvestedPawn.store(pawn);
                    CollectReticleWidgets();
                    {
                        std::lock_guard<std::mutex> lk(g_maskMutex);
                        g_maskCandidates.clear();
                    }
                    g_maskSlot.store(-1);
                    HarvestMaskCandidates("pawn", pawn, 0x6000,
                        postLoc.X, postLoc.Y, postLoc.Z);
                    HarvestMaskCandidates("ctl",  selfA, 0x1000,
                        postLoc.X, postLoc.Y, postLoc.Z);
                    std::uintptr_t camMgr = 0;
                    SafeReadPtr(selfA + Offsets::PlayerController::kPlayerCameraManager,
                                camMgr);
                    if (camMgr) HarvestMaskCandidates("cm", camMgr, 0x2000,
                        postLoc.X, postLoc.Y, postLoc.Z);
                    HarvestMaskCandidatesRecursive(/*passes*/3, /*range*/0x600,
                        postLoc.X, postLoc.Y, postLoc.Z);
                }
            }

            const auto n = g_hookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
            const int mode = g_injectMode.load(std::memory_order_relaxed);

            // Per-caller hit accounting is only meaningful in inject-mode 0
            // (ALL callers), where the periodic distribution dump is how the
            // render caller gets re-bisected after a game patch. In normal play
            // it was a mutex lock + hash-map insert on every one of the ~35 hook
            // calls per frame, feeding a summary that is never consulted - skip
            // it entirely outside the diagnostic mode.
            if (mode == 0) {
                {
                    std::lock_guard<std::mutex> lock(g_callerMutex);
                    ++g_callerCounts[retRva];
                }
                if (n - g_callerLastSummaryCount >= kCallerSummaryEvery) {
                    g_callerLastSummaryCount = n;
                    DumpCallerSummary(n);
                }
            }

            // Heartbeat: fires even when no UDP data arrives, so "hook never
            // called" is distinguishable from "tracking data missing".
            if (n == 1 || (n % 3000) == 0) {
                float hy = 0.0f, hp = 0.0f, hr = 0.0f;
                const bool haveData = g_receiver && g_receiver->GetRotation(hy, hp, hr);
                Log::Line("heartbeat hook=%llu  retRVA=0x%08llx  enabled=%s  udpData=%s  raw=(Y=%.2f P=%.2f R=%.2f)  yawMode=%s  injectMode=%d",
                    static_cast<unsigned long long>(n),
                    static_cast<unsigned long long>(retRva),
                    g_trackingEnabled.load() ? "ON" : "OFF",
                    haveData ? "YES" : "NO",
                    hy, hp, hr,
                    g_worldSpaceYaw.load() ? "world" : "local",
                    mode);
            }

            // Suppress tracking outside gameplay (main menu, PDA, pause): leave
            // outRotation/outLocation untouched so the rendered view uses the
            // game's clean rotation, and hide our reticle.
            if (!g_trackingEnabled.load(std::memory_order_relaxed) || !g_receiver
                || !inGameplay) {
                ReticleOverlay::UpdateAim(0.0, 0.0, 0.0, 1.0, false);
                return;
            }

            if (!ShouldInjectForCaller(retRva, mode)) {
                // Caller-gate is about which UE call sites get head-tracked
                // rotation written back (aim decoupling). The reticle's
                // active flag is a function of (inGameplay, enabled, data)
                // only - so refresh it here too, otherwise the reticle stays
                // stuck at whatever active value was set by the renderer
                // caller's last UpdateAim. After a death/respawn the brief
                // respawn flow can route GPV exclusively through non-renderer
                // callers; without this, g_aimActive stays at the false it
                // got during the death-screen frames and the reticle never
                // comes back even once the renderer caller resumes.
                ReticleOverlay::SetActive(true);
                return;
            }

            float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
            if (!GetProcessedRotation(yaw, pitch, roll)) {
                // No fresh tracker data - hide the reticle explicitly so we
                // don't leak a stale active=true from the previous frame.
                ReticleOverlay::UpdateAim(0.0, 0.0, 0.0, 1.0, false);
                return;
            }

            // Rotation-only gate of the tracking-mode cycle. With rotation
            // disabled the head angles are zeroed, so viewQ collapses to baseQ
            // (H becomes identity): the rendered view keeps the clean camera
            // rotation, the reticle stays centred, and only the positional
            // sway below remains active.
            if (!g_rotationEnabled.load(std::memory_order_relaxed)) {
                yaw = 0.0f;
                pitch = 0.0f;
                roll = 0.0f;
            }

            // Compose the final view rotation. Two paths:
            //   world yaw: plain FRotator addition. Yaw rotates around world Z
            //              regardless of pitch (horizon-locked, scuba-mask
            //              friendly).
            //   local yaw: head rotation applied in camera-local frame via
            //              quaternion post-multiply. Pitched-up turns lean.
            const FQuat4d baseQ = QuatFromEulerDeg(
                postOrig.Pitch, postOrig.Yaw, postOrig.Roll);
            FQuat4d viewQ{};
            if (!g_worldSpaceYaw.load(std::memory_order_relaxed)) {
                const FQuat4d headLocalQ = QuatFromEulerDeg(
                    static_cast<double>(pitch),
                    static_cast<double>(yaw),
                    -static_cast<double>(roll));
                viewQ = QuatMul(baseQ, headLocalQ);
                const FRotator fin = QuatToRotator(viewQ);
                outRotation->Pitch = fin.Pitch;
                outRotation->Yaw   = fin.Yaw;
                outRotation->Roll  = fin.Roll;
            } else {
                outRotation->Yaw   += yaw;
                outRotation->Pitch += pitch;
                outRotation->Roll  -= roll;
                viewQ = QuatFromEulerDeg(
                    outRotation->Pitch, outRotation->Yaw, outRotation->Roll);
            }
            const FQuat4d H = QuatMul(viewQ, QuatInv(baseQ));

            // Feed the reticle overlay the aim-vs-view relative rotation so it
            // can project the clean-aim crosshair into the tracked view. The
            // clean aim is the base (mouse/controller) direction; the reticle
            // moves to wherever that direction lands in the head-tracked frame.
            const FQuat4d qrel = QuatMul(QuatInv(viewQ), baseQ);
            ReticleOverlay::UpdateAim(qrel.X, qrel.Y, qrel.Z, qrel.W,
                                      inGameplay);

            // Drive the mask isolator with the same tracker delta. When a
            // slot is selected, head movement visibly rotates exactly that
            // mesh - lets us identify which slot is the helmet/mask.
            ApplyMaskIsolator(static_cast<double>(yaw),
                              static_cast<double>(pitch),
                              static_cast<double>(roll));

            // 6DOF: add the head-sway offset to the camera location FIRST, so
            // the mask comp below can pivot around the camera's FINAL position.
            // Gated by the same render-caller filter as rotation (we already
            // returned early for non-injecting callers), so weapons/AI/raycasts
            // keep the clean position - position aim-decoupling, like rotation.
            FVector posOffset{0.0, 0.0, 0.0};
            const bool haveOffset =
                GetProcessedPositionOffset(baseQ, yaw, pitch, roll, posOffset);
            if (haveOffset) {
                outLocation->X += posOffset.X;
                outLocation->Y += posOffset.Y;
                outLocation->Z += posOffset.Z;
            }

            // Apply screen-fix compensation to the mask group using the actual
            // H quaternion the hook applied - works for both world and local
            // yaw modes. Pivot around the CLEAN camera position (postLoc) as
            // before, and pass the 6DOF offset so the mask follows the camera's
            // translation: newLoc = postLoc + H*(oldLoc-postLoc) + offset.
            if (g_maskDiagEnabled.load(std::memory_order_relaxed)) {
                // Read-only measurement: suppresses the writing comp so we
                // observe the engine's own mask motion.
                MeasureMaskGroup(H);
            } else {
                ApplyMaskGroupCompensation(static_cast<double>(yaw),
                                           static_cast<double>(pitch),
                                           static_cast<double>(roll),
                                           postLoc, posOffset, H);
            }

            if (n == 1 || (n % 600) == 0) {
                Log::Line("pos #%llu enabled=%s offsetUE=(%.2f,%.2f,%.2f)",
                    static_cast<unsigned long long>(n),
                    g_positionEnabled.load() ? "ON" : "OFF",
                    posOffset.X, posOffset.Y, posOffset.Z);
            }

            if (n == 1 || (n % 600) == 0) {
                Log::Line("hook #%llu retRVA=0x%08llx loc_pre=(%.3f,%.3f,%.3f) loc_post=(%.3f,%.3f,%.3f) rot_post=(Y=%.2f P=%.2f R=%.3e) tracker=(Y=%.2f P=%.2f R=%.2f) result=(Y=%.2f P=%.2f R=%.2f)",
                    static_cast<unsigned long long>(n),
                    static_cast<unsigned long long>(retRva),
                    preLoc.X, preLoc.Y, preLoc.Z,
                    postLoc.X, postLoc.Y, postLoc.Z,
                    postOrig.Yaw, postOrig.Pitch, postOrig.Roll,
                    yaw, pitch, roll,
                    outRotation->Yaw, outRotation->Pitch, outRotation->Roll);
            }
        }

        std::wstring DllDir(void* hModule)
        {
            wchar_t buf[MAX_PATH] = {};
            GetModuleFileNameW(static_cast<HMODULE>(hModule), buf, MAX_PATH);
            std::wstring path(buf);
            const auto slash = path.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                path.resize(slash + 1);
            }
            return path;
        }

        std::wstring LogPathNextToDll(void* hModule)
        {
            return DllDir(hModule) + L"Subnautica2HeadTracking.log";
        }

        // Narrow sibling of DllDir for the ANSI IniReader (GetPrivateProfile*A).
        std::string DllDirNarrow(void* hModule)
        {
            char buf[MAX_PATH] = {};
            GetModuleFileNameA(static_cast<HMODULE>(hModule), buf, MAX_PATH);
            std::string path(buf);
            const auto slash = path.find_last_of("\\/");
            if (slash != std::string::npos) {
                path.resize(slash + 1);
            }
            return path;
        }

#if SN2HT_DEV_HOTKEYS
        void SaveMaskMarks()
        {
            if (g_marksFilePath.empty()) return;
            std::vector<MaskMark> snap;
            {
                std::lock_guard<std::mutex> lk(g_maskMarksMutex);
                snap = g_maskMarks;
            }
            FILE* f = nullptr;
            _wfopen_s(&f, g_marksFilePath.c_str(), L"w");
            if (!f) return;
            for (const auto& m : snap) {
                std::fprintf(f, "%s\t0x%zx\n", m.src.c_str(), m.srcOff);
            }
            std::fclose(f);
        }
#endif

        void LoadMaskMarks()
        {
            if (g_marksFilePath.empty()) return;
            FILE* f = nullptr;
            _wfopen_s(&f, g_marksFilePath.c_str(), L"r");
            if (!f) {
                Log::Line("mask-marks: no marks file at startup (file missing)");
                return;
            }
            std::vector<MaskMark> loaded;
            char line[256];
            while (std::fgets(line, sizeof(line), f)) {
                char src[64] = {};
                unsigned long long off = 0;
                if (std::sscanf(line, "%63s 0x%llx", src, &off) == 2) {
                    loaded.push_back({std::string(src), static_cast<std::size_t>(off)});
                }
            }
            std::fclose(f);
            {
                std::lock_guard<std::mutex> lk(g_maskMarksMutex);
                g_maskMarks = std::move(loaded);
            }
            // Auto-enable comp if we loaded any marks - the user already
            // dialed these in in a previous session.
            if (!g_maskMarks.empty()) g_maskCompEnabled.store(true);
            Log::Line("mask-marks: loaded %zu marks from file  comp=%s",
                g_maskMarks.size(), g_maskCompEnabled.load() ? "ON" : "OFF");
        }

        // Fail-safe: confirm the running EXE is the exact build every RVA in
        // ghidra_offsets.h was derived from before we hook anything. A game
        // patch relinks the binary and shifts every offset; hooking a stale
        // RVA then patches a jump into unrelated code and crashes the game a
        // couple of seconds in. We can't trust stale offsets, so on any
        // mismatch we log loudly and run fully vanilla (no hooks, no UObject
        // table walks). Three independent PE fields must all match the build
        // the offsets target - any single difference means a different build.
        enum class BuildCheck {
            Match,
            ReadFailed,  // couldn't read PE headers
            ExeNewer,    // game updated since this mod was built
            ExeOlder,    // game is older than this mod targets
            ExeDiffers,  // same TimeDateStamp, different size/checksum (rare)
        };
        BuildCheck ValidateRunningBuild()
        {
            const auto base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
            if (!base) {
                Log::Line("build-check: GetModuleHandle(nullptr) returned null - disabling");
                return BuildCheck::ReadFailed;
            }
            const auto e_lfanew = *reinterpret_cast<const std::uint32_t*>(base + 0x3c);
            const std::uint8_t* nt = base + e_lfanew;
            if (*reinterpret_cast<const std::uint32_t*>(nt) != 0x00004550u /* "PE\0\0" */) {
                Log::Line("build-check: PE signature not found - disabling");
                return BuildCheck::ReadFailed;
            }
            // COFF FileHeader starts at nt+4; TimeDateStamp at +8 within it.
            // Optional header starts at nt+0x18; SizeOfImage @+0x38, CheckSum
            // @+0x40 (PE32+).
            const std::uint32_t timeDateStamp = *reinterpret_cast<const std::uint32_t*>(nt + 4 + 4);
            const std::uint8_t* opt = nt + 4 + 20;
            const std::uint32_t sizeOfImage = *reinterpret_cast<const std::uint32_t*>(opt + 0x38);
            const std::uint32_t checkSum    = *reinterpret_cast<const std::uint32_t*>(opt + 0x40);

            using namespace Offsets::BuildFingerprint;
            Log::Line("build-check: running  ts=0x%08x size=0x%08x csum=0x%08x",
                timeDateStamp, sizeOfImage, checkSum);
            Log::Line("build-check: expected ts=0x%08x size=0x%08x csum=0x%08x",
                kTimeDateStamp, kSizeOfImage, kCheckSum);

            if (timeDateStamp == kTimeDateStamp
             && sizeOfImage   == kSizeOfImage
             && checkSum      == kCheckSum) {
                return BuildCheck::Match;
            }
            // TimeDateStamp is a Unix epoch in the COFF header. A relink (every
            // game patch) bumps it monotonically, so it's a reliable direction
            // indicator. Size/checksum drift without a timestamp change would
            // be a tampered or hand-patched EXE.
            if (timeDateStamp > kTimeDateStamp) return BuildCheck::ExeNewer;
            if (timeDateStamp < kTimeDateStamp) return BuildCheck::ExeOlder;
            return BuildCheck::ExeDiffers;
        }

        bool InstallCameraHook()
        {
            HMODULE exe = GetModuleHandleW(nullptr);
            if (!exe) {
                Log::Line("InstallCameraHook: GetModuleHandle(nullptr) returned null");
                return false;
            }
            const auto base = reinterpret_cast<std::uintptr_t>(exe);
            // g_processEvent is resolved lazily from a UObject vtable slot on
            // the first hook call (ResolveProcessEvent) - no hardcoded RVA.

            MODULEINFO mi{};
            std::uintptr_t end = base + 0x10000000ULL;  // generous fallback for vtable check
            if (GetModuleInformation(GetCurrentProcess(), exe, &mi, sizeof(mi))) {
                end = base + mi.SizeOfImage;
            }
            ue::SetModuleRange(base, end);
            Log::Line("Module range=[0x%llx .. 0x%llx)  size=0x%llx",
                static_cast<unsigned long long>(base),
                static_cast<unsigned long long>(end),
                static_cast<unsigned long long>(end - base));

            void* target = reinterpret_cast<void*>(base + kGetPlayerViewPointRva);
            Log::Line("Module base=0x%llx target=0x%llx (RVA 0x%llx)",
                static_cast<unsigned long long>(base),
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(target)),
                static_cast<unsigned long long>(kGetPlayerViewPointRva));

            using cameraunlock::hooks::HookManager;
            using cameraunlock::hooks::HookStatus;
            using cameraunlock::hooks::HookStatusToString;

            auto& hm = HookManager::Instance();
            if (auto s = hm.Initialize(); s != HookStatus::Ok && s != HookStatus::ErrorAlreadyInitialized) {
                Log::Line("MinHook init failed: %s", HookStatusToString(s));
                return false;
            }
            if (auto s = hm.CreateHook(
                    target,
                    reinterpret_cast<void*>(&GetPlayerViewPoint_Hook),
                    reinterpret_cast<void**>(&g_origGetPlayerViewPoint));
                s != HookStatus::Ok) {
                Log::Line("CreateHook failed: %s", HookStatusToString(s));
                return false;
            }
            if (auto s = hm.EnableHook(target); s != HookStatus::Ok) {
                Log::Line("EnableHook failed: %s", HookStatusToString(s));
                return false;
            }
            Log::Line("Hook installed on APlayerController::GetPlayerViewPoint");
            return true;
        }

        // The painted HUD reticle instance only exists once the HUD spins up
        // and is recreated on state changes (vehicles, menus). A one-shot
        // collect misses it, so refresh the set here, off the game thread -
        // enumerating ~244k UObjects would hitch the render path if done inline.
        DWORD WINAPI ReticleCollectorThread(LPVOID)
        {
            for (;;) {
                Sleep(2000);
                if (ue::ModuleBase() != 0) CollectReticleWidgets();
            }
        }

        DWORD WINAPI BootstrapThread(LPVOID module)
        {
            g_marksFilePath = DllDir(module) + L"Subnautica2HeadTracking.marks.txt";
            const auto logPath = LogPathNextToDll(module);
            Log::Open(logPath);
            Log::Line("Subnautica 2 Head Tracking - bootstrap");
            Log::Line("Process: PID=%lu", GetCurrentProcessId());

            const auto buildCheck = ValidateRunningBuild();
            if (buildCheck != BuildCheck::Match) {
                Log::Line("============================================================");
                Log::Line(" GAME BUILD MISMATCH - HEAD TRACKING DISABLED");
                switch (buildCheck) {
                    case BuildCheck::ExeNewer:
                        Log::Line(" Subnautica 2 is NEWER than this mod was built for.");
                        Log::Line(" The game was updated - check the Releases page");
                        Log::Line(" for a matching mod version:");
                        Log::Line(" https://github.com/itsloopyo/subnautica-2-headtracking/releases");
                        break;
                    case BuildCheck::ExeOlder:
                        Log::Line(" Subnautica 2 is OLDER than this mod was built for.");
                        Log::Line(" Let Steam finish updating the game, then relaunch.");
                        break;
                    case BuildCheck::ExeDiffers:
                        Log::Line(" The Subnautica 2 EXE differs from the build this mod");
                        Log::Line(" targets (same timestamp, different size/checksum).");
                        Log::Line(" Unexpected - if you haven't modified the EXE, please");
                        Log::Line(" file an issue on the repo.");
                        break;
                    case BuildCheck::ReadFailed:
                        Log::Line(" Could not read the game's PE headers (see log above).");
                        break;
                    default: break;
                }
                Log::Line(" To avoid crashing your session, head tracking is staying");
                Log::Line(" OFF and the game will run vanilla.");
                Log::Line("============================================================");
                return 0;  // no hooks, no UObject walks - game runs vanilla
            }
            Log::Line("build-check: PASS - offsets match this build, arming");

            LoadMaskMarks();

            // Yaw-mode + key from HeadTracking.ini, next to the DLL. World-space
            // (horizon-locked) yaw is the default when the file or key is absent
            // - a filesystem boundary, so a default read is correct here. The
            // flag is flipped live by the toggle key below.
            int yawModeKey = 0x22;  // Page Down
            {
                cameraunlock::IniReader ini;
                const std::string iniPath = DllDirNarrow(module) + "HeadTracking.ini";
                if (ini.Open(iniPath)) {
                    g_worldSpaceYaw.store(ini.ReadBool("Tracking", "WorldSpaceYaw", true));
                    yawModeKey = ini.ReadHex("Hotkeys", "ToggleYawMode", 0x22);
                    g_tooltipMoveOn.store(ini.ReadBool("Tooltip", "FollowReticle", true));
                    g_tooltipFollowScale.store(ini.ReadFloat("Tooltip", "FollowScale", 1.0f));
                    Log::Line("config: WorldSpaceYaw=%s  ToggleYawMode=0x%02x  TooltipFollow=%s scale=%.2f",
                        g_worldSpaceYaw.load() ? "true" : "false", yawModeKey,
                        g_tooltipMoveOn.load() ? "true" : "false",
                        g_tooltipFollowScale.load());
                } else {
                    Log::Line("config: no HeadTracking.ini next to DLL; "
                              "WorldSpaceYaw=true, ToggleYawMode=0x22 (defaults)");
                }
            }

            {
                cameraunlock::PositionSettings ps = g_posProcessor.GetSettings();
                ps.invert_x = true;
                ps.invert_z = true;
                // The processor clamps z to [-limit_z, +limit_z_back] assuming
                // negative z = forward. invert_z (above) flips that, so forward
                // is now +z and would hit the restricted limit_z_back. Swap the
                // two so the generous bound (0.40) stays on forward and the
                // restricted bound (0.10) stays on backward.
                ps.limit_z = 0.10f;
                ps.limit_z_back = 0.40f;
                g_posProcessor.SetSettings(ps);
            }

            g_receiver = std::make_unique<cameraunlock::UdpReceiver>();
            g_receiver->SetLog([](const std::string& msg) {
                Log::Line("[udp] %s", msg.c_str());
            });
            const bool bound = g_receiver->Start(4242);
            Log::Line("UDP receiver Start(4242) -> %s",
                bound ? "bound" : "retry-scheduled");

            g_hotkeys = std::make_unique<cameraunlock::input::HotkeyPoller>();

            // Standard CameraUnlock bindings. Every action is reachable both
            // from the nav cluster and from a Ctrl+Shift chord drawn from the
            // T/Y/U/G/H/J cluster, so keyboards without a nav cluster still
            // work. Both variants fire the same handler; the chord variant is
            // gated on ChordHeld() and edge-detected on its letter key by the
            // poller, so the bare letter is a no-op during gameplay.
            const auto recenter = []() {
                if (g_receiver) {
                    g_receiver->Recenter();
                    g_interp.Reset();
                    g_hasSmoothed = false;
                    g_posCenterPending.store(true);  // re-zero head sway too
                    Log::Line("Recenter");
                }
            };
            const auto toggleTracking = []() {
                const bool now = !g_trackingEnabled.exchange(!g_trackingEnabled.load());
                Log::Line("Tracking toggled: %s", now ? "ON" : "OFF");
            };
            // Three-state tracking-mode cycle:
            //   0 normal      -> rotation + position
            //   1 rotation    -> position disabled
            //   2 position    -> rotation disabled
            // advancing past 2 wraps back to 0.
            const auto cycleTrackingMode = []() {
                const int next = (g_trackingMode.load() + 1) % 3;
                g_trackingMode.store(next);
                switch (next) {
                    case 0:
                        g_rotationEnabled.store(true);
                        g_positionEnabled.store(true);
                        break;
                    case 1:
                        g_rotationEnabled.store(true);
                        g_positionEnabled.store(false);
                        break;
                    case 2:
                        g_rotationEnabled.store(false);
                        g_positionEnabled.store(true);
                        break;
                }
                g_posCenterPending.store(true);  // re-zero sway when position re-enables
                static const char* names[] = {
                    "NORMAL (rotation + position)",
                    "ROTATION ONLY (position off)",
                    "POSITION ONLY (rotation off)"
                };
                Log::Line("tracking-mode -> %d  (%s)", next, names[next]);
            };
            const auto toggleYawMode = []() {
                const bool now = !g_worldSpaceYaw.load();
                g_worldSpaceYaw.store(now);
                Log::Line("yaw-mode -> %s", now ? "WORLD (horizon-locked)" : "LOCAL (camera-local)");
            };

            // Recenter: Home / Ctrl+Shift+T
            g_hotkeys->SetRecenterKey(VK_HOME, recenter);
            g_hotkeys->AddHotkey(0x54 /* T */, [recenter]() { if (ChordHeld()) recenter(); });
            // Toggle tracking: End / Ctrl+Shift+Y
            g_hotkeys->SetToggleKey(VK_END, toggleTracking);
            g_hotkeys->AddHotkey(0x59 /* Y */, [toggleTracking]() { if (ChordHeld()) toggleTracking(); });
            // Cycle tracking mode: Page Up / Ctrl+Shift+G
            g_hotkeys->AddHotkey(VK_PRIOR, cycleTrackingMode);
            g_hotkeys->AddHotkey(0x47 /* G */, [cycleTrackingMode]() { if (ChordHeld()) cycleTrackingMode(); });
            // Yaw mode (world/local): Page Down (or [Hotkeys] ToggleYawMode) / Ctrl+Shift+H
            g_hotkeys->AddHotkey(yawModeKey, toggleYawMode);
            g_hotkeys->AddHotkey(0x48 /* H */, [toggleYawMode]() { if (ChordHeld()) toggleYawMode(); });

#if SN2HT_DEV_HOTKEYS
            // Discovery / tuning controls. Off in shipping builds - see
            // SN2HT_DEV_HOTKEYS at the top of this file.

            // F3 / F4: live-tune [Tooltip] FollowScale by +/-0.05 while the
            // prompt is on screen. The right value is 1 / your viewport DPI
            // scale - rather than guess, dial it in until widget speed matches
            // reticle speed, then copy the logged value into HeadTracking.ini.
            g_hotkeys->AddHotkey(VK_F3, []() {
                float s = g_tooltipFollowScale.load();
                s = std::max(0.05f, s - 0.10f);
                g_tooltipFollowScale.store(s);
                Log::Line("tooltip-scale -> %.2f (save to [Tooltip] FollowScale in HeadTracking.ini)", s);
            });
            g_hotkeys->AddHotkey(VK_F4, []() {
                float s = g_tooltipFollowScale.load();
                s = std::min(3.0f, s + 0.10f);
                g_tooltipFollowScale.store(s);
                Log::Line("tooltip-scale -> %.2f (save to [Tooltip] FollowScale in HeadTracking.ini)", s);
            });

            g_hotkeys->AddHotkey(VK_F6, []() {
                const bool now = !g_maskDiagEnabled.load();
                g_maskDiagEnabled.store(now);
                if (now) g_maskDiagArm.store(true);  // recapture baseline
                Log::Line("mask-diag -> %s  (read-only; recenter head, then move it to read maskDelta vs headH ratios)",
                    now ? "ON (comp writes suppressed)" : "OFF");
            });
            g_hotkeys->AddHotkey(VK_F8, []() {
                const bool now = !g_positionEnabled.load();
                g_positionEnabled.store(now);
                g_posCenterPending.store(true);  // re-zero on re-enable
                Log::Line("position (6DOF) -> %s", now ? "ON" : "OFF");
            });
            g_hotkeys->AddHotkey(VK_F7, []() {
                int next = (g_injectMode.load() + 1) % kNumInjectModes;
                g_injectMode.store(next);
                if (next >= 1 && next <= 11) {
                    Log::Line("inject-mode -> %d  (caller [%d] RVA 0x%08llx ONLY)",
                        next, next, static_cast<unsigned long long>(kKnownCallerRvas[next - 1]));
                } else {
                    Log::Line("inject-mode -> %d  (%s)", next, InjectModeName(next));
                }
            });
            g_hotkeys->AddHotkey(VK_SCROLL, []() {
                Log::Line("=== HUD child-widget dump (press in gameplay, reticle on screen) ===");
                CollectReticleWidgets();   // resolves getVis/getOpacity fns
                DumpHudWidgets();
                Log::Line("=== interaction-tooltip dump (press WHILE hovering an interactable) ===");
                DumpInteractionWidgets();
                Log::Line("=== widget-component dump (press WITH equipped-item prompt visible, eg air bladder) ===");
                DumpWidgetComponents();
            });
            g_hotkeys->AddHotkey(VK_INSERT, []() {
                std::size_t n = 0;
                {
                    std::lock_guard<std::mutex> lk(g_maskMutex);
                    n = g_maskCandidates.size();
                }
                if (n == 0) {
                    Log::Line("mask-slot: no candidates harvested yet (play a frame in-game)");
                    return;
                }
                int next = g_maskSlot.load() + 1;
                if (next >= static_cast<int>(n)) next = -1;
                g_maskSlot.store(next);
                Log::Line("mask-slot -> %d / %zu  (%s)",
                    next, n, DescribeMaskSlot(next).c_str());
            });
            g_hotkeys->AddHotkey(VK_DELETE, []() {
                std::size_t n = 0;
                {
                    std::lock_guard<std::mutex> lk(g_maskMutex);
                    n = g_maskCandidates.size();
                }
                if (n == 0) {
                    Log::Line("mask-slot: no candidates harvested yet (play a frame in-game)");
                    return;
                }
                int prev = g_maskSlot.load() - 1;
                if (prev < -1) prev = static_cast<int>(n) - 1;
                g_maskSlot.store(prev);
                Log::Line("mask-slot -> %d / %zu  (%s)",
                    prev, n, DescribeMaskSlot(prev).c_str());
            });
            g_hotkeys->AddHotkey(VK_F9, []() {
                const bool now = !g_maskCompEnabled.load();
                g_maskCompEnabled.store(now);
                g_maskSlot.store(-1);
                // Clear the per-slot last-written cache so the next frame
                // sees engine-fresh values and applies comp from scratch.
                {
                    std::lock_guard<std::mutex> lk(g_lastWrittenMutex);
                    g_lastWrittenBySlot.clear();
                    g_lastCleanE.clear();
                    g_rejectStreakBySlot.clear();
                }
                Log::Line("mask-comp -> %s  (members=[%s] mode=%d isolator OFF)",
                    now ? "ON" : "OFF",
                    DescribeMaskMembers().c_str(),
                    g_maskCompMode.load());
            });
            g_hotkeys->AddHotkey(VK_F10, []() {
                int next = (g_maskCompMode.load() + 1) % 4;
                g_maskCompMode.store(next);
                static const char* names[] = {
                    "H * old", "old * H", "Hinv * old", "old * Hinv"
                };
                Log::Line("mask-comp-mode -> %d  (%s)", next, names[next]);
            });
            // F11: toggle current isolator slot as a mask member. The mark
            // is stored as (src, srcOff) tuple so it survives pawn changes
            // and game restarts (slot indices are allocation-dependent and
            // change every launch).
            g_hotkeys->AddHotkey(VK_F11, []() {
                const int slot = g_maskSlot.load();
                if (slot < 0) {
                    Log::Line("mask-mark: isolator is OFF; press Insert to select a slot first");
                    return;
                }
                MaskCandidate cand{};
                bool have = false;
                {
                    std::lock_guard<std::mutex> lk(g_maskMutex);
                    if (slot < static_cast<int>(g_maskCandidates.size())) {
                        cand = g_maskCandidates[slot];
                        have = true;
                    }
                }
                if (!have) {
                    Log::Line("mask-mark: slot %d is out of range", slot);
                    return;
                }
                {
                    std::lock_guard<std::mutex> lk(g_maskMarksMutex);
                    const std::string s(cand.src);
                    auto it = std::find_if(g_maskMarks.begin(), g_maskMarks.end(),
                        [&](const MaskMark& m){
                            return m.src == s && m.srcOff == cand.srcOff;
                        });
                    if (it != g_maskMarks.end()) {
                        g_maskMarks.erase(it);
                        Log::Line("mask-mark: removed %s+0x%zx (now %zu marks)",
                            s.c_str(), cand.srcOff, g_maskMarks.size());
                    } else {
                        g_maskMarks.push_back({s, cand.srcOff});
                        Log::Line("mask-mark: added %s+0x%zx (now %zu marks)",
                            s.c_str(), cand.srcOff, g_maskMarks.size());
                    }
                }
                SaveMaskMarks();
            });
            g_hotkeys->AddHotkey(VK_F12, []() {
                {
                    std::lock_guard<std::mutex> lk(g_maskMarksMutex);
                    g_maskMarks.clear();
                }
                SaveMaskMarks();
                Log::Line("mask-mark: cleared - now 0 marks (file wiped)");
            });
#endif
            g_hotkeys->Start();
            Log::Line("Hotkeys armed: Home/Ctrl+Shift+T=recenter  End/Ctrl+Shift+Y=toggle  PgUp/Ctrl+Shift+G=tracking-mode-cycle  PgDn/Ctrl+Shift+H=yaw-mode(world/local)");
#if SN2HT_DEV_HOTKEYS
            Log::Line("Dev hotkeys: F6=mask-diag  F7=inject-mode-next  F8=position-toggle  ScrollLock=hud-dump  Ins/Del=mask-slot  F9=comp-toggle  F10=comp-mode  F11=mark/unmark  F12=clear-marks");
            Log::Line("Inject modes: 0=ALL  1..11=single-caller  12=NONE  13=all-perframe(5-9)  14=all-tier1+2(1-4)");
#endif
            Log::Line("Startup inject-mode = %d  (render-only filter: caller [2] RVA 0x%08llx -> aim decoupled)",
                g_injectMode.load(), static_cast<unsigned long long>(kKnownCallerRvas[1]));

            InstallCameraHook();

            CreateThread(nullptr, 0, ReticleCollectorThread, nullptr, 0, nullptr);

            // ReticleOverlay::Install() is deferred to the first hook call -
            // installing from this bootstrap thread runs too early in the
            // game's process to find a usable D3D12 device.

            Log::Line("Awaiting OpenTrack packets on UDP 4242");
            return 0;
        }
    }

    void Initialize(void* hModule)
    {
        CreateThread(nullptr, 0, BootstrapThread, hModule, 0, nullptr);
    }

    void Shutdown()
    {
        ReticleOverlay::Remove();
        cameraunlock::hooks::HookManager::Instance().Shutdown();
        if (g_receiver) {
            g_receiver->Stop();
            g_receiver.reset();
        }
        g_hotkeys.reset();
        Log::Line("Shutdown");
        Log::Close();
    }
}
