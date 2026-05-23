#Requires -Version 5.1
$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $PSScriptRoot
$modName = "Subnautica2HeadTracking"

Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

$asi = Join-Path $projectDir "build/Release/$modName.asi"
if (-not (Test-Path $asi)) {
    throw "Release build not found at $asi. Run 'pixi run build' first."
}

$version = (Select-String -Path (Join-Path $projectDir "pixi.toml") -Pattern '^version\s*=\s*"([^"]+)"').Matches[0].Groups[1].Value

$releaseDir = Join-Path $projectDir "release"
$staging = Join-Path $releaseDir "staging-installer"
$nexusStaging = Join-Path $releaseDir "staging-nexus"

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null
New-Item -ItemType Directory -Path $staging | Out-Null
New-Item -ItemType Directory -Path $nexusStaging | Out-Null

# Installer ZIP: install.cmd, uninstall.cmd, plugins/, vendor/, docs
Copy-Item (Join-Path $projectDir "scripts/install.cmd") -Destination $staging
Copy-Item (Join-Path $projectDir "scripts/uninstall.cmd") -Destination $staging
New-Item -ItemType Directory -Path (Join-Path $staging "plugins") | Out-Null
Copy-Item $asi -Destination (Join-Path $staging "plugins")

$ini = Join-Path $projectDir "HeadTracking.ini"
if (Test-Path $ini) { Copy-Item $ini -Destination (Join-Path $staging "plugins") }

$vendorSrc = Join-Path $projectDir "vendor"
if (Test-Path $vendorSrc) {
    Copy-Item $vendorSrc -Destination $staging -Recurse
}

# shared/ bundle for install.cmd/uninstall.cmd: find-game.ps1 +
# GamePathDetection.psm1 + games.json + optional script bodies.
# -NoRefresh because release.ps1 owns submodule sync; local repackages
# shouldn't mutate cameraunlock-core as a side effect.
Copy-SharedBundle -StagingDir $staging -NoRefresh

foreach ($doc in "README.md","LICENSE","CHANGELOG.md","THIRD-PARTY-NOTICES.md") {
    $p = Join-Path $projectDir $doc
    if (Test-Path $p) { Copy-Item $p -Destination $staging }
}

$installerZip = Join-Path $releaseDir "Subnautica2HeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $installerZip -Force
Write-Host "Wrote $installerZip" -ForegroundColor Green

# Nexus ZIP: mirror the deploy-path subtree (the exe dir) so the user extracts
# at the game install root and the files land next to their ASI loader. No
# loader bundled - Nexus users manage their own.
$nexusDeployDir = Join-Path $nexusStaging "Subnautica2\Binaries\Win64"
New-Item -ItemType Directory -Path $nexusDeployDir -Force | Out-Null
Copy-Item $asi -Destination $nexusDeployDir
if (Test-Path $ini) { Copy-Item $ini -Destination $nexusDeployDir }
$nexusZip = Join-Path $releaseDir "Subnautica2HeadTracking-v$version-nexus.zip"
Compress-Archive -Path (Join-Path $nexusStaging "*") -DestinationPath $nexusZip -Force
Write-Host "Wrote $nexusZip" -ForegroundColor Green
