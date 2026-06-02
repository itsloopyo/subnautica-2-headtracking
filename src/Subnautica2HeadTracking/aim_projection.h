#pragma once

namespace Subnautica2HeadTracking::AimProjection
{
    // Push the aim-vs-view relative rotation each hooked frame so the projector
    // can compute where the clean-aim crosshair lands in the head-tracked view.
    // qrel = trackedView^-1 * cleanView (unit quaternion x,y,z,w). active=false
    // invalidates the offset entirely (tracking off / no data / not in gameplay -
    // i.e. main menu, PDA, pause, where the player controller shows the cursor).
    void UpdateAim(double qx, double qy, double qz, double qw, bool active);

    // Push the live horizontal FOV (degrees) read from the engine's
    // FMinimalViewInfo each renderer-caller frame. This is the aspect-independent
    // FOV scalar (treated as the 16:9-reference horizontal FOV; the projector
    // re-derives per-aspect Hor+ scaling), so the projected offset stays correct
    // when the player changes the FOV slider or zooms. Values outside a sane
    // range are ignored. Fallback only - superseded by SetProjectionScale.
    void SetFovDegrees(float fovDegrees);

    // Push the game-measured projection scale: viewport pixels of screen offset
    // per unit of tan(angle) off the view axis, per axis. Measured by pushing
    // test points through the game's own ProjectWorldLocationToScreen, so it
    // inherits the game's true FOV, aspect-ratio constraint, and any runtime
    // FOV effects (zoom, underwater) - none of which the FMinimalViewInfo-FOV
    // model can capture (its FOV scalar is reinterpreted by the engine's
    // AspectRatioAxisConstraint, which we would otherwise have to guess).
    // Once a valid measurement arrives, it supersedes the FOV model.
    // Degenerate measurements are ignored.
    void SetProjectionScale(float pxPerTanRight, float pxPerTanUp);

    // Refresh only the active flag without touching the cached qrel. Called
    // by the GPV hook on non-renderer callers (caller-gate rejects them) so
    // the offset stays valid across frames even when the renderer caller
    // doesn't fire - e.g. the brief respawn flow where UE temporarily routes
    // GPV through a different code path. The cached qrel from the last
    // renderer-caller UpdateAim stays valid until the next renderer call.
    void SetActive(bool active);

    // Body-forward aim position as a pixel offset from screen centre
    // (game-window client pixels: +x right, +y down). Returns false when no
    // valid offset exists (tracking off, no data, not in gameplay, or aim
    // behind the tracked view), in which case dx/dy are left untouched. Drives
    // the native reticle and interaction-tooltip widget moves.
    bool GetScreenOffset(float& dx, float& dy);
}
