// Behaviour lock for the 6DOF lean boundary: which way the view moves for a
// physical lean, how much of the asymmetric budget each direction gets, and
// whether the shipped HeadTracking.ini still agrees with the code defaults.
//
// The mod used to carry InvertZ=true with LimitZ and LimitZBack swapped in the
// INI, the code defaults and the launcher-manifest seed alike. The two errors
// cancelled, so it behaved correctly, but the INI's Z keys named the opposite
// direction to every other mod's and any half-edit of that triple - a user
// clearing InvertZ, a sync that took one file and not the others - reversed the
// budgets while the direction still looked right.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

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

cameraunlock::PositionSettings DefaultSettings() {
    cameraunlock::PositionSettings s;
    s.sensitivity_x = pd::kSensitivityX;
    s.sensitivity_y = pd::kSensitivityY;
    s.sensitivity_z = pd::kSensitivityZ;
    s.invert_x = pd::kInvertX;
    s.invert_y = pd::kInvertY;
    s.invert_z = pd::kInvertZ;
    s.limit_x = pd::kLimitX;
    pd::ApplyVerticalLimit(s, pd::kLimitY);
    s.limit_z = pd::kLimitZ;
    s.limit_z_back = pd::kLimitZBack;
    return s;
}

cameraunlock::math::Vec3 Saturated(float rawX, float rawY, float rawZ,
                                   float limitY = pd::kLimitY) {
    cameraunlock::PositionProcessor processor;
    cameraunlock::PositionSettings settings = DefaultSettings();
    pd::ApplyVerticalLimit(settings, limitY);
    processor.SetSettings(settings);
    const cameraunlock::PositionData raw(rawX, rawY, rawZ);
    // Two ticks so the smoothing state has settled on the clamped value.
    processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
    return processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
}

double SaturatedSurge(float rawZ) {
    double surge = 0.0, sway = 0.0, heave = 0.0;
    pd::TrackerOffsetToUE(Saturated(0.0f, 0.0f, rawZ), surge, sway, heave);
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

void VerticalBudgetIsSymmetric() {
    // Raised well clear of limit_y_down's own 0.20 default, which is what made
    // the missing mirror invisible: at the shipped LimitY the two coincide.
    const float raised = 0.35f;
    CheckNear(static_cast<double>(Saturated(0.0f, 1.0f, 0.0f, raised).y), raised,
              "a raised LimitY widens the upward budget");
    CheckNear(static_cast<double>(Saturated(0.0f, -1.0f, 0.0f, raised).y), -raised,
              "a raised LimitY widens the downward budget by the same amount");
}

// Minimal reader for the shipped file: last "key = value" wins, sections are
// tracked so [Position] keys are not confused with same-named ones elsewhere.
std::string ReadIniValue(const char* section, const char* key) {
    std::ifstream file(SN2HT_SHIPPED_INI);
    if (!file) {
        std::printf("FAIL: cannot open %s\n", SN2HT_SHIPPED_INI);
        ++g_failures;
        return std::string();
    }
    std::string line, current, found;
    while (std::getline(file, line)) {
        const std::size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == ';' || line[start] == '#') continue;
        const std::size_t end = line.find_last_not_of(" \t\r");
        const std::string trimmed = line.substr(start, end - start + 1);
        if (trimmed.front() == '[') {
            current = trimmed.substr(1, trimmed.find(']') - 1);
            continue;
        }
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos || current != section) continue;
        std::string k = trimmed.substr(0, eq);
        std::string v = trimmed.substr(eq + 1);
        const std::size_t ke = k.find_last_not_of(" \t");
        k = k.substr(0, ke + 1);
        const std::size_t vs = v.find_first_not_of(" \t");
        if (vs != std::string::npos) v = v.substr(vs); else v.clear();
        if (k == key) found = v;
    }
    return found;
}

void CheckIniBool(const char* key, bool expected) {
    const std::string value = ReadIniValue("Position", key);
    const bool actual = (value == "true" || value == "1");
    if (actual == expected && !value.empty()) return;
    std::printf("FAIL: shipped INI [Position] %s is \"%s\", code default is %s\n",
                key, value.c_str(), expected ? "true" : "false");
    ++g_failures;
}

void CheckIniFloat(const char* key, float expected) {
    const std::string value = ReadIniValue("Position", key);
    if (!value.empty() && std::fabs(std::stod(value) - expected) <= 1e-6) return;
    std::printf("FAIL: shipped INI [Position] %s is \"%s\", code default is %.3f\n",
                key, value.c_str(), expected);
    ++g_failures;
}

void ShippedIniMatchesCodeDefaults() {
    CheckIniBool("InvertX", pd::kInvertX);
    CheckIniBool("InvertY", pd::kInvertY);
    CheckIniBool("InvertZ", pd::kInvertZ);
    CheckIniFloat("SensitivityX", pd::kSensitivityX);
    CheckIniFloat("SensitivityY", pd::kSensitivityY);
    CheckIniFloat("SensitivityZ", pd::kSensitivityZ);
    CheckIniFloat("LimitX", pd::kLimitX);
    CheckIniFloat("LimitY", pd::kLimitY);
    CheckIniFloat("LimitZ", pd::kLimitZ);
    CheckIniFloat("LimitZBack", pd::kLimitZBack);
}

} // namespace

int main() {
    ForwardLeanMovesViewForward();
    LeanBudgetsAreNotReversed();
    VerticalBudgetIsSymmetric();
    ShippedIniMatchesCodeDefaults();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all position checks passed\n");
    return 0;
}
