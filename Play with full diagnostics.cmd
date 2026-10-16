@echo off
setlocal
cd /d "%~dp0"
set "MM3_LOG_SHELL=%~dp0tools\bin\powershell\pwsh.exe"
if not exist "%MM3_LOG_SHELL%" set "MM3_LOG_SHELL=powershell.exe"
"%MM3_LOG_SHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\play-diagnostics.ps1" %*
set "MM3_LAUNCH_EXIT=%ERRORLEVEL%"
if not "%MM3_LAUNCH_EXIT%"=="0" pause
exit /b %MM3_LAUNCH_EXIT%
