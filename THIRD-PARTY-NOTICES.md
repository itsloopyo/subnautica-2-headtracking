# Third-Party Notices

Subnautica 2 Head Tracking bundles and/or depends on the following third-party
components.

## Ultimate ASI Loader

- **Version:** v9.7.1
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Loads the mod's `.asi` into the game process at startup.
- **Bundled:** yes. Bundled in the release ZIP and used as the install-time source.

Copyright (c) 2023 ThirteenAG

---

## MinHook

- **Version:** 1.3.3
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Inline function hooking for the camera and reticle hooks.
- **Bundled:** no. Statically linked into the mod DLL via cameraunlock-core.

---

## Dear ImGui

- **License:** MIT
- **Upstream:** https://github.com/ocornut/imgui
- **Usage:** Draws the D3D12 reticle overlay (via cameraunlock-core's DX12Overlay).
- **Bundled:** no. Statically linked into the mod DLL via cameraunlock-core.

---

## OpenTrack

- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** UDP pose protocol only. No OpenTrack code is included.
- **Bundled:** no.

---

## CameraUnlock Core

- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared head-tracking processing, math, and rendering helpers.
- **Bundled:** no. Statically linked into the mod DLL.

---

## Game

Subnautica 2 is a trademark of Unknown Worlds Entertainment / Krafton.
This mod is unaffiliated; it requires a legitimately purchased copy of the
game and does not contain, redistribute, or modify any game assets or
proprietary code.
