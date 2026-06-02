// Clean-aim -> screen-offset projection for the body-forward reticle.
//
// CPU-only: no D3D12 hooks, no swapchain interference. The offset computed
// here drives SetRenderTranslation on the game's own UMG reticle and tooltip
// widgets (see DriveReticleMove / DriveTooltipMove in headtracking_mod.cpp).
// This replaced the kiero+ImGui DX12 overlay, whose Present hook conflicted
// with DLSS Frame Generation (Streamline owns swapchain presentation when
// frame gen is active; a third-party Present hook device-removes the GPU).

#include <cameraunlock/rendering/aim_quat_projection.h>

#include "aim_projection.h"
#include "logging.h"

#include <atomic>
#include <mutex>
#include <windows.h>

namespace Subnautica2HeadTracking::AimProjection
{
    namespace
    {
        // Horizontal FOV at the 16:9 reference aspect, used to project clean
        // aim into the tracked view. The GPV hook reads the engine's live FOV
        // each renderer-caller frame (FMinimalViewInfo.FOV, an aspect-independent
        // scalar) and pushes it via SetFovDegrees, so the projection follows the
        // player's FOV slider. The seed below is the default until the first
        // live value arrives, and the fallback if a read ever looks invalid.
        // SN2 (UE5) uses Hor+ scaling, so the projection holds the vertical FOV
        // constant and re-expands horizontal by the live aspect ratio.
        constexpr float kReticleFovDefaultAt16x9 = 90.0f;
        constexpr float kReticleRefAspect = 16.0f / 9.0f;
        std::atomic<float> g_fovHorizontalAt16x9{kReticleFovDefaultAt16x9};

        std::mutex g_aimMutex;
        double g_qx = 0.0, g_qy = 0.0, g_qz = 0.0, g_qw = 1.0;
        bool   g_aimActive = false;

        // Aim position as a pixel offset from screen centre, published for the
        // widget movers on the game thread. Valid only while tracking is active
        // and the aim direction is in front of the tracked view.
        std::atomic<float> g_offsetX{0.0f};
        std::atomic<float> g_offsetY{0.0f};
        std::atomic<bool>  g_offsetValid{false};

        // The game's main window, used for the viewport dimensions the DX12
        // overlay used to get from the swapchain. Found by taking the largest
        // visible top-level window of this process (skips the splash screen and
        // any console window); re-enumerated if the cached handle dies (UE
        // recreates the window on fullscreen-mode changes).
        HWND g_gameWindow = nullptr;

        HWND GameWindow()
        {
            if (g_gameWindow && IsWindow(g_gameWindow)) return g_gameWindow;
            struct Ctx { DWORD pid; HWND best; LONG bestArea; };
            Ctx ctx{GetCurrentProcessId(), nullptr, 0};
            EnumWindows([](HWND wnd, LPARAM param) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(param);
                DWORD pid = 0;
                GetWindowThreadProcessId(wnd, &pid);
                if (pid != c->pid || !IsWindowVisible(wnd)) return TRUE;
                RECT rc{};
                if (!GetClientRect(wnd, &rc)) return TRUE;
                const LONG area = rc.right * rc.bottom;
                if (area > c->bestArea) { c->best = wnd; c->bestArea = area; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&ctx));
            if (ctx.best && ctx.best != g_gameWindow) {
                RECT rc{};
                GetClientRect(ctx.best, &rc);
                Log::Line("aim-projection: game window 0x%llx client=%ldx%ld",
                    reinterpret_cast<unsigned long long>(ctx.best),
                    rc.right, rc.bottom);
            }
            g_gameWindow = ctx.best;
            return ctx.best;
        }

        // Recompute the published offset from the cached qrel. Caller holds
        // g_aimMutex.
        void RecomputeOffsetLocked()
        {
            if (!g_aimActive) {
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }

            RECT rc{};
            const HWND wnd = GameWindow();
            if (!wnd || !GetClientRect(wnd, &rc) || rc.right <= 0 || rc.bottom <= 0) {
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }
            const float w = static_cast<float>(rc.right);
            const float h = static_cast<float>(rc.bottom);

            // Hor+ (MaintainYFOV) aspect model and viewport-edge NDC clamping
            // both live in cameraunlock-core; SN2 holds the vertical FOV
            // constant across aspect ratios and expands horizontal with width,
            // so a Vert- projection over-rotates ~2x at 32:9.
            const auto proj = cameraunlock::rendering::ProjectAimQuatHorPlus(
                g_qx, g_qy, g_qz, g_qw, w, h,
                g_fovHorizontalAt16x9.load(std::memory_order_relaxed),
                kReticleRefAspect);
            if (!proj.inFront) {  // aim behind tracked view - no valid offset
                g_offsetValid.store(false, std::memory_order_relaxed);
                return;
            }

            g_offsetX.store(proj.screenX - w * 0.5f, std::memory_order_relaxed);
            g_offsetY.store(proj.screenY - h * 0.5f, std::memory_order_relaxed);
            g_offsetValid.store(true, std::memory_order_relaxed);
        }
    }

    void UpdateAim(double qx, double qy, double qz, double qw, bool active)
    {
        std::lock_guard<std::mutex> lk(g_aimMutex);
        g_qx = qx; g_qy = qy; g_qz = qz; g_qw = qw;
        g_aimActive = active;
        RecomputeOffsetLocked();
    }

    void SetActive(bool active)
    {
        std::lock_guard<std::mutex> lk(g_aimMutex);
        g_aimActive = active;
        RecomputeOffsetLocked();
    }

    void SetFovDegrees(float fovDegrees)
    {
        // Ignore implausible reads (struct-layout drift after a patch, an
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
