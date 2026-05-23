#requires -Version 5.1
<#
Reversibly disables Subnautica 2's startup movies and splash for faster
iteration during head-tracking debugging.

Renames everything with a .htdebug-disabled suffix so we can restore
verbatim. UE5's MoviePlayer treats missing startup movies as a no-op
(skips straight to the loaded level), and a missing Splash.bmp falls
back to a blank window.

Usage:
  pixi run powershell -ExecutionPolicy Bypass -File scripts/fast-debug-load.ps1 disable
  pixi run powershell -ExecutionPolicy Bypass -File scripts/fast-debug-load.ps1 enable
  pixi run powershell -ExecutionPolicy Bypass -File scripts/fast-debug-load.ps1 status
#>

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('disable', 'enable', 'status')]
    [string]$Action,

    [string]$GameRoot = 'C:\XboxGames\Subnautica 2\Content\Subnautica2\Content'
)

$ErrorActionPreference = 'Stop'
$Suffix = '.htdebug-disabled'

if (-not (Test-Path $GameRoot)) {
    throw "Game content root not found: $GameRoot"
}

$targets = @(
    "$GameRoot\Movies\Caustics.mov",
    "$GameRoot\Movies\Circle_1_1.mp4",
    "$GameRoot\Movies\LifepodLow.mp4",
    "$GameRoot\Movies\LifepodVideo.mp4",
    "$GameRoot\Movies\LifepodVideoLow.mp4",
    "$GameRoot\Movies\LifepodVideoLowest.mp4",
    "$GameRoot\Movies\LifepodVideoWinGDK.mp4",
    "$GameRoot\Splash\Splash.bmp"
)

switch ($Action) {
    'disable' {
        foreach ($t in $targets) {
            $disabled = "$t$Suffix"
            if (Test-Path $t) {
                Move-Item -LiteralPath $t -Destination $disabled -Force
                Write-Host "DISABLED: $t"
            }
            elseif (Test-Path $disabled) {
                Write-Host "already disabled: $t"
            }
            else {
                Write-Host "missing (skipped): $t"
            }
        }
    }
    'enable' {
        foreach ($t in $targets) {
            $disabled = "$t$Suffix"
            if (Test-Path $disabled) {
                Move-Item -LiteralPath $disabled -Destination $t -Force
                Write-Host "RESTORED: $t"
            }
            elseif (Test-Path $t) {
                Write-Host "already enabled: $t"
            }
            else {
                Write-Host "missing (skipped): $t"
            }
        }
    }
    'status' {
        foreach ($t in $targets) {
            $disabled = "$t$Suffix"
            if (Test-Path $t) { Write-Host "ENABLED  $t" }
            elseif (Test-Path $disabled) { Write-Host "disabled $t" }
            else { Write-Host "missing  $t" }
        }
    }
}
