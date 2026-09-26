#pragma once

#include <cameraunlock/data/position_settings.h>
#include <cameraunlock/math/vec3.h>

namespace Subnautica2HeadTracking::Position {

// The lean budgets CameraUnlock.ini starts from, in metres: core's
// PositionSettings defaults, which are also the canonical config schema's.
inline constexpr float kLimitX = cameraunlock::PositionSettings{}.limit_x;
inline constexpr float kLimitY = cameraunlock::PositionSettings{}.limit_y;
inline constexpr float kLimitYDown = cameraunlock::PositionSettings{}.limit_y_down;
inline constexpr float kLimitZ = cameraunlock::PositionSettings{}.limit_z;
inline constexpr float kLimitZBack = cameraunlock::PositionSettings{}.limit_z_back;

// UE works in centimetres; the processor hands out metres.
inline constexpr double kMetersToUE = 100.0;

// Map a processed offset (metres, tracker axes: x = right, y = up, z = depth)
// onto UE camera-local surge (forward), sway (right) and heave (up), in
// centimetres.
//
// Depth is negated here, at the engine boundary, rather than through the
// processor's InvertZ. The processor inverts BEFORE its asymmetric clamp of
// [-LimitZ, +LimitZBack], so flipping the sign there hands the generous 0.40m
// allowance to leaning back and the 0.10m anti-clipping allowance to leaning
// in. Negative z is the forward lean throughout the library.
//
// Sway is negated here for the same reason of keeping one place to look. X is
// clamped symmetrically, so moving it changes nothing but where it is written.
inline void TrackerOffsetToUE(const cameraunlock::math::Vec3& off,
                              double& surge, double& sway, double& heave) {
    surge = -static_cast<double>(off.z) * kMetersToUE;
    sway  = -static_cast<double>(off.x) * kMetersToUE;
    heave =  static_cast<double>(off.y) * kMetersToUE;
}

} // namespace Subnautica2HeadTracking::Position
