#Requires -Version 5.1
param([string]$Configuration = "Release")

$ErrorActionPreference = "Stop"

# Dev convenience: build a Release dxgi.dll and drop it (plus dxgi_orig.dll
# and HeadTracking.ini) next to the SN2 exe. Iterates every detected install
# (Steam + Xbox/Game Pass) so both stores get the mod in one command.
# Mirrors what install.cmd does for end users but skips the unified-launcher
# arg parser and state file - this is for iterating between Ghidra and the
# game during development.

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/GamePathDetection.psm1") -Force

$dxgiProxy = Join-Path $projectDir "build/$Configuration/dxgi.dll"
if (-not (Test-Path $dxgiProxy)) {
    throw "Build output not found at $dxgiProxy. Run 'pixi run build' first."
}

$ini = Join-Path $projectDir "HeadTracking.ini"
$cfg = Get-GameConfig -GameId 'subnautica-2'

# Build the list of installs to hit. Steam is whatever Find-GamePath returns
# when env-var/Steam-manifest/etc. agree on a single location; Xbox is each
# configured xbox_path that has the WinGDK exe present. Both can coexist.
$installs = @()

$steamRoot = $null
try { $steamRoot = Find-GamePath -GameId 'subnautica-2' } catch { }
# Find-GamePath also returns Xbox paths if Steam isn't installed, so filter:
# only count it as a "Steam" install if the Win64 exe is at the resolved root.
if ($steamRoot) {
    $steamExe = Join-Path $steamRoot $cfg.Executable
    if (Test-Path $steamExe) {
        $installs += [pscustomobject]@{ Label = 'Steam'; Root = $steamRoot; ExeRelpath = $cfg.Executable }
    }
}

if ($cfg.ContainsKey('XboxPaths') -and $cfg.XboxPaths) {
    foreach ($xboxRoot in $cfg.XboxPaths) {
        $xboxExeRelpath = if ($cfg.ContainsKey('XboxExecutable') -and $cfg.XboxExecutable) {
            $cfg.XboxExecutable
        } else {
            $cfg.Executable
        }
        $xboxExe = Join-Path $xboxRoot $xboxExeRelpath
        if (Test-Path $xboxExe) {
            $installs += [pscustomobject]@{ Label = 'Xbox/Game Pass'; Root = $xboxRoot; ExeRelpath = $xboxExeRelpath }
        }
    }
}

if ($installs.Count -eq 0) {
    throw "Could not locate Subnautica 2 (Steam or Xbox). Set SUBNAUTICA_2_PATH or install via Steam/Xbox."
}

foreach ($install in $installs) {
    $exeDir = Split-Path -Parent (Join-Path $install.Root $install.ExeRelpath)
    Write-Host ""
    Write-Host "=== $($install.Label): $exeDir ===" -ForegroundColor Cyan

    # Stale loader cleanup from pre-DXGI releases.
    foreach ($legacy in "winmm.dll", "dinput8.dll", "xinput1_3.dll", "Subnautica2HeadTracking.asi") {
        $stale = Join-Path $exeDir $legacy
        if (Test-Path $stale) {
            Remove-Item $stale -Force
            Write-Host "  Removed stale $legacy" -ForegroundColor Yellow
        }
    }

    # Plant System32 dxgi.dll as dxgi_orig.dll if not already there. The
    # proxy's forwarders target dxgi_orig.<Name>, so this is what the loader
    # resolves to.
    $dxgiOrigTarget = Join-Path $exeDir "dxgi_orig.dll"
    if (-not (Test-Path $dxgiOrigTarget)) {
        $systemDxgi = Join-Path $env:SystemRoot "System32\dxgi.dll"
        Copy-Item $systemDxgi -Destination $dxgiOrigTarget -Force
        Write-Host "  Captured System32\dxgi.dll as dxgi_orig.dll" -ForegroundColor Cyan
    }

    Copy-Item $dxgiProxy -Destination $exeDir -Force
    Write-Host "  Deployed dxgi.dll"
    if (Test-Path $ini) {
        Copy-Item $ini -Destination $exeDir -Force
        Write-Host "  Deployed HeadTracking.ini"
    }
    # Default mask comp marks: deploy only when absent so a dev iterating on
    # F11 marks in-game doesn't get their tuned file stomped by every redeploy.
    $marksSrc = Join-Path $projectDir 'config/default-mask-marks.txt'
    $marksDst = Join-Path $exeDir 'Subnautica2HeadTracking.marks.txt'
    if (Test-Path $marksSrc) {
        if (Test-Path $marksDst) {
            Write-Host "  Existing Subnautica2HeadTracking.marks.txt preserved"
        } else {
            Copy-Item $marksSrc -Destination $marksDst -Force
            Write-Host "  Deployed Subnautica2HeadTracking.marks.txt (defaults)"
        }
    }
}

Write-Host ""
Write-Host "Deployed to $($installs.Count) install(s)." -ForegroundColor Green
