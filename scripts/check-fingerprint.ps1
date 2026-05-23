#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
  Compare the installed Subnautica 2 EXE's PE fingerprint against the
  values committed in src/Subnautica2HeadTracking/ghidra_offsets.h.
.DESCRIPTION
  Same three-field check (TimeDateStamp / SizeOfImage / CheckSum) that
  ValidateRunningBuild runs at mod load time. Use after a Steam patch
  lands to find out whether RVAs need rederiving before shipping a new
  mod version.

  Exit codes:
    0 = match (no rederivation needed)
    1 = mismatch (rederive + ship a new release)
    2 = could not locate the EXE
.PARAMETER ExePath
  Direct path to Subnautica2-Win64-Shipping.exe. If omitted, falls back
  to cameraunlock-core's Find-GamePath (env var / Steam manifest / etc).
#>
param(
    [Parameter(Position=0, Mandatory=$false)]
    [string]$ExePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

function Resolve-ExePath {
    param([string]$Provided = '')
    if ($Provided) {
        if (-not (Test-Path $Provided)) {
            throw "EXE not found at: $Provided"
        }
        return (Resolve-Path $Provided).Path
    }
    $config = Get-GameConfig -GameId 'subnautica-2'
    $gameRoot = Find-GamePath -GameId 'subnautica-2'
    if (-not $gameRoot) {
        throw "Could not locate Subnautica 2. Pass the EXE path positionally or set `$env:SUBNAUTICA_2_PATH."
    }
    $exe = Join-Path $gameRoot $config.Executable
    if (-not (Test-Path $exe)) {
        throw "EXE not found at expected location: $exe"
    }
    return $exe
}

function Read-PEFingerprint {
    param([string]$Path)
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Position = 0x3c
        $e_lfanew = $reader.ReadUInt32()
        $stream.Position = $e_lfanew
        $sig = $reader.ReadUInt32()
        if ($sig -ne 0x00004550) {
            throw ("Not a PE file: signature 0x{0:x} at e_lfanew=0x{1:x}" -f $sig, $e_lfanew)
        }
        $stream.Position = $e_lfanew + 8
        $tds = $reader.ReadUInt32()
        $stream.Position = $e_lfanew + 4 + 20 + 0x38
        $size = $reader.ReadUInt32()
        $stream.Position = $e_lfanew + 4 + 20 + 0x40
        $csum = $reader.ReadUInt32()
        return [pscustomobject]@{
            TimeDateStamp = $tds
            SizeOfImage   = $size
            CheckSum      = $csum
        }
    } finally {
        $stream.Dispose()
    }
}

function Read-ExpectedFingerprint {
    param([string]$ProjectDir)
    $headerPath = Join-Path $ProjectDir 'src/Subnautica2HeadTracking/ghidra_offsets.h'
    if (-not (Test-Path $headerPath)) {
        throw "ghidra_offsets.h not found at $headerPath"
    }
    $header = Get-Content -Raw $headerPath
    $values = @{}
    foreach ($name in @('kTimeDateStamp', 'kSizeOfImage', 'kCheckSum')) {
        $pattern = [regex]::Escape($name) + '\s*=\s*0x([0-9a-fA-F]+)u?\s*;'
        if ($header -notmatch $pattern) {
            throw "$name not found in ghidra_offsets.h"
        }
        $values[$name] = [Convert]::ToUInt32($Matches[1], 16)
    }
    return [pscustomobject]@{
        TimeDateStamp = $values['kTimeDateStamp']
        SizeOfImage   = $values['kSizeOfImage']
        CheckSum      = $values['kCheckSum']
    }
}

try {
    $exe = Resolve-ExePath -Provided $ExePath
} catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 2
}

Write-Host "EXE:      $exe"
$running  = Read-PEFingerprint  -Path $exe
$expected = Read-ExpectedFingerprint -ProjectDir $projectDir

Write-Host ("Running:  ts=0x{0:x8} size=0x{1:x8} csum=0x{2:x8}" -f $running.TimeDateStamp, $running.SizeOfImage, $running.CheckSum)
Write-Host ("Expected: ts=0x{0:x8} size=0x{1:x8} csum=0x{2:x8}" -f $expected.TimeDateStamp, $expected.SizeOfImage, $expected.CheckSum)

if ($running.TimeDateStamp -eq $expected.TimeDateStamp `
 -and $running.SizeOfImage -eq $expected.SizeOfImage `
 -and $running.CheckSum    -eq $expected.CheckSum) {
    Write-Host "MATCH - no rederivation needed." -ForegroundColor Green
    exit 0
}

if ($running.TimeDateStamp -gt $expected.TimeDateStamp) {
    Write-Host "MISMATCH: EXE is NEWER than the committed offsets." -ForegroundColor Yellow
    Write-Host "  The game was updated. Re-import in Ghidra and rederive RVAs before shipping."
} elseif ($running.TimeDateStamp -lt $expected.TimeDateStamp) {
    Write-Host "MISMATCH: EXE is OLDER than the committed offsets." -ForegroundColor Yellow
    Write-Host "  Steam may have a pending update, or this is a legacy/beta branch install."
} else {
    Write-Host "MISMATCH: same timestamp, different size/checksum." -ForegroundColor Yellow
    Write-Host "  Unusual - hand-patched/tampered EXE or a fluke rebuild."
}
exit 1
