"""Named instances isolate writable state and serialize only the same save root."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('play_profile_fixture', ROOT / 'tests/test_play_profiles.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
fixture.KEYS += ('MM3_SAVE_DIR', 'MM3_DISPLAY_CONFIG', 'MM3_LAUNCHER_CONFIG',
                 'RECOMP_NET', 'RECOMP_NET_PORT', 'RECOMP_NET_PEERS')
quoted = fixture.quoted
SHELL = fixture.SHELL


@unittest.skipUnless(SHELL, 'PowerShell is unavailable')
class PlayInstances(unittest.TestCase):
    tearDown = fixture.LauncherProfileTest.tearDown
    run_launcher = fixture.LauncherProfileTest.run_launcher

    def setUp(self):
        fixture.LauncherProfileTest.setUp(self)
        self.local_data = self.folder / 'isolated application data'
        self.environment['LOCALAPPDATA'] = str(self.local_data)

    def test_named_instances_isolate_state_and_restore_inherited_environment(self):
        inherited = {'MM3_SAVE_DIR': 'previous-save', 'MM3_DISPLAY_CONFIG': 'previous-display',
                     'MM3_LAUNCHER_CONFIG': 'previous-launcher'}
        response = self.run_launcher(['-Instance Host', '-Instance Client', '-Instance Host'], inherited)
        for row, name in zip(response['during'], ('Host', 'Client', 'Host')):
            state = self.local_data / 'MM3Recomp/instances' / name
            self.assertEqual(Path(row['MM3_SAVE_DIR']), state / 'save')
            self.assertEqual(Path(row['MM3_DISPLAY_CONFIG']), state / 'display.ini')
            self.assertEqual(Path(row['MM3_LAUNCHER_CONFIG']), state / 'launcher.ini')
            self.assertTrue((state / 'save').is_dir())
        self.assertNotEqual(response['during'][0]['MM3_SAVE_DIR'], response['during'][1]['MM3_SAVE_DIR'])
        self.assertEqual(response['during'][0], response['during'][2])
        for key, value in inherited.items():
            self.assertEqual(response['after'][key], value)
        self.assertTrue(response['location_restored'])

    def test_system_link_aliases_override_only_child_environment(self):
        inherited = {'RECOMP_NET': '0', 'RECOMP_NET_PORT': '3999', 'RECOMP_NET_PEERS': 'old.invalid:3998'}
        response = self.run_launcher(['-Instance Host -SystemLink -NetPort 3075 -NetPeers 127.0.0.1:3076',
                                      '-Instance Client -Network -NetworkPort 3076 -NetPeers 127.0.0.1:3075'], inherited)
        self.assertEqual([row['RECOMP_NET'] for row in response['during']], ['nat', 'nat'])
        self.assertEqual([row['RECOMP_NET_PORT'] for row in response['during']], ['3075', '3076'])
        self.assertEqual([row['RECOMP_NET_PEERS'] for row in response['during']],
                         ['127.0.0.1:3076', '127.0.0.1:3075'])
        for key, value in inherited.items():
            self.assertEqual(response['after'][key], value)
        for path in (self.folder / 'logs').glob('play-*.json'):
            metadata = json.loads(path.read_text(encoding='utf-8'))
            self.assertIn(metadata['launch_options']['instance'], ('Host', 'Client'))
            self.assertTrue(metadata['launch_options']['network'])
            self.assertIn(metadata['launch_options']['network_port'], (3075, 3076))

    def test_explicit_off_empty_peers_and_implicit_network_port(self):
        inherited = {'RECOMP_NET': 'nat', 'RECOMP_NET_PEERS': '127.0.0.1:9999'}
        response = self.run_launcher(['-Network:$false -NetPeers ""', '-NetPort 3077'], inherited)
        self.assertEqual(response['during'][0]['RECOMP_NET'], '0')
        self.assertIsNone(response['during'][0]['RECOMP_NET_PEERS'])
        self.assertEqual(response['during'][1]['RECOMP_NET'], 'nat')
        self.assertEqual(response['during'][1]['RECOMP_NET_PORT'], '3077')
        self.assertEqual(response['during'][1]['RECOMP_NET_PEERS'], inherited['RECOMP_NET_PEERS'])
        for key, value in inherited.items():
            self.assertEqual(response['after'][key], value)

    def test_invalid_names_and_ports_reject_before_state_or_log_creation(self):
        response = self.run_launcher(['-Instance ../escape', '-Instance .', '-Instance a/b',
                                      '-Instance ""', '-Instance ' + 'x' * 33,
                                      '-NetworkPort 0', '-NetPort 65536'], expect_failure=True)
        self.assertEqual(response['during'], [])
        self.assertFalse(self.local_data.exists())
        self.assertFalse((self.folder / 'logs').exists())

    @unittest.skipUnless(os.name == 'nt', 'requires Windows named mutexes')
    def test_mutex_uses_canonical_save_root_and_allows_distinct_instances(self):
        function = (
            '$tokens=$null; $errors=$null; '
            '$ast=[Management.Automation.Language.Parser]::ParseFile(' + quoted(self.launcher) +
            ',[ref]$tokens,[ref]$errors); if($errors.Count){throw "parse failed"}; '
            '$f=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] '
            '-and $n.Name -eq "Get-InstanceLaunchMutexName"},$true).Body.GetScriptBlock(); ')
        save = self.local_data / 'MM3Recomp/instances/Host/save'
        other = self.local_data / 'MM3Recomp/instances/Client/save'
        script = function + '$a=& $f ' + quoted(save) + '; $b=& $f ' + quoted(str(save).upper() + '\\')
        script += '; $c=& $f ' + quoted(other) + '; @($a,$b,$c)|ConvertTo-Json -Compress'
        result = subprocess.run([SHELL, '-NoProfile', '-Command', script], text=True,
                                capture_output=True, env=self.environment, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        first, canonical, different = json.loads(result.stdout)
        self.assertEqual(first, canonical)
        self.assertNotEqual(first, different)
        ready, release = self.folder / 'mutex-ready', self.folder / 'mutex-release'
        hold = ('$m=[Threading.Mutex]::new($false,' + quoted(first) + '); '
                'try { if(-not $m.WaitOne(0)){throw "fixture collision"}; '
                '[IO.File]::WriteAllText(' + quoted(ready) + ',"ready"); '
                'while(-not(Test-Path -LiteralPath ' + quoted(release) +
                ')){Start-Sleep -Milliseconds 20} }finally{$m.ReleaseMutex();$m.Dispose()}')
        owner = subprocess.Popen([SHELL, '-NoProfile', '-Command', hold],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, env=self.environment)
        try:
            deadline = time.monotonic() + 20
            while not ready.exists() and owner.poll() is None and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(ready.exists(), 'mutex owner did not become ready')
            probe = ('$result=@(); foreach($name in @(' + quoted(first) + ',' + quoted(different) +
                     ')){ $m=[Threading.Mutex]::new($false,$name); '
                     '$owned=$m.WaitOne(0);$result+=$owned;if($owned){$m.ReleaseMutex()};$m.Dispose() }; '
                     '$result|ConvertTo-Json -Compress')
            check = subprocess.run([SHELL, '-NoProfile', '-Command', probe], text=True,
                                   capture_output=True, env=self.environment, timeout=30)
            self.assertEqual(check.returncode, 0, check.stderr)
            self.assertEqual(json.loads(check.stdout), [False, True])
        finally:
            release.write_text('release')
            try:
                stdout, stderr = owner.communicate(timeout=10)
                self.assertEqual(owner.returncode, 0, stdout + stderr)
            finally:
                if owner.poll() is None:
                    owner.terminate()
                    owner.wait(timeout=10)


if __name__ == '__main__':
    unittest.main(verbosity=2)
