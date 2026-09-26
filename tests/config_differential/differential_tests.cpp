// The differential test for the conversion from HeadTracking.ini to the
// canonical config format.
//
//   Oracle  v0.6.2's reader and startup code, the newest published build
//           (oracle/oracle_reader.cpp; v0.6.2 is also the rolling dev build's
//           reader, since nothing in src/ changed between them)
//   Import  the frozen reader in src/legacy_config/ and this build's startup
//           code for it
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since v0.6.2 that change how the file is
// read. There are none. The frozen reader is v0.6.2's reader moved into its own
// file, every core source both compile holds the same bytes at v0.6.2's pin and
// at this repo's (CMakeLists.txt pins them), and src/ is unchanged since the
// tag, so the comparison may find no difference at all, floats bit for bit.
//
// Inputs: every distinct HeadTracking.ini a published build shipped (installer
// ZIP, Nexus ZIP and, from v0.3.2, the launcher seed, which were the same bytes
// in every release), no file, an empty file, core's mutation corpus over the
// file v0.6.1 and v0.6.2 shipped, and that file with ToggleYawMode set to every
// code from 0x01 to 0xFE. No published build wrote the file, so there is no
// first-run output: a player's file is one of the shipped ones, edited or not.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "oracle/oracle_reader.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"

namespace {

namespace fs = std::filesystem;
namespace legacy = Subnautica2HeadTracking::legacy;
namespace testing = cameraunlock::config::testing;

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
// One folder per input: GetPrivateProfileString, which every reader here sits
// on, is free to cache the file it last read. `game` stands for the folder the
// DLL loads from. Every folder lives under one root for the run, removed once at
// the end: removing thousands of folders one by one is most of the run time.

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
    // The folder as DllDirNarrow gave it, with its trailing backslash.
    std::string dll_dir() const { return game().string() + "\\"; }

    void WriteLegacy(const std::string& bytes) const { WriteFileBytes(legacy(), bytes); }

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

// The frozen reader's settings through the startup code in
// src/Subnautica2HeadTracking/headtracking_mod.cpp (BootstrapThread), which
// commit A did not change: the globals take the settings, the processor takes
// the position ones with LimitY on both vertical bounds and the smoothing pair,
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

// ---- Inputs --------------------------------------------------------------------

fs::path DataPath(const char* name) {
    return fs::path(SN2HT_SOURCE_DIR) / "tests" / "config_differential" / "data" / name;
}

// The newest published build's shipped file, which v0.6.1 shipped first.
std::string NewestShipped() { return ReadFileBytes(DataPath("shipped-v0.6.1.ini")); }

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

std::vector<cameraunlock::config::LegacyKey> CorpusReads() {
    std::vector<cameraunlock::config::LegacyKey> reads;
    for (const legacy::Key& k : legacy::ReadKeys()) reads.push_back({k.section, k.key});
    return reads;
}

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
        {"v0.6.1 and v0.6.2 shipped file and seed", true, NewestShipped()},
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

// ---- Comparison 1 ----------------------------------------------------------------

void OracleAgainstImport(const std::vector<Input>& inputs) {
    int compared = 0;
    for (const Input& input : inputs) {
        // Both read one copy, which neither writes.
        Scratch s;
        if (input.present) s.WriteLegacy(input.bytes);
        const Record oracle = ObserveOracle(sn2_oracle::Read(s.dll_dir()));
        legacy::Config read;
        const bool present = legacy::Load(s.legacy().string(), read);
        Check(present == input.present, input.name + ": the frozen reader finds the file exactly when it is there");
        const Record import = ObserveImport(read);
        const std::vector<std::string> diff = Differences(oracle, import);
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", input.name.c_str(), d.c_str());
        Check(diff.empty(), input.name + ": comparison 1, the oracle and the import agree");
        ++compared;
    }
    std::printf("comparison 1: %d inputs\n", compared);
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    RemoveTree(ScratchRoot());
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
