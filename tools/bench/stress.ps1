<#
Repeatable menu and race benchmark with an isolated save copy.
  pwsh tools\bench\stress.ps1 -Save <save template> -Plan <plan.json> [-Tag name]
Autonomous native-menu acquisition followed by repeated fixed-pose stress windows.
The mandatory Plan JSON owns X/Z/heading, conditions and collision requirements.
Retains settings, executable identity, frame CSV and results under
conformance_tmp\bench\<tag>. Unreached scenes and game exits fail the run.
#>
[CmdletBinding()]
param(
    [string]$Exe,
    [Parameter(Mandatory)][string]$Save,
    [Parameter(Mandatory)][string]$Plan,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$')]
    [string]$Tag = ('bench-' + [guid]::NewGuid().ToString('N')),
    [ValidateSet('Balanced', 'Quality', 'LowPower')][string]$Profile = 'Balanced',
    [ValidateSet('Stationary', 'Driving')][string]$Scenario = 'Stationary',
    [switch]$HitchTrace,
    [switch]$PaceAfterPresent,
    [switch]$QuietProfiling,
    [switch]$SampleThreads,
    [ValidateRange(0, 1)][Nullable[int]]$VSync,
    [ValidateRange(1, 16)][Nullable[int]]$MaximumFrameLatency,
    [ValidateRange(1, 60)][int]$FpsLimit,
    [ValidateRange(5, 300)][int]$Secs = 20,
    [ValidateRange(0, 120)][int]$StationarySeconds = 0,
    [ValidateRange(1, 255)][int]$ParticleBudget = 24,
    [ValidateRange(0.5, 8)][double]$RenderScale = 4,
    [ValidateSet(1, 2, 4, 8)][Nullable[int]]$MsaaSamples,
    [ValidateRange(0.5, 4)][Nullable[double]]$DrawDistance,
    [ValidateRange(0.5, 4)][Nullable[double]]$ViewDistance,
    [ValidateSet('4:3', '16:9', '16:10', 'auto')][string]$Aspect = '16:10',
    [ValidateRange(320, 16384)][int]$WindowWidth = 1280,
    [ValidateRange(240, 16384)][int]$WindowHeight = 800,
    [switch]$Fullscreen,
    [string]$ShaderCacheSeed,
    [double]$X, [double]$Z,
    [double]$Heading = 0
)
$ErrorActionPreference = 'Stop'
function Set-ProcessEnv([string]$Name, [object]$Value) {
    if ($null -eq $Value) {
        Remove-Item -LiteralPath ("Env:" + $Name) -ErrorAction SilentlyContinue
    } else {
        [Environment]::SetEnvironmentVariable($Name, [string]$Value, 'Process')
    }
}
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Exe) { $Exe = Join-Path $repo 'build\RelWithDebInfo\mm3_recomp.exe' }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$Save = (Resolve-Path -LiteralPath $Save).Path
$Plan = (Resolve-Path -LiteralPath $Plan).Path
$python = Join-Path $repo 'tools\venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { $python = (Get-Command python.exe).Source }
$stressScript = Join-Path $repo 'tools\bench\stress.py'
$validatedPlan = & $python $stressScript --plan $Plan --validate-plan
if ($LASTEXITCODE -ne 0) { throw 'Stress plan validation failed before launch.' }
$stressPlan = ($validatedPlan -join "`n") | ConvertFrom-Json
if ($stressPlan.scene.city -notin @(0, 1) -or $stressPlan.scene.mode -ne 1 -or
    $stressPlan.scene.time -ne 2 -or $stressPlan.scene.traffic -ne 1 -or $stressPlan.scene.peds -ne 1) {
    throw 'Autonomous acquisition supports Paris (0) or Washington DC (1), Cruise, evening, MAX traffic and pedestrians.'
}
$wantedCity = [int]$stressPlan.scene.city
$cityName = if ($wantedCity -eq 1) { 'Washington DC' } else { 'Paris' }
$X = [double]$stressPlan.coordinates.x
$Z = [double]$stressPlan.coordinates.z
$Heading = [double]$stressPlan.coordinates.heading
$Scenario = 'Driving'
if (Get-CimInstance Win32_Process -Filter "Name = 'mm3_recomp.exe'" | Where-Object { $_.ExecutablePath -eq $Exe }) {
    throw 'Close the existing game before launching a measured isolated stress session.'
}
if (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { throw 'Exe must be a file.' }
if (-not (Test-Path -LiteralPath $Save -PathType Container)) { throw 'Save must be a directory.' }
if ([double]::IsNaN($X) -or [double]::IsInfinity($X) -or [double]::IsNaN($Z) -or [double]::IsInfinity($Z)) {
    throw 'Coordinates must be finite.'
}
if ([double]::IsNaN($Heading) -or [double]::IsInfinity($Heading)) {
    throw 'Heading must be finite.'
}
$explicitPosition = $true
$benchRoot = [IO.Path]::GetFullPath((Join-Path $repo 'conformance_tmp\bench'))
$dir = [IO.Path]::GetFullPath((Join-Path $benchRoot $Tag))
if (-not $dir.StartsWith($benchRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Run directory must be inside conformance_tmp\bench.'
}
if ($dir.StartsWith($Save.TrimEnd('\') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The benchmark directory cannot be inside its save template.'
}
if ($ShaderCacheSeed) {
    $ShaderCacheSeed = (Resolve-Path -LiteralPath $ShaderCacheSeed).Path
    if (-not (Test-Path -LiteralPath $ShaderCacheSeed -PathType Container)) { throw 'ShaderCacheSeed must be a directory.' }
}
if (Test-Path -LiteralPath $dir) { throw "Run directory already exists: $dir" }
New-Item -ItemType Directory -Path $dir -Force | Out-Null
$runSave = Join-Path $dir 'save'
Copy-Item -LiteralPath $Save -Destination $runSave -Recurse
$cachedArchive = Join-Path $Save 'Cache\Data\Data_hd.zip'
if (Test-Path -LiteralPath $cachedArchive -PathType Leaf) {
    (Get-Item -LiteralPath (Join-Path $runSave 'Cache\Data\Data_hd.zip')).CreationTimeUtc =
        (Get-Item -LiteralPath $cachedArchive).CreationTimeUtc
}
$shaderCache = Join-Path $dir 'shader-cache'
New-Item -ItemType Directory -Path $shaderCache | Out-Null
if ($ShaderCacheSeed) {
    Get-ChildItem -LiteralPath $ShaderCacheSeed -Force | Copy-Item -Destination $shaderCache -Recurse
}
$pad = Join-Path $dir 'pad.txt'
[IO.File]::WriteAllText($pad, '')
$csv = Join-Path $dir 'frames.csv'
$log = Join-Path $dir 'stderr.log'
$settings = switch ($Profile) {
    'Quality' { @{ scale='3.0'; msaa='8'; distance='2'; fps=60 } }
    'LowPower' { @{ scale='1.5'; msaa='1'; distance='1'; fps=30 } }
    default { @{ scale='4.0'; msaa='4'; distance='2'; fps=60 } }
}
$settings.scale = $RenderScale.ToString('R', [cultureinfo]::InvariantCulture)
if ($null -ne $MsaaSamples) { $settings.msaa = [string]$MsaaSamples }
$settings.object_distance = if ($null -ne $DrawDistance) {
    $DrawDistance.ToString([Globalization.CultureInfo]::InvariantCulture)
} else { $settings.distance }
$settings.view_distance = if ($null -ne $ViewDistance) {
    $ViewDistance.ToString([Globalization.CultureInfo]::InvariantCulture)
} else { $settings.object_distance }
if (-not $PSBoundParameters.ContainsKey('FpsLimit')) { $FpsLimit = $settings.fps }
$envs = @{
    RECOMP_PB_EXEC='1'; RECOMP_FB_WINDOW='1'; RECOMP_USB='1'; RECOMP_PAD_LIVE=$pad
    RECOMP_AUDIO_PROFILE='1'; RECOMP_AUDIO_HIRES_WAIT=$(if ([Environment]::GetEnvironmentVariable('RECOMP_AUDIO_HIRES_WAIT', 'Process')) { [Environment]::GetEnvironmentVariable('RECOMP_AUDIO_HIRES_WAIT', 'Process') } else { '0' }); RECOMP_COLLISION_PROFILE='1'; RECOMP_PRODUCER_TRACE='1'; RECOMP_FILE_READ_CPU_TRACE='1'
    RECOMP_AUDIO_PROFILE_LIMIT=[Environment]::GetEnvironmentVariable('RECOMP_AUDIO_PROFILE_LIMIT', 'Process')
    RECOMP_APU_DSP_ACK='0x80458810'; MM3_SAVE_DIR=$runSave; MM3_PERFORMANCE_PROFILE=$Profile; MM3_LAUNCHER='0'; MM3_DISPLAY_CONFIG=(Join-Path $dir 'display.ini'); MM3_LAUNCHER_CONFIG=(Join-Path $dir 'launcher.ini')
    RECOMP_ASPECT=$(if ($Aspect -eq 'auto') { $null } else { $Aspect }); RECOMP_WINDOW_SIZE=("{0}x{1}" -f $WindowWidth, $WindowHeight); RECOMP_FULLSCREEN=$(if ($Fullscreen) { '1' } else { '0' }); RECOMP_RENDER_SCALE=$settings.scale
    RECOMP_MSAA=$settings.msaa; RECOMP_DRAW_DISTANCE=$settings.object_distance
    RECOMP_READBACK_POLL_US=[Environment]::GetEnvironmentVariable('RECOMP_READBACK_POLL_US', 'Process')
    RECOMP_READBACK_FENCE=$(if ([Environment]::GetEnvironmentVariable('RECOMP_READBACK_FENCE', 'Process')) { [Environment]::GetEnvironmentVariable('RECOMP_READBACK_FENCE', 'Process') } else { '0' })
    RECOMP_READBACK_IDLE_WAIT=$(if ([Environment]::GetEnvironmentVariable('RECOMP_READBACK_IDLE_WAIT', 'Process')) { [Environment]::GetEnvironmentVariable('RECOMP_READBACK_IDLE_WAIT', 'Process') } else { '0' })
    RECOMP_AUDIO_DSP_HANDOFF=$(if ([Environment]::GetEnvironmentVariable('RECOMP_AUDIO_DSP_HANDOFF', 'Process')) { [Environment]::GetEnvironmentVariable('RECOMP_AUDIO_DSP_HANDOFF', 'Process') } else { '1' })
    RECOMP_SLOW_FRAME_TRACE=$(if ([Environment]::GetEnvironmentVariable('RECOMP_SLOW_FRAME_TRACE', 'Process')) { [Environment]::GetEnvironmentVariable('RECOMP_SLOW_FRAME_TRACE', 'Process') } else { '0' })
    RECOMP_AUDIO_DSP_TRACE=$(if ([Environment]::GetEnvironmentVariable('RECOMP_AUDIO_DSP_TRACE', 'Process')) { [Environment]::GetEnvironmentVariable('RECOMP_AUDIO_DSP_TRACE', 'Process') } else { '0' })
    RECOMP_AUDIO_DSP_TRACE_CPU=$(if ([Environment]::GetEnvironmentVariable('RECOMP_AUDIO_DSP_TRACE_CPU', 'Process')) { [Environment]::GetEnvironmentVariable('RECOMP_AUDIO_DSP_TRACE_CPU', 'Process') } else { '0' })
    RECOMP_VIEW_DISTANCE=$settings.view_distance; RECOMP_ANISO='16'
    RECOMP_FPS_LIMIT="$FpsLimit"; RECOMP_D3D_PROFILE=$(if ($QuietProfiling) { $null } else { '1' }); RECOMP_FRAME_CSV=$csv
    MM3_DEBUG_TELEPORT='1'; MM3_PARTICLE_MAX_OVERDRAW="$ParticleBudget"; RECOMP_SHADER_CACHE='1'
    MM3_ROUTE_DIAGNOSTICS=$(if ([Environment]::GetEnvironmentVariable('MM3_ROUTE_DIAGNOSTICS', 'Process')) { [Environment]::GetEnvironmentVariable('MM3_ROUTE_DIAGNOSTICS', 'Process') } else { '0' })
    MM3_NATIVE_APU_MMIO=$(if ([Environment]::GetEnvironmentVariable('MM3_NATIVE_APU_MMIO', 'Process')) { [Environment]::GetEnvironmentVariable('MM3_NATIVE_APU_MMIO', 'Process') } else { '1' })
    RECOMP_XA2_STARTUP_BUFFERS=$(if ([Environment]::GetEnvironmentVariable('RECOMP_XA2_STARTUP_BUFFERS', 'Process')) { [Environment]::GetEnvironmentVariable('RECOMP_XA2_STARTUP_BUFFERS', 'Process') } else { '5' })
    RECOMP_SHADER_CACHE_DIR=$shaderCache; RECOMP_PAD_PRESS=$null; RECOMP_PAD_SCRIPT=$null
    RECOMP_D3D_MAX_FRAME_LATENCY=$(if ($PSBoundParameters.ContainsKey('MaximumFrameLatency')) { "$MaximumFrameLatency" } else { [Environment]::GetEnvironmentVariable('RECOMP_D3D_MAX_FRAME_LATENCY', 'Process') }); RECOMP_VSYNC=$(if ($PSBoundParameters.ContainsKey('VSync')) { "$VSync" } else { '0' }); RECOMP_PACE_AFTER_PRESENT=$(if ($PaceAfterPresent) { '1' } else { $null }); RECOMP_DSP_PROFILE=$(if ($QuietProfiling) { $null } else { '1' }); RECOMP_HITCH_TRACE=$(if ($HitchTrace) { '1' } else { $null }); RECOMP_PRESENT_CAPTURE=(Join-Path $dir 'present'); RECOMP_CAPTURE_SURFACE=$null
}
$previous = @{}
$p = $null
$gpuProcess = $null
$threadSampler = $null
$threadSamplerStop = Join-Path $dir 'thread-sampling.stop'
$windows = @()
$teleport = Join-Path $repo 'tools\powershell\Send-MM3Teleport.ps1'
function Assert-GameRunning {
    if ($p.HasExited) { throw "Game exited with code $($p.ExitCode); see $log" }
}
function Wait-Game([double]$seconds) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $seconds) { Assert-GameRunning; Start-Sleep -Milliseconds 250 }
    Assert-GameRunning
}
function Press-Pad([string]$button, [double]$after) {
    Assert-GameRunning
    [IO.File]::AppendAllText($pad, "$($button):150`n")
    Wait-Game (0.2 + $after)
}
function Qpc-Milliseconds { [Diagnostics.Stopwatch]::GetTimestamp() * 1000.0 / [Diagnostics.Stopwatch]::Frequency }
function Measure-Window([string]$name, [int]$seconds) {
    $p.Refresh()
    $cpuStart = $p.TotalProcessorTime.TotalMilliseconds
    $begin = Qpc-Milliseconds
    Wait-Game $seconds
    $end = Qpc-Milliseconds
    $p.Refresh()
    $script:windows += @{ name=$name; start=$begin; end=$end
        cpu_ms=$p.TotalProcessorTime.TotalMilliseconds - $cpuStart
        logical_processors=[Environment]::ProcessorCount }
}

# Owned-process reads validate native menu inputs; no process-memory writes.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MM3StutterReadOnly {
 [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, UIntPtr count, out UIntPtr read);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 public static byte[] Read(int pid, long address, int count) {
  IntPtr handle=OpenProcess(0x10,false,pid);
  if(handle==IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
  try { byte[] data=new byte[count]; UIntPtr read;
   if(!ReadProcessMemory(handle,new IntPtr(address),data,(UIntPtr)count,out read) || read.ToUInt64()!=(ulong)count) throw new System.ComponentModel.Win32Exception();
   return data;
  } finally {CloseHandle(handle);}
 }
}
"@
$script:guestOffset = $null
function Read-GuestBytes([uint32]$address, [int]$count) {
 if ($null -eq $script:guestOffset) {
  $memoryMatch=[regex]::Match((Get-Content -LiteralPath $log -Raw),'offset \+([0-9]+) from Xbox base')
  if (-not $memoryMatch.Success) { throw 'Guest memory offset not published.' }
  $script:guestOffset=[long]::Parse($memoryMatch.Groups[1].Value)
 }
 return ,([MM3StutterReadOnly]::Read($p.Id,$script:guestOffset+$address,$count))
}
function Read-GuestWord([uint32]$address) { [BitConverter]::ToUInt32((Read-GuestBytes $address 4),0) }
function Read-GuestFloat([uint32]$address) { [BitConverter]::ToSingle((Read-GuestBytes $address 4),0) }
function Get-MenuPickers {
 $configuration=Read-GuestWord 0x3C5CB4
 if ($configuration -lt 0x10000 -or $configuration -gt 0x07FFF000) { throw 'Native menu configuration is unavailable.' }
 $bytes=Read-GuestBytes ($configuration+0x150) 28
 return [ordered]@{city=[BitConverter]::ToInt32($bytes,0);weather=[BitConverter]::ToInt32($bytes,4);time=[BitConverter]::ToInt32($bytes,8);season=[BitConverter]::ToInt32($bytes,12);peds=[BitConverter]::ToInt32($bytes,16);traffic=[BitConverter]::ToInt32($bytes,20);cops=[BitConverter]::ToInt32($bytes,24)}
}
function Set-NativeCruisePicker([string]$name,[int]$wanted) {
 $keys=@('weather','time','season','peds','traffic','cops')
 for ($row=0;$row -lt 6;$row++) {
  $before=Get-MenuPickers
  Press-Pad 'right' 0.15
  $after=Get-MenuPickers
  $changed=@($keys | Where-Object {$before[$_] -ne $after[$_]})
  $inverse='left'
  if ($changed.Count -eq 0) {
   Press-Pad 'left' 0.15
   $after=Get-MenuPickers
   $changed=@($keys | Where-Object {$before[$_] -ne $after[$_]})
   $inverse='right'
  }
  if ($changed.Count -ne 1) { throw "Native Cruise focus probe changed $($changed.Count) pickers; scene setup is invalid." }
  $focused=$changed[0]
  Press-Pad $inverse 0.15
  $restored=Get-MenuPickers
  foreach ($key in $keys) { if ($restored[$key] -ne $before[$key]) { throw "Native picker probe failed to restore $key." } }
  Write-Output "Native Cruise row $focused, requested $name=$wanted"
  if ($focused -eq $name) {
   for ($step=0;$step -lt 6;$step++) {
    $values=Get-MenuPickers
    if ($values[$name] -eq $wanted) { return }
    Press-Pad $(if ($values[$name] -lt $wanted) {'right'} else {'left'}) 0.15
    $next=Get-MenuPickers
    foreach ($key in $keys) { if ($key -ne $name -and $next[$key] -ne $values[$key]) { throw "Changing $name unexpectedly changed $key." } }
   }
   throw "Native $name did not reach $wanted."
  }
  Press-Pad 'down' 0.15
 }
 throw "Native Cruise picker $name was not found in six rows."
}
function Get-CommittedScenario {
 [ordered]@{city=(Read-GuestWord 0x407C18);mode=(Read-GuestWord 0x407C1C);time=(Read-GuestWord 0x407C30);traffic=(Read-GuestFloat 0x407C64);peds=(Read-GuestFloat 0x407C60);road_car_density=(Read-GuestFloat 0x3A85FC);area_car_density=(Read-GuestFloat 0x3A61EC);density_multiplier=(Read-GuestFloat 0x3A7918)}
}
function Assert-CommittedScenario {
 $state=Get-CommittedScenario
 if ($state.city -ne $wantedCity -or $state.mode -ne 1 -or $state.time -ne 2 -or
     [Math]::Abs($state.traffic-1.0) -gt 0.00001 -or
     [Math]::Abs($state.peds-1.0) -gt 0.00001 -or
     [Math]::Abs($state.road_car_density-0.12) -gt 0.00001 -or
     [Math]::Abs($state.area_car_density-0.1) -gt 0.00001 -or
     [Math]::Abs($state.density_multiplier-1.1) -gt 0.00001) {
  throw ("Exact $cityName/Cruise/evening/MAX traffic scene failed native validation: "+($state|ConvertTo-Json -Compress))
 }
 return $state
}

try {
    foreach ($key in $envs.Keys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        Set-ProcessEnv $key $envs[$key]
    }
    $p = Start-Process -FilePath $Exe -WorkingDirectory $repo -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $dir 'stdout.log') -RedirectStandardError $log
    foreach ($key in $previous.Keys) { Set-ProcessEnv $key $previous[$key] }
    @{ executable=$Exe; sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash
       process_id=$p.Id; profile=$Profile; scenario=$Scenario; frontend_only=$false; quiet_profiling=[bool]$QuietProfiling; settings=$envs; explicit_position=$explicitPosition; coordinates=@($X, $Z); heading=$Heading
       presentation_request=@{ aspect=$Aspect; window_width=$WindowWidth; window_height=$WindowHeight; fullscreen=[bool]$Fullscreen }
       stationary_control_seconds_requested=$StationarySeconds
       native_thread_sampling_requested=[bool]$SampleThreads
       save_template=$Save; shader_cache_seed=$ShaderCacheSeed; logical_processors=[Environment]::ProcessorCount
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $dir 'run.json')
    Write-Output "Game PID $($p.Id), evidence $dir"
    $intro = [Diagnostics.Stopwatch]::StartNew()
    $frontendReady = $false
    while ($intro.Elapsed.TotalSeconds -lt 120) {
        Wait-Game 0.7
        # Movie-only presentations do not emit guest-flip CSV rows. Detect
        # frontend rendering without requiring the optional movie-trace build.
        if ((Test-Path -LiteralPath $csv) -and (Get-Item -LiteralPath $csv).Length -gt 128) {
            $frontendReady = $true; break
        }
        Press-Pad 'start' 0
    }
    if (-not $frontendReady) { throw 'No frontend guest flips before the startup timeout.' }
    Wait-Game 5
    Press-Pad 'start' 4
    Press-Pad 'a' 6
    Wait-Game 10
    # Native retail tests use Down+A for Single Race, then A for Cruise,
    # A for the requested city. Read-only native values confirm real actions.
    # Captured saved-profile startup is Career jobs; normalize through B,B.
    Press-Pad 'b' 2
    Press-Pad 'b' 2
    Press-Pad 'down' 1
    Press-Pad 'a' 4
    Press-Pad 'a' 4
    $cityPickers=Get-MenuPickers
    for ($cityStep=0;$cityPickers.city -ne $wantedCity -and $cityStep -lt 2;$cityStep++) {
        Press-Pad $(if ($cityPickers.city -lt $wantedCity) {'right'} else {'left'}) 0.3
        $cityPickers=Get-MenuPickers
    }
    if ($cityPickers.city -ne $wantedCity) { throw "Native city selector did not select $cityName." }
    Press-Pad 'a' 4
    # Searches move downward and the native list clamps at Weather. Visit
    # requested rows in their actual displayed order: Traffic, Peds, Cops,
    # Season, Time, Weather; never start a new search below its target.
    Set-NativeCruisePicker 'traffic' 5
    Set-NativeCruisePicker 'peds' 5
    Set-NativeCruisePicker 'cops' 4
    Set-NativeCruisePicker 'time' 2
    $selected=Get-MenuPickers
    if ($selected.city -ne $wantedCity -or $selected.traffic -ne 5 -or $selected.peds -ne 5 -or $selected.cops -ne 4 -or $selected.time -ne 2) { throw 'Native Cruise menu values are invalid.' }
    $selected | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'native-menu-selected.json')
    Press-Pad 'a' 8
    Press-Pad 'a' 8
    Press-Pad 'a' 12
    $position = $null
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        Assert-GameRunning
        try { $position = & $teleport -ProcessId $p.Id } catch { $position = $null }
        if ($position -and $position.available) { break }
        Press-Pad 'a' 9
    }
    if (-not $position -or -not $position.available) { throw 'No live player car; race benchmark is invalid.' }
    if ($null -ne $MaximumFrameLatency) {
        $latencyEvidence = '[D3D11] requested maximum frame latency '+$MaximumFrameLatency+': HRESULT 0x00000000'
        if (-not (Get-Content -LiteralPath $log -Raw).Contains($latencyEvidence)) { throw 'DXGI maximum-frame-latency request did not report success.' }
        Write-Output $latencyEvidence
    }
    $committed=Assert-CommittedScenario
    $committed | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'native-scene-start.json')
    Write-Output ('EXACT SCENARIO VERIFIED '+($committed|ConvertTo-Json -Compress))
    $position | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'position-start.json')
    if ($explicitPosition) {
        $position = & $teleport -ProcessId $p.Id -X $X -Z $Z -Heading $Heading
    }
    $position | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'position.json')
    Wait-Game 4
    if ($SampleThreads) {
        # Intentional diagnostic observer, not an FPS-validation condition.
        # It resumes each sampled thread before writing records and exits on
        # this run's stop file. No debugger or process-memory writes are used.
        $sampler = Join-Path $repo 'tools\diagnostics\sample_threads.py'
        $sampleDir = Join-Path $dir 'thread-samples'
        $threadSampler = Start-Process -FilePath $python -WindowStyle Hidden -PassThru -WorkingDirectory $repo `
            -ArgumentList @(('"'+$sampler+'"'),'capture','--pid',"$($p.Id)",'--exe',('"'+$Exe+'"'),'--output',('"'+$sampleDir+'"'),'--seconds','300','--interval-ms','13','--threads','4','--stop-file',('"'+$threadSamplerStop+'"')) `
            -RedirectStandardOutput (Join-Path $dir 'thread-sampler-output.log') -RedirectStandardError (Join-Path $dir 'thread-sampler-errors.log')
        # Keep the native process handle before exit so its status remains
        # available on Windows PowerShell as well as pwsh.
        $null = $threadSampler.Handle
    }
    if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) {
        $gpuProcess = Start-Process -FilePath nvidia-smi -WindowStyle Hidden -PassThru `
            -ArgumentList @('--query-gpu=utilization.gpu,power.draw', '--format=csv,noheader,nounits', '-lms', '500') `
            -RedirectStandardOutput (Join-Path $dir 'gpu.csv') -RedirectStandardError (Join-Path $dir 'gpu-errors.txt')
    }
    $planFile = Join-Path $dir 'stress-plan-request.json'
    $stressPlan | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $planFile
    & $python $stressScript $dir --plan $planFile --stationary-seconds $StationarySeconds
    $stressExit = $LASTEXITCODE
    if ($stressExit -gt 1) { throw "Stress controller failed; see $dir\stress-report.json" }
    $stressReport = Get-Content -LiteralPath (Join-Path $dir 'stress-report.json') -Raw | ConvertFrom-Json
    if (-not $stressReport.completed -or -not $stressReport.windows.Count) { throw 'Stress windows did not complete.' }
    $windows = @($stressReport.windows)
    (Assert-CommittedScenario) | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'native-scene-end.json')
    Write-Output ("STRESS COMPLETED windows=" + $windows.Count + " verified=" + $stressReport.all_windows_verified)
} finally {
    foreach ($key in $previous.Keys) { Set-ProcessEnv $key $previous[$key] }
    if ($threadSampler -and -not $threadSampler.HasExited) {
        [IO.File]::WriteAllText($threadSamplerStop, 'stop')
        # Do not terminate an observer while it might own a thread suspend.
        [void]$threadSampler.WaitForExit(10000)
    }
    if ($gpuProcess -and -not $gpuProcess.HasExited) { Stop-Process -Id $gpuProcess.Id -Force }
    if ($p) {
        # Let the normal exit flush buffered native diagnostics after the
        # measurement. Forced termination can discard a quiet run's log tail.
        $shutdownMethod = 'already-exited'
        if (-not $p.HasExited) {
            $p.Refresh()
            $shutdownMethod = 'window-close'
            [void]$p.CloseMainWindow()
            if (-not $p.WaitForExit(5000)) {
                $shutdownMethod = 'forced-after-close-timeout'
                Stop-Process -Id $p.Id -Force
            }
        }
        $p.WaitForExit(10000) | Out-Null
        @{method=$shutdownMethod;exit_code=$p.ExitCode} | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $dir 'shutdown.json')
    }
    if ($threadSampler) {
        if (-not $threadSampler.HasExited -and $p -and $p.HasExited) {
            # The target has exited, so no live target thread can remain held.
            if (-not $threadSampler.WaitForExit(10000)) {
                Stop-Process -Id $threadSampler.Id -Force
                [void]$threadSampler.WaitForExit(10000)
            }
        }
        @{exited=$threadSampler.HasExited;exit_code=$(if ($threadSampler.HasExited) { $threadSampler.ExitCode } else { $null })} | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $dir 'thread-sampler-status.json')
    }
    $resolvedSave = [IO.Path]::GetFullPath($runSave)
    if ($resolvedSave -ne [IO.Path]::GetFullPath((Join-Path $dir 'save')) -or
        -not $resolvedSave.StartsWith($benchRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a save outside this benchmark run.'
    }
    Remove-Item -LiteralPath $resolvedSave -Recurse -Force -ErrorAction SilentlyContinue
}
$python = Join-Path $repo 'tools\venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { $python = 'python' }
& $python (Join-Path $repo 'tools\bench\summarize.py') $dir
if ($LASTEXITCODE -ne 0) { throw 'Benchmark summary failed; see retained evidence.' }
if (-not $stressReport.all_windows_verified) { Write-Error 'Stress completed with failed movement/input/collision requirements; see stress-report.json.' }
