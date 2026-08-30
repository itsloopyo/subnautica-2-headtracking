# Changelog

## [Unreleased]

### Added

- `scripts/derive_rvas.py` + `scripts/derive_globals.py`: deterministic
  RVA re-derivation via pefile + capstone (PE signature scan + .pdata
  function table), independent of Ghidra full analysis. Ghidra's analysis
  repeatedly OOM'd/under-analyzed this 225MB UE5 binary; the signature
  approach relocates GPV / render caller / ObjObjects / FNamePool in seconds.

### Changed

- The `[Position]` Z keys now mean what they say. The mod shipped
  `InvertZ = true` with `LimitZ` and `LimitZBack` swapped: the two errors
  cancelled, so the lean was correct, but `LimitZ` named the backward limit here
  and the forward limit in every other mod, and clearing `InvertZ` on its own
  reversed the budgets while the direction still looked right. Depth is now
  negated at the engine boundary, `InvertZ` defaults false, and `LimitZ = 0.40`
  is the forward lean with `LimitZBack = 0.10` the backward one. An existing
  `HeadTracking.ini` carrying the old triple needs those three keys updated;
  deleting the file lets the installer reseed it.
- `LimitY` now reaches both vertical bounds. The clamp is
  `[-LimitYDown, +LimitY]` and the downward bound carried its own 0.20 m
  default, so raising `LimitY` widened upward travel alone.

- Three render-path log lines were gated on a call count, which runs at the
  player's frame rate: `mask-comp` every 120 calls and `pos #` / `hook #` every
  600 cost 300-780 KB and 120-280 KB an hour respectively, more on a fast
  machine, burying the startup chain a user is asked to read. `mask-comp` is now
  time-gated at 30 seconds; the `pos #` / `hook #` pair is a bounded burst of 20
  samples. The heartbeat, its 2-second ramp over the first 30 seconds, and the
  build-check diagnostics are unchanged.
- The log keeps one previous generation. It already started fresh on every
  launch, so a crash followed by a relaunch destroyed the session worth reading
  - the one the crash handler had just written into. It is now kept as
  `Subnautica2HeadTracking.prev.log`, and uninstall removes it.

- The mod no longer keeps a centre of its own and applies the tracker pose as
  absolute. The tracker app owns centring, so a mod-side centre sat in series
  with the tracker's and the two drifted apart. Centre in your tracker app
  instead. The recentre hotkey (Home / Ctrl+Shift+T) and the tracker-app
  recentre request handling are gone with it.
- smoothing is now two keys in `[Tracking]`: `LocalSmoothing` (default 0.0) for a tracker running on this machine and `RemoteSmoothing` (default 0.15) for a remote device on the network, selected per connection from the packet source address
- removed `[Tracking] Smoothing` and `[Position] Smoothing`; both rotation and position use the same pair of values
- removed the hidden 0.15 baseline smoothing floor, so a local tracker now gets zero-latency tracking by default

### Fixed

- Support the 2026-06-01 Steam build (PE ts 0x72247abc). The patch relinked
  the EXE, so the build-fingerprint failsafe disabled the mod (it correctly
  refused to hook stale RVAs). Added build profile `steam-win64-20260601`
  with re-derived RVAs (GPV 0x043ee420, render caller retRVA 0x04172827,
  ObjObjects 0x0cd23980, FNamePool 0x0cc3f780); older profiles retained.
- Build-mismatch log no longer claims "OLDER/NEWER" - SN2's PE TimeDateStamp
  is a deterministic-build hash, not a timestamp, so direction is meaningless.

## [0.6.0] - 2026-08-20

### Added

- split smoothing into local/remote, drop mod-side centring
- add build profiles for the 2026-08-20 patch (Steam + GDK)

### Fixed

- harden Lopari pin sync and tag handling in release workflow

### Other

- SN2 patch watch: record buildid 24418064 [skip ci]

## [0.5.0] - 2026-08-03

### Added

- recenter on tracker-app request, refresh rederive docs for the profile registry

### Fixed

- keep tracker pivot-forward compensation off, the position stream is eye-anchored 6DOF

### Other

- SN2 patch watch: record buildid 24153994 [skip ci]
- Link Discord, Lopari and Headcam from the README

## [0.4.1] - 2026-07-15

### Added

- fail fast when DISCORD_RELEASE_WEBHOOK is missing
- add Lopari run link to Discord release announcements
- sync Lopari catalog pin before Discord announcement
- add steam/gdk 20260714 build profiles for the latest patch

## [0.4.0] - 2026-07-10

### Added

- guard the .original backup against patched assemblies
- let Write-DeploymentSuccess take a full -Controls list
- announce new releases to Discord
- announce dev pre-releases to Discord
- require DISCORD_RELEASE_WEBHOOK before publishing dev builds
- add steam-win64-20260710 build profile
- add gdk-wingdk-20260710 build profile

### Fixed

- subscribe Camera.onPreCull via reflection for SRP-only Unity 6
- accept multiple FName decoder hits in derive_globals
- auto-discover mask group and compose from a clean baseline

### Other

- SN2 patch watch: record buildid 23922166 [skip ci]
- Add Metaphor: ReFantazio to games.json; fix ASI install template (x86) path bug
- Add Mirror's Edge, RV There Yet, Prey to games.json; add DX9 overlay header

## [0.3.2] - 2026-06-07

### Added

- aim projection, reframework/unreal hooks, input/logging hardening, games
- add Mass Effect Legendary Edition to games catalog
- expand games catalog, fix unicode games.json read, stage launcher manifest
- add Pacific Drive to games catalog
- add Homeworld: Remastered Collection to games catalog
- add manifest-mode installer validator and ASI loader subdir support
- authenticate GitHub API requests via env token when present
- add R.E.P.O. detection data

### Fixed

- restore il2cpp camera position by undoing applied local delta
- set SO_REUSEADDR so the receiver reclaims its port on relaunch
- harden release.ps1 - changelog gate before version bump, add -Force

### Other

- Remove orphaned ContainsCI test (migrated to cameraunlock-core)
- powershell: stop redirecting git stderr in Invoke-VersionCommit

## [0.3.1] - 2026-06-02

### Other

- Config: read full HeadTracking.ini in C++, default dev hotkeys off, dedup reticle/tooltip writes
- powershell: write state file BOM-less so Lopari JSON parser accepts it

## [0.3.0] - 2026-06-02

### Added

- add HeadTrackingSession and expand C++ core with RE Engine, Unreal, and tracking-session modules

### Fixed

- fail fast in ASI dev-deploy when the game is running

### Other

- SN2 patch watch: record buildid 23446003 [skip ci]
- reframework: strip VR runtime DLLs on install for flatscreen mode
- reframework: cache GetValue method and avoid per-call heap in ArrayGetValue; data: add BioShock Infinite
- uninstall: remove reframework_revision.txt marker dropped at game root
- install: render MOD_CONTROLS multi-line via percent expansion
- Add YAPYAP to games.json
- Consume shared infrastructure from cameraunlock-core
- Replace DX12 reticle overlay with native-reticle widget move
- Reticle move: target image widgets, add write/stomp diagnostics
- Aim projection: calibrate scale from game WorldToScreen, divide by UMG DPI

## [0.2.3] - 2026-06-02

### Other

- Add gdk-wingdk-20260602 build profile

## [0.2.2] - 2026-06-02

### Other

- Add steam-win64-20260601 build profile

## [0.2.1] - 2026-05-28

### Other

- Add crash diagnostics: unhandled-exception filter + startup snapshot
- Add Streamline/DLSS diagnostics + DisableMaskComp debug knob
- Fix CI build: remove duplicate ContainsCI definition
- find-game: escape `&` in GAME_DISPLAY_NAME so echo doesn't split
- templates: add uninstall.ps1; data: add Deus Ex Mankind Divided
- powershell: add NightlyRelease module for Patreon-gated nightly builds
- protocol: disable SIO_UDP_CONNRESET and add one-shot receiver diagnostics; powershell: write nightly manifest.json without UTF-8 BOM; data: add Mixtape
- powershell: stop redirecting git stderr in Update-CameraUnlockCoreToRemoteTip
- Add release nightly dispatch and publisher shim
- powershell: publish dev builds as GitHub pre-releases
- protocol: disable SIO_UDP_CONNRESET and add one-shot receiver diagnostics
- data: add Mixtape
- powershell: stop redirecting git stderr in Update-CameraUnlockCoreToRemoteTip
- powershell: run gh under Continue so its stderr doesn't abort the dev-release publish
- Drive reticle projection from live engine FOV

## [0.2.0] - 2026-05-24

### Other

- Gate tooltip follow on world-hover viewmodel token
- SN2 patch watch: record buildid 23357846 [skip ci]
- Add Game Pass (WinGDK) support via dxgi proxy + build profile registry
- data: add Ni no Kuni Remastered and Yakuza 0; switch find-game output to UTF-8
- detection: add Xbox/GDK build support for Subnautica 2 (and any future GDK title)
- Fix install.ps1 plugins/ lookup for release ZIP layout

## [0.1.0] - 2026-05-23

### Other

- Hello world

## [0.0.0] - 2026-05-21

### Added

- Initial release: head tracking for Subnautica 2 (UE5 / WinGDK) loaded via Ultimate ASI Loader.
- Decoupled look and aim: head rotation moves the view while the mouse still controls aim.
- 6DOF position tracking.
- D3D12 reticle overlay that follows the aim point.
- Recenter, toggle tracking, toggle position, and toggle yaw-mode hotkeys (nav cluster plus Ctrl+Shift chord alternatives).
