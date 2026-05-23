#Requires -Version 5.1
# Fully unattended. `pixi run release <major|minor|patch|X.Y.Z>` is the
# authorization - there is no confirmation gate. Preconditions (on main,
# clean tree, tag absent, valid semver) are the safety net; any failure
# exits non-zero with a one-line diagnostic.
param([string]$Version)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Error "Usage: pixi run release <major|minor|patch|X.Y.Z>"
    exit 1
}

$pixiPath      = Join-Path $projectDir "pixi.toml"
$changelogPath = Join-Path $projectDir "CHANGELOG.md"
$installCmdPath = Join-Path $projectDir "scripts/install.cmd"

# 1. Resolve + validate version against the canonical source (pixi.toml).
$pixiContent = Get-Content $pixiPath -Raw
if ($pixiContent -notmatch '(?m)^version\s*=\s*"([^"]+)"') {
    throw "No version field found in $pixiPath"
}
$currentVersion = $matches[1]
$newVersion = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
if (-not (Test-SemanticVersion -Version $newVersion)) {
    throw "Resolved version '$newVersion' is not valid semver (X.Y.Z)."
}
Write-Host "Releasing v$newVersion (current v$currentVersion)" -ForegroundColor Cyan

# 2. Preconditions - fail fast, never prompt.
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne "main") { throw "Releases must run on 'main' (currently on '$branch')." }
if (-not (Test-CleanGitStatus)) { throw "Working tree is not clean. Commit or stash first." }
if (Test-GitTagExists -Tag "v$newVersion") { throw "Tag v$newVersion already exists." }

# 3. Bump the version in the canonical source.
$pixiContent = $pixiContent -replace '(?m)^(version\s*=\s*")[^"]+(")', "`${1}$newVersion`${2}"
$pixiContent | Set-Content $pixiPath -NoNewline

# 3b. Mirror the bump into scripts/install.cmd's MOD_VERSION. release.yml
#     hard-fails the release if the tag version and install.cmd disagree, so
#     these must move together. -Raw + a digits-only regex keeps the file's
#     CRLF endings intact (a .cmd silently fails on Windows if rewritten LF).
$installContent = Get-Content $installCmdPath -Raw
if ($installContent -notmatch '(?m)^set "MOD_VERSION=[0-9]+\.[0-9]+\.[0-9]+"') {
    throw "No MOD_VERSION line found in $installCmdPath"
}
$installContent = $installContent -replace '(?m)^(set "MOD_VERSION=)[0-9]+\.[0-9]+\.[0-9]+(")', "`${1}$newVersion`${2}"
$installContent | Set-Content $installCmdPath -NoNewline

# 4. Release-config build.
Write-Host "Building release..." -ForegroundColor Cyan
pixi run build
if ($LASTEXITCODE -ne 0) { throw "Build failed; aborting release." }

# 5. Generate the changelog entry from commits since the last tag.
New-ChangelogFromCommits -ChangelogPath $changelogPath -Version $newVersion | Out-Null

# 6. Commit the version bump + changelog. "Release v..." matches the
#    build.yml skip guard so CI doesn't double-build this commit.
git add $pixiPath $changelogPath $installCmdPath
if ($LASTEXITCODE -ne 0) { throw "git add failed." }
git commit -m "Release v$newVersion"
if ($LASTEXITCODE -ne 0) { throw "git commit failed." }

# 7 + 8. Annotated tag, then push commits + tag (triggers release.yml).
New-ReleaseTag -Version $newVersion -Message "Release v$newVersion" -Branch "main"

Write-Host "Released v$newVersion. CI release workflow will publish the ZIPs." -ForegroundColor Green
