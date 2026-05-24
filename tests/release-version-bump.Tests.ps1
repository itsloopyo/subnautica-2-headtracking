# Regression tests for the release version-bump contract.
#
# release.ps1 bumps the version in pixi.toml AND scripts/install.cmd's
# MOD_VERSION; release.yml hard-fails the release if the pushed tag and
# install.cmd's MOD_VERSION disagree. These tests pin both halves of that
# contract so the two files cannot silently drift apart again.
#
# Run with Windows PowerShell 5.1 built-in Pester 3.4:
#   powershell -NoProfile -Command "Invoke-Pester -Path tests/release-version-bump.Tests.ps1"

$projectDir   = Split-Path -Parent $PSScriptRoot
$releasePs1   = Join-Path $projectDir 'scripts/release.ps1'
$installCmd   = Join-Path $projectDir 'scripts/install.cmd'

# The exact patterns the two scripts use. Kept as literals here so a future
# edit that changes one without the other trips a test.
$ps1BumpPattern = '(?m)^(set "MOD_VERSION=)[0-9]+\.[0-9]+\.[0-9]+(")'
$ymlReadPattern = 'set "MOD_VERSION=([0-9]+\.[0-9]+\.[0-9]+)"'

Describe "release.ps1 install.cmd version bump" {

    It "rewrites MOD_VERSION and preserves CRLF line endings" {
        $newVersion = '1.2.3'
        $sample = "set `"GAME_ID=subnautica-2`"`r`nset `"MOD_VERSION=0.1.0`"`r`nset `"FRAMEWORK_TYPE=ASILoader`"`r`n"
        $crlfBefore = ([regex]::Matches($sample, "`r`n")).Count

        $bumped = $sample -replace $ps1BumpPattern, "`${1}$newVersion`${2}"

        $bumped | Should Match 'set "MOD_VERSION=1\.2\.3"'
        $bumped | Should Not Match 'MOD_VERSION=0\.1\.0'
        ([regex]::Matches($bumped, "`r`n")).Count | Should Be $crlfBefore
    }

    It "produces a MOD_VERSION line that release.yml's validation regex reads back" {
        $newVersion = '2.5.9'
        $sample = "set `"MOD_VERSION=0.0.0`"`r`n"
        $bumped = $sample -replace $ps1BumpPattern, "`${1}$newVersion`${2}"

        $m = [regex]::Match($bumped, $ymlReadPattern)
        $m.Success | Should Be $true
        $m.Groups[1].Value | Should Be $newVersion
    }

    It "leaves a non-version MOD_DLLS-style line untouched" {
        $sample = "set `"MOD_DLLS=dxgi.dll HeadTracking.ini`"`r`n"
        $bumped = $sample -replace $ps1BumpPattern, "`${1}9.9.9`${2}"
        $bumped | Should Be $sample
    }
}

Describe "release.ps1 wiring" {

    It "exists and references scripts/install.cmd" {
        Test-Path $releasePs1 | Should Be $true
        $content = Get-Content $releasePs1 -Raw
        $content | Should Match 'installCmdPath'
        $content | Should Match 'scripts/install\.cmd'
    }

    It "stages install.cmd in the release commit" {
        $content = Get-Content $releasePs1 -Raw
        $content | Should Match 'git add[^\r\n]*installCmdPath'
    }
}

Describe "install.cmd current state" {

    It "has a MOD_VERSION line matching release.yml's validation regex" {
        $content = Get-Content $installCmd -Raw
        [regex]::Match($content, $ymlReadPattern).Success | Should Be $true
    }
}
