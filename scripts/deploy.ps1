#Requires -Version 5.1
param([string]$Configuration = "Release")

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/GamePathDetection.psm1") -Force

$gameDir = Find-GamePath -GameId "subnautica-2"
if (-not $gameDir) {
    throw "Could not locate Subnautica 2. Set SUBNAUTICA_2_PATH env var or pass the path explicitly."
}

$exeRelpath = "Subnautica2\Binaries\Win64\Subnautica2-Win64-Shipping.exe"
$exeDir = Split-Path -Parent (Join-Path $gameDir $exeRelpath)

$asi = Join-Path $projectDir "build/$Configuration/Subnautica2HeadTracking.asi"
if (-not (Test-Path $asi)) {
    throw "Build output not found at $asi. Run 'pixi run build' first."
}

Write-Host "Deploying to $exeDir" -ForegroundColor Cyan
Copy-Item $asi -Destination $exeDir -Force

$ini = Join-Path $projectDir "HeadTracking.ini"
if (Test-Path $ini) {
    Copy-Item $ini -Destination $exeDir -Force
}

$loaderTarget = Join-Path $exeDir "winmm.dll"
$staleLoader  = Join-Path $exeDir "xinput1_3.dll"
if (Test-Path $staleLoader) {
    Remove-Item $staleLoader -Force
    Write-Host "Removed stale xinput1_3.dll (game imports XINPUT1_4 not 1_3)" -ForegroundColor Yellow
}
if (-not (Test-Path $loaderTarget)) {
    $vendorLoader = Join-Path $projectDir "vendor/ultimate-asi-loader/dinput8.dll"
    if (-not (Test-Path $vendorLoader)) {
        throw "ASI loader not vendored at $vendorLoader. Run 'pixi run update-deps'."
    }
    Copy-Item $vendorLoader -Destination $loaderTarget -Force
    Write-Host "Installed Ultimate ASI Loader as winmm.dll" -ForegroundColor Cyan
}

Write-Host "Deployed." -ForegroundColor Green
