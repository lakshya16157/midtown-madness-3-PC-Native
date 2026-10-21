<#
Opt-in operator-guided acquisition for run.ps1 -ControlledScene -Capture -X ... -Z ... .
Commands are newline-terminated JSON records in control.jsonl; acknowledgements
appear in control-results.jsonl. No shell expressions are accepted. The operator
must inspect captures to verify the real screen, cursor and scene settings.

Retail route (confirmed in captured menus): Main -> Single Race ->
Cruise -> Paris -> Cruise setup -> Vehicle -> Transmission -> start.
Main Single Race is row 1; Cruise is row 0. Displayed Cruise setup rows are
Traffic, Pedestrians, Cops, Season, Time of Day, Weather; Day displays as Noon.
Inspect each selected value;
never infer the current screen from the number of elapsed button presses.

Examples, appended one per line and acknowledged before the next dependent step:
  {"command":"pad","button":"b","wait":2}
  {"command":"capture","name":"main"}
  {"command":"position"}
  {"command":"teleport"}  (uses run.ps1's recorded X, Z and Heading)
  {"command":"confirm","mode":"Cruise","city":"Paris","weather":"Clear",
   "time":"Day","season":"Summer","cops":0,"pedestrians":"Medium",
   "traffic":"Medium","car":"MINI Cooper S","transmission":"Automatic"}
  {"command":"measure"}   (uses run.ps1's recorded Scenario and Secs)
  {"command":"stress","plan":"stress-plan-request.json"}
  {"command":"finish"}

Confirm only after capturing the setup, selected car and live driving scene.
The capture evidence is retained with the explicit choices in scene.json.
Measure requires a live car and confirmation; any menu button invalidates that
confirmation. Capture readbacks occur outside the measured window. Finish stops
only this owned process, restores inherited environment and removes its save
copy through run.ps1's existing finally block. Sessions time out after 30 minutes.
#>
function Invoke-ControlledScene {
    $control = Join-Path $dir 'control.jsonl'
    $results = Join-Path $dir 'control-results.jsonl'
    [IO.File]::WriteAllText($control, '')
    @{ control=$control; results=$results; pad=$pad; process_id=$p.Id } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'session.json')
    # Allow renderer startup and cold shader preparation without sending input.
    Capture-Frame 'ready' 120
    Write-Output "Controlled scene ready; append JSON commands to $control"
    $next = 0
    $done = $false
    $sceneConfirmed = $false
    $raceValid = $false
    $lifetime = [Diagnostics.Stopwatch]::StartNew()
    while (-not $done) {
        if ($lifetime.Elapsed.TotalMinutes -ge 30) { throw 'Controlled scene session timed out.' }
        Wait-Game 0.25
        try { $lines = [IO.File]::ReadAllText($control) -split '\r?\n' } catch { continue }
        # Only newline-terminated records are complete, including while a
        # writer is appending a new command.
        while ($next -lt $lines.Length - 1 -and -not $done) {
            $line = $lines[$next++]
            if (-not $line.Trim()) { continue }
            $answer = @{ sequence=$next; ok=$false; qpc_ms=(Qpc-Milliseconds) }
            try {
                $request = $line | ConvertFrom-Json
                $answer.command = [string]$request.command
                switch ($request.command) {
                    'pad' {
                        if ($request.button -notin @('up','down','left','right','a','b','x','y','start','back')) {
                            throw 'Unsupported digital button.'
                        }
                        $delay = 1.0
                        if ($null -ne $request.wait) { $delay = [double]$request.wait }
                        if (-not [double]::IsFinite($delay) -or $delay -lt 0 -or $delay -gt 60) {
                            throw 'Pad wait must be finite, from zero through 60 seconds.'
                        }
                        Press-Pad $request.button $delay
                        $sceneConfirmed = $false
                    }
                    'capture' {
                        if ($request.name -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,49}$') {
                            throw 'Capture name must contain only letters, numbers, underscores or hyphens.'
                        }
                        Capture-Frame $request.name
                        $answer.file = $script:captures[-1].file
                    }
                    'position' { $answer.position = & $teleport -ProcessId $p.Id }
                    'teleport' {
                        $before = & $teleport -ProcessId $p.Id
                        $before | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'position-start.json')
                        $answer.position = & $teleport -ProcessId $p.Id -X $X -Z $Z -Heading $Heading
                        $answer.position | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'position.json')
                        Wait-Game 4
                    }
                    'confirm' {
                        if ($request.mode -ne 'Cruise' -or $request.city -ne 'Paris' -or
                            $request.weather -notin @('Clear','Rain','Snow') -or
                            $request.time -notin @('Dawn','Day','Dusk','Night') -or
                            $request.season -notin @('Spring','Summer','Autumn','Winter') -or
                            $null -eq $request.cops -or $request.cops -notin 0..4 -or
                            $request.pedestrians -notin @('None','Very Light','Light','Medium','Heavy','Very Heavy') -or
                            $request.traffic -notin @('None','Very Light','Light','Medium','Heavy','Very Heavy') -or
                            $request.transmission -notin @('Automatic','Manual') -or
                            $request.car -notmatch '^[A-Za-z0-9][A-Za-z0-9 ._-]{0,63}$') {
                            throw 'Confirm requires captured Cruise/Paris environment, densities, named car and transmission.'
                        }
                        @{ mode=$request.mode; city=$request.city; weather=$request.weather
                           time=$request.time; season=$request.season; car=$request.car
                           cops=[int]$request.cops; pedestrians=$request.pedestrians; traffic=$request.traffic
                           transmission=$request.transmission
                           verified_captures=@($script:captures); qpc_ms=(Qpc-Milliseconds)
                        } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $dir 'scene.json')
                        $sceneConfirmed = $true
                    }
                    'measure' {
                        if (-not $sceneConfirmed) { throw 'Confirm the captured Cruise setup and car before measuring.' }
                        if ($script:windows.Count) { throw 'This session has already measured a race window.' }
                        $before = & $teleport -ProcessId $p.Id
                        if (-not $before.available) { throw 'No live player car; measurement refused.' }
                        $before | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'position-measure-start.json')
                        if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) {
                            $script:gpuProcess = Start-Process -FilePath nvidia-smi -WindowStyle Hidden -PassThru `
                                -ArgumentList @('--query-gpu=utilization.gpu,power.draw', '--format=csv,noheader,nounits', '-lms', '500') `
                                -RedirectStandardOutput (Join-Path $dir 'gpu.csv') -RedirectStandardError (Join-Path $dir 'gpu-errors.txt')
                        }
                        if ($Scenario -eq 'Driving') {
                            [IO.File]::AppendAllText($pad, "rt:$($Secs * 1000 + 500)`n")
                            Wait-Game 0.25
                        }
                        Measure-Window 'race' $Secs
                        $after = & $teleport -ProcessId $p.Id
                        $after | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'position-end.json')
                        if (-not $after.available -or ($Scenario -eq 'Driving' -and
                            [Math]::Pow($after.x - $before.x, 2) + [Math]::Pow($after.z - $before.z, 2) -lt 1)) {
                            throw 'Race measurement lost the player car or Driving did not move it one metre.'
                        }
                        $raceValid = $true
                        $answer.position = $after
                    }
                    'stress' {
                        if (-not $sceneConfirmed) { throw 'Confirm the captured native scene before stress testing.' }
                        if ($script:windows.Count) { throw 'Use a fresh controlled session for repeated stress windows.' }
                        if ($request.plan -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]{0,79}\.json$') {
                            throw 'Stress plan must name a JSON file inside this run directory.'
                        }
                        $planFile = Join-Path $dir $request.plan
                        if (-not (Test-Path -LiteralPath $planFile -PathType Leaf)) { throw 'Stress plan file is unavailable.' }
                        $stressPython = Join-Path $repo 'tools\venv\Scripts\python.exe'
                        if (-not (Test-Path -LiteralPath $stressPython)) { $stressPython = (Get-Command python.exe).Source }
                        $stressScript = Join-Path $repo 'tools\bench\stress.py'
                        & $stressPython $stressScript $dir --plan $planFile | Out-Host
                        $stressExit = $LASTEXITCODE
                        $stressReportFile = Join-Path $dir 'stress-report.json'
                        if (-not (Test-Path -LiteralPath $stressReportFile)) { throw 'Stress controller did not produce evidence.' }
                        $stressReport = Get-Content -LiteralPath $stressReportFile -Raw | ConvertFrom-Json
                        if ($stressExit -gt 1 -or -not $stressReport.completed -or -not $stressReport.windows.Count) {
                            throw "Stress controller stopped before completing its windows: $($stressReport.error)"
                        }
                        $script:windows = @($stressReport.windows)
                        $raceValid = $true
                        $answer.report = $stressReportFile
                        $answer.verified = [bool]$stressReport.all_windows_verified
                        $answer.windows = $stressReport.windows.Count
                        # Command completion is separate from passing collision,
                        # movement and applied-input requirements in the report.
                    }
                    'finish' {
                        if (-not $raceValid) { throw 'Measure a verified race before finishing.' }
                        $done = $true
                    }
                    default { throw 'Expected pad, capture, position, teleport, confirm, measure or finish.' }
                }
                $answer.ok = $true
            } catch { $answer.error = $_.Exception.Message }
            $answer | ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath $results
        }
    }
}
