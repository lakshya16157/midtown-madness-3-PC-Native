@echo off
rem Vulkan test build with a per-frame timing file: logs\bench-vulkan.csv (overwritten each run).
setlocal
cd /d "%~dp0"
set "RECOMP_SDL_GPU_DRIVER=vulkan"
set "MM3_SETTINGS_OVERLAY=1"
set "RECOMP_FRAME_CSV=%~dp0logs\bench-vulkan.csv"
set "RECOMP_D3D_PROFILE=1"
rem Prints the shader states still drawn with the generic shader ([SDLGPU_SPEC] generic lines, for src/shaders/mm3_ps_states.txt).
set "RECOMP_SDLGPU_SPEC=log"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\play.ps1" -NoLauncher -Exe "%~dp0build-vulkan\RelWithDebInfo\mm3_recomp.exe" %*
exit /b %ERRORLEVEL%
