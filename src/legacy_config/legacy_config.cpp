// Frozen. See legacy_config.h.

#include "legacy_config.h"

#include <cmath>
#include <string>
#include <vector>

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

namespace Subnautica2HeadTracking::legacy {

namespace {

constexpr float kDefaultLocalSmoothing = 0.0f;
constexpr float kDefaultRemoteSmoothing = 0.15f;
constexpr unsigned kDefaultPort = 4242;

// The strtod behind IniReader::ReadFloat parses "nan" and "inf" as floats, and
// std::clamp does not reject a NaN, so a value that is not finite is replaced
// by the key's own default and one outside 0-1 is clamped into it, with a log
// line either way.
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

// Warned once per process. The old single Smoothing value is not carried into
// the two keys that replaced it.
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

bool Load(const std::string& ini_path, Config& out)
{
    cameraunlock::IniReader ini;
    if (!ini.Open(ini_path)) {
        Log::Line("config: no HeadTracking.ini next to DLL; using defaults");
        return false;
    }

    out.udp_port = ini.ReadInt("Network", "Port", out.udp_port);
    if (out.udp_port < 1024 || out.udp_port > 65535) {
        Log::Line("config: [Network] Port %d out of range 1024-65535, using %u",
            out.udp_port, kDefaultPort);
        out.udp_port = static_cast<int>(kDefaultPort);
    }

    out.enable_on_startup = ini.ReadBool("Tracking", "EnableOnStartup", true);
    out.yaw_sensitivity   = ini.ReadFloat("Tracking", "YawSensitivity", 1.0f);
    out.pitch_sensitivity = ini.ReadFloat("Tracking", "PitchSensitivity", 1.0f);
    out.roll_sensitivity  = ini.ReadFloat("Tracking", "RollSensitivity", 1.0f);
    out.invert_yaw        = ini.ReadBool("Tracking", "InvertYaw", false);
    out.invert_pitch      = ini.ReadBool("Tracking", "InvertPitch", false);
    out.invert_roll       = ini.ReadBool("Tracking", "InvertRoll", false);
    out.local_smoothing  = SanitizeSmoothing("LocalSmoothing",
        ini.ReadFloat("Tracking", "LocalSmoothing", out.local_smoothing),
        kDefaultLocalSmoothing);
    out.remote_smoothing = SanitizeSmoothing("RemoteSmoothing",
        ini.ReadFloat("Tracking", "RemoteSmoothing", out.remote_smoothing),
        kDefaultRemoteSmoothing);
    WarnRetiredSmoothingKey(ini, "Tracking", "Smoothing");
    out.show_reticle    = ini.ReadBool("Tracking", "ShowReticle", true);
    out.world_space_yaw = ini.ReadBool("Tracking", "WorldSpaceYaw", true);

    out.tooltip_follow_reticle = ini.ReadBool("Tooltip", "FollowReticle", true);
    out.tooltip_follow_scale   = ini.ReadFloat("Tooltip", "FollowScale", 1.0f);

    out.position_enabled       = ini.ReadBool("Position", "Enabled", true);
    out.position_sensitivity_x = ini.ReadFloat("Position", "SensitivityX", out.position_sensitivity_x);
    out.position_sensitivity_y = ini.ReadFloat("Position", "SensitivityY", out.position_sensitivity_y);
    out.position_sensitivity_z = ini.ReadFloat("Position", "SensitivityZ", out.position_sensitivity_z);
    out.position_invert_x      = ini.ReadBool("Position", "InvertX", out.position_invert_x);
    out.position_invert_y      = ini.ReadBool("Position", "InvertY", out.position_invert_y);
    out.position_invert_z      = ini.ReadBool("Position", "InvertZ", out.position_invert_z);
    out.limit_x                = ini.ReadFloat("Position", "LimitX", out.limit_x);
    out.limit_y                = ini.ReadFloat("Position", "LimitY", out.limit_y);
    out.limit_z                = ini.ReadFloat("Position", "LimitZ", out.limit_z);
    out.limit_z_back           = ini.ReadFloat("Position", "LimitZBack", out.limit_z_back);
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");

    out.yaw_mode_key = ini.ReadHex("Hotkeys", "ToggleYawMode", 0x22);

    out.disable_mask_comp = ini.ReadBool("Debug", "DisableMaskComp", false);

    Log::Line("config: Port=%d  EnableOnStartup=%s  Sens=(Y=%.2f P=%.2f R=%.2f)  Invert=(Y=%d P=%d R=%d)  LocalSmoothing=%.2f  RemoteSmoothing=%.2f  ShowReticle=%s  WorldSpaceYaw=%s",
        out.udp_port,
        out.enable_on_startup ? "true" : "false",
        out.yaw_sensitivity, out.pitch_sensitivity, out.roll_sensitivity,
        out.invert_yaw ? 1 : 0, out.invert_pitch ? 1 : 0, out.invert_roll ? 1 : 0,
        out.local_smoothing, out.remote_smoothing,
        out.show_reticle ? "true" : "false",
        out.world_space_yaw ? "true" : "false");
    Log::Line("config: TooltipFollow=%s scale=%.2f  PosEnabled=%s  ToggleYawMode=0x%02x  DisableMaskComp=%s",
        out.tooltip_follow_reticle ? "true" : "false",
        out.tooltip_follow_scale,
        out.position_enabled ? "true" : "false",
        out.yaw_mode_key,
        out.disable_mask_comp ? "true" : "false");
    return true;
}

std::vector<Key> ReadKeys()
{
    return {
        {"Network", "Port"},
        {"Tracking", "EnableOnStartup"},
        {"Tracking", "YawSensitivity"},
        {"Tracking", "PitchSensitivity"},
        {"Tracking", "RollSensitivity"},
        {"Tracking", "InvertYaw"},
        {"Tracking", "InvertPitch"},
        {"Tracking", "InvertRoll"},
        {"Tracking", "LocalSmoothing"},
        {"Tracking", "RemoteSmoothing"},
        {"Tracking", "ShowReticle"},
        {"Tracking", "WorldSpaceYaw"},
        {"Tooltip", "FollowReticle"},
        {"Tooltip", "FollowScale"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Hotkeys", "ToggleYawMode"},
        {"Debug", "DisableMaskComp"},
    };
}

}  // namespace Subnautica2HeadTracking::legacy
