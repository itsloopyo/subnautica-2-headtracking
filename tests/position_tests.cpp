// Behaviour lock for the 6DOF lean boundary: which way the view moves for a
// physical lean, and how much of the asymmetric budget each direction gets.
//
// The mod used to carry InvertZ=true with LimitZ and LimitZBack swapped in the
// INI, the code defaults and the launcher-manifest seed alike. The two errors
// cancelled, so it behaved correctly, but the INI's Z keys named the opposite
// direction to every other mod's and any half-edit of that triple - a user
// clearing InvertZ, a sync that took one file and not the others - reversed the
// budgets while the direction still looked right. Both mirrors now live in
// position_boundary.h, past the clamp, and no setting inverts an axis.

#include <cmath>
#include <cstdio>

#include "position_boundary.h"

#include <cameraunlock/processing/position_processor.h>

namespace {

namespace pd = Subnautica2HeadTracking::Position;

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void CheckNear(double actual, double expected, const char* what) {
    if (std::fabs(actual - expected) <= 1e-3) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what, expected, actual);
    ++g_failures;
}

// The processor's z runs negative for a forward lean; UE's camera-local
// forward is +X, which surge feeds.
void ForwardLeanMovesViewForward() {
    double surge = 0.0, sway = 0.0, heave = 0.0;
    pd::TrackerOffsetToUE(cameraunlock::math::Vec3(0.0f, 0.0f, -0.25f), surge, sway, heave);
    Check(surge > 0.0, "forward lean (processor z < 0) moves the view forward");

    pd::TrackerOffsetToUE(cameraunlock::math::Vec3(0.0f, 0.0f, 0.25f), surge, sway, heave);
    Check(surge < 0.0, "backward lean (processor z > 0) moves the view back");

    pd::TrackerOffsetToUE(cameraunlock::math::Vec3(0.25f, 0.0f, 0.0f), surge, sway, heave);
    Check(sway < 0.0, "tracker +x moves the view left (SN2 mirrors sway)");

    pd::TrackerOffsetToUE(cameraunlock::math::Vec3(0.0f, 0.1f, 0.0f), surge, sway, heave);
    CheckNear(heave, 0.1 * pd::kMetersToUE, "up maps to +heave, in centimetres");
}

// The settings BootstrapThread hands the processor at the default limits:
// core's defaults, whose sensitivities are 1 and whose inversions are off.
cameraunlock::PositionSettings DefaultSettings() {
    cameraunlock::PositionSettings s;
    s.limit_x = pd::kLimitX;
    s.limit_y = pd::kLimitY;
    s.limit_y_down = pd::kLimitYDown;
    s.limit_z = pd::kLimitZ;
    s.limit_z_back = pd::kLimitZBack;
    return s;
}

cameraunlock::math::Vec3 Saturated(const cameraunlock::PositionSettings& settings,
                                   float rawX, float rawY, float rawZ) {
    cameraunlock::PositionProcessor processor;
    processor.SetSettings(settings);
    const cameraunlock::PositionData raw(rawX, rawY, rawZ);
    // Two ticks so the smoothing state has settled on the clamped value.
    processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
    return processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
}

double SaturatedSurge(float rawZ) {
    double surge = 0.0, sway = 0.0, heave = 0.0;
    pd::TrackerOffsetToUE(Saturated(DefaultSettings(), 0.0f, 0.0f, rawZ), surge, sway, heave);
    return surge;
}

void LeanBudgetsAreNotReversed() {
    // A metre of physical lean either way, far past both limits, so the output
    // is whichever budget that direction actually got.
    CheckNear(SaturatedSurge(-1.0f), pd::kLimitZ * pd::kMetersToUE,
              "forward lean gets the LimitZ budget");
    CheckNear(SaturatedSurge(1.0f), -pd::kLimitZBack * pd::kMetersToUE,
              "backward lean gets the LimitZBack budget");
}

void EachVerticalLimitBoundsItsOwnDirection() {
    // Apart and clear of core's 0.20m default for both, so a bound taken from
    // the wrong field shows.
    cameraunlock::PositionSettings settings = DefaultSettings();
    settings.limit_y = 0.35f;
    settings.limit_y_down = 0.05f;
    CheckNear(static_cast<double>(Saturated(settings, 0.0f, 1.0f, 0.0f).y), 0.35,
              "PositionLimitY bounds raising the head");
    CheckNear(static_cast<double>(Saturated(settings, 0.0f, -1.0f, 0.0f).y), -0.05,
              "PositionLimitYDown bounds lowering it");
}

} // namespace

int main() {
    ForwardLeanMovesViewForward();
    LeanBudgetsAreNotReversed();
    EachVerticalLimitBoundsItsOwnDirection();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all position checks passed\n");
    return 0;
}
