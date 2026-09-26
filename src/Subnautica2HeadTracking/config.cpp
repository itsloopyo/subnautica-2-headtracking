#include "config.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"

namespace Subnautica2HeadTracking::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;
using ::cameraunlock::input::FormatKeyBindings;
using ::cameraunlock::input::KeyModifiers;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for subnautica-2.
constexpr const char* kDisplayName = "Subnautica 2";

constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

void Save(const char* rows, const std::function<void(Config&)>& change) {
    const cfg::ConfigSaveResult result = g_owner->Save(change);
    if (result.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("config: %s %s: %s", rows, cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    }
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    legacy::Config read;
    const bool present = legacy::Load(input.ansi_path, read);
    const Config defaults;

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> pose_shaping;
    // v0.6.2 shipped every sensitivity at 1 and every inversion false, and its
    // position boundary (position_boundary.h) already held both axis mirrors,
    // so the mod applies the pose as the tracker sends it and folds nothing.
    const auto shaping = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, pose_shaping, dropped);
    };
    shaping(read.yaw_sensitivity, 1.0f, "Tracking", "YawSensitivity");
    shaping(read.pitch_sensitivity, 1.0f, "Tracking", "PitchSensitivity");
    shaping(read.roll_sensitivity, 1.0f, "Tracking", "RollSensitivity");
    shaping(read.invert_yaw, false, "Tracking", "InvertYaw");
    shaping(read.invert_pitch, false, "Tracking", "InvertPitch");
    shaping(read.invert_roll, false, "Tracking", "InvertRoll");
    shaping(read.position_sensitivity_x, 1.0f, "Position", "SensitivityX");
    shaping(read.position_sensitivity_y, 1.0f, "Position", "SensitivityY");
    shaping(read.position_sensitivity_z, 1.0f, "Position", "SensitivityZ");
    shaping(read.position_invert_x, false, "Position", "InvertX");
    shaping(read.position_invert_y, false, "Position", "InvertY");
    shaping(read.position_invert_z, false, "Position", "InvertZ");

    // The reader keeps the port inside 1024-65535 and each smoothing value
    // finite and inside 0-1, so both carry over as they are.
    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.world_space_yaw = read.world_space_yaw;
    // The reticle always follows the aim now; only a file that turned that off
    // loses something.
    if (!read.show_reticle) dropped.push_back({cfg::DropRule::Reticle, "Tracking", "ShowReticle", "false"});

    // [Position] Enabled chose the startup mode and nothing else: the cycle
    // reached every mode either way.
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(
        read.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                              : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = channels.rotation_enabled;
    out.position_enabled = channels.position_enabled;

    // The reader checks no limit, so one that is not a finite number imports
    // as its default (N2). A finite one outside 0-10 has no rule: the owner
    // cannot write it and defers the import. LimitY set both vertical bounds.
    out.position_limit_x = cfg::LegacyFiniteOrDefault(read.limit_x, defaults.position_limit_x, "Position", "LimitX", dropped);
    const float limit_y = cfg::LegacyFiniteOrDefault(read.limit_y, defaults.position_limit_y, "Position", "LimitY", dropped);
    out.position_limit_y = limit_y;
    out.position_limit_y_down = limit_y;
    out.position_limit_z = cfg::LegacyFiniteOrDefault(read.limit_z, defaults.position_limit_z, "Position", "LimitZ", dropped);
    out.position_limit_z_back =
        cfg::LegacyFiniteOrDefault(read.limit_z_back, defaults.position_limit_z_back, "Position", "LimitZBack", dropped);

    // End, Page Up and the three Ctrl+Shift chords were bound in code; only the
    // yaw key was in the file, read with no range check. A code outside
    // 0x01-0xFE imports as unbound (N1), and 0 was unbound already.
    out.toggle_key = FormatKeyBindings({{KeyModifiers::kNone, VK_END}, {kChord, 'Y'}});
    out.cycle_tracking_mode_key = FormatKeyBindings({{KeyModifiers::kNone, VK_PRIOR}, {kChord, 'G'}});
    const std::string yaw_key = cfg::LegacyVirtualKeyToBindings(read.yaw_mode_key, "Hotkeys", "ToggleYawMode", dropped);
    const std::string yaw_chord = FormatKeyBindings({{kChord, 'H'}});
    out.yaw_mode_key = yaw_key.empty() ? yaw_chord : yaw_key + ", " + yaw_chord;

    out.tooltip_follow_reticle = read.tooltip_follow_reticle;
    out.tooltip_follow_scale =
        cfg::LegacyFiniteOrDefault(read.tooltip_follow_scale, defaults.tooltip_follow_scale, "Tooltip", "FollowScale", dropped);
    out.disable_mask_comp = read.disable_mask_comp;

    return present ? cfg::ImportResult::Imported(std::move(dropped), std::move(pose_shaping))
                   : cfg::ImportResult::Absent(std::move(dropped), std::move(pose_shaping));
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::PositionLimitX>(&Config::position_limit_x)
        .Concept<Concept::PositionLimitY>(&Config::position_limit_y)
        .Concept<Concept::PositionLimitYDown>(&Config::position_limit_y_down)
        .Concept<Concept::PositionLimitZ>(&Config::position_limit_z)
        .Concept<Concept::PositionLimitZBack>(&Config::position_limit_z_back)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<Concept::YawModeKey>(&Config::yaw_mode_key)
        .Local("Tooltip", "FollowReticle", &Config::tooltip_follow_reticle, cfg::BoolCodec(),
               "true: the prompt naming what you point at moves with the reticle, so it stays where\n"
               "you aim. false: it stays at the centre of the view.")
        .Local("Tooltip", "FollowScale", &Config::tooltip_follow_scale, cfg::FloatCodec(),
               "Scales how far the reticle and that prompt move to follow your aim. Leave it at 1.0\n"
               "unless they overshoot your aim at your resolution (lower it) or fall short (raise it).")
        .Local("Debug", "DisableMaskComp", &Config::disable_mask_comp, cfg::BoolCodec(),
               "true: turn off the correction that keeps the mask fixed on screen as your head\n"
               "turns; head tracking stays on. For a crash or conflict with another program that\n"
               "hooks the game's rendering, such as an overlay or ReShade.");
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::LegacyImport<Config> Import() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    for (const legacy::Key& key : legacy::ReadKeys()) import.keys.push_back({key.section, key.key});
    return import;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::filesystem::path& folder, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = (folder / kIniName).wstring();
    options.table = Table();
    options.import = Import();
    options.legacy_path = (folder / kLegacyIniName).wstring();
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::filesystem::path& folder, cfg::DefaultsFile defaults) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(folder, std::move(defaults)));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
    if (!result.reason.empty()) Log::Line("config: %s", result.reason.c_str());
    Log::Line("config: %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

cameraunlock::TrackingMode StartupTrackingMode(const Config& config) {
    const auto mode = cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
    if (!mode) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which the table never gives");
    return *mode;
}

void SaveWorldSpaceYaw(bool world_space_yaw) {
    Save("[General] WorldSpaceYaw", [world_space_yaw](Config& c) { c.world_space_yaw = world_space_yaw; });
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save("[General] RotationEnabled and [Position] PositionEnabled", [channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

}  // namespace Subnautica2HeadTracking::config
