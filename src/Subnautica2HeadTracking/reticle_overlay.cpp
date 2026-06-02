// D3D12 overlay for the head-tracking reticle (Subnautica 2 renders on D3D12).
// Built on cameraunlock-core's DX12Overlay (kiero + ImGui). This translation
// unit owns the implementation macro so the heavy includes stay isolated.

#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#include <cameraunlock/rendering/dx12_overlay.h>
#include <cameraunlock/rendering/aim_quat_projection.h>

#include "reticle_overlay.h"
#include "logging.h"

#include <atomic>
#include <mutex>

namespace Subnautica2HeadTracking::ReticleOverlay
{
    namespace
    {
        cameraunlock::rendering::DX12Overlay g_overlay;
        bool g_installed = false;

        // Horizontal FOV at the 16:9 reference aspect, used to project clean
        // aim into the tracked view. The GPV hook reads the engine's live FOV
        // each renderer-caller frame (FMinimalViewInfo.FOV, an aspect-independent
        // scalar) and pushes it via SetFovDegrees, so the reticle follows the
        // player's FOV slider. The seed below is the default until the first
        // live value arrives, and the fallback if a read ever looks invalid.
        // SN2 (UE5) uses Hor+ scaling, so DrawReticle holds the vertical FOV
        // constant and re-expands horizontal by the live aspect ratio.
        constexpr float kReticleFovDefaultAt16x9 = 90.0f;
        constexpr float kReticleRefAspect = 16.0f / 9.0f;
        std::atomic<float> g_fovHorizontalAt16x9{kReticleFovDefaultAt16x9};

        std::mutex g_aimMutex;
        double g_qx = 0.0, g_qy = 0.0, g_qz = 0.0, g_qw = 1.0;
        bool   g_aimActive = false;

        // Body-forward reticle position as a pixel offset from screen centre,
        // published each frame from DrawReticle for the tooltip mover on the
        // game thread. Valid only while the reticle is drawn (in front, active).
        std::atomic<float> g_offsetX{0.0f};
        std::atomic<float> g_offsetY{0.0f};
        std::atomic<bool>  g_offsetValid{false};

        void DrawReticle(float w, float h)
        {
            double qx, qy, qz, qw;
            bool active;
            {
                std::lock_guard<std::mutex> lk(g_aimMutex);
                qx = g_qx; qy = g_qy; qz = g_qz; qw = g_qw;
                active = g_aimActive;
            }

            if (!active) {  // game's own reticle is hidden - hide ours too
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }

            // Project the clean-aim direction into the tracked view. The
            // Hor+ (MaintainYFOV) aspect model and the viewport-edge NDC
            // clamping both live in cameraunlock-core; SN2 holds the vertical
            // FOV constant across aspect ratios and expands horizontal with
            // width, so a Vert- projection over-rotates ~2x at 32:9.
            const auto proj = cameraunlock::rendering::ProjectAimQuatHorPlus(
                qx, qy, qz, qw, w, h,
                g_fovHorizontalAt16x9.load(std::memory_order_relaxed),
                kReticleRefAspect);
            if (!proj.inFront) {  // aim behind tracked view - hide
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }

            g_offsetX.store(proj.screenX - w * 0.5f, std::memory_order_relaxed);
            g_offsetY.store(proj.screenY - h * 0.5f, std::memory_order_relaxed);
            g_offsetValid.store(true, std::memory_order_relaxed);

            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            const ImU32 col = IM_COL32(220, 235, 255, 165);  // slightly blue-white, ~65%
            const ImVec2 c{proj.screenX, proj.screenY};
            dl->AddCircleFilled(c, 2.5f, col, 16);            // dot
            dl->AddCircle(c, 16.0f, col, 32, 1.5f);           // ring
        }
    }

    bool Install()
    {
        if (g_installed) return true;
        g_overlay.SetRenderCallback([](float w, float h) { DrawReticle(w, h); });
        const bool ok = g_overlay.Install();
        g_installed = ok;
        Log::Line("reticle-overlay (DX12) install -> %s", ok ? "OK" : "FAILED");
        return ok;
    }

    void Remove()
    {
        if (!g_installed) return;
        g_overlay.Remove();
        g_installed = false;
        Log::Line("reticle-overlay removed");
    }

    void UpdateAim(double qx, double qy, double qz, double qw, bool active)
    {
        std::lock_guard<std::mutex> lk(g_aimMutex);
        g_qx = qx; g_qy = qy; g_qz = qz; g_qw = qw;
        g_aimActive = active;
    }

    void SetActive(bool active)
    {
        std::lock_guard<std::mutex> lk(g_aimMutex);
        g_aimActive = active;
    }

    void SetFovDegrees(float fovDegrees)
    {
        // Ignore implausible reads (struct-layout drift after a patch, a
        // uninitialised frame): keep the last good value rather than letting a
        // garbage FOV throw the reticle across the screen. 10-170 brackets every
        // realistic horizontal FOV.
        if (fovDegrees >= 10.0f && fovDegrees <= 170.0f) {
            g_fovHorizontalAt16x9.store(fovDegrees, std::memory_order_relaxed);
        }
    }

    bool GetScreenOffset(float& dx, float& dy)
    {
        if (!g_offsetValid.load(std::memory_order_relaxed)) return false;
        dx = g_offsetX.load(std::memory_order_relaxed);
        dy = g_offsetY.load(std::memory_order_relaxed);
        return true;
    }
}
