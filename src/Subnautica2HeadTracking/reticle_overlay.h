#pragma once

namespace Subnautica2HeadTracking::ReticleOverlay
{
    // Install the D3D12 overlay hook and ImGui context. Idempotent; safe to
    // call once during mod bootstrap.
    bool Install();

    // Tear down the overlay (called from Shutdown).
    void Remove();

    // Push the aim-vs-view relative rotation each hooked frame so the overlay
    // can project the clean-aim crosshair into the head-tracked view.
    // qrel = trackedView^-1 * cleanView (unit quaternion x,y,z,w). active=false
    // hides the reticle entirely (tracking off / no data / not in gameplay -
    // i.e. main menu, PDA, pause, where the player controller shows the cursor).
    void UpdateAim(double qx, double qy, double qz, double qw, bool active);

    // Push the live horizontal FOV (degrees) read from the engine's
    // FMinimalViewInfo each renderer-caller frame. This is the aspect-independent
    // FOV scalar (treated as the 16:9-reference horizontal FOV; the overlay
    // re-derives per-aspect Hor+ scaling). The overlay projects the clean-aim
    // crosshair with this instead of a hardcoded FOV, so the reticle stays
    // correct when the player changes the FOV slider or zooms. Values outside a
    // sane range are ignored by the overlay.
    void SetFovDegrees(float fovDegrees);

    // Refresh only the active flag without touching the cached qrel. Called
    // by the GPV hook on non-renderer callers (caller-gate rejects them) so
    // the reticle keeps drawing across frames even when the renderer caller
    // doesn't fire - e.g. the brief respawn flow where UE temporarily routes
    // GPV through a different code path. The cached qrel from the last
    // renderer-caller UpdateAim stays valid until the next renderer call.
    void SetActive(bool active);

    // Body-forward reticle position as a pixel offset from screen centre
    // (backbuffer pixels: +x right, +y down). Returns false when no reticle is
    // currently drawn (tracking off, no data, not in gameplay, or aim behind
    // the tracked view), in which case dx/dy are left untouched. Used to drag
    // the interaction tooltip to where the body is actually aiming.
    bool GetScreenOffset(float& dx, float& dy);
}
