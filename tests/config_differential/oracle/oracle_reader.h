#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "cameraunlock/data/position_settings.h"

// The oracle: what v0.6.2, the newest published build, ran on after reading
// HeadTracking.ini. oracle_reader.cpp transcribes its reader and startup code.
namespace sn2_oracle {

enum Action { kToggle = 0, kCycleMode = 1, kYawMode = 2 };

// One HotkeyPoller registration: the action, the code, and 3 where the
// callback is ChordGuarded (fires only while Ctrl and Shift are both held), 0
// where it fires whatever is held.
using Registration = std::tuple<int, int, unsigned>;

struct Published {
    int udp_port = 0;
    bool tracking_enabled = false;
    float yaw_sens = 0, pitch_sens = 0, roll_sens = 0;
    bool invert_yaw = false, invert_pitch = false, invert_roll = false;
    float local_smoothing = 0, remote_smoothing = 0;
    bool reticle_move_on = false;
    bool world_space_yaw = false;
    bool tooltip_move_on = false;
    float tooltip_follow_scale = 0;
    bool rotation_enabled = false;
    bool position_enabled = false;
    int tracking_mode = 0;
    // What BootstrapThread handed g_posProcessor.SetSettings.
    cameraunlock::PositionSettings position;
    bool disable_mask_comp = false;
    std::vector<Registration> hotkeys;
};

// `dll_dir` is the folder the DLL loaded from, with its trailing backslash, as
// DllDirNarrow returned it.
Published Read(const std::string& dll_dir);

}  // namespace sn2_oracle
