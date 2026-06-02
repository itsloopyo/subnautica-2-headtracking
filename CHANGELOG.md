# Changelog

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

## [Unreleased]

### Fixed

- Support the 2026-06-01 Steam build (PE ts 0x72247abc). The patch relinked
  the EXE, so the build-fingerprint failsafe disabled the mod (it correctly
  refused to hook stale RVAs). Added build profile `steam-win64-20260601`
  with re-derived RVAs (GPV 0x043ee420, render caller retRVA 0x04172827,
  ObjObjects 0x0cd23980, FNamePool 0x0cc3f780); older profiles retained.
- Build-mismatch log no longer claims "OLDER/NEWER" - SN2's PE TimeDateStamp
  is a deterministic-build hash, not a timestamp, so direction is meaningless.

### Added

- `scripts/derive_rvas.py` + `scripts/derive_globals.py`: deterministic
  RVA re-derivation via pefile + capstone (PE signature scan + .pdata
  function table), independent of Ghidra full analysis. Ghidra's analysis
  repeatedly OOM'd/under-analyzed this 225MB UE5 binary; the signature
  approach relocates GPV / render caller / ObjObjects / FNamePool in seconds.

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
