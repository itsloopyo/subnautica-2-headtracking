# Subnautica 2 Head Tracking

An unofficial decoupled look+aim head tracking mod for Subnautica 2, look around naturally with your head while your mouse or controller controls your aim, with no VR headset required.

![Mod GIF](https://raw.githubusercontent.com/itsloopyo/subnautica-2-headtracking/main/assets/readme-clip.gif)

## Features

- **Decoupled look and aim** - your head moves the camera while the mouse or controller keeps controlling aim, so weapons still fire where the reticle points.
- **6DOF position tracking** - lean and move your head in space, with asymmetric forward/back limits to prevent clipping through the player.

## Requirements

- [Subnautica 2 on Steam](https://store.steampowered.com/app/1962700/Subnautica_2/).
- An [OpenTrack](https://github.com/opentrack/opentrack)-compatible head tracker (VR headset, webcam, or phone app).
- Windows 10 or 11, 64-bit.

## Installation

1. Download `Subnautica2HeadTracking-vX.Y.Z-installer.zip` from the [Releases page](https://github.com/itsloopyo/subnautica-2-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder
directly with a positional argument:

```powershell
install.cmd "D:\Games\Subnautica 2\Content"
```

or set the `SUBNAUTICA_2_PATH` environment variable to your install root.

### Manual Installation

If you would rather place the files by hand, drop the Ultimate ASI Loader
(`winmm.dll`) and the mod files into the game's binaries folder:

```
<game>\Subnautica2\Binaries\Win64\
```

The mod files are `Subnautica2HeadTracking.asi` and `HeadTracking.ini`.
The Nexus ZIP contains only these mod files (no loader), laid out under
`Subnautica2\Binaries\Win64\` - extract it into your game install root and
they drop straight into place. You still need an ASI Loader present in that
folder first.

## Setting Up OpenTrack

In OpenTrack, set the **Output** to "UDP over network" and target
`127.0.0.1:4242`. That is all the mod needs - it listens for the standard
OpenTrack UDP packet stream.

### VR Headset Setup

1. Connect your headset to the PC with Air Link or Virtual Desktop and start SteamVR.
2. In OpenTrack, choose the SteamVR input plugin so it reads your headset pose.
3. Set Output to UDP over network, `127.0.0.1:4242`.

### Webcam Setup

1. In OpenTrack, set the input to the `neuralnet` tracker.
2. Aim your webcam at your face and calibrate per the OpenTrack tracker guide.
3. Set Output to UDP over network, `127.0.0.1:4242`.

### Phone App Setup

If your phone app smooths its own output, send directly to your PC on
port `4242`. If you want OpenTrack's curve mapping and filtering, point
the app at a local OpenTrack instance and let OpenTrack relay to
`127.0.0.1:4242`.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action                          | Nav-cluster | Chord          |
|---------------------------------|-------------|----------------|
| Recenter                        | `Home`      | `Ctrl+Shift+T` |
| Toggle tracking                 | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode             | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode (world / local) | `Page Down` | `Ctrl+Shift+H` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

Settings live in `HeadTracking.ini`, next to the game executable in
`Subnautica2\Binaries\Win64\`. Edit it and restart the game to apply changes.

```ini
[Network]
Port = 4242            ; OpenTrack UDP port
BindAddress = 0.0.0.0  ; listen on all interfaces

[Tracking]
EnableOnStartup = true ; start with tracking active
YawSensitivity = 1.0   ; multiplier for left/right look
PitchSensitivity = 1.0 ; multiplier for up/down look
RollSensitivity = 1.0  ; multiplier for head tilt
InvertYaw = false
InvertPitch = false
InvertRoll = false
Smoothing = 0.0        ; 0.0 = responsive, 1.0 = heavy (0.15 floor applied internally)
AimDecoupling = true   ; head moves view, mouse/controller still aims
ShowReticle = true     ; draw the aim-compensated reticle
WorldSpaceYaw = true   ; true = horizon-locked yaw (default), false = camera-local

[Position]
Enabled = true         ; 6DOF head position tracking
SensitivityX = 1.0
SensitivityY = 1.0
SensitivityZ = 1.0
InvertX = true         ; sideways lean direction
InvertY = false        ; vertical move direction
InvertZ = true         ; forward/back lean direction
LimitX = 0.30          ; max sideways lean in meters
LimitY = 0.20          ; max vertical move in meters
; Z limits are swapped because InvertZ = true: with inversion, forward maps to
; +z (the LimitZBack bound) and backward maps to -z (the LimitZ bound). Keeping
; forward generous (0.40) and backward restricted (0.10) avoids clipping through
; the player, so LimitZBack holds the generous value here.
LimitZ = 0.10          ; backward lean limit in meters
LimitZBack = 0.40      ; forward lean limit in meters
Smoothing = 0.15

[Hotkeys]
; Recenter (Home), toggle tracking (End), and cycle tracking mode (Page Up)
; are fixed, each also reachable with a Ctrl+Shift chord. Only the yaw-mode
; toggle is rebindable here. VK code: PageDown = 0x22.
ToggleYawMode = 0x22
```

## Troubleshooting

**Mod not loading**

- Confirm `winmm.dll` and `Subnautica2HeadTracking.asi` are in `Subnautica2\Binaries\Win64\`.
- Windows may block the downloaded DLL: right-click it, Properties, then Unblock.

**No tracking response**

- Verify OpenTrack is running and its Output is sending to `127.0.0.1:4242`.
- A firewall may be blocking UDP on port 4242; allow it.

**Jittery / unstable tracking**

- Raise `Smoothing` in `[Tracking]` toward 0.3-0.5.
- On a wireless or phone tracker, expect more jitter; the built-in 0.15 smoothing floor helps but more smoothing reduces it further.

**Wrong rotation axis**

- If a look axis goes the wrong way, set the matching `Invert` flag (`InvertYaw` / `InvertPitch` / `InvertRoll`) to `true`.

**Yaw feels wrong when looking up or down at extreme angles**

- Try toggling between world-locked and camera-local yaw with `Page Down`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is
preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The mod loader (Ultimate
ASI Loader) is only removed if the installer put it there. Use
`uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires Visual Studio 2022 with the Desktop C++ workload and CMake.

```powershell
git clone --recursive https://github.com/itsloopyo/subnautica-2-headtracking
cd subnautica-2-headtracking
pixi run build
pixi run install
```

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Unknown Worlds Entertainment](https://unknownworlds.com/) for [Subnautica 2](https://store.steampowered.com/app/1962700/Subnautica_2/).
- [ThirteenAG](https://github.com/ThirteenAG) for the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
- The [OpenTrack](https://github.com/opentrack/opentrack) contributors for the head-tracking UDP protocol.
- The [CameraUnlock shared core](https://github.com/itsloopyo/cameraunlock-core) for the head-tracking processing pipeline used across all our mods.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Unknown
Worlds Entertainment. Use at your own risk.
