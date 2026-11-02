"""Exercise scripts/play.py, and below scripts/play.ps1, with harmless fixtures standing in for the game."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
GAME_NAME = "mm3_recomp.exe" if os.name == "nt" else "mm3_recomp"

# Records every MM3_/RECOMP_ variable the fixture sees. Console keys go to the
# private jsonl file only, never to stdout.
COLLECTOR = '''
import json, os
seen = {k: v for k, v in os.environ.items() if k.startswith(("MM3_", "RECOMP_"))}
with open(os.environ["PLAY_TEST_COLLECTED"], "a", encoding="utf-8") as handle:
    handle.write(json.dumps(seen) + "\\n")
print(json.dumps({k: v for k, v in seen.items() if "XBOX" not in k}), flush=True)
'''

# Stands in for a running game: signals readiness, then waits for a release file.
GAME_SCRIPT = '''
import os, time
open(os.environ["MM3_FIXTURE_READY"], "w").close()
while not os.path.exists(os.environ["MM3_FIXTURE_RELEASE"]):
    time.sleep(0.02)
'''

HOLD_COLLECTOR = '''
import os, sys, time
print("stdout before hold", flush=True)
print("stderr before hold", file=sys.stderr, flush=True)
while not os.path.exists(os.environ["PLAY_TEST_RELEASE"]):
    time.sleep(0.02)
for i in range(2500):
    print("output-%d " % i + "x" * 128)
for i in range(2500):
    print("error-%d " % i + "y" * 128, file=sys.stderr)
sys.stderr.write("final stderr without newline")
sys.stderr.flush()
sys.exit(37)
'''


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class PlayLauncherTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="mm3 launch profile ")
        self.folder = Path(self.temporary.name)
        (self.folder / "scripts").mkdir()
        (self.folder / "game_files").mkdir()
        (self.folder / "game_files/default.xbe").write_bytes(b"fixture marker only")
        self.launcher = self.folder / "scripts/play.py"
        shutil.copyfile(ROOT / "scripts/play.py", self.launcher)
        support = self.folder / "tools/xboxrecomp/tools/launch"
        support.mkdir(parents=True)
        shutil.copyfile(ROOT / "tools/xboxrecomp/tools/launch/process_support.py", support / "process_support.py")
        self.collector = self.folder / "collect settings.py"
        self.collector.write_text(COLLECTOR, encoding="utf-8")
        self.collected = self.folder / "settings.jsonl"
        self.environment = {key: value for key, value in os.environ.items()
                            if not key.startswith(("MM3_", "RECOMP_"))}
        self.environment["PLAY_TEST_COLLECTED"] = str(self.collected)
        self.environment["PYTHONHOME"] = sys.base_prefix
        self.parent_environment = dict(os.environ)

    def tearDown(self):
        self.temporary.cleanup()

    def launch(self, arguments=(), overrides=None, exe=None):
        environment = dict(self.environment, **(overrides or {}))
        command = [sys.executable, str(self.launcher), "--exe", str(exe or self.collector), *arguments]
        result = subprocess.run(command, text=True, encoding="utf-8", errors="replace",
                                capture_output=True, env=environment, cwd=self.folder, timeout=120)
        # The launcher is a subprocess: the caller's own environment must be untouched.
        self.assertEqual(dict(os.environ), self.parent_environment)
        return result

    def launch_ok(self, arguments=(), overrides=None):
        result = self.launch(arguments, overrides)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def records(self):
        if not self.collected.exists():
            return []
        return [json.loads(line) for line in self.collected.read_text(encoding="utf-8").splitlines()]

    def manifests(self):
        return sorted((self.folder / "logs").glob("play-*.json"))

    @staticmethod
    def metadata(path):
        return json.loads(Path(path).read_text(encoding="utf-8"))

    def test_profile_selection_explicit_fps_and_repeated_launches(self):
        for arguments in (["--profile", "Quality"], ["--profile", "LowPower"], [], ["--fps-limit", "45"]):
            self.launch_ok(arguments)
        during = self.records()
        self.assertEqual([row.get("MM3_PERFORMANCE_PROFILE") for row in during],
                         ["Quality", "LowPower", "Balanced", "Balanced"])
        self.assertEqual([row.get("RECOMP_FPS_LIMIT") for row in during], [None, None, None, "45"])
        for row in during:
            # Graphics/FPS profile values are chosen by the shared C helper.
            self.assertIsNone(row.get("RECOMP_RENDER_SCALE"))
            self.assertEqual(row["RECOMP_PB_EXEC"], "1")
            self.assertIsNone(row.get("MM3_BUILDING_TRACE"))
            self.assertIsNone(row.get("RECOMP_TEXTURE_STREAM_TRACE"))
            self.assertIsNone(row.get("RECOMP_ASYNC_WAIT_TRACE"))
        metadata = [self.metadata(path) for path in self.manifests()]
        self.assertEqual([row["profile_defaults_for_unset_settings"]["msaa"] for row in metadata],
                         ["8", "1", "4", "4"])

    def test_individual_environment_overrides_take_priority_and_survive_launch(self):
        overrides = {"MM3_PERFORMANCE_PROFILE": "Quality", "RECOMP_RENDER_SCALE": "2.25",
                     "RECOMP_MSAA": "8", "RECOMP_DRAW_DISTANCE": "1.25", "RECOMP_ANISO": "4",
                     "RECOMP_FPS_LIMIT": "47", "RECOMP_USB": "0", "MM3_FOV_DEGREES": "97"}
        self.launch_ok(["--profile", "LowPower", "--fps-limit", "30", "--fov", "100"], overrides)
        for key, value in overrides.items():
            self.assertEqual(self.records()[0][key], value)

    def test_failing_entry_point_is_reported_and_recorded(self):
        self.collector.write_text("raise RuntimeError('intentional fixture failure')", encoding="utf-8")
        result = self.launch(["--profile", "LowPower", "--fps-limit", "42", "--fov", "105"])
        self.assertNotEqual(result.returncode, 0)
        metadata = self.metadata(self.manifests()[0])
        self.assertEqual(metadata["state"], "exited")
        self.assertEqual(metadata["exit_code"], 1)
        log = Path(metadata["log_path"]).read_text(encoding="utf-8")
        self.assertIn("[stderr] ", log)
        self.assertIn("intentional fixture failure", log)

    def test_fov_boundaries_and_decimal_formatting(self):
        for arguments in (["--fov", "60"], ["--fov", "110"], ["--fov", "100.5"], []):
            self.launch_ok(arguments)
        self.assertEqual([row.get("MM3_FOV_DEGREES") for row in self.records()],
                         ["60", "110", "100.5", None])

    def test_out_of_range_fov_never_runs_entry_point(self):
        marker = self.folder / "entry-point-was-run"
        self.collector.write_text("open(%r, 'w').write('unexpected')" % str(marker), encoding="utf-8")
        for arguments in (["--fov", "59"], ["--fov", "111"], ["--fov", "nan"]):
            result = self.launch(arguments)
            self.assertNotEqual(result.returncode, 0)
        self.assertFalse(marker.exists())
        self.assertFalse((self.folder / "logs").exists())

    def test_trace_switch_records_launch_configuration_and_respects_overrides(self):
        overrides = {"MM3_BUILDING_TRACE": "0", "RECOMP_DRAW_DISTANCE": "1.25", "MM3_FOV_DEGREES": "97"}
        self.launch_ok(["--trace-buildings", "--profile", "LowPower", "--fov", "100"], overrides)
        self.launch_ok(["--no-trace-buildings"], overrides)
        first, second = self.records()
        self.assertEqual(first["MM3_BUILDING_TRACE"], "0")
        self.assertEqual(first["RECOMP_TEXTURE_STREAM_TRACE"], "1")
        self.assertEqual(first["RECOMP_ASYNC_WAIT_TRACE"], "1")
        self.assertIsNone(second.get("RECOMP_TEXTURE_STREAM_TRACE"))
        self.assertIsNone(second.get("RECOMP_ASYNC_WAIT_TRACE"))
        manifests = self.manifests()
        self.assertEqual(len(manifests), 2)
        metadata = self.metadata(manifests[0])
        self.assertEqual(metadata["state"], "exited")
        self.assertEqual(metadata["exit_code"], 0)
        self.assertEqual(metadata["environment"]["RECOMP_DRAW_DISTANCE"], "1.25")
        self.assertEqual(metadata["environment"]["MM3_FOV_DEGREES"], "97")
        self.assertEqual(metadata["profile_defaults_for_unset_settings"]["draw_distance"], "1")
        self.assertTrue(metadata["launch_options"]["trace_buildings"])
        self.assertEqual(metadata["executable"]["sha256"], sha256(self.collector))
        self.assertEqual(metadata["arguments"][-1], str(self.collector))
        files = {entry["path"]: entry for entry in metadata["configuration_files"]}
        xbe = self.folder / "game_files/default.xbe"
        self.assertEqual(files[str(xbe)]["sha256"], sha256(xbe))
        for manifest in manifests:
            saved = self.metadata(manifest)
            log = Path(saved["log_path"]).read_text(encoding="utf-8")
            self.assertRegex(log, r"(?m)^\d{4}-\d\d-\d\dT.*\[stdout\] ")
            self.assertIn('"MM3_PERFORMANCE_PROFILE"', log)

    def test_console_keys_are_redacted_from_metadata_and_preserved_for_child(self):
        # Synthetic values only. The child stores them privately; stdout never shows them.
        overrides = {"RECOMP_XBOX_HDKEY": "11" * 16, "RECOMP_XBOX_SIGKEY": "22" * 16,
                     "RECOMP_XBOX_LANKEY": "33" * 16}
        self.launch_ok([], overrides)
        for key, value in overrides.items():
            self.assertEqual(self.records()[0][key], value)
        manifest = self.manifests()[0]
        metadata_text = manifest.read_text(encoding="utf-8")
        metadata = json.loads(metadata_text)
        log = Path(metadata["log_path"]).read_text(encoding="utf-8")
        for key, value in overrides.items():
            self.assertEqual(metadata["environment"][key], "[REDACTED]")
            self.assertNotIn(value, metadata_text)
            self.assertNotIn(value, log)

    def test_multiple_instances_requires_explicit_save_root_before_launch(self):
        result = self.launch(["--allow-multiple-instances"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MM3_SAVE_DIR", result.stderr)
        self.assertEqual(self.records(), [])
        self.assertFalse((self.folder / "logs").exists())

    def start_game(self, save_root, tag, allow=True):
        """Start a launcher whose game fixture is named like the real executable."""
        ready, release = self.folder / (tag + " ready"), self.folder / (tag + " release")
        environment = dict(self.environment, MM3_SAVE_DIR=str(save_root),
                           MM3_FIXTURE_READY=str(ready), MM3_FIXTURE_RELEASE=str(release))
        command = [sys.executable, str(self.launcher), "--exe", str(self.game_fixture)]
        if allow:
            command.append("--allow-multiple-instances")
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True, encoding="utf-8",
                                   errors="replace", env=environment, cwd=self.folder)
        # The fixture game is a Python interpreter reading its script from stdin.
        process.stdin.write(GAME_SCRIPT)
        process.stdin.close()
        process.stdin = None  # communicate() must not try to flush the closed pipe
        return process, ready, release

    def make_game_fixture(self):
        self.game_fixture = self.folder / GAME_NAME
        # The base interpreter, not a virtualenv launcher, which needs pyvenv.cfg beside it.
        interpreter = Path(getattr(sys, "_base_executable", sys.executable))
        if os.name == "nt":
            shutil.copy2(interpreter, self.game_fixture)
            for dll in interpreter.parent.glob("*.dll"):
                shutil.copy2(dll, self.folder / dll.name)
        else:
            os.symlink(interpreter, self.game_fixture)

    @staticmethod
    def wait_ready(process, ready):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and not ready.exists():
            if process.poll() is not None:
                raise AssertionError("fixture launch failed: " + process.communicate()[1])
            time.sleep(.05)
        if not ready.exists():
            raise AssertionError("fixture did not start")
        if process.poll() is not None:
            raise AssertionError("fixture exited early")

    def test_multiple_instances_allow_distinct_roots_block_aliases_and_keep_default_guard(self):
        self.make_game_fixture()
        active = []
        try:
            root_one, root_two = self.folder / "save one", self.folder / "save two"
            first, first_ready, first_release = self.start_game(root_one, "first")
            active.append((first, first_release))
            self.wait_ready(first, first_ready)
            second, second_ready, second_release = self.start_game(root_two, "second")
            active.append((second, second_release))
            self.wait_ready(second, second_ready)
            self.assertIsNone(first.poll(), "starting a distinct-root instance disturbed the first")
            # A lexical alias must resolve to the same save root and be refused.
            alias, alias_ready, alias_release = self.start_game("save one/./unused/..", "alias")
            active.append((alias, alias_release))
            output, error = alias.communicate(timeout=30)
            self.assertNotEqual(alias.returncode, 0, output)
            self.assertIn("using save directory", error)
            self.assertFalse(alias_ready.exists())
            # Without --allow-multiple-instances the process guard refuses it.
            normal, normal_ready, normal_release = self.start_game(self.folder / "save three", "normal", allow=False)
            active.append((normal, normal_release))
            output, error = normal.communicate(timeout=30)
            self.assertNotEqual(normal.returncode, 0, output)
            self.assertIn("already running", error)
            self.assertFalse(normal_ready.exists())
            manifests = self.manifests()
            self.assertEqual(len(manifests), 2, "refused launches created log metadata")
            for manifest in manifests:
                metadata = self.metadata(manifest)
                self.assertTrue(metadata["launch_options"]["allow_multiple_instances"])
                self.assertIn(metadata["environment"]["MM3_SAVE_DIR"], {str(root_one), str(root_two)})
            self.assertIsNone(first.poll())
            self.assertIsNone(second.poll())
        finally:
            for process, release in active:
                release.touch()
            for process, release in active:
                _, error = process.communicate(timeout=30)
                if release.name.startswith(("first ", "second ")):
                    self.assertEqual(process.returncode, 0, error)

    def test_second_game_is_refused_before_launch_or_log_creation(self):
        self.make_game_fixture()
        release = self.folder / "game release"
        game = subprocess.Popen([str(self.game_fixture)], stdin=subprocess.PIPE,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True,
                                env=dict(self.environment, MM3_FIXTURE_READY=str(self.folder / "game ready"),
                                         MM3_FIXTURE_RELEASE=str(release)), cwd=self.folder)
        game.stdin.write(GAME_SCRIPT)
        game.stdin.close()
        game.stdin = None  # communicate() must not try to flush the closed pipe
        try:
            self.wait_ready(game, self.folder / "game ready")
            result = self.launch(exe=self.game_fixture)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("already running", result.stderr)
            self.assertIn(str(game.pid), result.stderr)
            self.assertIn("Close all existing game windows", result.stderr)
            self.assertIn("cache", result.stderr)
            self.assertFalse((self.folder / "logs").exists())
            self.assertIsNone(game.poll(), "preflight disturbed the existing game process")
        finally:
            release.touch()
            game.communicate(timeout=30)

    def test_both_streams_are_saved_before_exit_and_nonzero_exit_code_survives(self):
        release = self.folder / "release child"
        self.collector.write_text(HOLD_COLLECTOR, encoding="utf-8")
        environment = dict(self.environment, PLAY_TEST_RELEASE=str(release))
        # Redirect the outer launcher to files so its console echo cannot fill a pipe.
        with (self.folder / "outer.out").open("w", encoding="utf-8") as output, \
                (self.folder / "outer.err").open("w", encoding="utf-8") as error:
            process = subprocess.Popen([sys.executable, str(self.launcher), "--exe", str(self.collector)],
                                       stdout=output, stderr=error, env=environment, cwd=self.folder)
            try:
                deadline = time.monotonic() + 20
                observed = ""
                while time.monotonic() < deadline:
                    logs = list((self.folder / "logs").glob("play-*.log"))
                    if logs:
                        observed = logs[0].read_text(encoding="utf-8")
                    if "stdout before hold" in observed and "stderr before hold" in observed:
                        break
                    if process.poll() is not None:
                        self.fail("launcher exited before the live capture: " +
                                  (self.folder / "outer.err").read_text(encoding="utf-8"))
                    time.sleep(.05)
                self.assertIn("[stdout] stdout before hold", observed)
                self.assertIn("[stderr] stderr before hold", observed)
                self.assertIsNone(process.poll())
                release.touch()
                self.assertEqual(process.wait(timeout=30), 37)
            finally:
                release.touch()
                if process.poll() is None:
                    process.wait(timeout=30)
        metadata = self.metadata(self.manifests()[0])
        self.assertEqual(metadata["exit_code"], 37)
        self.assertEqual(metadata["state"], "exited")
        self.assertNotIn("capture_error", metadata)
        log = Path(metadata["log_path"]).read_text(encoding="utf-8")
        self.assertEqual(log.count("[stdout] output-"), 2500)
        self.assertEqual(log.count("[stderr] error-"), 2500)
        # Two pipes read concurrently: lines land in arrival order, so the
        # unterminated last line is checked for, not its position.
        self.assertIn("[stderr] final stderr without newline\n", log)


# The Windows launcher (scripts/play.ps1) keeps its own tests, below.
SHELL = shutil.which("pwsh") or shutil.which("powershell")
KEYS = ("MM3_PERFORMANCE_PROFILE", "RECOMP_RENDER_SCALE", "RECOMP_MSAA",
        "RECOMP_DRAW_DISTANCE", "RECOMP_VIEW_DISTANCE", "RECOMP_ANISO", "RECOMP_FPS_LIMIT",
        "RECOMP_ASPECT",
        "RECOMP_PB_EXEC", "RECOMP_FB_WINDOW", "RECOMP_USB", "RECOMP_APU_DSP_ACK",
        "MM3_FOV_DEGREES", "MM3_BUILDING_TRACE", "RECOMP_TEXTURE_STREAM_TRACE",
        "RECOMP_ASYNC_WAIT_TRACE", "MM3_LIGHT_DUMP", "MM3_LIGHT_DEBUG")


def quoted(path):
    return "'" + str(path).replace("'", "''") + "'"


@unittest.skipUnless(SHELL, "PowerShell is unavailable")
class PowerShellLauncherProfileTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="mm3 launch profile ")
        self.folder = Path(self.temporary.name)
        (self.folder / "scripts").mkdir()
        (self.folder / "game_files").mkdir()
        (self.folder / "game_files/default.xbe").write_bytes(b"fixture marker only")
        self.launcher = self.folder / "scripts/play.ps1"
        self.launcher.write_bytes((ROOT / "scripts/play.ps1").read_bytes())
        self.collector = self.folder / "collect settings.ps1"
        self.collected = self.folder / "settings.jsonl"
        names = ",".join(quoted(key) for key in KEYS)
        self.snapshot = ("$settings = [ordered]@{}; foreach ($key in @(" + names + ")) { "
                         "$settings[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }; ")
        self.collector.write_text(self.snapshot + "$json = $settings | ConvertTo-Json -Compress; "
                                  "Add-Content -LiteralPath " + quoted(self.collected) +
                                  " -Value $json; [Console]::Out.WriteLine($json)", encoding="utf-8")
        self.environment = {key: value for key, value in os.environ.items()
                            if not key.startswith(("MM3_", "RECOMP_"))}

    def tearDown(self):
        self.temporary.cleanup()

    def run_launcher(self, launches, overrides=None, expect_failure=False, culture=None):
        self.environment.update(overrides or {})
        script = "$ErrorActionPreference = 'Stop'; $initial = (Get-Location).Path; $during = @(); "
        if culture:
            script += "[Threading.Thread]::CurrentThread.CurrentCulture = " + quoted(culture) + "; "
        for arguments in launches:
            invoke = "& " + quoted(self.launcher) + " -Exe " + quoted(self.collector) + " " + arguments
            script += "try { " + invoke + " } catch {} ; " if expect_failure else invoke + "; "
        script += ("if (Test-Path -LiteralPath " + quoted(self.collected) + ") { "
                   "$during = @(Get-Content -LiteralPath " + quoted(self.collected) +
                   " | ForEach-Object { $_ | ConvertFrom-Json }) }; ")
        script += self.snapshot
        script += ("[ordered]@{ during = $during; after = $settings; "
                   "location_restored = ((Get-Location).Path -eq $initial) } | ConvertTo-Json -Depth 5 -Compress")
        result = subprocess.run([SHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                                text=True, capture_output=True, env=self.environment, cwd=self.folder)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout.splitlines()[-1])

    def test_profile_selection_explicit_fps_and_repeated_launches_restore_environment(self):
        result = self.run_launcher(["-Profile Quality", "-Profile LowPower", "", "-FpsLimit 45"])
        self.assertEqual([row["MM3_PERFORMANCE_PROFILE"] for row in result["during"]],
                         ["Quality", "LowPower", "Balanced", "Balanced"])
        self.assertEqual([row["RECOMP_FPS_LIMIT"] for row in result["during"]], [None, None, None, "45"])
        for row in result["during"]:
            # Graphics/FPS profile values are chosen by the shared C helper.
            self.assertIsNone(row["RECOMP_RENDER_SCALE"])
            # The executable combines per-user preferences with these defaults.
            self.assertIsNone(row["RECOMP_ASPECT"])
            self.assertEqual(row["RECOMP_PB_EXEC"], "1")
            self.assertEqual(row["RECOMP_APU_DSP_ACK"], "0x80458810")
            self.assertIsNone(row["MM3_BUILDING_TRACE"])
            self.assertIsNone(row["RECOMP_TEXTURE_STREAM_TRACE"])
            self.assertIsNone(row["RECOMP_ASYNC_WAIT_TRACE"])
        self.assertTrue(result["location_restored"])
        self.assertTrue(all(value is None for value in result["after"].values()))
        metadata = [json.loads(path.read_text(encoding="utf-8"))
                    for path in sorted((self.folder / "logs").glob("play-*.json"))]
        self.assertEqual([row["profile_defaults_for_unset_settings"]["msaa"] for row in metadata],
                         ["8", "1", "4", "4"])
        self.assertEqual([row["profile_defaults_for_unset_settings"]["view_distance"] for row in metadata],
                         ["2", "1", "2", "2"])
        self.assertTrue(all(row["profile_defaults_for_unset_settings"]["world_ambient_lift"] == "0"
                            for row in metadata))

    def test_individual_environment_overrides_take_priority_and_survive_launch(self):
        overrides = {"MM3_PERFORMANCE_PROFILE": "Quality", "RECOMP_RENDER_SCALE": "2.25",
                     "RECOMP_MSAA": "8", "RECOMP_DRAW_DISTANCE": "1.25", "RECOMP_VIEW_DISTANCE": "2.5", "RECOMP_ANISO": "4",
                     "RECOMP_FPS_LIMIT": "47", "RECOMP_USB": "0", "MM3_FOV_DEGREES": "97"}
        result = self.run_launcher(["-Profile LowPower -FpsLimit 30 -Fov 100"], overrides)
        for key, value in overrides.items():
            self.assertEqual(result["during"][0][key], value)
            self.assertEqual(result["after"][key], value)
        self.assertTrue(result["location_restored"])

    def test_default_executable_uses_newest_staged_or_normal_build(self):
        # Invoke the actual selection function's AST, without launching any
        # executable or interacting with a game already running on the machine.
        candidates = [self.folder / path for path in (
            "build/RelWithDebInfo/mm3_recomp.exe", "build/Release/mm3_recomp.exe",
            "build/current/RelWithDebInfo/mm3_recomp.exe")]
        for index, path in enumerate(candidates):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"selection fixture only")
            os.utime(path, (100000 + index, 100000 + index))
        script = (
            "$tokens = $null; $parseErrors = $null; "
            "$ast = [Management.Automation.Language.Parser]::ParseFile(" +
            quoted(self.launcher) + ", [ref]$tokens, [ref]$parseErrors); "
            "if ($parseErrors.Count) { throw 'launcher parse failed' }; "
            "$selector = $ast.Find({ param($node) "
            "$node -is [Management.Automation.Language.FunctionDefinitionAst] -and "
            "$node.Name -eq 'Get-DefaultExecutable' }, $true).Body.GetScriptBlock(); "
            "$selected = @(); $selected += & $selector " + quoted(self.folder) + "; "
            "(Get-Item -LiteralPath " + quoted(candidates[0]) +
            ").LastWriteTimeUtc = [DateTime]::UtcNow; "
            "$selected += & $selector " + quoted(self.folder) + "; "
            "(Get-Item -LiteralPath " + quoted(candidates[1]) +
            ").LastWriteTimeUtc = [DateTime]::UtcNow.AddMinutes(1); "
            "$selected += & $selector " + quoted(self.folder) + "; "
            "$selected | ConvertTo-Json -Compress")
        result = subprocess.run([SHELL, "-NoProfile", "-Command", script], text=True,
                                capture_output=True, env=self.environment, cwd=self.folder, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), [str(candidates[2]), str(candidates[0]), str(candidates[1])])
        # Explicit helpers still take precedence despite all three native builds.
        response = self.run_launcher([""])
        self.assertEqual(len(response["during"]), 1)
        metadata = json.loads(next((self.folder / "logs").glob("play-*.json")).read_text(encoding="utf-8"))
        self.assertEqual(metadata["executable"]["path"], str(self.collector))

    def test_no_light_diagnostics_applies_only_to_selected_child_launches(self):
        overrides = {"MM3_LIGHT_DUMP": str(self.folder / "lighting captures"),
                     "MM3_LIGHT_DEBUG": "1"}
        result = self.run_launcher(["-NoLightDiagnostics", "", "-NoLightDiagnostics", ""], overrides)
        for index, row in enumerate(result["during"]):
            for key, value in overrides.items():
                self.assertEqual(row[key], value if index % 2 else None)
        for key, value in overrides.items():
            self.assertEqual(result["after"][key], value)
        self.assertTrue(result["location_restored"])
        manifests = sorted((self.folder / "logs").glob("play-*.json"))
        self.assertEqual(len(manifests), 4)
        for index, path in enumerate(manifests):
            metadata = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(metadata["launch_options"]["no_light_diagnostics"], index % 2 == 0)
            for key, value in overrides.items():
                if index % 2:
                    self.assertEqual(metadata["environment"][key], value)
                else:
                    self.assertNotIn(key, metadata["environment"])

    def test_no_light_diagnostics_restores_inherited_settings_after_child_failure(self):
        overrides = {"MM3_LIGHT_DUMP": str(self.folder / "lighting captures"),
                     "MM3_LIGHT_DEBUG": "1"}
        self.collector.write_text(self.collector.read_text(encoding="utf-8") +
                                  "; throw 'intentional child failure'", encoding="utf-8")
        result = self.run_launcher(["-NoLightDiagnostics", "", "-NoLightDiagnostics"], overrides,
                                   expect_failure=True)
        self.assertEqual(len(result["during"]), 3)
        for index, row in enumerate(result["during"]):
            for key, value in overrides.items():
                self.assertEqual(row[key], value if index == 1 else None)
        for key, value in overrides.items():
            self.assertEqual(result["after"][key], value)
        self.assertTrue(result["location_restored"])
        manifests = sorted((self.folder / "logs").glob("play-*.json"))
        self.assertEqual(len(manifests), 3)
        for index, path in enumerate(manifests):
            metadata = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(metadata["state"], "exited")
            self.assertNotEqual(metadata["exit_code"], 0)
            self.assertEqual(metadata["launch_options"]["no_light_diagnostics"], index != 1)

    def test_error_metadata_counts_runtime_failures_and_ignores_probe_noise(self):
        last = "[ICALL] non-code target 0x00000000 caller=0x00123456"
        self.collector.write_text(
            "[Console]::Error.WriteLine('[FILE_PROBE] candidate_miss count=128 err=3 ERROR_PATH_NOT_FOUND'); "
            "[Console]::Error.WriteLine('  [KERNEL] PsTerminateSystemThread: status=0x00000000 (worker)'); "
            "[Console]::Error.WriteLine('  [FILE] -> 0xC000003A NOT_FOUND (win32 err=3 ERROR_PATH_NOT_FOUND)'); "
            "[Console]::Error.WriteLine('[FILE_ERROR] operation=NtReadFile status=0xC0000022 err=5'); "
            "[Console]::Error.WriteLine('[CRASH] code=0xC0000005 guest_esp=0x00F70000'); "
            "[Console]::Error.WriteLine('[CRASH_CONTEXT] pause_active=1 race=0x01000000'); "
            "[Console]::Error.WriteLine('[ICALL_FAIL] unresolved target'); "
            "[Console]::Error.WriteLine('[BUILDING_ALLOCATION_FAILURE] requested_quads=800'); "
            "[Console]::Error.WriteLine('[D3D11_MAP_ERROR] square mask view failed hr=8007000E; keeping native mask'); "
            "[Console]::Error.WriteLine('[ICALL] Failed resolve VA 0x00000000 caller=0x00123456'); "
            "[Console]::Error.WriteLine(" + quoted(last) + "); "
            "[Console]::Error.WriteLine('[ICALL_STATE] sample=1 ebp=00000000'); "
            "[Console]::Error.WriteLine('[FILE_PROBE] candidate_miss count=256 err=3'); "
            "exit 0", encoding="utf-8")
        self.run_launcher([""])
        metadata = json.loads(next((self.folder / "logs").glob("play-*.json")).read_text(encoding="utf-8"))
        self.assertEqual(metadata["error_line_count"], 8)
        self.assertEqual(metadata["last_error_line"], last)
        self.assertEqual(metadata["last_error_source"], "stderr")
        self.assertRegex(metadata["last_error_utc"], r"^\d{4}-\d\d-\d\dT")
        self.assertEqual(metadata["exit_code"], 0)
        self.assertEqual(metadata["exit_code_hex"], "0x00000000")

    def test_failed_entry_point_still_restores_added_settings_and_working_directory(self):
        self.collector.write_text("throw 'intentional fixture failure'", encoding="utf-8")
        result = self.run_launcher(["-Profile LowPower -FpsLimit 42 -Fov 105"], expect_failure=True)
        self.assertTrue(result["location_restored"])
        self.assertTrue(all(value is None for value in result["after"].values()))

    def test_fov_boundaries_decimal_and_unset_restore_environment(self):
        result = self.run_launcher(["-Fov 60", "-Fov 110", "-Fov 100.5", ""], culture="fr-FR")
        self.assertEqual([row["MM3_FOV_DEGREES"] for row in result["during"]],
                         ["60", "110", "100.5", None])
        self.assertTrue(result["location_restored"])
        self.assertTrue(all(value is None for value in result["after"].values()))

    def test_out_of_range_fov_never_runs_entry_point(self):
        marker = self.folder / "entry-point-was-run"
        self.collector.write_text("Set-Content -LiteralPath " + quoted(marker) + " -Value 'unexpected'",
                                  encoding="utf-8")
        result = self.run_launcher(["-Fov 59", "-Fov 111"], expect_failure=True)
        self.assertFalse(marker.exists())
        self.assertTrue(result["location_restored"])
        self.assertTrue(all(value is None for value in result["after"].values()))

    def test_trace_switch_records_launch_configuration_and_respects_overrides(self):
        overrides = {"MM3_BUILDING_TRACE": "0", "RECOMP_DRAW_DISTANCE": "1.25",
                     "MM3_FOV_DEGREES": "97"}
        result = self.run_launcher(["-TraceBuildings -Profile LowPower -Fov 100", "-NoTraceBuildings"], overrides)
        first, second = result["during"]
        self.assertEqual(first["MM3_BUILDING_TRACE"], "0")
        self.assertEqual(first["RECOMP_TEXTURE_STREAM_TRACE"], "1")
        self.assertEqual(first["RECOMP_ASYNC_WAIT_TRACE"], "1")
        self.assertIsNone(second["RECOMP_TEXTURE_STREAM_TRACE"])
        self.assertIsNone(second["RECOMP_ASYNC_WAIT_TRACE"])
        self.assertEqual(result["after"]["MM3_BUILDING_TRACE"], "0")
        manifests = sorted((self.folder / "logs").glob("play-*.json"))
        self.assertEqual(len(manifests), 2)
        metadata = json.loads(manifests[0].read_text(encoding="utf-8"))
        self.assertEqual(metadata["state"], "exited")
        self.assertEqual(metadata["exit_code"], 0)
        self.assertEqual(metadata["environment"]["RECOMP_DRAW_DISTANCE"], "1.25")
        self.assertEqual(metadata["environment"]["MM3_FOV_DEGREES"], "97")
        self.assertEqual(metadata["profile_defaults_for_unset_settings"]["draw_distance"], "1")
        self.assertTrue(metadata["launch_options"]["trace_buildings"])
        self.assertEqual(metadata["executable"]["sha256"], hashlib.sha256(self.collector.read_bytes()).hexdigest())
        self.assertEqual(metadata["arguments"][-1], str(self.collector))
        files = {entry["path"]: entry for entry in metadata["configuration_files"]}
        xbe = self.folder / "game_files/default.xbe"
        self.assertEqual(files[str(xbe)]["sha256"], hashlib.sha256(xbe.read_bytes()).hexdigest())
        for manifest in manifests:
            saved = json.loads(manifest.read_text(encoding="utf-8"))
            log = Path(saved["log_path"]).read_text(encoding="utf-8")
            self.assertRegex(log, r"^\d{4}-\d\d-\d\dT.*\[stdout\] ")
            self.assertIn('"MM3_PERFORMANCE_PROFILE"', log)

    @unittest.skipUnless(os.name == "nt", "requires a harmless Windows native process fixture")
    def test_second_game_is_refused_before_launch_or_log_creation(self):
        # A renamed native ping process exercises the real process-name guard
        # independently of any game the user happens to be playing. Keep its
        # Popen handle so cleanup can only terminate this test-owned process.
        fixture = self.folder / "mm3_recomp.exe"
        ping = Path(os.environ["SystemRoot"]) / "System32/ping.exe"
        shutil.copyfile(ping, fixture)
        process = subprocess.Popen([str(fixture), "-t", "127.0.0.1"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            script = ("$ErrorActionPreference = 'Stop'; $initial = (Get-Location).Path; "
                      "$fixture = Get-Process -Id " + str(process.pid) + "; "
                      "if ($fixture.ProcessName -ne 'mm3_recomp') { throw 'invalid native fixture' }; "
                      "$refusal = $null; try { & " + quoted(self.launcher) +
                      " -Exe " + quoted(fixture) +
                      " } catch { $refusal = $_.Exception.Message }; ")
            script += self.snapshot
            script += ("[ordered]@{ refusal = $refusal; after = $settings; "
                       "location_restored = ((Get-Location).Path -eq $initial) } "
                       "| ConvertTo-Json -Depth 5 -Compress")
            result = subprocess.run([SHELL, "-NoProfile", "-ExecutionPolicy", "Bypass",
                                     "-Command", script], text=True, capture_output=True,
                                    env=self.environment, cwd=self.folder, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            response = json.loads(result.stdout.splitlines()[-1])
            self.assertIn("already running", response["refusal"])
            self.assertIn(str(process.pid), response["refusal"])
            self.assertIn("Close all existing game windows", response["refusal"])
            self.assertIn("cache", response["refusal"])
            self.assertFalse((self.folder / "logs").exists())
            self.assertTrue(response["location_restored"])
            self.assertTrue(all(value is None for value in response["after"].values()))
            self.assertIsNone(process.poll(), "preflight disturbed the existing native process")
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)

    def test_both_streams_are_saved_before_exit_and_nonzero_exit_code_survives(self):
        release = self.folder / "release child"
        self.collector.write_text(
            "[Console]::Out.WriteLine('stdout before hold'); "
            "[Console]::Error.WriteLine('stderr before hold'); "
            "while (-not (Test-Path -LiteralPath " + quoted(release) + ")) { Start-Sleep -Milliseconds 20 }; "
            "for ($i = 0; $i -lt 2500; ++$i) { [Console]::Out.WriteLine(('output-{0} ' -f $i) + ('x' * 128)) }; "
            "for ($i = 0; $i -lt 2500; ++$i) { [Console]::Error.WriteLine(('error-{0} ' -f $i) + ('y' * 128)) }; "
            "[Console]::Error.Write('final stderr without newline'); exit 37", encoding="utf-8")
        # Redirect the outer shell to files as well, so its console echo cannot
        # fill a pipe while the test deliberately leaves the child running.
        with (self.folder / "outer.out").open("w", encoding="utf-8") as output, \
                (self.folder / "outer.err").open("w", encoding="utf-8") as error:
            process = subprocess.Popen([SHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                        str(self.launcher), "-Exe", str(self.collector)],
                                       stdout=output, stderr=error, env=self.environment, cwd=self.folder)
            try:
                deadline = time.monotonic() + 20
                observed = ""
                while time.monotonic() < deadline:
                    logs = list((self.folder / "logs").glob("play-*.log"))
                    if logs:
                        observed = logs[0].read_text(encoding="utf-8")
                    if "stdout before hold" in observed and "stderr before hold" in observed:
                        break
                    if process.poll() is not None:
                        self.fail("launcher exited before the live capture: " +
                                  (self.folder / "outer.err").read_text(encoding="utf-8"))
                    time.sleep(.05)
                self.assertIn("[stdout] stdout before hold", observed)
                self.assertIn("[stderr] stderr before hold", observed)
                self.assertIsNone(process.poll())
                release.touch()
                self.assertEqual(process.wait(timeout=30), 37)
            finally:
                release.touch()
                if process.poll() is None:
                    process.wait(timeout=30)
        metadata = json.loads(next((self.folder / "logs").glob("play-*.json")).read_text(encoding="utf-8"))
        self.assertEqual(metadata["exit_code"], 37)
        self.assertEqual(metadata["exit_code_hex"], "0x00000025")
        self.assertEqual(metadata["error_line_count"], 0)
        self.assertIsNone(metadata["last_error_line"])
        self.assertEqual(metadata["state"], "exited")
        self.assertFalse(metadata["launch_options"]["no_console_log"])
        self.assertNotIn("capture_error", metadata)
        log = Path(metadata["log_path"]).read_text(encoding="utf-8")
        self.assertEqual(log.count("[stdout] output-"), 2500)
        self.assertEqual(log.count("[stderr] error-"), 2500)
        # Relative order between two pipes is not guaranteed. Per-stream EOF
        # still retains its final fragment, and default console echo is intact.
        stderr_lines = [line for line in log.splitlines() if "[stderr] " in line]
        self.assertTrue(stderr_lines[-1].endswith("[stderr] final stderr without newline"))
        console = (self.folder / "outer.out").read_text(encoding="utf-8")
        self.assertEqual(console.count("output-"), 2500)
        self.assertEqual(console.count("error-"), 2500)
        self.assertIn("stdout before hold\n", console)
        self.assertIn("stderr before hold\n", console)
        self.assertIn("final stderr without newline\n", console)

    def test_concurrent_flood_is_preserved_and_visible_without_console_echo(self):
        release = self.folder / "release flood"
        count = 6000
        payload = "x" * 512
        # Native .NET threads write both streams simultaneously, each well
        # beyond pipe capacity. The outer stdout pipe is deliberately unread
        # until exit: -NoConsoleLog must not echo this flood into that pipe.
        self.collector.write_text("""Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Threading;
public static class FloodLogFixture {
    public static void Run(string release) {
        Console.Out.WriteLine("stdout before flood");
        Console.Error.WriteLine("[FILE_ERROR] flood fixture initial error");
        string payload = new string('x', 512);
        var output = new Thread(() => {
            for (int i = 0; i < 6000; ++i)
                Console.Out.WriteLine("flood-out-{0:D5} {1}", i, payload);
        });
        var error = new Thread(() => {
            for (int i = 0; i < 6000; ++i)
                Console.Error.WriteLine("flood-err-{0:D5} {1}", i, payload);
        });
        output.Start(); error.Start(); output.Join(); error.Join();
        Console.Out.WriteLine("stdout flood complete; waiting");
        Console.Error.WriteLine("stderr flood complete; waiting");
        while (!File.Exists(release)) Thread.Sleep(20);
        Console.Out.WriteLine("final stdout after release");
        Console.Error.Write("[CRASH_CONTEXT] final stderr without newline");
    }
}
'@
[FloodLogFixture]::Run(""" + quoted(release) + ")\nexit 37\n", encoding="utf-8")
        process = subprocess.Popen(
            [SHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
             str(self.launcher), "-NoConsoleLog", "-Exe", str(self.collector)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env=self.environment, cwd=self.folder)
        try:
            deadline = time.monotonic() + 30
            observed = ""
            while time.monotonic() < deadline:
                logs = list((self.folder / "logs").glob("play-*.log"))
                if logs:
                    observed = logs[0].read_text(encoding="utf-8")
                if ("[stdout] stdout flood complete; waiting" in observed and
                        "[stderr] stderr flood complete; waiting" in observed):
                    break
                if process.poll() is not None:
                    output, error = process.communicate(timeout=5)
                    self.fail("launcher exited before the live flood capture: " + output + error)
                time.sleep(.05)
            self.assertIn("[stdout] stdout flood complete; waiting", observed)
            self.assertIn("[stderr] stderr flood complete; waiting", observed)
            self.assertIsNone(process.poll(), "live file was only flushed at exit")
            release.touch()
            output, error = process.communicate(timeout=30)
            self.assertEqual(process.returncode, 37, output + error)
        finally:
            release.touch()
            if process.poll() is None:
                process.communicate(timeout=30)
        self.assertNotIn("flood-out-", output)
        self.assertNotIn("flood-err-", output)
        metadata = json.loads(next((self.folder / "logs").glob("play-*.json")).read_text(encoding="utf-8"))
        self.assertTrue(metadata["launch_options"]["no_console_log"])
        self.assertEqual(metadata["state"], "exited")
        self.assertEqual(metadata["exit_code"], 37)
        self.assertEqual(metadata["exit_code_hex"], "0x00000025")
        self.assertEqual(metadata["error_line_count"], 2)
        self.assertEqual(metadata["last_error_line"], "[CRASH_CONTEXT] final stderr without newline")
        self.assertEqual(metadata["last_error_source"], "stderr")
        self.assertNotIn("capture_error", metadata)
        log = Path(metadata["log_path"]).read_text(encoding="utf-8")
        stdout = [line.split("[stdout] ", 1)[1] for line in log.splitlines() if "[stdout] " in line]
        stderr = [line.split("[stderr] ", 1)[1] for line in log.splitlines() if "[stderr] " in line]
        self.assertEqual(stdout, ["stdout before flood"] +
                         [f"flood-out-{i:05d} {payload}" for i in range(count)] +
                         ["stdout flood complete; waiting", "final stdout after release"])
        self.assertEqual(stderr, ["[FILE_ERROR] flood fixture initial error"] +
                         [f"flood-err-{i:05d} {payload}" for i in range(count)] +
                         ["stderr flood complete; waiting", "[CRASH_CONTEXT] final stderr without newline"])
        self.assertEqual(len(log.splitlines()), 2 * (count + 3))


if __name__ == "__main__":
    unittest.main()
