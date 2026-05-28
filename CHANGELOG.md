# Changelog

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
