@echo off
rem Normal DirectX 11 game with a per-frame timing file: logs\bench-dx11.csv (overwritten each run).
setlocal
cd /d "%~dp0"
set "RECOMP_FRAME_CSV=%~dp0logs\bench-dx11.csv"
set "RECOMP_D3D_PROFILE=1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\play.ps1" -NoLauncher -Exe "%~dp0build\RelWithDebInfo\mm3_recomp.exe" %*
exit /b %ERRORLEVEL%
