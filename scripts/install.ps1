#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GivenPath,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# End-user installer: enumerates every detected SN2 install (Steam + Xbox/Game
# Pass) and deploys to all of them. Called by install.cmd; not intended to be
# run directly by users (the .cmd handles the /y arg parsing + pause UX). The
# dev-tree pixi-run-install path stays on scripts/deploy.ps1 - this script is
# what ships in the release ZIP.
#
# Deployment per install:
#   1. Plant System32\dxgi.dll as dxgi_orig.dll (idempotent; existing copy kept)
#   2. Back up any pre-existing dxgi.dll to dxgi.dll.backup (first install only)
#   3. Copy dxgi.dll + HeadTracking.ini from plugins/ next to the exe
#   4. Write .headtracking-state.json at the install root
#
# If $GivenPath is supplied, it overrides detection and is treated as a single
# target. We infer Steam vs Xbox by which executable lives under it.

$scriptDir  = $PSScriptRoot
$projectRoot = Split-Path -Parent $scriptDir

# Layout duality: release ZIP has scripts/shared/ next to install.ps1, dev tree
# has cameraunlock-core/powershell/ one level up.
$modulePath = Join-Path $scriptDir 'shared/GamePathDetection.psm1'
if (-not (Test-Path $modulePath)) {
    $modulePath = Join-Path $projectRoot 'cameraunlock-core/powershell/GamePathDetection.psm1'
}
if (-not (Test-Path $modulePath)) {
    throw "GamePathDetection.psm1 not found. Installer ZIP is corrupt or dev checkout is incomplete."
}
Import-Module $modulePath -Force

$cfg = Get-GameConfig -GameId 'subnautica-2'
if (-not $cfg) {
    throw "subnautica-2 not in games.json. cameraunlock-core checkout is incomplete."
}

$steamExeRelpath = $cfg.Executable
$xboxExeRelpath  = if ($cfg.ContainsKey('XboxExecutable') -and $cfg.XboxExecutable) { $cfg.XboxExecutable } else { $cfg.Executable }

function New-Install([string]$Label, [string]$Root, [string]$ExeRelpath) {
    [pscustomobject]@{ Label = $Label; Root = $Root.TrimEnd('\','/'); ExeRelpath = $ExeRelpath }
}

$installs = @()

if ($GivenPath) {
    if (-not (Test-Path -LiteralPath $GivenPath -PathType Container)) {
        throw "Given path does not exist: $GivenPath"
    }
    $steamCandidate = Join-Path $GivenPath $steamExeRelpath
    $xboxCandidate  = Join-Path $GivenPath $xboxExeRelpath
    if (Test-Path -LiteralPath $steamCandidate) {
        $installs += New-Install 'Steam (given path)' $GivenPath $steamExeRelpath
    } elseif (Test-Path -LiteralPath $xboxCandidate) {
        $installs += New-Install 'Xbox/Game Pass (given path)' $GivenPath $xboxExeRelpath
    } else {
        throw "Neither Steam nor WinGDK executable found under $GivenPath. Looked for: $steamExeRelpath, $xboxExeRelpath."
    }
} else {
    # Steam: query libraries via appmanifest.
    $libraries = @(Find-SteamLibraries)
    foreach ($lib in $libraries) {
        $manifest = Join-Path $lib "steamapps\appmanifest_$($cfg.SteamAppId).acf"
        if (-not (Test-Path $manifest)) { continue }
        $content = Get-Content -Raw -Path $manifest
        if ($content -match '"installdir"\s+"([^"]+)"') {
            $root = Join-Path $lib "steamapps\common\$($matches[1])"
            if (Test-Path (Join-Path $root $steamExeRelpath)) {
                $installs += New-Install 'Steam' $root $steamExeRelpath
            }
        }
    }
    # Env var override (treat as Steam-layout unless WinGDK exe is what's there).
    if ($cfg.EnvVar) {
        $envPath = [Environment]::GetEnvironmentVariable($cfg.EnvVar)
        if ($envPath -and (Test-Path -LiteralPath $envPath -PathType Container)) {
            $alreadyTracked = $installs | Where-Object { $_.Root -ieq $envPath.TrimEnd('\','/') }
            if (-not $alreadyTracked) {
                if (Test-Path (Join-Path $envPath $steamExeRelpath)) {
                    $installs += New-Install "Env $($cfg.EnvVar) (Steam layout)" $envPath $steamExeRelpath
                } elseif (Test-Path (Join-Path $envPath $xboxExeRelpath)) {
                    $installs += New-Install "Env $($cfg.EnvVar) (WinGDK layout)" $envPath $xboxExeRelpath
                }
            }
        }
    }
    # Xbox/Game Pass: each configured xbox_paths entry that holds the WinGDK exe.
    if ($cfg.ContainsKey('XboxPaths') -and $cfg.XboxPaths) {
        foreach ($xboxRoot in $cfg.XboxPaths) {
            if (Test-Path (Join-Path $xboxRoot $xboxExeRelpath)) {
                $installs += New-Install 'Xbox/Game Pass' $xboxRoot $xboxExeRelpath
            }
        }
    }
}

if ($installs.Count -eq 0) {
    Write-Host "ERROR: Could not locate Subnautica 2 (Steam or Xbox/Game Pass)." -ForegroundColor Red
    Write-Host "Either install via Steam or the Xbox app, set the SUBNAUTICA_2_PATH env var,"
    Write-Host "or pass the install root explicitly: install.cmd `"<path>`""
    exit 1
}

# Source dir for the planted DLL + ini. Release ZIP: plugins/ sits next to
# install.ps1 ($scriptDir). Dev-tree fallback: $projectRoot/plugins.
$srcDir = Join-Path $scriptDir 'plugins'
if (-not (Test-Path $srcDir)) {
    $srcDir = Join-Path $projectRoot 'plugins'
}
if (-not (Test-Path $srcDir)) {
    throw "plugins/ folder not found next to install.ps1. Installer ZIP is corrupt."
}
$dxgiProxy = Join-Path $srcDir 'dxgi.dll'
$ini = Join-Path $srcDir 'HeadTracking.ini'
if (-not (Test-Path $dxgiProxy)) { throw "dxgi.dll missing from plugins/. Installer ZIP is corrupt." }

$systemDxgi = Join-Path $env:SystemRoot 'System32\dxgi.dll'
if (-not (Test-Path $systemDxgi)) { throw "System DXGI not found at $systemDxgi. Windows is missing dxgi.dll, which shouldn't happen." }

# Game-running check across all candidate exe names so we don't write a DLL
# under an exe that's holding a file handle on us.
$exeNames = @()
foreach ($i in $installs) { $exeNames += [System.IO.Path]::GetFileName($i.ExeRelpath) }
$exeNames = $exeNames | Select-Object -Unique
foreach ($name in $exeNames) {
    $running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName + '.exe' -ieq $name }
    if ($running) {
        Write-Host "ERROR: $name is currently running. Close the game before installing." -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "=== Subnautica 2 Head Tracking - Install ===" -ForegroundColor Cyan
Write-Host ("Found {0} install(s) to deploy to:" -f $installs.Count)
foreach ($i in $installs) { Write-Host ("  - {0}: {1}" -f $i.Label, $i.Root) }
Write-Host ""

foreach ($install in $installs) {
    $exeDir = Split-Path -Parent (Join-Path $install.Root $install.ExeRelpath)
    Write-Host "=== $($install.Label): $exeDir ===" -ForegroundColor Cyan

    $dxgiOrigTarget = Join-Path $exeDir 'dxgi_orig.dll'
    if (-not (Test-Path $dxgiOrigTarget)) {
        Copy-Item $systemDxgi -Destination $dxgiOrigTarget -Force
        Write-Host "  Captured System32\dxgi.dll as dxgi_orig.dll"
    } else {
        Write-Host "  dxgi_orig.dll already present, keeping existing copy"
    }

    foreach ($file in 'dxgi.dll','HeadTracking.ini') {
        $src = Join-Path $srcDir $file
        if (-not (Test-Path $src)) { continue }
        $dst = Join-Path $exeDir $file
        if (Test-Path $dst) {
            $backup = "$dst.backup"
            if (-not (Test-Path $backup)) {
                Copy-Item $dst -Destination $backup -Force
                Write-Host "  Backed up original $file to $file.backup"
            }
        }
        Copy-Item $src -Destination $dst -Force
        Write-Host "  Deployed $file"
    }

    # Mask comp marks: deploy the shipped defaults only if the user has no
    # marks file. Marks identify which Pawn child components are mask pieces
    # so the mod can pin them to the head-tracked camera. A user who's
    # re-tuned marks via the dev hotkeys (F11) has a file we must not stomp.
    $marksName = 'Subnautica2HeadTracking.marks.txt'
    $marksSrc = Join-Path $srcDir $marksName
    $marksDst = Join-Path $exeDir $marksName
    if (Test-Path $marksSrc) {
        if (Test-Path $marksDst) {
            Write-Host "  Existing $marksName preserved (user-tuned marks)"
        } else {
            Copy-Item $marksSrc -Destination $marksDst -Force
            Write-Host "  Deployed $marksName (default mask comp marks)"
        }
    }

    # Version source: prefer install.cmd's MOD_VERSION (the file release.ps1
    # bumps and release.yml validates against the tag). Fall back to pixi.toml
    # for dev-tree runs where install.cmd may not exist next to install.ps1.
    $versionStr = '0.0.0'
    $installCmd = Join-Path $scriptDir 'install.cmd'
    if (Test-Path $installCmd) {
        $m = Select-String -Path $installCmd -Pattern 'set "MOD_VERSION=([0-9]+\.[0-9]+\.[0-9]+)"' -ErrorAction SilentlyContinue
        if ($m) { $versionStr = $m.Matches[0].Groups[1].Value }
    }
    if ($versionStr -eq '0.0.0') {
        $pixiToml = Join-Path $projectRoot 'pixi.toml'
        if (Test-Path $pixiToml) {
            $m = Select-String -Path $pixiToml -Pattern '^version\s*=\s*"([^"]+)"' -ErrorAction SilentlyContinue
            if ($m) { $versionStr = $m.Matches[0].Groups[1].Value }
        }
    }
    $statePath = Join-Path $install.Root '.headtracking-state.json'
    $stateJson = @"
{
  "schema_version": 1,
  "framework": {
    "type": "None",
    "installed_by_us": false
  },
  "mod": {
    "id": "subnautica-2",
    "name": "Subnautica2HeadTracking",
    "version": "$versionStr",
    "platform": "$($install.Label)"
  }
}
"@
    # Windows PowerShell 5.1's `Set-Content -Encoding UTF8` writes a UTF-8 BOM,
    # which the Lopari launcher's strict JSON parser rejects - the mod then
    # shows as not installed. Write BOM-less UTF-8 explicitly.
    [System.IO.File]::WriteAllText($statePath, $stateJson, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "  Wrote .headtracking-state.json"
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Installation Complete!"                  -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Launch the game normally."
Write-Host "Controls: Home / Ctrl+Shift+T = recenter, End / Ctrl+Shift+Y = toggle tracking,"
Write-Host "          Page Up / Ctrl+Shift+G = cycle tracking mode,"
Write-Host "          Page Down / Ctrl+Shift+H = toggle yaw mode"
Write-Host ""
exit 0
