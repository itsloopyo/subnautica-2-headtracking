@echo off
:: ============================================
:: Subnautica 2 Head Tracking - Uninstall
:: ============================================
:: Thin shim that dispatches to uninstall.ps1. PowerShell does the work
:: because this mod uninstalls from MULTIPLE installs at once (Steam +
:: Xbox/Game Pass).
::
:: Launcher CLI: uninstall.cmd [GAME_PATH] [/y] [/force]
::   GAME_PATH (optional positional): force a single uninstall target.
::   /y / -y / --yes: non-interactive (skip the trailing pause).
::   /force / --force: discard .backup files instead of restoring them.
:: ============================================
::
:: Deliberately not a thin wrapper over cameraunlock-core's shared uninstall body.
:: It walks the same Steam and Xbox/Game Pass installs install.ps1 deployed to.
:: See install.cmd for the full reasoning.

:: Pinned off for the arg parser below. With delayed expansion on, cmd.exe
:: strips a `!` out of the expanded text of `set "_ARG=%~1"`, so a real game
:: path like C:\Games\Oh! My Game loses the `!`, `if exist` fails, and a valid
:: folder is rejected with exit 2.
setlocal disabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "YES_FLAG="
set "FORCE_FLAG="
set "_GIVEN_PATH="

:parse_args
if "%~1"=="" goto :args_done
set "_ARG=%~1"
if /i "%_ARG%"=="/y"      ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="-y"      ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="--yes"   ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "%_ARG%"=="/force"  ( set "FORCE_FLAG=1" & shift & goto :parse_args )
if /i "%_ARG%"=="--force" ( set "FORCE_FLAG=1" & shift & goto :parse_args )
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

set "_PS_ARGS=-NoProfile -ExecutionPolicy Bypass -File "!SCRIPT_DIR!uninstall.ps1""
if defined _GIVEN_PATH set "_PS_ARGS=!_PS_ARGS! -GivenPath "!_GIVEN_PATH!""
if defined YES_FLAG    set "_PS_ARGS=!_PS_ARGS! -Yes"
if defined FORCE_FLAG  set "_PS_ARGS=!_PS_ARGS! -Force"

powershell !_PS_ARGS!
set "_EC=%errorlevel%"

if not defined YES_FLAG ( echo. & pause )
exit /b %_EC%
