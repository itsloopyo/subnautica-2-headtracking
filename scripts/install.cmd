@echo off
:: ============================================
:: Subnautica 2 Head Tracking - Install
:: ============================================
:: Thin shim that dispatches to install.ps1. PowerShell does the actual
:: work because this mod deploys to MULTIPLE installs at once (Steam +
:: Xbox/Game Pass), which the canonical single-path batch template can't
:: express cleanly.
::
:: Launcher CLI: install.cmd [GAME_PATH] [/y]
::   GAME_PATH (optional positional): force a single install target.
::   /y / -y / --yes: non-interactive (skip the trailing pause).
:: ============================================
::
:: Deliberately not a thin wrapper over cameraunlock-core's shared install bodies.
:: install.ps1 deploys to every install it finds at once: Steam and Xbox/Game
:: Pass, whose exe directories are Subnautica2\Binaries\Win64 and
:: Subnautica2\Binaries\WinGDK, where a shared body resolves one game path and
:: deploys there. It also plants %SystemRoot%\System32\dxgi.dll as dxgi_orig.dll
:: for the proxy to forward to, which is not a backup of anything the game shipped
:: and has no equivalent in the shared shim body.

:: Release-contract token: release.ps1 bumps this string, release.yml hard-fails
:: if it disagrees with the pushed tag. The value isn't read at install time
:: (install.ps1 reads the version from pixi.toml inside the ZIP), but the
:: line must stay present and formatted exactly as below.
set "MOD_VERSION=0.6.1"

:: Pinned off for the arg parser below. With delayed expansion on, cmd.exe
:: strips a `!` out of the expanded text of `set "_ARG=%~1"`, so a real game
:: path like C:\Games\Oh! My Game loses the `!`, `if exist` fails, and a valid
:: folder is rejected with exit 2.
setlocal disabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "YES_FLAG="
set "_GIVEN_PATH="

:parse_args
if "%~1"=="" goto :args_done
set "_ARG=%~1"
if /i "%_ARG%"=="/y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="-y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="--yes" ( set "YES_FLAG=1" & shift & goto :parse_args )
if "%_ARG:~0,2%"=="--" ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if "%_ARG:~0,1%"=="/"  ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if "%_ARG:~0,1%"=="-"  ( echo ERROR: unknown flag "%_ARG%" & exit /b 2 )
if not defined _GIVEN_PATH (
    if exist "%_ARG%\" ( set "_GIVEN_PATH=%_ARG%" & shift & goto :parse_args )
)
echo ERROR: unrecognised argument "%_ARG%"
exit /b 2
:args_done

:: Enabled only now the path is captured. `!_PS_ARGS!` is substituted after
:: cmd.exe has finished scanning the line for `&`, `^` and `)`, so those
:: survive in the path too; `%_PS_ARGS%` would be substituted before that scan
:: and hand the shell the path's punctuation as syntax.
setlocal enabledelayedexpansion

set "_PS_ARGS=-NoProfile -ExecutionPolicy Bypass -File "!SCRIPT_DIR!install.ps1""
if defined _GIVEN_PATH set "_PS_ARGS=!_PS_ARGS! -GivenPath "!_GIVEN_PATH!""
if defined YES_FLAG    set "_PS_ARGS=!_PS_ARGS! -Yes"

powershell !_PS_ARGS!
set "_EC=%errorlevel%"

if not defined YES_FLAG ( echo. & pause )
exit /b %_EC%
