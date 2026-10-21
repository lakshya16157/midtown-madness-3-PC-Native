@echo off
setlocal
cd /d "%~dp0"
set "MM3_PROFILE_SHELL=%~dp0tools\bin\powershell\pwsh.exe"
if not exist "%MM3_PROFILE_SHELL%" set "MM3_PROFILE_SHELL=powershell.exe"
"%MM3_PROFILE_SHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\profile-running-game.ps1" %*
pause
