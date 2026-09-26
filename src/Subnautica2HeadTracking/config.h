#pragma once

#include <filesystem>
#include <string>

#include "position_boundary.h"

#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace Subnautica2HeadTracking {

// The settings CameraUnlock.ini holds, at their defaults.
struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;
    // True: head yaw turns about the world's up axis (horizon-locked).
    bool world_space_yaw = true;

    // The tracking mode at startup, the pair the mode hotkey saves.
    bool rotation_enabled = true;
    bool position_enabled = true;

    // Smoothing for a tracker on this machine and for one on another device.
    // Rotation and position both use the pair.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    float position_limit_x = Position::kLimitX;
    float position_limit_y = Position::kLimitY;
    float position_limit_y_down = Position::kLimitYDown;
    float position_limit_z = Position::kLimitZ;
    float position_limit_z_back = Position::kLimitZBack;

    std::string toggle_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    std::string cycle_tracking_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    std::string yaw_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault;

    // Move the interaction prompt with the reticle, and how far the prompt and
    // the reticle move per backbuffer pixel the aim point sits off centre.
    bool tooltip_follow_reticle = true;
    float tooltip_follow_scale = 1.0f;

    bool disable_mask_comp = false;
};

}  // namespace Subnautica2HeadTracking

// CameraUnlock.ini, beside the game exe, in cameraunlock-core's canonical config
// format. One ConfigOwner reads and writes it; nothing else in the mod touches
// it. HeadTracking.ini, the file every earlier build read, is imported once
// while CameraUnlock.ini is absent and is never written.
namespace Subnautica2HeadTracking::config {

cameraunlock::config::ConfigTable<Config> Table();

cameraunlock::config::RenderHeader Header();

// HeadTracking.ini through the frozen reader in src/legacy_config/, mapped into
// Config.
cameraunlock::config::LegacyImport<Config> Import();

// The owner's options for CameraUnlock.ini in `folder`, with HeadTracking.ini
// beside it as the legacy file and Defaults.ini where `defaults` says.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::filesystem::path& folder,
                                                              cameraunlock::config::DefaultsFile defaults);

// Reads, imports or creates CameraUnlock.ini in `folder`, logs what the owner
// reports, and returns the settings the session runs on. Call once, from the
// bootstrap thread, with the log open. `defaults` is DefaultsFile::PerUser() in
// the mod.
Config Load(const std::filesystem::path& folder, cameraunlock::config::DefaultsFile defaults);

// The tracking mode the settings start in. The table never gives both rows
// false.
cameraunlock::TrackingMode StartupTrackingMode(const Config& config);

// Saves the value a hotkey has just applied. The session keeps it whether or
// not the save succeeds; a failed save is logged. Called on the hotkey thread.
void SaveWorldSpaceYaw(bool world_space_yaw);
void SaveTrackingMode(cameraunlock::TrackingMode mode);

}  // namespace Subnautica2HeadTracking::config
