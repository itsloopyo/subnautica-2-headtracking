// v0.6.2's reader and startup code, transcribed from
// v0.6.2:src/Subnautica2HeadTracking/headtracking_mod.cpp (tag d4b8deb):
//
//   lines 88, 102, 117-118, 187-206, 646, 1043, 1079, 1083
//                    the globals the reader writes, at their initial values
//   lines 142-182    SanitizeSmoothing and WarnRetiredSmoothingKey, verbatim
//   lines 3128-3234  the config block of BootstrapThread, verbatim apart from
//                    the globals becoming fields of Published
//   lines 3293-3300  the hotkey registrations, as data
//
// The whole file cannot be compiled into a test (it hooks the game), so what is
// transcribed is everything between reading the file and the state the session
// starts in. src/position_boundary.h and src/logging.h beside this file are byte
// copies of v0.6.2's, and every cameraunlock-core source it includes holds the
// same bytes at v0.6.2's pin (bd22895) and at this repo's pin (CMakeLists.txt
// checks both).

#include "oracle_reader.h"

#include <cmath>
#include <memory>
#include <string>
#include <windows.h>

#include "src/logging.h"
#include "src/position_boundary.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/processing/position_processor.h"
#include "cameraunlock/protocol/udp_receiver.h"

namespace sn2_oracle {

namespace Log = ::cameraunlock::logging;

namespace {

float SanitizeSmoothing(const char* key, float v, float fallback)
{
    if (!std::isfinite(v)) {
        Log::Line("config: [Tracking] %s is not a finite number, using %.2f",
            key, fallback);
        return fallback;
    }
    if (v < 0.0f || v > 1.0f) {
        const float clamped = (v < 0.0f) ? 0.0f : 1.0f;
        Log::Line("config: [Tracking] %s %.2f is outside 0.0-1.0, using %.2f",
            key, v, clamped);
        return clamped;
    }
    return v;
}

void WarnRetiredSmoothingKey(const cameraunlock::IniReader& ini,
    const char* section, const char* key)
{
    static bool warned = false;
    if (warned) return;
    if (ini.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "WARNING: Config key [%s] %s has been retired and is IGNORED. Smoothing "
        "is now two keys: LocalSmoothing (default 0, applies to a tracker on "
        "this machine) and RemoteSmoothing (default 0.15, applies to a tracker "
        "on the network). The old value is not migrated because the semantics "
        "changed - it carried a hidden 0.15 floor that no longer exists. Set "
        "the two new keys.",
        section, key);
}

}  // namespace

Published Read(const std::string& dll_dir)
{
    // The globals, at their initial values.
    Published g;
    g.tracking_enabled = true;
    g.yaw_sens = 1.0f;
    g.pitch_sens = 1.0f;
    g.roll_sens = 1.0f;
    g.invert_yaw = false;
    g.invert_pitch = false;
    g.invert_roll = false;
    g.local_smoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    g.remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);
    g.reticle_move_on = true;
    g.world_space_yaw = true;
    g.tooltip_move_on = true;
    g.tooltip_follow_scale = 1.0f;
    g.rotation_enabled = true;
    g.position_enabled = true;
    g.tracking_mode = 0;
    g.disable_mask_comp = false;
    cameraunlock::PositionProcessor g_posProcessor;

    int yawModeKey = 0x22;  // Page Down
    int udpPort = cameraunlock::UdpReceiver::kDefaultPort;
    {
        namespace pd = Subnautica2HeadTracking::Position;
        cameraunlock::PositionSettings ps = g_posProcessor.GetSettings();
        ps.sensitivity_x = pd::kSensitivityX;
        ps.sensitivity_y = pd::kSensitivityY;
        ps.sensitivity_z = pd::kSensitivityZ;
        ps.invert_x = pd::kInvertX;
        ps.invert_y = pd::kInvertY;
        ps.invert_z = pd::kInvertZ;
        ps.limit_x = pd::kLimitX;
        pd::ApplyVerticalLimit(ps, pd::kLimitY);
        ps.limit_z = pd::kLimitZ;
        ps.limit_z_back = pd::kLimitZBack;

        g_posProcessor.SetTrackerPivotForward(0.0f);

        cameraunlock::IniReader ini;
        const std::string iniPath = dll_dir + "HeadTracking.ini";
        if (ini.Open(iniPath)) {
            udpPort = ini.ReadInt("Network", "Port", udpPort);
            if (udpPort < 1024 || udpPort > 65535) {
                Log::Line("config: [Network] Port %d out of range 1024-65535, using %u",
                    udpPort, cameraunlock::UdpReceiver::kDefaultPort);
                udpPort = cameraunlock::UdpReceiver::kDefaultPort;
            }

            g.tracking_enabled = ini.ReadBool("Tracking", "EnableOnStartup", true);
            g.yaw_sens       = ini.ReadFloat("Tracking", "YawSensitivity", 1.0f);
            g.pitch_sens     = ini.ReadFloat("Tracking", "PitchSensitivity", 1.0f);
            g.roll_sens      = ini.ReadFloat("Tracking", "RollSensitivity", 1.0f);
            g.invert_yaw     = ini.ReadBool("Tracking", "InvertYaw", false);
            g.invert_pitch   = ini.ReadBool("Tracking", "InvertPitch", false);
            g.invert_roll    = ini.ReadBool("Tracking", "InvertRoll", false);
            g.local_smoothing  = SanitizeSmoothing("LocalSmoothing",
                ini.ReadFloat("Tracking", "LocalSmoothing", g.local_smoothing),
                static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing));
            g.remote_smoothing = SanitizeSmoothing("RemoteSmoothing",
                ini.ReadFloat("Tracking", "RemoteSmoothing", g.remote_smoothing),
                static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing));
            WarnRetiredSmoothingKey(ini, "Tracking", "Smoothing");
            g.reticle_move_on = ini.ReadBool("Tracking", "ShowReticle", true);
            g.world_space_yaw = ini.ReadBool("Tracking", "WorldSpaceYaw", true);

            g.tooltip_move_on = ini.ReadBool("Tooltip", "FollowReticle", true);
            g.tooltip_follow_scale = ini.ReadFloat("Tooltip", "FollowScale", 1.0f);

            g.position_enabled = ini.ReadBool("Position", "Enabled", true);
            ps.sensitivity_x = ini.ReadFloat("Position", "SensitivityX", ps.sensitivity_x);
            ps.sensitivity_y = ini.ReadFloat("Position", "SensitivityY", ps.sensitivity_y);
            ps.sensitivity_z = ini.ReadFloat("Position", "SensitivityZ", ps.sensitivity_z);
            ps.invert_x      = ini.ReadBool("Position", "InvertX", ps.invert_x);
            ps.invert_y      = ini.ReadBool("Position", "InvertY", ps.invert_y);
            ps.invert_z      = ini.ReadBool("Position", "InvertZ", ps.invert_z);
            ps.limit_x       = ini.ReadFloat("Position", "LimitX", ps.limit_x);
            pd::ApplyVerticalLimit(ps, ini.ReadFloat("Position", "LimitY", ps.limit_y));
            ps.limit_z       = ini.ReadFloat("Position", "LimitZ", ps.limit_z);
            ps.limit_z_back  = ini.ReadFloat("Position", "LimitZBack", ps.limit_z_back);
            WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

            yawModeKey = ini.ReadHex("Hotkeys", "ToggleYawMode", 0x22);

            g.disable_mask_comp = ini.ReadBool("Debug", "DisableMaskComp", false);
        } else {
            Log::Line("config: no HeadTracking.ini next to DLL; using defaults");
        }
        ps.local_smoothing  = g.local_smoothing;
        ps.remote_smoothing = g.remote_smoothing;
        g_posProcessor.SetSettings(ps);
    }
    g.udp_port = udpPort;
    g.position = g_posProcessor.GetSettings();

    // Toggle tracking: End / Ctrl+Shift+Y
    g.hotkeys.push_back({kToggle, VK_END, 0});
    g.hotkeys.push_back({kToggle, 0x59 /* Y */, 3});
    // Cycle tracking mode: Page Up / Ctrl+Shift+G
    g.hotkeys.push_back({kCycleMode, VK_PRIOR, 0});
    g.hotkeys.push_back({kCycleMode, 0x47 /* G */, 3});
    // Yaw mode (world/local): Page Down (or [Hotkeys] ToggleYawMode) / Ctrl+Shift+H
    g.hotkeys.push_back({kYawMode, yawModeKey, 0});
    g.hotkeys.push_back({kYawMode, 0x48 /* H */, 3});
    return g;
}

}  // namespace sn2_oracle
