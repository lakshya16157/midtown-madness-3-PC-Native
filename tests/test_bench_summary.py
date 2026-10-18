import contextlib
import csv
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('bench_summary', ROOT / 'tools/bench/summarize.py')
BENCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BENCH)


class BenchmarkSummaryTests(unittest.TestCase):
    def make_run(self, folder, present=True):
        (folder / 'windows.json').write_text(json.dumps([
            {'name': 'race', 'start': 100, 'end': 500, 'cpu_ms': 320, 'logical_processors': 16}
        ]), encoding='utf-8-sig')
        with (folder / 'frames.csv').open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(['qpc_ms', 'frame_ms', 'present_qpc_ms', 'present_ms'])
            # This straddling interval must not count as a hitch within the window.
            writer.writerow([110, 100, 110 if present else '', 100 if present else ''])
            for timestamp in range(130, 531, 20):
                writer.writerow([timestamp, 20, timestamp - 2 if present else '', 20 if present else ''])

    def test_separate_host_metrics_and_interval_boundaries(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.make_run(folder)
            with contextlib.redirect_stdout(io.StringIO()):
                results = BENCH.summarize_run(folder)
            self.assertEqual(results[0]['frame_ms']['frames'], 19)
            self.assertEqual(results[0]['present_ms']['frames'], 19)
            self.assertEqual(results[0]['frame_ms']['fps'], 50)
            self.assertEqual(results[0]['frame_ms']['hitch'], 0)
            self.assertEqual(results[0]['process_cpu_percent'], 5)
            self.assertEqual(json.loads((folder / 'summary.json').read_text()), results)

    def test_missing_host_samples_rejects_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            self.make_run(folder, present=False)
            with contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, 'insufficient present_ms'):
                BENCH.summarize_run(folder)
            self.assertFalse((folder / 'summary.json').exists())

    @unittest.skipUnless(os.name == 'nt', 'Windows benchmark capture helper')
    def test_controlled_scene_requires_verified_setup_and_live_car(self):
        pwsh = ROOT / 'tools/bin/powershell/pwsh.exe'
        if not pwsh.exists():
            self.skipTest('Checkout PowerShell is unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            def quote(value):
                return "'" + str(value).replace("'", "''") + "'"
            script = folder / 'session.ps1'
            script.write_text(f'''
$ErrorActionPreference = 'Stop'
. {quote(ROOT / 'tools/bench/session.ps1')}
$dir = {quote(folder)}; $p = [pscustomobject]@{{ Id=4321 }}
$pad = Join-Path $dir 'pad.txt'; [IO.File]::WriteAllText($pad,'')
$X=643.2; $Z=52.6; $Heading=0; $Secs=5; $Scenario='Stationary'
$windows=@(); $captures=@(); $gpuProcess=$null; $buttons=@(); $measured=0; $waits=@()
$teleport = {{
    param($ProcessId,$X,$Z,$Heading)
    if ($ProcessId -ne 4321) {{ throw 'wrong process probed' }}
    if ($PSBoundParameters.ContainsKey('X')) {{
        if ($X -ne 643.2 -or $Z -ne 52.6 -or $Heading -ne 0) {{ throw 'run coordinates not used' }}
        return @{{ result='applied'; x=$X; y=17; z=$Z }}
    }}
    return @{{ available=$true; x=643.2; y=17; z=52.6 }}
}}
function Qpc-Milliseconds {{ 1234.5 }}
function Wait-Game([double]$seconds) {{ $script:waits += $seconds }}
function Get-Command {{ param($Name) if ($Name -ne 'nvidia-smi') {{ throw 'unexpected executable lookup' }} }}
function Press-Pad($button,$delay) {{ $script:buttons += $button }}
function Capture-Frame($name) {{
    $script:captures += @{{ scene=$name; file=($name+'.bmp') }}
    if ($name -eq 'ready') {{
        @(
            '{{"command":"pad","button":"arbitrary-code"}}',
            '{{"command":"measure"}}',
            '{{"command":"pad","button":"down","wait":2}}',
            '{{"command":"capture","name":"cruise-setup"}}',
            '{{"command":"teleport"}}',
            '{{"command":"position"}}',
            '{{"command":"confirm","mode":"Cruise","city":"Paris","weather":"Clear","time":"Day","season":"Summer","cops":0,"pedestrians":"Medium","traffic":"Medium","car":"MINI Cooper S","transmission":"Automatic"}}',
            '{{"command":"measure"}}',
            '{{"command":"measure"}}',
            '{{"command":"finish"}}'
        ) | Set-Content -LiteralPath (Join-Path $dir 'control.jsonl')
    }}
}}
function Measure-Window($name,$seconds) {{
    if ($name -ne 'race' -or $seconds -ne 5) {{ throw 'measurement settings changed' }}
    $script:windows += @{{ name=$name; start=100; end=5100 }}; $script:measured++
}}
Invoke-ControlledScene
$results = @(Get-Content -LiteralPath (Join-Path $dir 'control-results.jsonl') | ConvertFrom-Json)
if ($results.Count -ne 10 -or $results[0].ok -or $results[1].ok -or $results[8].ok) {{ throw 'invalid command or duplicate/unverified measurement accepted' }}
if (-not $results[7].ok -or -not $results[9].ok -or $measured -ne 1 -or $buttons -ne 'down') {{ throw 'guided session did not complete exactly one race' }}
$scene = Get-Content -LiteralPath (Join-Path $dir 'scene.json') -Raw | ConvertFrom-Json
if ($scene.mode -ne 'Cruise' -or $scene.city -ne 'Paris' -or $scene.car -ne 'MINI Cooper S' -or $scene.traffic -ne 'Medium' -or $scene.transmission -ne 'Automatic' -or $scene.verified_captures.Count -ne 2) {{ throw 'verified scene evidence missing' }}
if (-not (Test-Path -LiteralPath (Join-Path $dir 'position.json')) -or -not (Test-Path -LiteralPath (Join-Path $dir 'position-end.json'))) {{ throw 'car state evidence missing' }}
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile({quote(ROOT / 'tools/bench/run.ps1')},[ref]$tokens,[ref]$errors)
if ($errors.Count) {{ throw 'runner parse failed' }}
$write=$ast.Find({{ param($node) $node -is [Management.Automation.Language.PipelineAst] -and $node.Extent.Text -like '*windows.json*' }},$true)
if (-not $write) {{ throw 'production window writer missing' }}
Invoke-Expression $write.Extent.Text
if ((Get-Content -LiteralPath (Join-Path $dir 'windows.json') -Raw).TrimStart()[0] -ne '[') {{ throw 'single controlled window must remain a JSON array for summarize.py' }}
''', encoding='utf-8')
            subprocess.run([str(pwsh), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script)], check=True)

    @unittest.skipUnless(os.name == 'nt', 'Windows benchmark capture helper')
    def test_initial_scene_uses_live_car_probe_and_records_metadata(self):
        pwsh = ROOT / 'tools/bin/powershell/pwsh.exe'
        if not pwsh.exists():
            self.skipTest('Checkout PowerShell is unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            def quote(value):
                return "'" + str(value).replace("'", "''") + "'"
            script = folder / 'initial-scene.ps1'
            script.write_text(f'''
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile({quote(ROOT / 'tools/bench/run.ps1')}, [ref]$tokens, [ref]$errors)
if ($errors.Count) {{ throw 'runner parse failed' }}
$function = $ast.Find({{ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-InitialScene' }}, $true)
if (-not $function) {{ throw 'production scene helper missing' }}
Invoke-Expression $function.Extent.Text
$dir = {quote(folder)}; $p = [pscustomobject]@{{ Id=4321 }}
function Assert-GameRunning {{}}
function Qpc-Milliseconds {{ 1234.5 }}
$teleport = {{
    param($ProcessId)
    if ($ProcessId -ne 4321) {{ throw 'wrong process probed' }}
    if ($script:probe -eq 'error') {{ throw 'probe unavailable' }}
    [pscustomobject]@{{ available=($script:probe -eq 'race'); x=643.2; z=52.6 }}
}}
foreach ($probe in @('menu', 'race', 'error')) {{
    $name = Get-InitialScene
    $expected = if ($probe -eq 'race') {{ 'early-race' }} else {{ 'frontend' }}
    $saved = Get-Content -LiteralPath (Join-Path $dir 'initial-scene.json') -Raw | ConvertFrom-Json
    if ($name -ne $expected -or $saved.name -ne $expected -or $saved.qpc_ms -ne 1234.5) {{ throw 'scene label or probe metadata differs' }}
    if ($probe -eq 'error') {{
        if ($saved.error -ne 'probe unavailable' -or $null -ne $saved.position) {{ throw 'probe failure evidence lost' }}
    }} elseif ($saved.position.available -ne ($probe -eq 'race') -or $saved.position.x -ne 643.2 -or $saved.error) {{
        throw 'live car probe evidence lost'
    }}
}}
''', encoding='utf-8')
            subprocess.run([str(pwsh), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script)], check=True)

    @unittest.skipUnless(os.name == 'nt', 'Windows benchmark capture helper')
    def test_capture_waits_for_complete_bmp_and_records_each_scene(self):
        pwsh = ROOT / 'tools/bin/powershell/pwsh.exe'
        if not pwsh.exists():
            self.skipTest('Checkout PowerShell is unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            def quote(value):
                return "'" + str(value).replace("'", "''") + "'"
            script = folder / 'capture.ps1'
            script.write_text(f'''
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile({quote(ROOT / 'tools/bench/run.ps1')}, [ref]$tokens, [ref]$errors)
if ($errors.Count) {{ throw 'runner parse failed' }}
$function = $ast.Find({{ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Capture-Frame' }}, $true)
if (-not $function) {{ throw 'production capture helper missing' }}
Invoke-Expression $function.Extent.Text
$dir = {quote(folder)}
$capturePrefix = Join-Path $dir 'present'; $captureOrdinal = 0; $captures = @()
$log = Join-Path $dir 'stderr.log'; $polls = 0; $completed = 0
function Assert-GameRunning {{}}
function Qpc-Milliseconds {{ 1234.5 }}
function Wait-Game([double]$seconds) {{
    if ($seconds -eq 1) {{ return }}
    if ($seconds -ne 0.25) {{ throw 'unexpected polling interval' }}
    $script:polls++
    if ((Get-Content -LiteralPath ($capturePrefix + '.flag') -Raw) -ne '1') {{ throw 'capture was not armed once' }}
    $path = '{{0}}-{{1:D3}}.bmp' -f $capturePrefix, $script:captureOrdinal
    $bytes = [byte[]]::new($(if ($script:polls % 2) {{54}} else {{58}}))
    $bytes[0] = 66; $bytes[1] = 77
    [BitConverter]::GetBytes([uint32]58).CopyTo($bytes,2)
    [IO.File]::WriteAllBytes($path,$bytes)
    if (-not ($script:polls % 2)) {{ [IO.File]::Delete($capturePrefix + '.flag'); $script:completed++ }}
}}
$Capture = $false
Capture-Frame 'disabled'
if ($polls -or $captures.Count -or (Test-Path -LiteralPath ($capturePrefix + '.flag'))) {{ throw 'disabled capture changed defaults' }}
$Capture = $true
Capture-Frame 'frontend'
if ($polls -ne 2 -or $captureOrdinal -ne 1 -or $captures.Count -ne 1) {{ throw 'partial BMP was accepted or capture index is wrong' }}
Capture-Frame 'race'
if ($polls -ne 4 -or $completed -ne 2 -or $captureOrdinal -ne 2) {{ throw 'scene capture did not complete' }}
$saved = Get-Content -LiteralPath (Join-Path $dir 'captures.json') -Raw | ConvertFrom-Json
if ($saved.Count -ne 2 -or $saved[0].scene -ne 'frontend' -or $saved[1].scene -ne 'race' -or $saved[1].qpc_ms -ne 1234.5) {{ throw 'capture metadata differs' }}
if ($saved[0].file -notlike '*present-000.bmp' -or $saved[1].file -notlike '*present-001.bmp') {{ throw 'wrong capture filenames' }}
''', encoding='utf-8')
            subprocess.run([str(pwsh), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script)], check=True)

    @unittest.skipUnless(os.name == 'nt', 'Windows benchmark runner')
    def test_runner_launch_failure_restores_environment_and_isolates_save(self):
        pwsh = ROOT / 'tools/bin/powershell/pwsh.exe'
        if not pwsh.exists():
            self.skipTest('Checkout PowerShell is unavailable')
        tag = 'contract-' + uuid.uuid4().hex
        run_dir = ROOT / 'conformance_tmp/bench' / tag
        def quote(value):
            return "'" + str(value).replace("'", "''") + "'"
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            save = folder / 'save'
            save.mkdir()
            (save / 'keep.txt').write_text('original save')
            script = folder / 'test.ps1'
            script.write_text(f'''
$ErrorActionPreference = 'Stop'
$env:RECOMP_MSAA = '7'
$env:RECOMP_PAD_SCRIPT = 'inherited-control'
function Start-Process {{ throw 'injected launch failure' }}
try {{
    try {{
        & {quote(ROOT / 'tools/bench/run.ps1')} -Exe $env:ComSpec -Save {quote(save)} -Tag {quote(tag)}
        throw 'runner unexpectedly succeeded'
    }} catch {{
        if ($_.Exception.Message -ne 'injected launch failure') {{ throw }}
    }}
    if ($env:RECOMP_MSAA -ne '7' -or $env:RECOMP_PAD_SCRIPT -ne 'inherited-control') {{ throw 'environment not restored' }}
    if (Test-Path -LiteralPath {quote(run_dir / 'save')}) {{ throw 'disposable save remains' }}
    if ((Get-Content -LiteralPath {quote(save / 'keep.txt')} -Raw) -ne 'original save') {{ throw 'original save changed' }}
}} finally {{
    $target = [IO.Path]::GetFullPath({quote(run_dir)})
    $boundary = [IO.Path]::GetFullPath({quote(ROOT / 'conformance_tmp/bench')}) + [IO.Path]::DirectorySeparatorChar
    if (-not $target.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) {{ throw 'cleanup escaped boundary' }}
    Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
}}
''', encoding='utf-8')
            subprocess.run([str(pwsh), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script)], check=True)
            self.assertEqual((save / 'keep.txt').read_text(), 'original save')

    @unittest.skipUnless(os.name == 'nt', 'Windows benchmark runner')
    def test_runner_rejects_recursive_save_copy(self):
        pwsh = ROOT / 'tools/bin/powershell/pwsh.exe'
        if not pwsh.exists():
            self.skipTest('Checkout PowerShell is unavailable')
        tag = 'contract-' + uuid.uuid4().hex
        result = subprocess.run([str(pwsh), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                 str(ROOT / 'tools/bench/run.ps1'), '-Exe', os.environ['ComSpec'],
                                 '-Save', str(ROOT), '-Tag', tag], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('cannot be inside its save template', result.stderr)
        self.assertFalse((ROOT / 'conformance_tmp/bench' / tag).exists())


if __name__ == '__main__':
    unittest.main()
