<#
Profile-guided optimisation of mm3_recomp.exe (MSVC). The optimised build
computes exactly what the normal build does; only the machine code is laid
out and inlined for the paths a real play session uses.

  pwsh scripts\pgo.ps1 train     build the instrumented game in build-pgo and
                                 put it where Steam/the .cmd start the game
                                 (the normal exe is kept as *.normal.exe)
  ...play it for a while (it runs slower), then quit the game normally...
  pwsh scripts\pgo.ps1 optimise  collect the training counts, build the
                                 optimised game and install it the same way
  pwsh scripts\pgo.ps1 restore   put the normal build back

The training counts (build-pgo\pgo\*.pgc and the .pgd) stay in build-pgo, so
later code changes can be re-optimised with `optimise` without retraining;
functions that changed since the training run are compiled without profile.
#>
param([Parameter(Mandatory)][ValidateSet('train', 'optimise', 'restore')][string]$Step,
      [int]$Jobs = 8)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Set-Location $repo
. (Join-Path $repo 'scripts\enter-env.ps1') *> $null
$pgoTree = Join-Path $repo 'build-pgo'
$pgoData = Join-Path $pgoTree 'pgo'
$built = Join-Path $pgoTree 'RelWithDebInfo\mm3_recomp.exe'
$played = Join-Path $repo 'build\RelWithDebInfo\mm3_recomp.exe'
$normal = Join-Path $repo 'build\RelWithDebInfo\mm3_recomp.normal.exe'
$runtimeName = 'pgort140.dll'

function Assert-GameClosed {
    if (Get-Process mm3_recomp -ErrorAction SilentlyContinue) { throw 'Quit the game first.' }
}

function Invoke-PgoBuild([string]$mode) {
    (Get-Process -Id $PID).PriorityClass = 'BelowNormal'
    cmake -S $repo -B $pgoTree -G 'Ninja Multi-Config' "-DMM3_PGO=$mode" | Out-Null
    if ($LASTEXITCODE) { throw "configure ($mode) failed" }
    cmake --build $pgoTree --config RelWithDebInfo --target mm3_recomp --parallel $Jobs
    if ($LASTEXITCODE) { throw "build ($mode) failed" }
}

function Install-Built([switch]$WithRuntime) {
    Assert-GameClosed
    if (-not (Test-Path -LiteralPath $normal)) { Copy-Item -LiteralPath $played -Destination $normal }
    Copy-Item -LiteralPath $built -Destination $played -Force
    $runtimeTarget = Join-Path (Split-Path $played) $runtimeName
    if ($WithRuntime) {
        $runtime = Join-Path (Split-Path (Get-Command cl).Source) $runtimeName
        Copy-Item -LiteralPath $runtime -Destination $runtimeTarget -Force
    } elseif (Test-Path -LiteralPath $runtimeTarget) {
        Remove-Item -LiteralPath $runtimeTarget -Force
    }
}

switch ($Step) {
    'train' {
        Invoke-PgoBuild 'GEN'
        Install-Built -WithRuntime
        Write-Host 'Training build installed. Play normally (day and night, both cities if you can), then quit the game.'
    }
    'optimise' {
        Assert-GameClosed
        # The instrumented run writes mm3_recomp!N.pgc beside the exe it ran
        # as (or the profile database, or the game's working folder).
        $counts = @(Get-ChildItem -Path (Split-Path $played), (Split-Path $built), $repo -Filter 'mm3_recomp!*.pgc' -ErrorAction SilentlyContinue)
        foreach ($file in $counts) { Move-Item -LiteralPath $file.FullName -Destination $pgoData -Force }
        if (-not (Get-ChildItem -Path $pgoData -Filter '*.pgc' -ErrorAction SilentlyContinue)) {
            throw "No training counts in $pgoData; play the training build and quit it normally first."
        }
        # New counts are not a build input: remove the exe so the link re-runs.
        if (Test-Path -LiteralPath $built) { Remove-Item -LiteralPath $built -Force }
        Invoke-PgoBuild 'USE'
        Install-Built
        Write-Host 'Optimised build installed.'
    }
    'restore' {
        Assert-GameClosed
        if (-not (Test-Path -LiteralPath $normal)) { throw 'No saved normal build.' }
        Move-Item -LiteralPath $normal -Destination $played -Force
        $runtimeTarget = Join-Path (Split-Path $played) $runtimeName
        if (Test-Path -LiteralPath $runtimeTarget) { Remove-Item -LiteralPath $runtimeTarget -Force }
        Write-Host 'Normal build restored.'
    }
}
