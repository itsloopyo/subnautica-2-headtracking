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
// a limit that is finite and outside the rows' 0-10 imports as the nearest end
// of it (N4); a ToggleYawMode code outside 0x01-0xFE imports as unbound (N1),
// and so does one naming a Ctrl, Shift or Alt key on its own (N3). No default
// moved, so the no-file input may not differ either.
//
// A row the player never changed from what v0.6.2 shipped follows Defaults.ini:
// the import lists it in follows_defaults_ini and the migration writes it
// `default`, the tracking mode pair as one unit. The test derives that list
// from what the import read, a row whose every observed value is v0.6.2's
// default or, for a limit, not a finite number (N2), and holds the import's
// list to it on every input. A limit N4 clamped was set by the player, so its
// row is not on the list. Every shipped file, the empty file and no file list
// every row and migrate to the committed file byte for byte.
//
// The Z limits go by the lean they bounded, not by their keys. The legacy
// processor applied InvertZ before its [-LimitZ, +LimitZBack] clamp, so with
// InvertZ true LimitZBack bounded the forward lean. This build inverts nothing.
// The test finds which key bounded each lean by running a lean past both limits
// through v0.6.0's depth path and v0.6.2's (LegacyLeanKeys), and expects each
// limit on the row of its lean, so v0.1.0 to v0.6.0's InvertZ true, LimitZ 0.10
// and LimitZBack 0.40 is forward 0.40 and backward 0.10, both what v0.6.2
// shipped. Files with InvertZ true and edited Z limits also carry the limits the
// migration must give, written out by hand.
//
// Each input migrates three times: over a Defaults.ini the owner creates with
// the built-in values, from a read-only HeadTracking.ini, and over a
// Defaults.ini that differs from the built-in value on every global row the
// table binds. Over the first two the session runs as the import read, since
// v0.6.2's defaults are the built-in values. Over the third a row the player never
// changed is `default` and takes Defaults.ini's value, and a changed row keeps
// the player's. After every load HeadTracking.ini keeps its bytes, write time and
// attributes, and the folder holds it and CameraUnlock.ini and nothing else.
// The distinct migrated files
// are written beside the executable under migrated\, for lint-migrated.mjs to
// run core's canonical config lint over.
//
// Inputs: every distinct HeadTracking.ini a published build shipped (installer
// ZIP, Nexus ZIP and, from v0.3.2, the launcher seed, which were the same bytes
// in every release), no file, an empty file, core's mutation corpus over the
// file v0.6.1 and v0.6.2 shipped, that file with ToggleYawMode set to every
// code from 0x01 to 0xFE, and the Z limit cases above. No published build wrote
// the file, so there is no first-run output: a player's file is one of the
// shipped ones, edited or not.

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
#include "cameraunlock/processing/position_processor.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
namespace config = Subnautica2HeadTracking::config;
namespace legacy = Subnautica2HeadTracking::legacy;
namespace testing = cameraunlock::config::testing;
using Subnautica2HeadTracking::Config;
using cfg::schema::Concept;

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

float FromBits(const std::string& text) {
    const std::uint32_t bits = static_cast<std::uint32_t>(std::stoul(text, nullptr, 16));
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
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

// ---- Rows that follow Defaults.ini ---------------------------------------------------
//
// The row a record entry observes, or none for a setting the canonical format
// keeps in a local row or has no row for. start.mode stands for the tracking
// mode pair.

std::optional<Concept> RowOf(const std::string& entry) {
    static const std::map<std::string, Concept> rows = {
        {"field.udp_port", Concept::UdpPort},
        {"start.enabled", Concept::EnableOnStartup},
        {"start.world_space_yaw", Concept::WorldSpaceYaw},
        {"start.mode", Concept::RotationEnabled},
        {"field.local_smoothing", Concept::LocalSmoothing},
        {"field.pos.local_smoothing", Concept::LocalSmoothing},
        {"field.remote_smoothing", Concept::RemoteSmoothing},
        {"field.pos.remote_smoothing", Concept::RemoteSmoothing},
        {"field.pos.limit_x", Concept::PositionLimitX},
        {"field.pos.limit_y", Concept::PositionLimitY},
        {"field.pos.limit_y_down", Concept::PositionLimitYDown},
        {"field.pos.limit_z", Concept::PositionLimitZ},
        {"field.pos.limit_z_back", Concept::PositionLimitZBack},
        {"hotkey.Toggle", Concept::ToggleKey},
        {"hotkey.CycleTrackingMode", Concept::CycleTrackingModeKey},
        {"hotkey.YawMode", Concept::YawModeKey},
    };
    static const std::set<std::string> unbound = {
        "field.tooltip_follow_scale", "field.disable_mask_comp", "start.reticle_follows_aim",
        "start.tooltip_follows_reticle",
    };
    const auto it = rows.find(entry);
    if (it != rows.end()) return it->second;
    if (unbound.count(entry) || entry.rfind("field.rot.", 0) == 0 || entry.rfind("field.pos.sensitivity_", 0) == 0 ||
        entry.rfind("field.pos.invert_", 0) == 0) {
        return std::nullopt;
    }
    throw std::logic_error("no row observes " + entry);
}

// Every global row the table binds, each of which follows Defaults.ini.
const std::set<Concept>& AllRows() {
    static const std::set<Concept> all = {
        Concept::UdpPort,         Concept::EnableOnStartup,    Concept::WorldSpaceYaw,   Concept::RotationEnabled,
        Concept::PositionEnabled, Concept::LocalSmoothing,     Concept::RemoteSmoothing, Concept::PositionLimitX,
        Concept::PositionLimitY,  Concept::PositionLimitYDown, Concept::PositionLimitZ,  Concept::PositionLimitZBack,
        Concept::ToggleKey,       Concept::CycleTrackingModeKey, Concept::YawModeKey,
    };
    return all;
}

// ---- Which legacy key bounded each lean -----------------------------------------
//
// The processor of every published build applied InvertZ before its
// [-LimitZ, +LimitZBack] clamp (core e4c1813, 3465659 and bd22895 alike). v0.3.1
// to v0.6.0 then took the camera's forward offset as +z (headtracking_mod.cpp,
// `const double s = static_cast<double>(off.z) * kMetersToUE`), and v0.6.1 and
// v0.6.2 as -z (position_boundary.h). v0.1.0 to v0.3.0 read none of these keys.
// A lean of ten metres either way, far past two limits set apart, shows which
// key bounded it. The two builds must agree on that key, and the camera must
// move with the lean in one of them: v0.6.0 with InvertZ true, v0.6.2 with it
// false.

enum class ZKey { LimitZ, LimitZBack };

struct LeanKeys {
    ZKey forward;
    ZKey backward;
};

LeanKeys FindLeanKeys(bool invert_z) {
    constexpr float kProbeZ = 1.0f;
    constexpr float kProbeZBack = 2.0f;
    cameraunlock::PositionSettings settings;
    settings.invert_z = invert_z;
    settings.limit_z = kProbeZ;
    settings.limit_z_back = kProbeZBack;
    const std::string what = std::string("with InvertZ ") + (invert_z ? "true" : "false");
    const auto key = [&](float raw_z, const std::string& lean) {
        cameraunlock::PositionProcessor processor;
        processor.SetSettings(settings);
        const float off_z = processor
                                .Process(cameraunlock::PositionData(0.0f, 0.0f, raw_z),
                                         cameraunlock::math::Quat4::Identity(), 1.0f)
                                .z;
        const double v060 = static_cast<double>(off_z) * 100.0;
        const double v062 = sn2_oracle::Surge(settings, raw_z);
        Check(std::fabs(v060) == std::fabs(v062),
              what + ", v0.6.0 and v0.6.2 bound the " + lean + " lean by the same key");
        const double with_lean = raw_z < 0.0f ? 1.0 : -1.0;
        Check(v060 * with_lean > 0.0 || v062 * with_lean > 0.0,
              what + ", the camera moves with the " + lean + " lean in v0.6.0 or v0.6.2");
        const double bound = std::fabs(v060) / 100.0;
        Check(bound == kProbeZ || bound == kProbeZBack, what + ", the " + lean + " lean stops at a limit");
        return bound == kProbeZ ? ZKey::LimitZ : ZKey::LimitZBack;
    };
    return {key(-10.0f, "forward"), key(10.0f, "backward")};
}

const LeanKeys& LegacyLeanKeys(bool invert_z) {
    static const LeanKeys plain = FindLeanKeys(false);
    static const LeanKeys inverted = FindLeanKeys(true);
    return invert_z ? inverted : plain;
}

// The import's record with each Z limit on the row of the lean it bounded.
Record ByLean(Record r) {
    const LeanKeys& keys = LegacyLeanKeys(r.at("field.pos.invert_z") == Flag(true));
    const std::string z = r.at("field.pos.limit_z");
    const std::string back = r.at("field.pos.limit_z_back");
    r["field.pos.limit_z"] = keys.forward == ZKey::LimitZ ? z : back;
    r["field.pos.limit_z_back"] = keys.backward == ZKey::LimitZ ? z : back;
    return r;
}

// The rows the player never changed: every entry of the row reads as it does
// with no file, v0.6.2's defaults, the Z limits by lean, or is a limit that is
// not a finite number (N2). The mode pair is both rows or neither.
std::set<Concept> UntouchedRows(const Record& imported, const Record& defaults) {
    std::set<Concept> changed;
    for (const auto& [entry, value] : ByLean(imported)) {
        const std::optional<Concept> row = RowOf(entry);
        const bool nonFiniteLimit = entry.rfind("field.pos.limit_", 0) == 0 && !std::isfinite(FromBits(value));
        if (row && defaults.at(entry) != value && !nonFiniteLimit) changed.insert(*row);
    }
    if (changed.count(Concept::RotationEnabled)) changed.insert(Concept::PositionEnabled);
    std::set<Concept> untouched;
    for (const Concept row : AllRows()) {
        if (!changed.count(row)) untouched.insert(row);
    }
    return untouched;
}

std::string Names(const std::set<Concept>& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<std::size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// What the session runs on over a Defaults.ini other than the built-in one: the
// import's record, with each row the import left to Defaults.ini as that file
// gives it.
Record OverDefaults(Record want, const std::set<Concept>& follows, const Record& defaults_ini) {
    for (auto& [entry, value] : want) {
        const std::optional<Concept> row = RowOf(entry);
        if (row && follows.count(*row)) value = defaults_ini.at(entry);
    }
    return want;
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
        {{"Tooltip", "FollowScale"}, {{"field.tooltip_follow_scale", Bits(1.0f)}}},
        {{"Hotkeys", "ToggleYawMode"}, {{"hotkey.YawMode", "3:0x48"}}},
    };
    return effects;
}

// A Ctrl, Shift or Alt key on its own imports as unbound (N3), any other code
// outside 0x01-0xFE too (N1).
cfg::DropRule RuleFor(const std::string& section, const std::string& key, const std::string& value) {
    static const std::set<std::string> modifiers = {"0x10", "0x11", "0x12", "0xA0", "0xA1", "0xA2", "0xA3", "0xA4", "0xA5"};
    if (section == "Tracking" && key == "ShowReticle") return cfg::DropRule::Reticle;
    if (section == "Hotkeys") return modifiers.count(value) ? cfg::DropRule::ModifierKey : cfg::DropRule::KeyCodeOutOfRange;
    if (section == "Tooltip") return cfg::DropRule::NonFiniteNumber;
    return cfg::DropRule::PoseShaping;
}

// The position limit rows' range, 0 to 10, which N4 clamps a finite limit to.
constexpr float kLimitMin = 0.0f;
constexpr float kLimitMax = 10.0f;

// v0.6.2's default of each limit field, which N2 gives a limit that is not finite.
const std::map<std::string, float>& LimitDefaults() {
    static const std::map<std::string, float> defaults = {
        {"field.pos.limit_x", 0.30f},      {"field.pos.limit_y", 0.20f},       {"field.pos.limit_y_down", 0.20f},
        {"field.pos.limit_z", 0.40f},      {"field.pos.limit_z_back", 0.10f},
    };
    return defaults;
}

Record Expected(const Record& read, const cfg::ImportResult& result, const std::string& name, int& clamped) {
    const LeanKeys& keys = LegacyLeanKeys(read.at("field.pos.invert_z") == Flag(true));
    Record imported = ByLean(read);
    for (const cfg::DroppedValue& d : result.dropped) {
        if (d.section == "Position" && d.key.rfind("Limit", 0) == 0) {
            // The fields the limit fills, a Z limit the row of the lean it bounded.
            std::vector<std::string> fields;
            if (d.key == "LimitX") {
                fields = {"field.pos.limit_x"};
            } else if (d.key == "LimitY") {
                fields = {"field.pos.limit_y", "field.pos.limit_y_down"};
            } else {
                const bool forward = keys.forward == (d.key == "LimitZ" ? ZKey::LimitZ : ZKey::LimitZBack);
                fields = {forward ? "field.pos.limit_z" : "field.pos.limit_z_back"};
            }
            const float value = FromBits(imported.at(fields.front()));
            if (!std::isfinite(value)) {
                // N2 gives the default of the row the limit moved to.
                Check(d.rule == cfg::DropRule::NonFiniteNumber, name + ": [Position] " + d.key + " is dropped by N2");
                for (const std::string& f : fields) imported[f] = Bits(LimitDefaults().at(f));
            } else {
                // N4 gives the nearest end of the range.
                Check(d.rule == cfg::DropRule::NumberOutOfRange && (value < kLimitMin || value > kLimitMax),
                      name + ": [Position] " + d.key + " is dropped by N4, being outside 0-10");
                ++clamped;
                for (const std::string& f : fields) imported[f] = Bits(std::clamp(value, kLimitMin, kLimitMax));
            }
            continue;
        }
        const auto it = DropEffects().find({d.section, d.key});
        Check(it != DropEffects().end(), name + ": the import drops [" + d.section + "] " + d.key + ", which no rule here covers");
        if (it == DropEffects().end()) continue;
        Check(d.rule == RuleFor(d.section, d.key, d.value),
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

// ---- Inputs --------------------------------------------------------------------

fs::path DataPath(const char* name) {
    return fs::path(SN2HT_SOURCE_DIR) / "tests" / "config_differential" / "data" / name;
}

// The newest published build's shipped file, which v0.6.1 shipped first.
std::string NewestShipped() { return ReadFileBytes(DataPath("shipped-v0.6.1.ini")); }

const char* const kNewestShippedName = "v0.6.1 and v0.6.2 shipped file and seed";

// Every file a published build shipped and the two with nothing in them: no
// player changed a row in any of them, so every row follows Defaults.ini and the
// migration gives the committed file.
bool IsUnedited(const std::string& name) {
    return name.find("shipped file") != std::string::npos || name == "no file" || name == "empty file";
}

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
    // PositionLimitZ and PositionLimitZBack, forward then backward, that the
    // migration over the built-in Defaults.ini must give, written out by hand.
    std::optional<std::pair<float, float>> lean = std::nullopt;
};

// `base` with each `from` replaced by its `to`; each `from` is there once.
std::string Edited(std::string base, const std::vector<std::pair<std::string, std::string>>& lines) {
    for (const auto& [from, to] : lines) {
        const std::size_t at = base.find(from);
        if (at == std::string::npos || base.find(from, at + 1) != std::string::npos) {
            throw std::logic_error("'" + from + "' is not in the file exactly once");
        }
        base.replace(at, from.size(), to);
    }
    return base;
}

std::string WithYawKey(const std::string& base, int code) {
    char to[32];
    std::snprintf(to, sizeof(to), "ToggleYawMode = 0x%02X", static_cast<unsigned>(code));
    return Edited(base, {{"ToggleYawMode = 0x22", to}});
}

std::vector<Input> Inputs() {
    using Lean = std::pair<float, float>;
    const Lean shipped{0.40f, 0.10f};
    const std::string v060 = ReadFileBytes(DataPath("shipped-v0.6.0.ini"));
    const std::string v061 = NewestShipped();
    std::vector<Input> inputs = {
        {"v0.1.0 shipped file", true, ReadFileBytes(DataPath("shipped-v0.1.0.ini")), shipped},
        {"v0.2.0 to v0.3.0 shipped file", true, ReadFileBytes(DataPath("shipped-v0.2.0.ini")), shipped},
        {"v0.3.1 to v0.5.0 shipped file and seed", true, ReadFileBytes(DataPath("shipped-v0.3.1.ini")), shipped},
        {"v0.6.0 shipped file and seed", true, v060, shipped},
        {kNewestShippedName, true, v061, shipped},
        {"no file", false, {}, shipped},
        {"empty file", true, {}, shipped},
        // InvertZ true, as v0.1.0 to v0.6.0 shipped it: LimitZBack bounded the
        // forward lean and LimitZ the backward one.
        {"v0.6.0 file, LimitZBack = 0.60", true, Edited(v060, {{"LimitZBack = 0.40", "LimitZBack = 0.60"}}),
         Lean{0.60f, 0.10f}},
        {"v0.6.0 file, LimitZ = 0.05", true, Edited(v060, {{"LimitZ = 0.10", "LimitZ = 0.05"}}), Lean{0.40f, 0.05f}},
        {"v0.6.0 file, LimitZ = 0.20 and LimitZBack = 0.60", true,
         Edited(v060, {{"LimitZ = 0.10", "LimitZ = 0.20"}, {"LimitZBack = 0.40", "LimitZBack = 0.60"}}),
         Lean{0.60f, 0.20f}},
        {"v0.6.0 file, LimitZ = 0.40 and LimitZBack = 0.10", true,
         Edited(v060, {{"LimitZ = 0.10", "LimitZ = 0.40"}, {"LimitZBack = 0.40", "LimitZBack = 0.10"}}),
         Lean{0.10f, 0.40f}},
        {"v0.6.0 file, LimitZBack = nan", true, Edited(v060, {{"LimitZBack = 0.40", "LimitZBack = nan"}}), shipped},
        {"v0.6.0 file, LimitZ = inf", true, Edited(v060, {{"LimitZ = 0.10", "LimitZ = inf"}}), shipped},
        // N4: a finite limit outside 0-10 is clamped on the row of its lean.
        {"v0.6.0 file, LimitZBack = 25", true, Edited(v060, {{"LimitZBack = 0.40", "LimitZBack = 25"}}),
         Lean{10.0f, 0.10f}},
        {"v0.6.0 file, LimitZ = -1", true, Edited(v060, {{"LimitZ = 0.10", "LimitZ = -1"}}), Lean{0.40f, 0.0f}},
        {"v0.6.1 file, LimitZ = 25", true, Edited(v061, {{"LimitZ = 0.40", "LimitZ = 25"}}), Lean{10.0f, 0.10f}},
        {"v0.6.1 file, InvertZ = true", true, Edited(v061, {{"InvertZ = false", "InvertZ = true"}}), Lean{0.10f, 0.40f}},
        {"v0.6.1 file, InvertZ = true and LimitZBack = 0.60", true,
         Edited(v061, {{"InvertZ = false", "InvertZ = true"}, {"LimitZBack = 0.10", "LimitZBack = 0.60"}}),
         Lean{0.60f, 0.40f}},
        // InvertZ false, as v0.6.1 and v0.6.2 shipped it: each key bounded the
        // lean it names.
        {"v0.6.1 file, LimitZ = 0.60", true, Edited(v061, {{"LimitZ = 0.40", "LimitZ = 0.60"}}), Lean{0.60f, 0.10f}},
        {"v0.6.1 file, LimitZBack = 0.05", true, Edited(v061, {{"LimitZBack = 0.10", "LimitZBack = 0.05"}}),
         Lean{0.40f, 0.05f}},
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
    std::set<std::string> files;
};

// Runs the owner's Load in `s`, whose game folder holds the input as
// HeadTracking.ini or nothing, checks what a load must do beyond comparison 2,
// and returns the settings the session runs on.
Config Migrate(const Input& input, const Scratch& s, const std::string& label, Tally& tally) {
    const std::optional<FileState> legacy_before = StateOf(s.legacy());
    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(s.Options()).Load();
    Check(StateOf(s.legacy()) == legacy_before, label + ": a load leaves HeadTracking.ini's bytes, write time and attributes");

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
    const Record defaults = ObserveImport(legacy::Config{});
    Tally builtin, readonly, skewed;
    Config skewedConfig;
    const std::vector<std::string> skewedDiagnostics = CanonicalDiagnostics(kSkewedDefaults, skewedConfig);
    for (const std::string& d : skewedDiagnostics) std::printf("  the skewed Defaults.ini: %s\n", d.c_str());
    Check(skewedDiagnostics.empty(), "the skewed Defaults.ini sets every row with no diagnostic");
    const Record skewedRecord = ObserveCanonical(skewedConfig);
    {
        std::set<Concept> differs;
        for (const auto& [entry, value] : ObserveCanonical(table.defaults())) {
            const std::optional<Concept> row = RowOf(entry);
            if (row && skewedRecord.at(entry) != value) differs.insert(*row);
        }
        if (differs.count(Concept::RotationEnabled)) differs.insert(Concept::PositionEnabled);
        Check(differs == AllRows(), "the skewed Defaults.ini differs from the built-in values on every row");
    }
    int touched = 0;
    int modeTouched = 0;
    int compared = 0;
    int clamped = 0;
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
            const std::set<Concept> follows(result.follows_defaults_ini.begin(), result.follows_defaults_ini.end());
            Check(follows.size() == result.follows_defaults_ini.size(), name + ": follows_defaults_ini names each row once");
            const std::set<Concept> untouched = UntouchedRows(imported, defaults);
            if (follows != untouched) {
                std::printf("  %s: follows Defaults.ini %s, untouched %s\n", name.c_str(), Names(follows).c_str(),
                            Names(untouched).c_str());
            }
            Check(follows == untouched, name + ": the rows left to Defaults.ini are exactly the ones the player never changed");
            if (untouched != AllRows()) ++touched;
            if (!untouched.count(Concept::RotationEnabled)) ++modeTouched;
            if (IsUnedited(name)) Check(untouched == AllRows(), name + ": every row follows Defaults.ini");
            const Record want = OverDefaults(Expected(imported, result, name, clamped), follows, defaults);
            const Config migrated = Migrate(input, s, name, builtin);
            if (input.lean) {
                Check(migrated.position_limit_z == input.lean->first &&
                          migrated.position_limit_z_back == input.lean->second,
                      name + ": gives PositionLimitZ " + std::to_string(migrated.position_limit_z) +
                          " and PositionLimitZBack " + std::to_string(migrated.position_limit_z_back) +
                          ", the limits of the forward and the backward lean");
            }
            const std::vector<std::string> diff = Differences(want, ObserveCanonical(migrated));
            for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": comparison 2, the session runs as the import read, apart from the approved drops");

            if (fs::exists(s.canonical())) {
                // Over the built-in values the table's own defaults stand for Defaults.ini.
                Config reread;
                CanonicalDiagnostics(ReadFileBytes(s.canonical()), reread);
                Check(Differences(ObserveCanonical(reread), ObserveCanonical(migrated)).empty(),
                      name + ": CameraUnlock.ini reads back as the settings the session runs on");
                // Fresh equals upgrade: the file the newest build shipped, an
                // empty file and no file at all end as the committed file,
                // `default` on every row.
                if (IsUnedited(name)) {
                    Check(ReadFileBytes(s.canonical()) == committed,
                          name + ": gives the committed file, byte for byte");
                }
            }
        }
        int clampedAgain = 0;
        const Record want = OverDefaults(
            Expected(imported, result, name, clampedAgain),
            std::set<Concept>(result.follows_defaults_ini.begin(), result.follows_defaults_ini.end()), defaults);

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
            const Config c = Migrate(input, s, name + " (read-only)", readonly);
            Check(Differences(want, ObserveCanonical(c)).empty(),
                  name + ": a read-only HeadTracking.ini imports as a writable one does");
            Check((GetFileAttributesW(s.legacy().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
                  name + ": HeadTracking.ini keeps its read-only attribute");
        }

        if (input.present) {
            // Over a Defaults.ini that differs everywhere: a row the player
            // never changed is `default` and takes Defaults.ini's value, and a
            // changed row keeps the player's. With no legacy file
            // every row is Defaults.ini's, which config_tests covers.
            Scratch s;
            s.WriteLegacy(input.bytes);
            s.WriteDefaults(kSkewedDefaults);
            const Config c = Migrate(input, s, name + " (skewed Defaults.ini)", skewed);
            const std::set<Concept> follows(result.follows_defaults_ini.begin(), result.follows_defaults_ini.end());
            const std::vector<std::string> diff = Differences(OverDefaults(want, follows, skewedRecord), ObserveCanonical(c));
            for (const std::string& d : diff) std::printf("  comparison 2, %s (skewed Defaults.ini): %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), name + ": over a Defaults.ini that differs everywhere, the untouched rows take its values "
                                       "and the changed rows keep the import's");
            if (fs::exists(s.canonical())) {
                const std::string migrated = ReadFileBytes(s.canonical());
                for (const Concept row : follows) {
                    const std::string key = cfg::schema::kConcepts[static_cast<std::size_t>(row)].key;
                    Check(migrated.find("\r\n" + key + "=default\r\n") != std::string::npos,
                          name + " (skewed Defaults.ini): " + key + " is written default");
                }
            }
        }
        ++compared;
    }
    std::printf("comparisons 1 and 2: %d inputs\n", compared);
    std::printf("over built-in Defaults.ini: %d created, %d migrated\n", builtin.created, builtin.migrated);
    std::printf("read-only: %d migrated; skewed Defaults.ini: %d migrated\n", readonly.migrated, skewed.migrated);
    std::printf("%d inputs changed a row from v0.6.2's default, %d of them the tracking mode\n", touched, modeTouched);
    std::printf("%d limits outside 0-10 clamped (N4)\n", clamped);
    Check(clamped > 0, "the inputs reach a limit outside 0-10, which N4 clamps");
    Check(touched > 0 && modeTouched > 0,
          "the inputs change rows, the tracking mode among them, which then do not follow Defaults.ini");

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
