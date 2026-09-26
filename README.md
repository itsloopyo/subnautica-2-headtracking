# Subnautica 2 Head Tracking

![Subnautica 2 running with this mod](https://raw.githubusercontent.com/itsloopyo/subnautica-2-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Subnautica 2 that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - your head moves the camera while the mouse or controller keeps controlling aim, so weapons still fire where the reticle points.
- **6DOF position tracking** - lean and move your head in space, with asymmetric forward/back limits to prevent clipping through the player.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Subnautica 2, either the [Steam build](https://store.steampowered.com/app/1962700/Subnautica_2/) or the Xbox Game Pass build. Both store versions are supported; the installer auto-detects whichever (or both) you have.
- An [OpenTrack](https://github.com/opentrack/opentrack)-compatible head tracker (VR headset, webcam, or phone app).
- Windows 10 or 11, 64-bit.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Subnautica 2**, and click
**Play with head tracking**.

### Standalone Installer

1. Download `Subnautica2HeadTracking-vX.Y.Z-installer.zip` from the [Releases page](https://github.com/itsloopyo/subnautica-2-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

`install.cmd` finds every installed copy of Subnautica 2 on your machine
and deploys to all of them. If you have both Steam and Xbox Game Pass
installed, both are mod-enabled in one pass.

If the installer cannot find your game, point it at the install root
directly with a positional argument:

```powershell
install.cmd "D:\Games\Subnautica 2"               :: Steam-layout root
install.cmd "C:\XboxGames\Subnautica 2\Content"   :: Xbox Game Pass root
```

or set the `SUBNAUTICA_2_PATH` environment variable to your install root.

### Xbox Game Pass Notes

The Xbox app install is more locked down than the Steam install. Use the
game's `Content` folder as the root, normally:

```cmd
C:\XboxGames\Subnautica 2\Content
```

The mod files must end up here:

```cmd
C:\XboxGames\Subnautica 2\Content\Subnautica2\Binaries\WinGDK\
```

`install.cmd` copies the mod there and also plants `dxgi_orig.dll` from
your own Windows install. If Windows denies the copy, close the Xbox app,
right-click `install.cmd`, choose **Run as administrator**, and pass the
Content path if auto-detection still fails:

```cmd
install.cmd "C:\XboxGames\Subnautica 2\Content"
```

### Manual Installation

If you would rather place the files by hand, drop the mod into the
binaries folder for whichever build you have:

| Store     | Target folder |
|-----------|---------------|
| Steam     | `<steam>\steamapps\common\Subnautica2\Subnautica2\Binaries\Win64\` |
| Xbox Game Pass | `C:\XboxGames\Subnautica 2\Content\Subnautica2\Binaries\WinGDK\`   |

You need two files in that folder:

1. `dxgi.dll` - the mod itself (from the release ZIP's `plugins/`). This
   is a DXGI proxy: every DXGI call the game makes flows through us,
   which is how we hook the camera path. The same `dxgi.dll` works for
   both Steam and Xbox Game Pass; the proxy fingerprints the running exe and
   selects the right RVA profile at load time.
2. `dxgi_orig.dll` - **a copy of your own `C:\Windows\System32\dxgi.dll`**.
   The mod's exports forward here, so the game still reaches the real
   DXGI through us. Copy it yourself, matching whichever build you have:
   ```cmd
   :: Steam
   copy C:\Windows\System32\dxgi.dll "<steam>\steamapps\common\Subnautica2\Subnautica2\Binaries\Win64\dxgi_orig.dll"
   :: Xbox Game Pass
   copy C:\Windows\System32\dxgi.dll "C:\XboxGames\Subnautica 2\Content\Subnautica2\Binaries\WinGDK\dxgi_orig.dll"
   ```

The mod creates its settings file, `CameraUnlock.ini`, in the same folder the
first time the game starts with it (see [Configuration](#configuration)).

The Nexus ZIP contains both `Binaries\Win64\` and `Binaries\WinGDK\`
trees in one bundle. Extract it at the package root and the files land
in the right place for whichever build you have - the unused folder is
harmless. You still need to do the `dxgi_orig.dll` copy step above. The
GitHub installer ZIP's `install.cmd` does the copy automatically and
hits both builds at once if both are installed.

For the Xbox app build, extract the Nexus ZIP into:

```cmd
C:\XboxGames\Subnautica 2\Content
```

Then run this from an administrator Command Prompt:

```cmd
copy "C:\Windows\System32\dxgi.dll" "C:\XboxGames\Subnautica 2\Content\Subnautica2\Binaries\WinGDK\dxgi_orig.dll"
```

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets by default - use whichever your keyboard has:

| Action                          | Nav-cluster | Chord          |
|---------------------------------|-------------|----------------|
| Toggle tracking                 | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode             | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode (world / local) | `Page Down` | `Ctrl+Shift+H` |

The mod applies the pose your tracker sends and keeps no centre of its own. To
recentre, use the centre control in your tracker app: Center in opentrack,
CENTER in Headcam, or the equivalent in whatever you run.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` when you
change them, so the game starts in the mode you left it in. Turning tracking on
or off is not saved: the game starts with head tracking on or off as
`EnableOnStartup` says. Each hotkey is a list of keys in the `[Hotkeys]`
section of `CameraUnlock.ini`, the chords included, and you can change any of
them there.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, at one of these paths depending on the store the game came from:

- `Subnautica2\Binaries\Win64\CameraUnlock.ini`
- `Subnautica2\Binaries\WinGDK\CameraUnlock.ini`

It creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.

A setting that the defaults below set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it. `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.

Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults below. Every setting they set to `default` then follows `Defaults.ini`.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Subnautica 2 head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[Tooltip]
; true: the prompt naming what you point at moves with the reticle, so it stays where
; you aim. false: it stays at the centre of the view.
FollowReticle=true
; Scales how far the reticle and that prompt move to follow your aim. Leave it at 1.0
; unless they overshoot your aim at your resolution (lower it) or fall short (raise it).
FollowScale=1.0

[Debug]
; true: turn off the correction that keeps the mask fixed on screen as your head
; turns; head tracking stays on. For a crash or conflict with another program that
; hooks the game's rendering, such as an overlay or ReShade.
DisableMaskComp=false
```
<!-- /cameraunlock:config -->

Changes take effect the next time the game starts. If both builds are
installed, each has its own `CameraUnlock.ini`.

On the Xbox app build, Windows may block normal saves under the game
folder. Open Notepad as administrator, then open:

```cmd
C:\XboxGames\Subnautica 2\Content\Subnautica2\Binaries\WinGDK\CameraUnlock.ini
```

If you installed the game to another drive or folder, use that install's
`Content\Subnautica2\Binaries\WinGDK\CameraUnlock.ini` path instead.

## Troubleshooting

**Mod not loading**

- Confirm `dxgi.dll` and `dxgi_orig.dll` are both in the binaries folder for your build - `Subnautica2\Binaries\Win64\` for Steam, `Subnautica2\Binaries\WinGDK\` for Xbox Game Pass. Missing `dxgi_orig.dll` is the most common cause - the proxy forwards every DXGI export there, so without it the game crashes on launch.
- Windows may block the downloaded DLL: right-click `dxgi.dll`, Properties, then Unblock.
- Check the mod log next to the DLL for a `build-check: PASS - matched profile ...` line.

**No tracking response**

- Verify OpenTrack is running and its Output is sending to `127.0.0.1:4242`.
- A firewall may be blocking UDP on port 4242; allow it.

**Jittery / unstable tracking**

- Raise the smoothing key that matches your tracker: `LocalSmoothing` in `[Smoothing]` for a tracker on this machine, `RemoteSmoothing` for a device on the network. 0.3-0.5 is a good starting point.
- On a wireless or phone tracker, expect more jitter; `RemoteSmoothing` defaults to 0.15 and can go higher.

**Wrong rotation axis**

- The mod applies the pose as your tracker sends it and has no axis inversion setting. If a look axis goes the wrong way, invert it in your tracker app.

**Yaw feels wrong when looking up or down at extreme angles**

- Try toggling between world-locked and camera-local yaw with `Page Down`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. The installer
never writes `CameraUnlock.ini` or `HeadTracking.ini`, so your settings stay
as they are.

## Uninstalling

Run `uninstall.cmd`. This removes `dxgi.dll` and `dxgi_orig.dll` from the
binaries folder of every detected install (Steam and / or Xbox Game Pass),
and leaves `CameraUnlock.ini` and `HeadTracking.ini`, your settings, where
they are. If you had a pre-existing `dxgi.dll` (e.g.
ReShade) when you installed the mod, the original is restored from its
`.backup` copy. Pass `/force` to discard the backup instead.

## Building from Source

Requires Visual Studio 2022 with the Desktop C++ workload and CMake.

```powershell
git clone --recursive https://github.com/itsloopyo/subnautica-2-headtracking
cd subnautica-2-headtracking
pixi run build
pixi run install
```

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Unknown Worlds Entertainment](https://unknownworlds.com/) for [Subnautica 2](https://store.steampowered.com/app/1962700/Subnautica_2/).
- The [OpenTrack](https://github.com/opentrack/opentrack) contributors for the head-tracking UDP protocol.
- The [CameraUnlock shared core](https://github.com/itsloopyo/cameraunlock-core) for the head-tracking processing pipeline used across all our mods.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Unknown
Worlds Entertainment or KRAFTON. Use at your own risk.
