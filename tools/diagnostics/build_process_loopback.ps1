param([string]$OutputDirectory = 'build/audio-loopback')
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $repository 'scripts/enter-env.ps1')
$output = Join-Path $repository $OutputDirectory
New-Item -ItemType Directory -Path $output -Force | Out-Null
$source = Join-Path $PSScriptRoot 'process_loopback.cpp'
& cl.exe /nologo /std:c++17 /EHsc /W4 /O2 /DUNICODE /D_UNICODE $source "/Fe:$output/process_loopback.exe" "/Fo:$output/process_loopback.obj"
if ($LASTEXITCODE -ne 0) { throw 'Process-specific WASAPI capture helper failed to build.' }
Write-Output (Join-Path $output 'process_loopback.exe')
