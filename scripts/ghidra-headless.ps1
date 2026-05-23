#Requires -Version 5.1
# Run a Jython/Python Ghidra script against the Subnautica 2 shipping EXE, headless.
#
# Usage: pixi run ghidra-script <script-name> [-- <extra args to the script>]
#        script-name relative to scripts/ghidra/, with or without .py
#
# On first run, imports the game EXE into a Ghidra project at
# C:\temp\subnautica-2 and auto-analyzes. Subsequent runs reuse the project.

param(
    [Parameter(Mandatory = $true)][string]$Script
)

$ErrorActionPreference = "Stop"

$GhidraRoot  = "C:\ProgramData\chocolatey\lib\ghidra\tools\ghidra_12.0_PUBLIC"
$ProjectDir  = "C:\temp\subnautica-2"
$ProjectNm   = "Subnautica2"
$ProgramExe  = "C:\Program Files (x86)\Steam\steamapps\common\Subnautica2\Subnautica2\Binaries\Win64\Subnautica2-Win64-Shipping.exe"
$ProgramNm   = [IO.Path]::GetFileName($ProgramExe)

if (-not (Test-Path $GhidraRoot))  { throw "Ghidra not found at $GhidraRoot" }
if (-not (Test-Path $ProgramExe))  { throw "Game EXE not found at $ProgramExe" }

$RepoRoot  = Split-Path -Parent $PSScriptRoot
$ScriptDir = Join-Path $RepoRoot "scripts\ghidra"

if (-not $Script.EndsWith(".py")) { $Script = "$Script.py" }
$ScriptPath = Join-Path $ScriptDir $Script
if (-not (Test-Path $ScriptPath)) { throw "script not found: $ScriptPath" }

$env:GHIDRA_INSTALL_DIR = $GhidraRoot

$ProjectGpr = Join-Path $ProjectDir "$ProjectNm.gpr"
$NeedImport = -not (Test-Path $ProjectGpr)

if ($NeedImport) {
    Write-Host "First-time import: $ProgramExe -> $ProjectDir\$ProjectNm" -ForegroundColor Cyan
    Write-Host "  This will take several minutes (~225 MB binary, full analysis)." -ForegroundColor DarkGray
    if (-not (Test-Path $ProjectDir)) { New-Item -ItemType Directory -Path $ProjectDir -Force | Out-Null }
}

Write-Host "Running $Script against $ProgramNm via pyghidra..." -ForegroundColor Cyan

$py = @"
import os, pyghidra
pyghidra.run_script(
    binary_path=r'$ProgramExe' if $(if ($NeedImport) { 'True' } else { 'False' }) else None,
    script_path=r'$ScriptPath',
    project_location=r'$ProjectDir',
    project_name=r'$ProjectNm',
    program_name=r'$ProgramNm',
    analyze=$(if ($NeedImport) { 'True' } else { 'False' }),
    nested_project_location=False,
)
"@

& py -3 -c $py
