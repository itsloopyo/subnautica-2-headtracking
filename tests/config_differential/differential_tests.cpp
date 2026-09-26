// The differential test for the conversion from HeadTracking.ini to
// CameraUnlock.ini.
//
// Three readings of every input:
//
//   Oracle     v0.6.2's reader and startup code, the newest published build
//              (oracle/oracle_reader.cpp; the repo publishes v* releases only)
//   Import     the frozen reader in src/legacy_config/, and the startup code
//              the commit that froze it ran it through
//   Migration  the config owner's Load in a folder holding only the input as
//              HeadTracking.ini, which imports it through config::Import into a
//              new CameraUnlock.ini, then this build's startup code on what the
//              session runs on
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since v0.6.2 that change how the file is
// read. There are none. The frozen reader is v0.6.2's reader moved into its own
// file, every core source both compile holds the same bytes at v0.6.2's pin and
// at this repo's (CMakeLists.txt pins them), and src/ was unchanged since the
// tag when it was frozen, so the comparison may find no difference at all,
// floats bit for bit.
//
// Comparison 2, import against migration, is the proof for the migration: no
// difference but the approved ones, each of which the import records as
// dropped. A sensitivity or axis inversion the player set away from what v0.6.2
// shipped (1 and false for every one) is dropped (pose_shaping), which includes
// the InvertX=true and InvertZ=true that v0.1.0 to v0.6.0 shipped; ShowReticle
// false is dropped (reticle), since the reticle always follows the aim now; a
// limit or FollowScale that is not a finite number imports as its default (N2);
// a ToggleYawMode code outside 0x01-0xFE imports as unbound (N1). No default
// moved, so the no-file input may not differ either. A position limit the reader
// took that is finite and outside 0-10 has no approved rule: the owner cannot
// write it and defers the import (kUnrepresentable), and the session runs on
// what the import read.
//
// Each input migrates three times: over a Defaults.ini the owner creates with
// the built-in values, from a read-only HeadTracking.ini, and over a
// Defaults.ini that differs from the built-in value on every global row the
// table binds. All three give the settings the import read, since the migration
// writes `default` only where the imported value is what `default` gives at that
// launch. After every load HeadTracking.ini keeps its bytes, write time and
// attributes, and the folder holds it and CameraUnlock.ini and nothing else
// (HeadTracking.ini alone after a deferred import). The distinct migrated files
// are written beside the executable under migrated\, for lint-migrated.mjs to
// run core's canonical config lint over.
//
// Inputs: every distinct HeadTracking.ini a published build shipped (installer
// ZIP, Nexus ZIP and, from v0.3.2, the launcher seed, which were the same bytes
// in every release), no file, an empty file, core's mutation corpus over the
// file v0.6.1 and v0.6.2 shipped, and that file with ToggleYawMode set to every
// code from 0x01 to 0xFE. No published build wrote the file, so there is no
// first-run output: a player's file is one of the shipped ones, edited or not.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle/oracle_reader.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
namespace config = Subnautica2HeadTracking::config;
namespace legacy = Subnautica2HeadTracking::legacy;
namespace testing = cameraunlock::config::testing;
using Subnautica2HeadTracking::Config;

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
}

std::string ReadFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFileBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

// ---- Scratch folders ---------------------------------------------------------
//
// One folder per reading: GetPrivateProfileString, which every reader here sits
// on, is free to cache the file it last read. `game` stands for the folder the
// DLL loads from; Defaults.ini sits in `global` beside it. Every folder lives
// under one root for the run, removed once at the end: removing thousands of
// folders one by one is most of the run time.

// The read-only inputs lose the attribute first, so remove_all can delete them.
void RemoveTree(const fs::path& root) {
    if (!fs::exists(root)) return;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    fs::remove_all(root);
}

const fs::path& ScratchRoot() {
    static const fs::path root = [] {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        fs::path r = fs::path(temp) / ("sn2ht_diff_" + std::to_string(GetCurrentProcessId()));
        RemoveTree(r);
        return r;
    }();
    return root;
}

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        root_ = ScratchRoot() / std::to_string(s_next++);
        fs::create_directories(root_ / "game");
    }

    fs::path game() const { return root_ / "game"; }
    fs::path legacy() const { return game() / "HeadTracking.ini"; }
    fs::path canonical() const { return game() / "CameraUnlock.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }
    // The folder as DllDirNarrow gave it, with its trailing backslash.
    std::string dll_dir() const { return game().string() + "\\"; }

    void WriteLegacy(const std::string& bytes) const { WriteFileBytes(legacy(), bytes); }

    void WriteDefaults(const std::string& bytes) const {
        fs::create_directories(defaults().parent_path());
        WriteFileBytes(defaults(), bytes);
    }

    std::set<std::string> Names() const {
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(game())) names.insert(entry.path().filename().string());
        return names;
    }

    cfg::ConfigOwnerOptions<Config> Options() const {
        return config::OwnerOptions(game(), cfg::DefaultsFile::At(defaults().wstring()));
    }

private:
    fs::path root_;
};

// ---- What a reading does -------------------------------------------------------
//
// A Record names everything the running mod acts on after reading the file:
// `field.*` the settings, `start.*` the state the session starts in, `hotkey.*`
// the bindings that can fire, each as `modifiers:code` (Ctrl 1, Shift 2, as
// cameraunlock::input::KeyModifiers numbers them) in ascending order. Floats are
// their bits.

using Record = std::map<std::string, std::string>;

std::string Bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(bits));
    return text;
}

std::string Flag(bool value) { return value ? "1" : "0"; }

const char* const kActionNames[] = {"Toggle", "CycleTrackingMode", "YawMode"};

// The bindings a set of HotkeyPoller registrations can fire. The poller skips
// code 0, and GetAsyncKeyState reports no code above 0xFF or below 0 down (core's
// probe-n1); 0xFF it can.
//
// v0.6.2 registered End, Page Up and the yaw key with no guard, so they fired
// with Ctrl and Shift held too; core's RegisterKeyBindings does not fire a
// binding without modifiers while both are held. That is the fleet's rule, the
// same for every list, so a record names a binding by its key and modifiers
// alone, and the changelog says what changed.
void AddHotkeys(Record& r, const std::vector<sn2_oracle::Registration>& registrations) {
    std::map<int, std::vector<std::pair<unsigned, int>>> byAction;
    for (int action = 0; action < 3; ++action) byAction[action];
    for (const auto& [action, vk, modifiers] : registrations) {
        if (vk < 0x01 || vk > 0xFF) continue;
        byAction[action].push_back({modifiers, vk});
    }
    for (auto& [action, items] : byAction) {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        std::string text;
        for (const auto& [modifiers, vk] : items) {
            char item[32];
            std::snprintf(item, sizeof(item), "%s%u:0x%02X", text.empty() ? "" : " ", modifiers, static_cast<unsigned>(vk));
            text += item;
        }
        r[std::string("hotkey.") + kActionNames[action]] = text;
    }
}

const char* ModeName(bool rotation, bool position) {
    if (rotation && position) return "RotationAndPosition";
    if (rotation) return "RotationOnly";
    if (position) return "PositionOnly";
    return "none";
}

void AddPosition(Record& r, const cameraunlock::PositionSettings& p) {
    r["field.pos.sensitivity_x"] = Bits(p.sensitivity_x);
    r["field.pos.sensitivity_y"] = Bits(p.sensitivity_y);
    r["field.pos.sensitivity_z"] = Bits(p.sensitivity_z);
    r["field.pos.invert_x"] = Flag(p.invert_x);
    r["field.pos.invert_y"] = Flag(p.invert_y);
    r["field.pos.invert_z"] = Flag(p.invert_z);
    r["field.pos.limit_x"] = Bits(p.limit_x);
    r["field.pos.limit_y"] = Bits(p.limit_y);
    r["field.pos.limit_y_down"] = Bits(p.limit_y_down);
    r["field.pos.limit_z"] = Bits(p.limit_z);
    r["field.pos.limit_z_back"] = Bits(p.limit_z_back);
    r["field.pos.local_smoothing"] = Bits(p.local_smoothing);
    r["field.pos.remote_smoothing"] = Bits(p.remote_smoothing);
}

Record ObserveOracle(const sn2_oracle::Published& g) {
    Record r;
    r["field.udp_port"] = std::to_string(g.udp_port);
    r["field.rot.yaw_sensitivity"] = Bits(g.yaw_sens);
    r["field.rot.pitch_sensitivity"] = Bits(g.pitch_sens);
    r["field.rot.roll_sensitivity"] = Bits(g.roll_sens);
    r["field.rot.invert_yaw"] = Flag(g.invert_yaw);
    r["field.rot.invert_pitch"] = Flag(g.invert_pitch);
    r["field.rot.invert_roll"] = Flag(g.invert_roll);
    r["field.local_smoothing"] = Bits(g.local_smoothing);
    r["field.remote_smoothing"] = Bits(g.remote_smoothing);
    r["field.tooltip_follow_scale"] = Bits(g.tooltip_follow_scale);
    r["field.disable_mask_comp"] = Flag(g.disable_mask_comp);
    AddPosition(r, g.position);
    r["start.enabled"] = Flag(g.tracking_enabled);
    r["start.mode"] = ModeName(g.rotation_enabled, g.position_enabled);
    r["start.world_space_yaw"] = Flag(g.world_space_yaw);
    r["start.reticle_follows_aim"] = Flag(g.reticle_move_on);
    r["start.tooltip_follows_reticle"] = Flag(g.tooltip_move_on);
    AddHotkeys(r, g.hotkeys);
    return r;
}

// The frozen reader's settings through the startup code of 40c2d8f, the commit
// that froze it (BootstrapThread in src/Subnautica2HeadTracking/
// headtracking_mod.cpp): the globals take the settings, the processor takes the
// position ones with LimitY on both vertical bounds and the smoothing pair,
// rotation starts on, and the hotkeys are v0.6.2's.
Record ObserveImport(const legacy::Config& c) {
    Record r;
    r["field.udp_port"] = std::to_string(c.udp_port);
    r["field.rot.yaw_sensitivity"] = Bits(c.yaw_sensitivity);
    r["field.rot.pitch_sensitivity"] = Bits(c.pitch_sensitivity);
    r["field.rot.roll_sensitivity"] = Bits(c.roll_sensitivity);
    r["field.rot.invert_yaw"] = Flag(c.invert_yaw);
    r["field.rot.invert_pitch"] = Flag(c.invert_pitch);
    r["field.rot.invert_roll"] = Flag(c.invert_roll);
    r["field.local_smoothing"] = Bits(c.local_smoothing);
    r["field.remote_smoothing"] = Bits(c.remote_smoothing);
    r["field.tooltip_follow_scale"] = Bits(c.tooltip_follow_scale);
    r["field.disable_mask_comp"] = Flag(c.disable_mask_comp);
    cameraunlock::PositionSettings p;
    p.sensitivity_x = c.position_sensitivity_x;
    p.sensitivity_y = c.position_sensitivity_y;
    p.sensitivity_z = c.position_sensitivity_z;
    p.invert_x = c.position_invert_x;
    p.invert_y = c.position_invert_y;
    p.invert_z = c.position_invert_z;
    p.limit_x = c.limit_x;
    p.limit_y = c.limit_y;
    p.limit_y_down = c.limit_y;
    p.limit_z = c.limit_z;
    p.limit_z_back = c.limit_z_back;
    p.local_smoothing = c.local_smoothing;
    p.remote_smoothing = c.remote_smoothing;
    AddPosition(r, p);
    r["start.enabled"] = Flag(c.enable_on_startup);
    r["start.mode"] = ModeName(true, c.position_enabled);
    r["start.world_space_yaw"] = Flag(c.world_space_yaw);
    r["start.reticle_follows_aim"] = Flag(c.show_reticle);
    r["start.tooltip_follows_reticle"] = Flag(c.tooltip_follow_reticle);
    AddHotkeys(r, {{sn2_oracle::kToggle, VK_END, 0},
                   {sn2_oracle::kToggle, 0x59, 3},
                   {sn2_oracle::kCycleMode, VK_PRIOR, 0},
                   {sn2_oracle::kCycleMode, 0x47, 3},
                   {sn2_oracle::kYawMode, c.yaw_mode_key, 0},
                   {sn2_oracle::kYawMode, 0x48, 3}});
    return r;
}

// The settings a session runs on through this build's startup code
// (BootstrapThread): no rotation or position sensitivity or inversion, the
// processor's settings from core's defaults with the limits and the smoothing
// pair from the file, the mode from the pair, the reticle always following the
// aim, and each hotkey list through ParseKeyBindings and RegisterKeyBindings.
Record ObserveCanonical(const Config& c) {
    Record r;
    r["field.udp_port"] = std::to_string(c.udp_port);
    r["field.rot.yaw_sensitivity"] = Bits(1.0f);
    r["field.rot.pitch_sensitivity"] = Bits(1.0f);
    r["field.rot.roll_sensitivity"] = Bits(1.0f);
    r["field.rot.invert_yaw"] = Flag(false);
    r["field.rot.invert_pitch"] = Flag(false);
    r["field.rot.invert_roll"] = Flag(false);
    r["field.local_smoothing"] = Bits(c.local_smoothing);
    r["field.remote_smoothing"] = Bits(c.remote_smoothing);
    r["field.tooltip_follow_scale"] = Bits(c.tooltip_follow_scale);
    r["field.disable_mask_comp"] = Flag(c.disable_mask_comp);
    cameraunlock::PositionSettings p;
    p.limit_x = c.position_limit_x;
    p.limit_y = c.position_limit_y;
    p.limit_y_down = c.position_limit_y_down;
    p.limit_z = c.position_limit_z;
    p.limit_z_back = c.position_limit_z_back;
    p.local_smoothing = c.local_smoothing;
    p.remote_smoothing = c.remote_smoothing;
    AddPosition(r, p);
    const cameraunlock::TrackingModeChannels channels =
        cameraunlock::EncodeTrackingMode(config::StartupTrackingMode(c));
    r["start.enabled"] = Flag(c.enable_on_startup);
    r["start.mode"] = ModeName(channels.rotation_enabled, channels.position_enabled);
    r["start.world_space_yaw"] = Flag(c.world_space_yaw);
    r["start.reticle_follows_aim"] = Flag(true);
    r["start.tooltip_follows_reticle"] = Flag(c.tooltip_follow_reticle);
    std::vector<sn2_oracle::Registration> registrations;
    const std::pair<int, const std::string*> lists[] = {{sn2_oracle::kToggle, &c.toggle_key},
                                                        {sn2_oracle::kCycleMode, &c.cycle_tracking_mode_key},
                                                        {sn2_oracle::kYawMode, &c.yaw_mode_key}};
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        Check(parsed.ok(), "the hotkey list '" + *list + "' parses");
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            registrations.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    AddHotkeys(r, registrations);
    return r;
}

std::vector<std::string> Differences(const Record& a, const Record& b) {
    std::vector<std::string> out;
    for (const auto& [name, value] : a) {
        const auto it = b.find(name);
        if (it == b.end()) {
            out.push_back(name + " only on the left");
        } else if (it->second != value) {
            out.push_back(name + ": " + value + " / " + it->second);
        }
    }
    for (const auto& [name, value] : b) {
        if (a.find(name) == a.end()) out.push_back(name + " only on the right");
    }
    return out;
}

// ---- The approved differences ----------------------------------------------------
//
// What a session runs on once the import has dropped a value: the record the
// import gave, with each dropped value's effect applied. A rule this map never
// applies (FollowsDefault) fails.

const std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, std::string>>>& DropEffects() {
    static const std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, std::string>>> effects = {
        {{"Tracking", "YawSensitivity"}, {{"field.rot.yaw_sensitivity", Bits(1.0f)}}},
        {{"Tracking", "PitchSensitivity"}, {{"field.rot.pitch_sensitivity", Bits(1.0f)}}},
        {{"Tracking", "RollSensitivity"}, {{"field.rot.roll_sensitivity", Bits(1.0f)}}},
        {{"Tracking", "InvertYaw"}, {{"field.rot.invert_yaw", "0"}}},
        {{"Tracking", "InvertPitch"}, {{"field.rot.invert_pitch", "0"}}},
        {{"Tracking", "InvertRoll"}, {{"field.rot.invert_roll", "0"}}},
        {{"Position", "SensitivityX"}, {{"field.pos.sensitivity_x", Bits(1.0f)}}},
        {{"Position", "SensitivityY"}, {{"field.pos.sensitivity_y", Bits(1.0f)}}},
        {{"Position", "SensitivityZ"}, {{"field.pos.sensitivity_z", Bits(1.0f)}}},
        {{"Position", "InvertX"}, {{"field.pos.invert_x", "0"}}},
        {{"Position", "InvertY"}, {{"field.pos.invert_y", "0"}}},
        {{"Position", "InvertZ"}, {{"field.pos.invert_z", "0"}}},
        {{"Tracking", "ShowReticle"}, {{"start.reticle_follows_aim", "1"}}},
        {{"Position", "LimitX"}, {{"field.pos.limit_x", Bits(0.30f)}}},
        {{"Position", "LimitY"}, {{"field.pos.limit_y", Bits(0.20f)}, {"field.pos.limit_y_down", Bits(0.20f)}}},
        {{"Position", "LimitZ"}, {{"field.pos.limit_z", Bits(0.40f)}}},
        {{"Position", "LimitZBack"}, {{"field.pos.limit_z_back", Bits(0.10f)}}},
        {{"Tooltip", "FollowScale"}, {{"field.tooltip_follow_scale", Bits(1.0f)}}},
        {{"Hotkeys", "ToggleYawMode"}, {{"hotkey.YawMode", "3:0x48"}}},
    };
    return effects;
}

cfg::DropRule RuleFor(const std::string& section, const std::string& key) {
    if (section == "Tracking" && key == "ShowReticle") return cfg::DropRule::Reticle;
    if (section == "Hotkeys") return cfg::DropRule::KeyCodeOutOfRange;
    if (section == "Tooltip" || key.rfind("Limit", 0) == 0) return cfg::DropRule::NonFiniteNumber;
    return cfg::DropRule::PoseShaping;
}

Record Expected(Record imported, const cfg::ImportResult& result, const std::string& name) {
    for (const cfg::DroppedValue& d : result.dropped) {
        const auto it = DropEffects().find({d.section, d.key});
        Check(it != DropEffects().end(), name + ": the import drops [" + d.section + "] " + d.key + ", which no rule here covers");
        if (it == DropEffects().end()) continue;
        Check(d.rule == RuleFor(d.section, d.key),
              name + ": [" + d.section + "] " + d.key + " is dropped by the rule that covers it");
        for (const auto& [field, value] : it->second) imported[field] = value;
    }
    for (const cfg::PoseShapingValue& v : result.pose_shaping) {
        const bool dropped = std::any_of(result.dropped.begin(), result.dropped.end(), [&](const cfg::DroppedValue& d) {
            return d.rule == cfg::DropRule::PoseShaping && d.section == v.section && d.key == v.key && d.value == v.value;
        });
        Check(v.folded != dropped, name + ": [" + v.section + "] " + v.key + "=" + v.value +
                                       " is dropped exactly when it is not what v0.6.2 shipped");
    }
    return imported;
}

// A position limit the frozen reader took that is a finite number outside 0-10,
// which the canonical rows cannot hold. No approved rule covers it, so the owner
// defers such a file: it stays as it is, nothing is saved, the session runs on
// what the import read, and the import is tried again at every launch until core
// widens the range or the owner rules on it.
const char* const kUnrepresentable =
    "a finite position limit below 0 or above 10, which the canonical rows cannot hold, so the import defers";

bool Unrepresentable(const legacy::Config& c) {
    for (const float limit : {c.limit_x, c.limit_y, c.limit_z, c.limit_z_back}) {
        if (std::isfinite(limit) && (limit < 0.0f || limit > 10.0f)) return true;
    }
    return false;
}

// ---- Inputs --------------------------------------------------------------------

fs::path DataPath(const char* name) {
    return fs::path(SN2HT_SOURCE_DIR) / "tests" / "config_differential" / "data" / name;
}

// The newest published build's shipped file, which v0.6.1 shipped first.
std::string NewestShipped() { return ReadFileBytes(DataPath("shipped-v0.6.1.ini")); }

const char* const kNewestShippedName = "v0.6.1 and v0.6.2 shipped file and seed";

// Every key the frozen reader reads, and how the corpus varies each one. The
// reader refuses a port outside 1024-65535 and clamps a smoothing value into
// 0-1; it checks nothing else.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "Port", "5771", {"80", "70000"}},
        {"Tracking", "EnableOnStartup", "false", {}},
        {"Tracking", "YawSensitivity", "0.5", {}},
        {"Tracking", "PitchSensitivity", "0.5", {}},
        {"Tracking", "RollSensitivity", "0.5", {}},
        {"Tracking", "InvertYaw", "true", {}},
        {"Tracking", "InvertPitch", "true", {}},
        {"Tracking", "InvertRoll", "true", {}},
        {"Tracking", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Tracking", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"Tracking", "ShowReticle", "false", {}},
        {"Tracking", "WorldSpaceYaw", "false", {}},
        {"Tooltip", "FollowReticle", "false", {}},
        {"Tooltip", "FollowScale", "1.25", {}},
        {"Position", "Enabled", "false", {}},
        {"Position", "SensitivityX", "0.5", {}},
        {"Position", "SensitivityY", "0.5", {}},
        {"Position", "SensitivityZ", "0.5", {}},
        {"Position", "InvertX", "true", {}},
        {"Position", "InvertY", "true", {}},
        {"Position", "InvertZ", "true", {}},
        {"Position", "LimitX", "0.5", {}},
        {"Position", "LimitY", "0.35", {}},
        {"Position", "LimitZ", "0.6", {}},
        {"Position", "LimitZBack", "0.15", {}},
        {"Hotkeys", "ToggleYawMode", "0x2E", {}, true},
        {"Debug", "DisableMaskComp", "true", {}},
    };
}

// The generator refuses the call when these and the descriptors name different
// keys, so the corpus covers every key the import reads.
std::vector<cfg::LegacyKey> CorpusReads() { return config::Import().keys; }

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

std::string WithYawKey(const std::string& base, int code) {
    const std::string from = "ToggleYawMode = 0x22";
    const std::size_t at = base.find(from);
    if (at == std::string::npos) throw std::logic_error("no ToggleYawMode = 0x22 in the shipped file");
    char to[32];
    std::snprintf(to, sizeof(to), "ToggleYawMode = 0x%02X", static_cast<unsigned>(code));
    std::string out = base;
    return out.replace(at, from.size(), to);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {"v0.1.0 shipped file", true, ReadFileBytes(DataPath("shipped-v0.1.0.ini"))},
        {"v0.2.0 to v0.3.0 shipped file", true, ReadFileBytes(DataPath("shipped-v0.2.0.ini"))},
        {"v0.3.1 to v0.5.0 shipped file and seed", true, ReadFileBytes(DataPath("shipped-v0.3.1.ini"))},
        {"v0.6.0 shipped file and seed", true, ReadFileBytes(DataPath("shipped-v0.6.0.ini"))},
        {kNewestShippedName, true, NewestShipped()},
        {"no file", false, {}},
        {"empty file", true, {}},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(NewestShipped(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    for (int code = 0x01; code <= 0xFE; ++code) {
        char name[48];
        std::snprintf(name, sizeof(name), "ToggleYawMode = 0x%02X", static_cast<unsigned>(code));
        inputs.push_back({name, true, WithYawKey(NewestShipped(), code)});
    }
    return inputs;
}

// ---- Checks on a load ------------------------------------------------------------

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    unsigned long long written = 0;
    DWORD attributes = 0;
    bool operator==(const FileState& other) const {
        return bytes == other.bytes && written == other.written && attributes == other.attributes;
    }
};

std::optional<FileState> StateOf(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + path.string());
    }
    FileState state;
    state.bytes = ReadFileBytes(path);
    state.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

bool AsciiCrlf(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c > 0x7E) return false;
        if (c == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
        if (c == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (c < 0x20 && c != '\r' && c != '\n') return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    return std::any_of(log.begin(), log.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
}

// What the reader and the table find in a canonical file, read over the table's
// own defaults, which stand for a Defaults.ini holding the built-in values.
std::vector<std::string> CanonicalDiagnostics(const std::string& bytes, Config& out) {
    std::vector<std::string> found;
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(bytes);
    for (const cfg::CanonicalDiagnostic& d : doc.diagnostics) found.push_back("reader: " + cfg::DescribeCanonicalDiagnostic(d));
    const cfg::ConfigTable<Config> table = config::Table();
    out = table.defaults();
    for (const cfg::CanonicalDiagnostic& d : cfg::ApplyCanonical(doc, table, out).diagnostics) {
        found.push_back("table: " + cfg::DescribeCanonicalDiagnostic(d));
    }
    return found;
}

// A Defaults.ini holding a value other than the built-in one on every global row
// the table binds, so a migration that wrote `default` where the imported value
// is not what `default` gives would read back differently over it.
const char* const kSkewedDefaults =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.5\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nPositionLimitX=0.5\r\nPositionLimitY=0.45\r\n"
    "PositionLimitYDown=0.35\r\nPositionLimitZ=0.6\r\nPositionLimitZBack=0.25\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n";

// The folder beside this executable the migrated files are written to, for
// lint-migrated.mjs, which CTest runs after this test.
fs::path MigratedFolder() {
    std::vector<wchar_t> exe(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length == 0) throw std::runtime_error("cannot find this executable's path");
        if (length < exe.size()) return fs::path(std::wstring(exe.data(), length)).parent_path() / "migrated";
        exe.resize(exe.size() * 2);
    }
}

struct Tally {
    int created = 0;
    int migrated = 0;
    int deferred = 0;
    std::set<std::string> files;
};

// Runs the owner's Load in `s`, whose game folder holds the input as
// HeadTracking.ini or nothing, checks what a load must do beyond comparison 2,
// and returns the settings the session runs on.
Config Migrate(const Input& input, bool unrepresentable, const Scratch& s, const std::string& label, Tally& tally) {
    const std::optional<FileState> legacy_before = StateOf(s.legacy());
    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(StateOf(s.legacy()) == legacy_before, label + ": a load leaves HeadTracking.ini's bytes, write time and attributes");

    if (unrepresentable) {
        ++tally.deferred;
        Check(loaded.status == cfg::ConfigLoadStatus::Deferred,
              label + ": " + kUnrepresentable + ", not " + cfg::ConfigLoadStatusName(loaded.status));
        Check(s.Names() == std::set<std::string>{"HeadTracking.ini"}, label + ": a deferred import creates no file");
        Check(loaded.reason.find("cannot be converted") != std::string::npos,
              label + ": the player is told which value could not be converted");
        return loaded.config;
    }

    const cfg::ConfigLoadStatus want = input.present ? cfg::ConfigLoadStatus::Migrated : cfg::ConfigLoadStatus::Created;
    if (loaded.status != want) std::printf("  %s: %s, %s\n", label.c_str(), cfg::ConfigLoadStatusName(loaded.status), loaded.reason.c_str());
    Check(loaded.status == want, label + ": every legacy input imports, and no file gives a created one");
    if (loaded.status != want) return loaded.config;
    ++(input.present ? tally.migrated : tally.created);
    Check(s.Names() == (input.present ? std::set<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}
                                      : std::set<std::string>{"CameraUnlock.ini"}),
          label + ": the game folder holds HeadTracking.ini and CameraUnlock.ini and nothing else");

    const std::string migrated = ReadFileBytes(s.canonical());
    Check(cfg::HasCanonicalStamp(migrated), label + ": CameraUnlock.ini carries the stamp");
    Check(AsciiCrlf(migrated), label + ": CameraUnlock.ini is ASCII with CRLF line ends");
    Config reread;
    const std::vector<std::string> diagnostics = CanonicalDiagnostics(migrated, reread);
    for (const std::string& d : diagnostics) std::printf("  %s: CameraUnlock.ini, %s\n", label.c_str(), d.c_str());
    Check(diagnostics.empty(), label + ": CameraUnlock.ini reads with no diagnostic");
    if (input.present) tally.files.insert(migrated);

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(s.canonical());
    const cfg::ConfigLoadResult<Config> again = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical, label + ": the next start reads CameraUnlock.ini");
    Check(Differences(ObserveCanonical(again.config), ObserveCanonical(loaded.config)).empty(),
          label + ": the next start runs on the same settings");
    Check(StateOf(s.canonical()) == created && StateOf(s.legacy()) == legacy_before,
          label + ": the next start changes neither file");
    Check(!input.present || LogSays(again.log, "is left as it was and is not read"),
          label + ": the next start logs that HeadTracking.ini is not read");
    return loaded.config;
}

// ---- Comparisons -----------------------------------------------------------------

void Compare(const std::vector<Input>& inputs) {
    const std::string committed = ReadFileBytes(fs::path(SN2HT_SOURCE_DIR) / "HeadTracking.ini");
    const cfg::ConfigTable<Config> table = config::Table();
    Tally builtin, readonly, skewed;
    int compared = 0;
    for (const Input& input : inputs) {
        const std::string& name = input.name;

        // Comparison 1. Both read one copy, which neither writes.
        legacy::Config read;
        Record imported;
        {
            Scratch s;
            if (input.present) s.WriteLegacy(input.bytes);
            const Record oracle = ObserveOracle(sn2_oracle::Read(s.dll_dir()));
            const bool present = legacy::Load(s.legacy().string(), read);
            Check(present == input.present, name + ": the frozen reader finds the file exactly when it is there");
            imported = ObserveImport(read);
            const std::vector<std::string> diff = Differences(oracle, imported);
            for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": comparison 1, the oracle and the import agree");
        }
        const bool unrepresentable = Unrepresentable(read);

        // Comparison 2 over a Defaults.ini the owner creates with the built-in
        // values. The import's own result says what it dropped.
        cfg::ImportResult result;
        {
            Scratch s;
            if (input.present) s.WriteLegacy(input.bytes);
            Config mapped = table.defaults();
            result = config::Import().run({s.legacy().wstring(), s.legacy().string(), false}, mapped);
            Check(result.status == (input.present ? cfg::ImportStatus::Imported : cfg::ImportStatus::Absent),
                  name + ": the import reads every input, as the published build did");
            const Record want = Expected(imported, result, name);
            const Config migrated = Migrate(input, unrepresentable, s, name, builtin);
            const std::vector<std::string> diff = Differences(want, ObserveCanonical(migrated));
            for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": comparison 2, the session runs as the import read, apart from the approved drops");

            if (!unrepresentable && fs::exists(s.canonical())) {
                // Over the built-in values the table's own defaults stand for Defaults.ini.
                Config reread;
                CanonicalDiagnostics(ReadFileBytes(s.canonical()), reread);
                Check(Differences(ObserveCanonical(reread), ObserveCanonical(migrated)).empty(),
                      name + ": CameraUnlock.ini reads back as the settings the session runs on");
                // Fresh equals upgrade: the file the newest build shipped, and no
                // file at all, both end as the committed file.
                if (name == kNewestShippedName || name == "no file") {
                    Check(ReadFileBytes(s.canonical()) == committed,
                          name + ": gives the committed file, byte for byte");
                }
            }
        }
        const Record want = Expected(imported, result, name);

        if (input.present) {
            // From a read-only HeadTracking.ini, which keeps its attribute. The
            // import run on its own first leaves the folder as it was.
            Scratch s;
            s.WriteLegacy(input.bytes);
            SetFileAttributesW(s.legacy().c_str(), FILE_ATTRIBUTE_READONLY);
            const std::set<std::string> before = s.Names();
            Config unused = table.defaults();
            config::Import().run({s.legacy().wstring(), s.legacy().string(), false}, unused);
            Check(s.Names() == before && ReadFileBytes(s.legacy()) == input.bytes,
                  name + ": the import leaves a read-only folder as it was");
            const Config c = Migrate(input, unrepresentable, s, name + " (read-only)", readonly);
            Check(Differences(want, ObserveCanonical(c)).empty(),
                  name + ": a read-only HeadTracking.ini imports as a writable one does");
            Check((GetFileAttributesW(s.legacy().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
                  name + ": HeadTracking.ini keeps its read-only attribute");
        }

        if (input.present) {
            // Over a Defaults.ini that differs everywhere. With no legacy file
            // the settings are Defaults.ini's own, so only an input with a file
            // is held to the import here.
            Scratch s;
            s.WriteLegacy(input.bytes);
            s.WriteDefaults(kSkewedDefaults);
            const Config c = Migrate(input, unrepresentable, s, name + " (skewed Defaults.ini)", skewed);
            const std::vector<std::string> diff = Differences(want, ObserveCanonical(c));
            for (const std::string& d : diff) std::printf("  comparison 2, %s (skewed Defaults.ini): %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": the migration gives the import's settings over a Defaults.ini that differs everywhere");
        }
        ++compared;
    }
    std::printf("comparisons 1 and 2: %d inputs\n", compared);
    std::printf("over built-in Defaults.ini: %d created, %d migrated, %d deferred (%s)\n", builtin.created,
                builtin.migrated, builtin.deferred, kUnrepresentable);
    std::printf("read-only: %d migrated, %d deferred; skewed Defaults.ini: %d migrated, %d deferred\n",
                readonly.migrated, readonly.deferred, skewed.migrated, skewed.deferred);
    Check(builtin.deferred > 0, "the corpus reaches a limit the canonical rows cannot hold");

    // Core's canonical config lint runs over these next (lint-migrated.mjs).
    std::set<std::string> files = builtin.files;
    files.insert(readonly.files.begin(), readonly.files.end());
    files.insert(skewed.files.begin(), skewed.files.end());
    const fs::path lint = MigratedFolder();
    fs::remove_all(lint);
    fs::create_directories(lint);
    std::size_t n = 0;
    for (const std::string& file : files) WriteFileBytes(lint / (std::to_string(n++) + ".ini"), file);
    std::printf("%zu distinct migrated files written to %s\n", files.size(), lint.string().c_str());
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Compare(Inputs());
    RemoveTree(ScratchRoot());
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
