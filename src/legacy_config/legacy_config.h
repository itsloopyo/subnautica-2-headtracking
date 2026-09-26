#pragma once

#include <string>
#include <vector>

// The pre-canonical HeadTracking.ini reader, frozen. It reads a file the way
// the last build before the canonical config format did, so a player's old
// file is carried over as that build read it. Never edit anything in this
// folder: tests/config_differential/ pins every file here by hash.
//
// Frozen from src/Subnautica2HeadTracking/headtracking_mod.cpp at 6cbedf1,
// whose reader is byte for byte v0.6.2's (SanitizeSmoothing,
// WarnRetiredSmoothingKey and the config block of BootstrapThread), with three
// changes: it fills this frozen copy of that commit's settings and their
// defaults instead of the mod's globals, it writes nothing, and it lives in
// namespace Subnautica2HeadTracking::legacy. The defaults are written as the
// literals the code held then (cameraunlock-core's PositionSettings limits,
// its smoothing defaults and UdpReceiver::kDefaultPort), so a later change to
// core cannot move what an old file means.
namespace Subnautica2HeadTracking::legacy {

struct Config {
    // [Network]
    int udp_port = 4242;

    // [Tracking]
    bool enable_on_startup = true;
    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;
    bool show_reticle = true;
    bool world_space_yaw = true;

    // [Tooltip]
    bool tooltip_follow_reticle = true;
    float tooltip_follow_scale = 1.0f;

    // [Position]. LimitY set both vertical bounds, up and down.
    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    bool position_invert_x = false;
    bool position_invert_y = false;
    bool position_invert_z = false;
    float limit_x = 0.30f;
    float limit_y = 0.20f;
    float limit_z = 0.40f;
    float limit_z_back = 0.10f;

    // [Hotkeys]. A virtual-key code, read with IniReader::ReadHex and not
    // range-checked: the build registered whatever it read.
    int yaw_mode_key = 0x22;  // Page Down

    // [Debug]
    bool disable_mask_comp = false;
};

// Reads `ini_path` into `out`; keys the file lacks keep their defaults. Returns
// whether the file was there to read (IniReader::Open), which is when the
// published build logged its settings rather than "using defaults".
bool Load(const std::string& ini_path, Config& out);

struct Key {
    const char* section;
    const char* key;
};

// Every key Load takes a value from. The retired [Tracking] Smoothing and
// [Position] Smoothing are read only to warn that they are ignored, so they
// are not among them.
std::vector<Key> ReadKeys();

}  // namespace Subnautica2HeadTracking::legacy
