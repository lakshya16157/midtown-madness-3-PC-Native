@echo off
rem Test build: the Linux / Steam Deck renderer (SDL3 GPU) on Vulkan, on Windows.
rem Build it with: cmake -S . -B build-vulkan -G "Ninja Multi-Config" -DXBOXRECOMP_RENDERER=SDL3_GPU
setlocal
cd /d "%~dp0"
set "RECOMP_SDL_GPU_DRIVER=vulkan"
rem F10 opens the in-game Settings window (the one Linux has) instead of the Windows dialog.
set "MM3_SETTINGS_OVERLAY=1"
set "MM3_LOG_SHELL=%~dp0tools\bin\powershell\pwsh.exe"
if not exist "%MM3_LOG_SHELL%" set "MM3_LOG_SHELL=powershell.exe"
"%MM3_LOG_SHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\play.ps1" -Exe "%~dp0build-vulkan\RelWithDebInfo\mm3_recomp.exe" %*
set "MM3_LAUNCH_EXIT=%ERRORLEVEL%"
if not "%MM3_LAUNCH_EXIT%"=="0" pause
exit /b %MM3_LAUNCH_EXIT%
