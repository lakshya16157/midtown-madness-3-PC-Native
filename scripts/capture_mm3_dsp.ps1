<#
.SYNOPSIS
Capture MM3's own GP/EP audio programs for the next native build.
.DESCRIPTION
Runs one owned, hidden and muted game process with isolated saves, preferences
and logs. Game data is reused through a temporary directory junction. Captured
programs are derived game data and stay under ignored game_files\dsp.
Setup should rebuild when the result has Captured=$true. Existing valid
programs are reused unless -Force is supplied. The executable must include
MM3_DSP_CAPTURE, RECOMP_DSP_INTERPRETER and RECOMP_DSP_CAPTURE_ALL support.
ReplayDirectory optionally retains one complete version-3 frame per processor
for full state and DMA/FIFO replay with the matching build. These are startup
frames and do not establish representative gameplay audio coverage.
#>
[CmdletBinding()]
param(
    [string]$Executable,
    [string]$ProgramDirectory = 'game_files\dsp',
    [ValidateRange(1, 180)][int]$TimeoutSeconds = 120,
    [string]$ReplayDirectory,
    [ValidateRange(0, 1000000)][int]$ReplayFrameIndex = 2048,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$programBytes = 4096 * 4

function Get-ProgramInfo([string]$Path) {
    try {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
        $bytes = [IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -ne $programBytes) { return $null }
        $nonzero = 0
        for ($at = 0; $at -lt $bytes.Length; $at += 4) {
            if ($bytes[$at + 3] -ne 0) { return $null }
            if ($bytes[$at] -bor $bytes[$at + 1] -bor $bytes[$at + 2]) { ++$nonzero }
        }
        if (-not $nonzero) { return $null }
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $hash = [BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant() }
        finally { $hasher.Dispose() }
        [pscustomobject]@{ Path = $Path; Bytes = $bytes.Length; ExecutedWords = $nonzero; SHA256 = $hash }
    } catch [IO.IOException] { return $null }
}
function Stop-OwnedProcess([Diagnostics.Process]$Owned) {
    $Owned.Refresh()
    if (-not $Owned.HasExited) {
        try { $Owned.Kill() }
        catch { $Owned.Refresh(); if (-not $Owned.HasExited) { throw } }
    }
    if (-not $Owned.WaitForExit(10000)) { throw "Owned DSP capture process $($Owned.Id) did not stop." }
}
function Get-ReplayInfo([string]$Path) {
    try {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
        $bytes = [IO.File]::ReadAllBytes($Path)
        if ($bytes.Length -lt 32 -or $bytes.Length -gt 64MB) { return $null }
        if ([BitConverter]::ToUInt32($bytes, 0) -ne 0x44535046 -or
            [BitConverter]::ToUInt32($bytes, 4) -ne 3) { return $null }
        $stateBytes = [BitConverter]::ToUInt32($bytes, 8)
        if (-not $stateBytes -or $stateBytes -gt 1MB) { return $null }
        [long]$at = 12 + $stateBytes
        while ($at + 20 -le $bytes.Length) {
            $kind = [BitConverter]::ToUInt32($bytes, $at)
            $address = [BitConverter]::ToUInt32($bytes, $at + 4)
            $length = [BitConverter]::ToUInt32($bytes, $at + 8)
            $direction = [BitConverter]::ToUInt32($bytes, $at + 12)
            $dmaError = [BitConverter]::ToUInt32($bytes, $at + 16)
            $at += 20
            if (-not $kind) {
                if ($address -or $length -or $direction -or $dmaError -or
                    $at + $stateBytes -ne $bytes.Length) { return $null }
                $hasher = [Security.Cryptography.SHA256]::Create()
                try { $hash = [BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant() }
                finally { $hasher.Dispose() }
                return [pscustomobject]@{ Path = $Path; Bytes = $bytes.Length; Version = 3; StateBytes = $stateBytes; SHA256 = $hash }
            }
            if ($kind -gt 2 -or $direction -gt 1 -or $dmaError -gt 1) { return $null }
            if (-not $dmaError) { $at += $length }
        }
        return $null
    } catch [IO.IOException] { return $null }
}
function Publish-Files([object[]]$Files, [string]$Directory) {
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    foreach ($file in $Files) {
        $target = Join-Path $Directory ([IO.Path]::GetFileName($file.Path))
        $staged = $target + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
        try {
            [IO.File]::Copy($file.Path, $staged, $false)
            # PowerShell converts an untyped $null string argument to "".
            # File.Replace needs a real null when no backup path is wanted.
            if ([IO.File]::Exists($target)) { [IO.File]::Replace($staged, $target, [NullString]::Value) }
            else { [IO.File]::Move($staged, $target) }
        } finally {
            if ([IO.File]::Exists($staged)) { [IO.File]::Delete($staged) }
        }
    }
}

$destination = if ([IO.Path]::IsPathRooted($ProgramDirectory)) {
    [IO.Path]::GetFullPath($ProgramDirectory)
} else { [IO.Path]::GetFullPath((Join-Path $repo $ProgramDirectory)) }
$existing = @('gp', 'ep' | ForEach-Object { Get-ProgramInfo (Join-Path $destination "$_.p.bin") } |
    Where-Object { $null -ne $_ })
if (-not $Force -and -not $ReplayDirectory -and $existing.Count -eq 2) {
    return [pscustomobject]@{ Captured = $false; ProgramDirectory = $destination; Programs = $existing; ProcessId = $null; DurationSeconds = 0 }
}
if (-not $Executable) {
    $Executable = @('build\RelWithDebInfo\mm3_recomp.exe', 'build\Release\mm3_recomp.exe',
                    'build\current\RelWithDebInfo\mm3_recomp.exe') |
        ForEach-Object { Join-Path $repo $_ } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        ForEach-Object { Get-Item -LiteralPath $_ } |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Executable) { throw 'Build mm3_recomp.exe before capturing the DSP programs.' }
$exe = if ([IO.Path]::IsPathRooted($Executable)) { $Executable } else { Join-Path $repo $Executable }
$exe = (Resolve-Path -LiteralPath $exe).Path
$gameFiles = Join-Path $repo 'game_files'
if (-not (Test-Path -LiteralPath (Join-Path $gameFiles 'default.xbe') -PathType Leaf)) {
    throw 'Extract the supported game disc before capturing the DSP programs.'
}
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$scratch = [IO.Path]::GetFullPath((Join-Path $temporaryRoot ('MM3DspCapture-' + [Guid]::NewGuid().ToString('N'))))
$gameLink = Join-Path $scratch 'game_files'
$process = $null
$guard = $null
$ownsGuard = $false
$stdout = $null
$stderr = $null
$clock = [Diagnostics.Stopwatch]::StartNew()
$captured = $false
$programs = @()
$replays = @()
$replayDestination = if ($ReplayDirectory) {
    if ([IO.Path]::IsPathRooted($ReplayDirectory)) { [IO.Path]::GetFullPath($ReplayDirectory) }
    else { [IO.Path]::GetFullPath((Join-Path $repo $ReplayDirectory)) }
} else { $null }
$ownedStopped = $true
try {
    $guard = [Threading.Mutex]::new($false, 'Local\MM3RecompSharedCacheLauncher')
    try { $ownsGuard = $guard.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $ownsGuard = $true }
    if (-not $ownsGuard) { throw 'Another MM3 launcher is active. Finish that session before DSP capture.' }
    $running = @(Get-Process -Name mm3_recomp -ErrorAction SilentlyContinue)
    if ($running.Count) { throw "Close the running MM3 game before DSP capture (PID $($running.Id -join ', '))." }

    New-Item -ItemType Directory -Path $scratch | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $scratch 'programs') | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $scratch 'save') | Out-Null
    New-Item -ItemType Junction -Path $gameLink -Target $gameFiles | Out-Null
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exe
    $info.WorkingDirectory = $scratch
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # Only the child environment changes; invoking PowerShell keeps every
    # original variable even when startup, capture or cleanup fails.
    foreach ($name in @($info.EnvironmentVariables.Keys)) {
        if ($name -like 'MM3_*' -or $name -like 'RECOMP_*') { $info.EnvironmentVariables.Remove($name) }
    }
    $settings = @{
        MM3_LAUNCHER = '0'; MM3_DSP_CAPTURE = '1'
        MM3_SAVE_DIR = (Join-Path $scratch 'save')
        MM3_DISPLAY_CONFIG = (Join-Path $scratch 'display.ini')
        MM3_LAUNCHER_CONFIG = (Join-Path $scratch 'launcher.ini')
        MM3_PERFORMANCE_PROFILE = 'LowPower'; MM3_LOG_UNBUFFERED = '1'
        RECOMP_PB_EXEC = '1'; RECOMP_USB = '1'; RECOMP_KEYBOARD = '0'
        RECOMP_APU_DSP_ACK = '0x80458810'; RECOMP_APU_DSP = '1'
        RECOMP_DSP_INTERPRETER = '1'; RECOMP_DSP_CAPTURE_ALL = '1'
        RECOMP_AUDIO_MUTE = '1'; RECOMP_NET = '0'
        RECOMP_DSP_PROGRAM_DIR = (Join-Path $scratch 'programs')
        RECOMP_RENDER_SCALE = '1'; RECOMP_MSAA = '1'; RECOMP_ANISO = '1'
        RECOMP_DRAW_DISTANCE = '1'; RECOMP_VIEW_DISTANCE = '1'
        RECOMP_FPS_LIMIT = '30'; RECOMP_ASPECT = '4:3'
        RECOMP_SHADER_CACHE_DIR = (Join-Path $scratch 'shaders')
    }
    if ($replayDestination) {
        $settings.RECOMP_DSP_FRAME_DUMP = Join-Path $scratch 'replay'
        $settings.RECOMP_DSP_FRAME_INDEX = [string]$ReplayFrameIndex
    }
    foreach ($name in $settings.Keys) { $info.EnvironmentVariables[$name] = $settings[$name] }
    $process = [Diagnostics.Process]::Start($info)
    if (-not $process) { throw 'Could not start the DSP capture process.' }
    $ownedStopped = $false
    $ownedId = $process.Id
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    Write-Verbose "Capturing DSP programs with owned PID $ownedId."
    while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $programs = @('gp', 'ep' | ForEach-Object { Get-ProgramInfo (Join-Path $settings.RECOMP_DSP_PROGRAM_DIR "$_.p.bin") } |
            Where-Object { $null -ne $_ })
        if ($replayDestination) {
            $replays = @('gp', 'ep' | ForEach-Object { Get-ReplayInfo (Join-Path $scratch "replay.$_.frame.bin") } |
                Where-Object { $null -ne $_ })
        }
        if ($programs.Count -eq 2 -and (-not $replayDestination -or $replays.Count -eq 2)) { $captured = $true; break }
        if ($process.WaitForExit(100)) { break }
    }
    Stop-OwnedProcess $process
    $ownedStopped = $true
    # A short-lived child may finish both writes during WaitForExit's last
    # 100 ms interval. Validate once more after its handles are closed.
    if (-not $captured) {
        $programs = @('gp', 'ep' | ForEach-Object { Get-ProgramInfo (Join-Path $settings.RECOMP_DSP_PROGRAM_DIR "$_.p.bin") } |
            Where-Object { $null -ne $_ })
        if ($replayDestination) {
            $replays = @('gp', 'ep' | ForEach-Object { Get-ReplayInfo (Join-Path $scratch "replay.$_.frame.bin") } |
                Where-Object { $null -ne $_ })
        }
        $captured = $programs.Count -eq 2 -and (-not $replayDestination -or $replays.Count -eq 2)
    }
    if (-not $captured) {
        $tail = ($stderr.GetAwaiter().GetResult() -split "\r?\n" | Select-Object -Last 12) -join [Environment]::NewLine
        $reason = if ($clock.Elapsed.TotalSeconds -ge $TimeoutSeconds) { "Timed out after $TimeoutSeconds seconds" } else { "Game exited with code $($process.ExitCode)" }
        $expected = if ($replayDestination) { 'both complete DSP programs and replay frames' } else { 'both complete DSP programs' }
        throw ($reason + " before $expected were captured. Existing programs were preserved." + [Environment]::NewLine + $tail)
    }
    Publish-Files $programs $destination
    if ($replayDestination) {
        Publish-Files $replays $replayDestination
        $replays = @('gp', 'ep' | ForEach-Object { Get-ReplayInfo (Join-Path $replayDestination "replay.$_.frame.bin") } |
            Where-Object { $null -ne $_ })
    }
    $published = @('gp', 'ep' | ForEach-Object { Get-ProgramInfo (Join-Path $destination "$_.p.bin") } |
        Where-Object { $null -ne $_ })
    if ($published.Count -ne 2) { throw 'The captured DSP files could not be published completely.' }
    return [pscustomobject]@{
        Captured = $true; ProgramDirectory = $destination; Programs = $published
        ReplayDirectory = $replayDestination; ReplayFrames = $replays
        ReplayFrameIndex = $ReplayFrameIndex
        ProcessId = $ownedId; DurationSeconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
    }
} finally {
    if ($null -ne $process) {
        try { Stop-OwnedProcess $process; $ownedStopped = $true }
        catch { $ownedStopped = $false; Write-Warning "Owned DSP process cleanup failed: $_" }
        finally { $process.Dispose() }
    }
    if ($null -ne $guard) {
        if ($ownsGuard) { $guard.ReleaseMutex() }
        $guard.Dispose()
    }
    # Unlink the junction without following it before recursive cleanup.
    # Validate the absolute target again and preserve unexpected links.
    if ((Test-Path -LiteralPath $scratch) -and $ownedStopped) {
        $resolved = [IO.Path]::GetFullPath($scratch)
        if (-not $resolved.StartsWith($temporaryRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($resolved) -notmatch '^MM3DspCapture-[a-f0-9]{32}$' -or
            ((Get-Item -LiteralPath $scratch -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            Write-Warning "DSP capture cleanup refused an unexpected temporary path: $scratch"
        } else {
            if (Test-Path -LiteralPath $gameLink) {
                if (-not ((Get-Item -LiteralPath $gameLink -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                    throw "DSP capture game junction changed; scratch was preserved: $scratch"
                }
                [IO.Directory]::Delete($gameLink)
            }
            $links = @(Get-ChildItem -LiteralPath $scratch -Force -Recurse |
                Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
            if ($links.Count) { Write-Warning "DSP capture scratch contains an unexpected link; preserved: $scratch" }
            else { Remove-Item -LiteralPath $scratch -Recurse -Force }
        }
    } elseif ((Test-Path -LiteralPath $scratch) -and -not $ownedStopped) {
        Write-Warning "DSP capture scratch preserved for the process that could not be stopped: $scratch"
    }
}
