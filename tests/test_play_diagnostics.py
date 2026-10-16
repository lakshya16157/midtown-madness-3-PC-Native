"""Exercise diagnostic environment inheritance/restoration without a game."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELLS = []
if shutil.which('powershell.exe'):
    POWERSHELLS.append(('Windows PowerShell', shutil.which('powershell.exe')))
LOCAL_PWSH = ROOT / 'tools/bin/powershell/pwsh.exe'
if LOCAL_PWSH.is_file():
    POWERSHELLS.append(('PowerShell 7', str(LOCAL_PWSH)))
FLAGS = ('RECOMP_PB_WRAP_TRACE', 'RECOMP_FILE_PROBE_TRACE',
         'RECOMP_TEXTURE_STREAM_TRACE', 'MM3_BUILDING_TRACE', 'RECOMP_FILE_READ_CPU_TRACE',
         'RECOMP_PRODUCER_TRACE')


@unittest.skipUnless(os.name == 'nt' and POWERSHELLS, 'Windows PowerShell fixture')
class DiagnosticEnvironmentTest(unittest.TestCase):
    def launch_fixture(self, powershell, present, fail=False, light=True):
        with tempfile.TemporaryDirectory(prefix='mm3-diagnostics-env-') as tmp:
            root = Path(tmp)
            (root / 'scripts').mkdir()
            shutil.copyfile(ROOT / 'scripts/play-diagnostics.ps1', root / 'scripts/play-diagnostics.ps1')
            (root / 'inspect.py').write_text(
                'import json, os, pathlib\n'
                'keys = ' + repr(FLAGS + ('RECOMP_HITCH_TRACE', 'RECOMP_D3D_PROFILE', 'RECOMP_FRAME_CSV')) + '\n'
                'pathlib.Path(os.environ["FIXTURE_CHILD_JSON"]).write_text(json.dumps({k: os.environ[k] for k in keys if k in os.environ}))\n',
                encoding='utf8')
            (root / 'scripts/play.ps1').write_text(
                'param([switch]$Keyboard, [switch]$NoConsoleLog, [string]$Exe)\n'
                '$start = New-Object Diagnostics.ProcessStartInfo\n'
                '$start.FileName = $env:FIXTURE_PYTHON\n'
                '$start.Arguments = \'"\' + (Join-Path $PSScriptRoot "../inspect.py") + \'"\'\n'
                '$start.UseShellExecute = $false; $start.CreateNoWindow = $true\n'
                '$child = [Diagnostics.Process]::Start($start)\n'
                '$child.WaitForExit()\n'
                'if ($child.ExitCode -ne 0) { throw "fixture child failed" }\n'
                '$child.Dispose()\n'
                'if ($env:FIXTURE_THROW -eq "1") { throw "intentional fixture failure" }\n',
                encoding='utf8')
            # Suppress only the passive monitor. The stub play.ps1 starts a real
            # harmless native Python child so its actual environment is tested.
            (root / 'driver.ps1').write_text(r'''
$ErrorActionPreference = 'Stop'
function Start-Process {
    param($FilePath, $WindowStyle, [switch]$PassThru, $WorkingDirectory,
          $ArgumentList, $RedirectStandardOutput, $RedirectStandardError)
    return [pscustomobject]@{ Id = -1 }
}
$caught = $false
$launch = @{}
if ($env:FIXTURE_LIGHT -eq '1') { $launch.Light = $true }
try { & (Join-Path $PSScriptRoot 'scripts/play-diagnostics.ps1') @launch }
catch {
    if ($_.Exception.Message -ne 'intentional fixture failure') { throw }
    $caught = $true
}
$restored = @{}
foreach ($key in @('RECOMP_PB_WRAP_TRACE', 'RECOMP_FILE_PROBE_TRACE',
                  'RECOMP_TEXTURE_STREAM_TRACE', 'MM3_BUILDING_TRACE', 'RECOMP_FILE_READ_CPU_TRACE',
                  'RECOMP_PRODUCER_TRACE',
                  'RECOMP_HITCH_TRACE', 'RECOMP_D3D_PROFILE', 'RECOMP_FRAME_CSV')) {
    if (Test-Path -LiteralPath ('Env:' + $key)) {
        $restored[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    }
}
@{ restored=$restored; caught=$caught } | ConvertTo-Json |
    Set-Content -LiteralPath $env:FIXTURE_RESTORED_JSON -Encoding utf8
''', encoding='utf8')
            env = os.environ.copy()
            for key in FLAGS + ('RECOMP_HITCH_TRACE', 'RECOMP_D3D_PROFILE', 'RECOMP_FRAME_CSV'):
                env.pop(key, None)
            prior = {key: 'original-' + key for key in FLAGS} if present else {}
            env.update(prior)
            env.update(FIXTURE_PYTHON=sys.executable,
                       FIXTURE_CHILD_JSON=str(root / 'child.json'),
                       FIXTURE_RESTORED_JSON=str(root / 'restored.json'),
                       FIXTURE_LIGHT='1' if light else '0',
                       FIXTURE_THROW='1' if fail else '0')
            result = subprocess.run([powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                     '-File', str(root / 'driver.ps1')],
                                    env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            child = json.loads((root / 'child.json').read_text(encoding='utf-8-sig'))
            restored = json.loads((root / 'restored.json').read_text(encoding='utf-8-sig'))
            if light:
                self.assertFalse((set(FLAGS) - {'RECOMP_PRODUCER_TRACE'}) & set(child), child)
                self.assertEqual(child['RECOMP_PRODUCER_TRACE'], '1')
            else:
                for key in FLAGS:
                    self.assertEqual(child[key], '1')
            self.assertEqual(child['RECOMP_HITCH_TRACE'], '1')
            self.assertEqual(child['RECOMP_D3D_PROFILE'], '1')
            self.assertTrue(child['RECOMP_FRAME_CSV'].endswith('frames.csv'))
            self.assertEqual(restored['restored'], prior)
            self.assertEqual(restored['caught'], fail)

    def test_light_removes_absent_flags_and_restores_absence(self):
        for name, powershell in POWERSHELLS:
            with self.subTest(shell=name):
                self.launch_fixture(powershell, present=False)

    def test_light_removes_present_flags_and_restores_original_values(self):
        for name, powershell in POWERSHELLS:
            with self.subTest(shell=name):
                self.launch_fixture(powershell, present=True)

    def test_light_restores_original_values_after_launch_failure(self):
        for name, powershell in POWERSHELLS:
            with self.subTest(shell=name):
                self.launch_fixture(powershell, present=True, fail=True)

    def test_full_enables_cpu_context_and_restores_original_values(self):
        for name, powershell in POWERSHELLS:
            with self.subTest(shell=name):
                self.launch_fixture(powershell, present=True, light=False)


if __name__ == '__main__':
    unittest.main()
