# Subnautica 2 Head Tracking - Nexus / Manual Install

This ZIP is the manual-install bundle. It ships both the Steam (`Win64`)
and Game Pass / Xbox app (`WinGDK`) layouts in one tree so a single
extract at your game's package root drops the files into the right
folder for whichever build you have. Unused folders are harmless and can
be left in place.

If you want the GitHub installer (`install.cmd`) instead, grab the
`-installer.zip` from the project's GitHub Releases page - it does
everything below automatically and handles both Steam and Game Pass at
the same time if both are installed.

## What's in this ZIP

```
Subnautica2/
  Binaries/
    Win64/                   <- used by the Steam build
      dxgi.dll
      HeadTracking.ini
      Subnautica2HeadTracking.marks.txt
    WinGDK/                  <- used by the Game Pass / Xbox build
      dxgi.dll
      HeadTracking.ini
      Subnautica2HeadTracking.marks.txt
```

## Where to extract

### Steam

Extract this ZIP into your Steam install root, the folder that contains
`Subnautica2.exe`'s parent directory. Typically:

```
<steam library>\steamapps\common\Subnautica2\
```

After extracting, the files should live at
`<steam library>\steamapps\common\Subnautica2\Subnautica2\Binaries\Win64\`.

### Game Pass / Xbox app

Extract this ZIP into the game's `Content` folder. Typically:

```
C:\XboxGames\Subnautica 2\Content\
```

After extracting, the files should live at
`C:\XboxGames\Subnautica 2\Content\Subnautica2\Binaries\WinGDK\`.

The `Content` folder is normally writable by your user account even
though the parent (`C:\XboxGames\Subnautica 2\`) is locked - the mod
files land in a writable subfolder.

If Windows refuses the extract or copy, close the Xbox app and your
archive tool, then run the archive tool or Command Prompt as
administrator. Do not extract into `C:\XboxGames\Subnautica 2\` itself;
extract into the `Content` folder.

## One required manual step: `dxgi_orig.dll`

The mod is a DXGI proxy: every DXGI call the game makes flows through
our `dxgi.dll`, which forwards each call onward to `dxgi_orig.dll`.
That forwarder file isn't in the ZIP because it has to be a copy of
**your own** `C:\Windows\System32\dxgi.dll` (export set must match your
Windows build).

Run **one** of these in an admin command prompt, matching whichever
build(s) you're using:

```cmd
:: Steam
copy "C:\Windows\System32\dxgi.dll" "<steam library>\steamapps\common\Subnautica2\Subnautica2\Binaries\Win64\dxgi_orig.dll"

:: Game Pass
copy "C:\Windows\System32\dxgi.dll" "C:\XboxGames\Subnautica 2\Content\Subnautica2\Binaries\WinGDK\dxgi_orig.dll"
```

If both are installed, run both - each build needs its own copy.

## Editing `HeadTracking.ini` on Game Pass / Xbox

The Xbox app folder ACLs can prevent a normal Notepad window from saving
changes. Open Notepad as administrator, then open:

```cmd
C:\XboxGames\Subnautica 2\Content\Subnautica2\Binaries\WinGDK\HeadTracking.ini
```

If your game is installed elsewhere, replace `C:\XboxGames\Subnautica 2\Content`
with that install's `Content` folder.

## Verifying it loaded

1. Launch the game (Steam or the Xbox app, whichever you've installed).
2. Look in the binaries folder for `Subnautica2HeadTracking.log`. The
   first lines include `build-check: PASS - matched profile ...` if
   everything is wired up. If the file doesn't appear, the proxy
   didn't load - usually `dxgi_orig.dll` is missing.
3. In OpenTrack, set Output to "UDP over network" targeted at
   `127.0.0.1:4242`.

## Uninstalling

Delete from the matching `Binaries\Win64\` and/or `Binaries\WinGDK\`
folder:

- `dxgi.dll`
- `dxgi_orig.dll`
- `HeadTracking.ini`
- `Subnautica2HeadTracking.marks.txt`
- `Subnautica2HeadTracking.log` (if present)

If you had a different `dxgi.dll` shim there before (ReShade, SpecialK,
etc.), restore it from your own backup - the manual install doesn't
keep one.

## Controls

| Action                          | Nav-cluster | Chord          |
|---------------------------------|-------------|----------------|
| Recenter                        | `Home`      | `Ctrl+Shift+T` |
| Toggle tracking                 | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode             | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode (world / local) | `Page Down` | `Ctrl+Shift+H` |

For configuration, OpenTrack setup, and troubleshooting, see the full
README in the project's GitHub repo:
https://github.com/itsloopyo/subnautica-2-headtracking
