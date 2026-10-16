<# Start an interactive game with detailed diagnostics and automatic hang evidence. #>
[CmdletBinding()]
param([string]$Exe, [switch]$Light)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$prefix = if ($Light) { 'diagnostics-' } else { 'full-diagnostics-' }
$run = Join-Path $repo ('logs\' + $prefix + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $run | Out-Null
$settings = @{
    RECOMP_HITCH_TRACE='1'; RECOMP_D3D_PROFILE='1'; RECOMP_FRAME_CSV=(Join-Path $run 'frames.csv')
    RECOMP_PB_CAPTURE=(Join-Path $run 'parser'); RECOMP_PB_WRAP_TRACE='1'
    RECOMP_FILE_DIAGNOSTICS='1'; RECOMP_FILE_PROBE_TRACE='1'
    RECOMP_FILE_READ_CPU_TRACE='1'
    RECOMP_PRODUCER_TRACE='1'
    RECOMP_TEXTURE_STREAM_TRACE='1'; RECOMP_ASYNC_WAIT_TRACE='1'
    RECOMP_CS_TRACE_CONTENTION='1'; RECOMP_CS_COUNTERS='1'
    RECOMP_KERNEL_SUMMARY='1'; RECOMP_KERNEL_LOG_BUDGET='2000'
    MM3_BUILDING_TRACE='1'
}
if ($Light) {
    # Retain frame/hitch measurements and the passive hang observer while
    # keeping high-volume tracing out of a user's performance test. Producer
    # samples only log gaps >= 40 ms and include cycle/read counters.
    foreach ($key in @($settings.Keys)) {
        if ($key -notin @('RECOMP_HITCH_TRACE', 'RECOMP_D3D_PROFILE', 'RECOMP_FRAME_CSV',
                         'RECOMP_PRODUCER_TRACE')) {
            $settings[$key] = $null
        }
    }
}
$previous = @{}
$monitor = $null
try {
    foreach ($key in $settings.Keys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        if ($null -eq $settings[$key]) {
            # PowerShell can bind a null hashtable value as an empty string.
            # Presence-only native getenv checks enable tracing even for "".
            Remove-Item -LiteralPath ("Env:" + $key) -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($key, $settings[$key], 'Process')
        }
    }
    $settings | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'settings.json') -Encoding utf8
    $python = Join-Path $repo 'tools\venv\Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $python)) { $python = (Get-Command python.exe).Source }
    $watch = Join-Path $repo 'tools\diagnostics\watch_game.py'
    $monitor = Start-Process -FilePath $python -WindowStyle Hidden -PassThru -WorkingDirectory $repo `
        -ArgumentList @(('"' + $watch + '"'), '--parent-pid', "$PID", '--output-dir', ('"' + $run + '"')) `
        -RedirectStandardOutput (Join-Path $run 'monitor.log') -RedirectStandardError (Join-Path $run 'monitor-errors.log')
    Write-Host "Detailed diagnostics: $run"
    # The persisted log contains every record; a hidden console echo needlessly
    # adds synchronous host I/O during the capture.
    $launch = @{Keyboard=$true; NoConsoleLog=$true}
    if ($Exe) { $launch.Exe = $Exe }
    & (Join-Path $PSScriptRoot 'play.ps1') @launch
} finally {
    foreach ($key in $previous.Keys) {
        if ($null -eq $previous[$key]) {
            Remove-Item -LiteralPath ("Env:" + $key) -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process')
        }
    }
}
