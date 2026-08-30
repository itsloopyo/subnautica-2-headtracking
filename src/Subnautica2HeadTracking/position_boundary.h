#pragma once

#include <cameraunlock/data/position_settings.h>
#include <cameraunlock/math/vec3.h>

namespace Subnautica2HeadTracking::Position {

// Defaults for the INI's [Position] section. The mod writes no INI of its own,
// so a user who deletes HeadTracking.ini falls back to these; the shipped file
// and the launcher-manifest seed must therefore repeat them exactly.
// tests/position_tests.cpp checks that they do.
inline constexpr float kSensitivityX = 1.0f;
inline constexpr float kSensitivityY = 1.0f;
inline constexpr float kSensitivityZ = 1.0f;
inline constexpr bool  kInvertX = false;
inline constexpr bool  kInvertY = false;
inline constexpr bool  kInvertZ = false;
inline constexpr float kLimitX = 0.30f;
inline constexpr float kLimitY = 0.20f;
inline constexpr float kLimitZ = 0.40f;
inline constexpr float kLimitZBack = 0.10f;

// Apply the INI's single vertical limit to both vertical bounds.
//
// The processor clamps y as [-limit_y_down, +limit_y], and limit_y_down is a
// separate field carrying its own default. The INI exposes one LimitY, so it
// has to reach both bounds; assigning limit_y alone widened the upward budget
// and silently left downward travel pinned at the core's 0.20m.
inline void ApplyVerticalLimit(cameraunlock::PositionSettings& settings, float limitY) {
    settings.limit_y = limitY;
    settings.limit_y_down = limitY;
}

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
// in. This mod compensated for that by shipping LimitZ and LimitZBack swapped,
// which behaved correctly but meant the INI's two Z keys named the opposite
// direction to every other mod's. Negative z is the forward lean throughout
// the library.
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
