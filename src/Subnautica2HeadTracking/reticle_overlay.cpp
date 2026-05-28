// D3D12 overlay for the head-tracking reticle (Subnautica 2 renders on D3D12).
// Built on cameraunlock-core's DX12Overlay (kiero + ImGui). This translation
// unit owns the implementation macro so the heavy includes stay isolated.

#define CAMERAUNLOCK_DX12_OVERLAY_IMPLEMENTATION
#include <cameraunlock/rendering/dx12_overlay.h>

#include "reticle_overlay.h"
#include "logging.h"

#include <atomic>
#include <cmath>
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

            // Clean-aim direction (UE forward +X) in the tracked camera
            // frame = qrel * (1,0,0): first column of qrel's rotation
            // matrix. UE camera-local axes: X=fwd, Y=right, Z=up.
            const double depth = 1.0 - 2.0 * (qy*qy + qz*qz);
            const double right = 2.0 * (qx*qy + qw*qz);
            const double up    = 2.0 * (qx*qz - qw*qy);
            if (depth <= 0.01) {  // aim behind tracked view - hide
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }
            // Hor+ FOV: SN2 holds the vertical FOV constant across aspect
            // ratios and expands horizontal with width. Derive the constant
            // vertical FOV from the calibrated 16:9 horizontal value, then
            // re-expand horizontal by the live aspect. The old
            // `tanV = tanH / aspect` assumed a fixed horizontal FOV (Vert-),
            // which over-rotated the reticle horizontally on ultrawide (~2x too
            // sensitive at 32:9). At 16:9 both models coincide.
            const float aspect = w / h;
            const float fovH16x9 = g_fovHorizontalAt16x9.load(std::memory_order_relaxed);
            const float tanV = std::tan(fovH16x9 * 0.5f * 0.01745329252f) / kReticleRefAspect;
            const float tanH = tanV * aspect;
            float ndcX = static_cast<float>(right / depth) / tanH;
            float ndcY = static_cast<float>(up    / depth) / tanV;
            // Clamp at the viewport edge. Without this, body-forward approaching
            // 90deg off-axis sends right/depth into the thousands (depth->0) and
            // the reticle/tooltip-offset shoots far off-screen. Capping NDC to
            // |1| keeps motion bounded to the visible viewport - the reticle and
            // tooltip both pin to the screen edge instead of accelerating away.
            if (ndcX >  1.0f) ndcX =  1.0f;
            if (ndcX < -1.0f) ndcX = -1.0f;
            if (ndcY >  1.0f) ndcY =  1.0f;
            if (ndcY < -1.0f) ndcY = -1.0f;
            const float cx = w * 0.5f + ndcX * (w * 0.5f);
            const float cy = h * 0.5f - ndcY * (h * 0.5f);

            g_offsetX.store(cx - w * 0.5f, std::memory_order_relaxed);
            g_offsetY.store(cy - h * 0.5f, std::memory_order_relaxed);
            g_offsetValid.store(true, std::memory_order_relaxed);

            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            const ImU32 col = IM_COL32(220, 235, 255, 165);  // slightly blue-white, ~65%
            const ImVec2 c{cx, cy};
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
