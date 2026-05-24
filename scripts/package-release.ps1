#Requires -Version 5.1
$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $PSScriptRoot
$modName = "Subnautica2HeadTracking"

Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

$dxgiProxy = Join-Path $projectDir "build/Release/dxgi.dll"
if (-not (Test-Path $dxgiProxy)) {
    throw "Release build not found at $dxgiProxy. Run 'pixi run build' first."
}

$version = (Select-String -Path (Join-Path $projectDir "pixi.toml") -Pattern '^version\s*=\s*"([^"]+)"').Matches[0].Groups[1].Value

$releaseDir = Join-Path $projectDir "release"
$staging = Join-Path $releaseDir "staging-installer"
$nexusStaging = Join-Path $releaseDir "staging-nexus"

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null
New-Item -ItemType Directory -Path $staging | Out-Null
New-Item -ItemType Directory -Path $nexusStaging | Out-Null

# Installer ZIP: install.cmd, uninstall.cmd, plugins/, docs.
# No vendor/ - the dxgi proxy is built from source, and dxgi_orig.dll is
# copied from the user's own System32 by install.cmd.
Copy-Item (Join-Path $projectDir "scripts/install.cmd") -Destination $staging
Copy-Item (Join-Path $projectDir "scripts/install.ps1") -Destination $staging
Copy-Item (Join-Path $projectDir "scripts/uninstall.cmd") -Destination $staging
Copy-Item (Join-Path $projectDir "scripts/uninstall.ps1") -Destination $staging
New-Item -ItemType Directory -Path (Join-Path $staging "plugins") | Out-Null
Copy-Item $dxgiProxy -Destination (Join-Path $staging "plugins")

$ini = Join-Path $projectDir "HeadTracking.ini"
if (Test-Path $ini) { Copy-Item $ini -Destination (Join-Path $staging "plugins") }

$marks = Join-Path $projectDir "config/default-mask-marks.txt"
if (-not (Test-Path $marks)) {
    throw "Default marks file not found at $marks. The mask comp pipeline needs this to ship with the release; without it end users get a world-anchored mask."
}
Copy-Item $marks -Destination (Join-Path $staging "plugins/Subnautica2HeadTracking.marks.txt")

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

# Nexus ZIP: ships both deploy-path subtrees so a single extract at the game's
# package root populates whichever build the user has. Steam goes under
# Subnautica2\Binaries\Win64\; Game Pass / Xbox goes under
# Subnautica2\Binaries\WinGDK\. The unused folder just stays unused on the
# other platform - the WinGDK exe never loads the Win64 dxgi.dll and vice
# versa. Nexus users still copy %SystemRoot%\System32\dxgi.dll to
# dxgi_orig.dll themselves; documented in the bundled README.
foreach ($subdir in "Subnautica2\Binaries\Win64", "Subnautica2\Binaries\WinGDK") {
    $deployDir = Join-Path $nexusStaging $subdir
    New-Item -ItemType Directory -Path $deployDir -Force | Out-Null
    Copy-Item $dxgiProxy -Destination $deployDir
    if (Test-Path $ini) { Copy-Item $ini -Destination $deployDir }
    Copy-Item $marks -Destination (Join-Path $deployDir "Subnautica2HeadTracking.marks.txt")
}
$nexusReadme = Join-Path $projectDir "scripts/nexus-readme.md"
if (Test-Path $nexusReadme) { Copy-Item $nexusReadme -Destination (Join-Path $nexusStaging "README.md") }
$nexusZip = Join-Path $releaseDir "Subnautica2HeadTracking-v$version-nexus.zip"
Compress-Archive -Path (Join-Path $nexusStaging "*") -DestinationPath $nexusZip -Force
Write-Host "Wrote $nexusZip" -ForegroundColor Green
