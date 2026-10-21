<# Sample the CPU of an already running game (started from Steam or the .cmd)
   to find what the game thread spends its time on. Nothing is launched and
   the game is never modified: each sampled thread is paused for a few
   microseconds to read where it is, then resumed. Results go to
   logs\cpu-profile-<time>. #>
[CmdletBinding()]
param([int]$DelaySeconds = 10, [int]$Seconds = 60)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$games = @(Get-Process mm3_recomp -ErrorAction SilentlyContinue)
if ($games.Count -ne 1) {
    Write-Host 'Start the game first (Steam or the .cmd), drive to where it stutters, then run this again.'
    exit 1
}
$game = $games[0]
$python = Join-Path $repo 'tools\venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { $python = (Get-Command python.exe).Source }
$sampler = Join-Path $repo 'tools\diagnostics\sample_threads.py'
$out = Join-Path $repo ('logs\cpu-profile-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
Write-Host "Go back to the game now. Sampling starts in $DelaySeconds seconds and lasts $Seconds seconds."
Write-Host 'Keep driving where it stutters (for example the long Washington DC straight at night).'
for ($left = $DelaySeconds; $left -gt 0; --$left) { Write-Host -NoNewline "$left.. "; Start-Sleep -Seconds 1 }
Write-Host ''
Write-Host "Sampling for $Seconds seconds..."
& $python $sampler capture --pid $game.Id --output $out --seconds $Seconds --interval-ms 13 --threads 6 | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host 'Sampling failed; see the messages above.'; exit 1 }
& $python $sampler resolve --output $out | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host 'Samples were saved but could not be named; Claude can retry that step.' }
Write-Host "Done. Results: $out"
Write-Host 'You can keep playing. Tell Claude the profile is ready.'
