"""Stress plans and native evidence cannot silently become unsupported claims."""
import copy
import ctypes
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mm3_stress_harness", ROOT / "tools/bench/stress.py")
stress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stress)


def plan(**changes):
    value = {"coordinates": {"x": 100, "z": 200, "heading": 0},
             "scene": {"city": 0, "mode": 1, "time": 2, "traffic": 1, "peds": 1}}
    value.update(changes)
    return stress.validate_plan(value)


def snapshot():
    return {"scene": {"city": 0, "mode": 1, "time": 2, "traffic": 1, "peds": 1},
            "player": {"available": True, "x": 100, "y": 10, "z": 200, "heading": 0, "speed": 0},
            "collisions": {"total": 7, "wall": None, "vehicle": None},
            "traffic": {"count": None},
            "police": {"count": None, "ids": None, "active_count": None, "active_ids": None}}


def active_police_snapshot():
    state = snapshot()
    state["police"] = {"count": 2, "ids": [10, 20], "count_kind": "pool",
                       "active_count": 2, "active_ids": [10, 20],
                       "nearby_count": 2, "nearby_ids": [10, 20],
                       "actors": [{"id": 10, "mode": 1, "body": 0x10000, "x": 110, "y": 10, "z": 200},
                                  {"id": 20, "mode": 2, "body": 0x20000, "x": 100, "y": 12, "z": 210}]}
    return state


class StressHarnessTests(unittest.TestCase):
    def test_default_pattern_fits_with_native_release_gaps(self):
        value = plan()
        self.assertEqual(stress.pattern_duration_ms(value["pattern"]), 27700)
        self.assertLess(stress.pattern_duration_ms(value["pattern"]), value["window_seconds"] * 1000 - 1000)
        self.assertEqual(value["police_mode"], "active")

    def test_omitted_scale_preserves_poll_order_and_optional_scales_are_cartesian(self):
        unchanged = plan(poll_us=[0, 250])
        self.assertEqual(stress.ordered_conditions(unchanged, 0), [(0, None, None, None), (250, None, None, None)])
        self.assertEqual(stress.ordered_conditions(unchanged, 1), [(250, None, None, None), (0, None, None, None)])
        varied = plan(poll_us=[0, 250], render_scales=[4, 2.25, 2])
        expected = [(0, 4, None, None), (250, 4, None, None), (0, 2.25, None, None), (250, 2.25, None, None), (0, 2, None, None), (250, 2, None, None)]
        self.assertEqual(stress.ordered_conditions(varied, 0), expected)
        self.assertEqual(stress.ordered_conditions(varied, 1), list(reversed(expected)))
        backend = plan(fence_modes=[0, 1], render_scales=[4], poll_us=[250])
        self.assertEqual(stress.ordered_conditions(backend, 0), [(250, 4, 0, None), (250, 4, 1, None)])
        self.assertEqual(stress.ordered_conditions(backend, 1), [(250, 4, 1, None), (250, 4, 0, None)])
        audio = plan(audio_wait_modes=[0, 1], fence_modes=[1], render_scales=[4], poll_us=[250])
        self.assertEqual(stress.ordered_conditions(audio, 0), [(250, 4, 1, 0), (250, 4, 1, 1)])
        self.assertEqual(stress.ordered_conditions(audio, 1), [(250, 4, 1, 1), (250, 4, 1, 0)])

    def test_native_apu_conditions_are_opt_in_and_counterbalance_full_cartesian_order(self):
        value = plan(native_apu_modes=[0, 1], audio_wait_modes=[0, 1], poll_us=[250])
        legacy = [(250, None, None, 0), (250, None, None, 1)]
        self.assertEqual(stress.ordered_conditions(value, 0), legacy)
        expected = [(*condition, mode) for mode in (0, 1) for condition in legacy]
        self.assertEqual(stress.ordered_conditions(value, 0, include_native_apu=True), expected)
        self.assertEqual(stress.ordered_conditions(value, 1, include_native_apu=True), list(reversed(expected)))
        self.assertEqual(stress.ordered_conditions(plan(), 0, include_native_apu=True), [(250, None, None, None, None)])

    def test_bad_or_ambiguous_plans_are_rejected(self):
        for changes in ({"coordinates": {"x": 100, "z": 200}},
                        {"coordinates": {"x": math.nan, "z": 200, "heading": 0}},
                        {"repeats": True}, {"poll_us": [0, 0]}, {"police_count": 100},
                        {"pattern": ["rt:1000"]}, {"window_seconds": 20},
                        {"minimum_collisions": -1}, {"collision_kind": "guessed_wall"},
                        {"render_scales": []}, {"render_scales": [0]}, {"render_scales": [4, 4]},
                        {"fence_modes": []}, {"fence_modes": [False]}, {"fence_modes": [2]}, {"fence_modes": [0, 0]},
                        {"audio_wait_modes": []}, {"audio_wait_modes": [False]}, {"audio_wait_modes": [2]}, {"audio_wait_modes": [0, 0]},
                        {"native_apu_modes": []}, {"native_apu_modes": [False]}, {"native_apu_modes": [2]},
                        {"native_apu_modes": [0, 0]}, {"native_apu_modes": "1"}, {"native_apu_modes": [-1]},
                        {"police_mode": "guessed_active"}, {"shell": "whoami"}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                plan(**changes)

    def test_fence_change_waits_for_applied_backend_acknowledgement(self):
        class Client:
            def __init__(self):
                self.commands = []
                self.replies = iter([{"requested_fence": 1, "effective_fence": 0},
                                     {"requested_fence": 1, "effective_fence": 1}])
            def command(self, command):
                self.commands.append(command)
                return next(self.replies)
        client = Client()
        with patch.object(stress.time, "monotonic", side_effect=[0, .1]), patch.object(stress.time, "sleep"):
            self.assertEqual(stress.apply_fence(client, 1)["effective_fence"], 1)
        self.assertEqual(client.commands, ["stress fence 1", "stress fence 1"])

    def test_audio_wait_change_waits_for_producer_acknowledgement(self):
        class Client:
            def __init__(self):
                self.commands = []
                self.replies = iter([{"requested_audio_wait": 1, "effective_audio_wait": 0},
                                     {"requested_audio_wait": 1, "effective_audio_wait": 1}])
            def command(self, command):
                self.commands.append(command)
                return next(self.replies)
        client = Client()
        with patch.object(stress.time, "monotonic", side_effect=[0, .1]), patch.object(stress.time, "sleep"):
            self.assertEqual(stress.apply_audio_wait(client, 1)["effective_audio_wait"], 1)
        self.assertEqual(client.commands, ["stress audio_wait 1", "stress audio_wait 1"])

    def test_native_apu_acknowledgement_and_query_reject_unavailable_or_wrong_mode(self):
        class Client:
            def __init__(self, reply):
                self.reply, self.commands = reply, []
            def command(self, command):
                self.commands.append(command)
                return self.reply
        accepted = {"ok": True, "requested_native_apu": 1, "effective_native_apu": 1}
        client = Client(accepted)
        self.assertEqual(stress.apply_native_apu(client, 1), accepted)
        self.assertEqual(stress.check_native_apu(client, 1), accepted)
        self.assertEqual(client.commands, ["stress native_apu 1", "stress native_apu"])
        for reply in ({"ok": False, "requested_native_apu": 1, "effective_native_apu": 0},
                      {"ok": True, "requested_native_apu": 1, "effective_native_apu": 0},
                      {"ok": True, "requested_native_apu": 1, "effective_native_apu": True},
                      {"ok": True}):
            with self.subTest(reply=reply), self.assertRaises(ValueError):
                stress.apply_native_apu(Client(reply), 1)
            with self.subTest(query=reply), self.assertRaises(ValueError):
                stress.check_native_apu(Client(reply), 1)

    def test_native_apu_full_runner_records_six_paired_windows_and_detects_drift(self):
        # Run the production controller with a simulated clock/pipe and real
        # plan, snapshot, police-zero, movement and report validation. Only
        # frame-file parsing/input-log attribution are separate tested seams.
        for drift in (False, True):
            with self.subTest(drift=drift), tempfile.TemporaryDirectory() as temporary:
                folder = Path(temporary)
                pad = folder / "pad.txt"
                pad.write_text("")
                (folder / "stderr.log").write_text("")
                metadata = {"process_id": 123, "logical_processors": 4,
                            "settings": {"RECOMP_PAD_LIVE": str(pad), "RECOMP_FRAME_CSV": str(folder / "frames.csv"),
                                         "MM3_NATIVE_APU_MMIO": "1"}}
                class Client:
                    def __init__(self):
                        self.now, self.mode, self.lines = 0, 1, 0
                        self.state, self.commands = snapshot(), []
                        self.state["police"] = {"count": 0, "ids": []}
                        self.state["collisions"]["effects"] = 0
                    def qpc_ms(self): return self.now
                    def cpu_ms(self): return self.now * .5
                    def sleep(self, seconds): self.now += seconds * 1000
                    def command(self, command):
                        self.commands.append((self.now, command))
                        if command == "stress snapshot":
                            lines = len(pad.read_text().splitlines())
                            if lines != self.lines:
                                self.lines = lines
                                self.state["player"]["x"] += 10
                                self.state["player"]["heading"] += .2
                                self.state["collisions"]["effects"] += 2
                                if drift: self.mode = 1
                            self.state["qpc_ms"] = self.now
                            return copy.deepcopy(self.state)
                        if command.startswith("teleport "):
                            self.state["player"].update(x=100, z=200, heading=0)
                            self.lines = len(pad.read_text().splitlines())
                            return {"result": "applied"}
                        if command == "stress clear": return {"requested_total": 0, "total": 0}
                        if command == "stress poll 250": return {"effective_us": 250}
                        if command == "stress native_apu": return {"ok": True, "effective_native_apu": self.mode}
                        if command.startswith("stress native_apu "):
                            self.mode = int(command.rsplit(" ", 1)[1])
                            return {"ok": True, "requested_native_apu": self.mode, "effective_native_apu": self.mode}
                        raise AssertionError(command)
                client = Client()
                value = plan(repeats=3, native_apu_modes=[0, 1], window_seconds=10,
                             pattern=["rt:1000", "rt+lleft:500", "rt+lright:500"])
                with patch.object(stress, "load_run", return_value=metadata), \
                     patch.object(stress.time, "sleep", side_effect=client.sleep), \
                     patch.object(stress.analysis, "read_frames", return_value=({"frame_ms": [{"end_qpc_ms": 1e9}]}, {})), \
                     patch.object(stress.analysis, "analyze", return_value={}), \
                     patch.object(stress, "input_evidence", return_value={"pattern_applied": True}):
                    if drift:
                        with self.assertRaisesRegex(ValueError, "condition changed"):
                            stress.run_stress(folder, value, client=client)
                        failed = json.loads((folder / "stress-report.json").read_text())
                        self.assertFalse(failed["completed"])
                        self.assertFalse(failed["all_windows_verified"])
                        continue
                    report = stress.run_stress(folder, value, client=client)
                expected = [0, 1, 1, 0, 0, 1]
                self.assertTrue(report["completed"] and report["all_windows_verified"])
                self.assertEqual([r["native_apu"]["effective_native_apu"] for r in report["results"]], expected)
                self.assertEqual(report["metadata"]["settings"]["MM3_NATIVE_APU_MMIO"], "1")
                for record, mode in zip(report["results"], expected):
                    self.assertTrue(record["name"].endswith(f"-nativeapu{mode}"))
                    self.assertEqual(record["condition_settings"]["MM3_NATIVE_APU_MMIO"], str(mode))
                    self.assertEqual(record["native_apu_checks"]["before"]["effective_native_apu"], mode)
                    self.assertEqual(record["native_apu_checks"]["after"]["effective_native_apu"], mode)
                    start, end = record["window"]["start"], record["window"]["end"]
                    self.assertFalse(any(start <= when < end and command.startswith("stress native_apu ")
                                         for when, command in client.commands))

    def test_native_pipe_retries_unsent_missing_and_busy_instances_while_process_alive(self):
        invalid = ctypes.c_void_p(-1).value
        class Kernel:
            def __init__(self):
                self.results = iter([(invalid, 2), (invalid, 231), (42, 0)])
                self.opens, self.waits = 0, []
            def CreateFileW(self, *args):
                self.opens += 1
                handle, self.error = next(self.results)
                return handle
            def WaitNamedPipeW(self, name, milliseconds):
                self.waits.append(milliseconds)
                return False
        client = stress.NativeClient.__new__(stress.NativeClient)
        client.kernel = Kernel()
        alive_checks = []
        client.cpu_ms = lambda: alive_checks.append(True)
        with patch.object(stress.ctypes, "get_last_error", lambda: client.kernel.error, create=True), \
                patch.object(stress.time, "monotonic", side_effect=[0, .01, .02]), patch.object(stress.time, "sleep"):
            self.assertEqual(client._open_pipe("fixture"), 42)
        self.assertEqual(client.kernel.opens, 3)
        self.assertEqual(client.kernel.waits, [50])
        self.assertEqual(len(alive_checks), 3)

    def test_native_pipe_deadline_is_bounded_and_command_error_includes_unsent_stage(self):
        client = stress.NativeClient.__new__(stress.NativeClient)
        client.pid = 123
        with patch.object(client, "_open_pipe", side_effect=FileNotFoundError("fixture unavailable")):
            with self.assertRaisesRegex(RuntimeError, "stress police 12.*stage=open.*unsent"):
                client.command("stress police 12")
        invalid = ctypes.c_void_p(-1).value
        class Kernel:
            def CreateFileW(self, *args):
                return invalid
        client.kernel = Kernel()
        client.cpu_ms = lambda: 0
        with patch.object(stress.ctypes, "get_last_error", return_value=2, create=True), \
                patch.object(stress.ctypes, "WinError", side_effect=lambda code: OSError(code, "fixture"), create=True), \
                patch.object(stress.time, "monotonic", side_effect=[0, 3.1]):
            with self.assertRaises(OSError):
                client._open_pipe("fixture")

    def test_fixed_pose_heading_wrap_is_valid_but_different_spawn_rejected(self):
        state = snapshot()
        state["player"]["heading"] = 2 * math.pi
        stress.verify_snapshot(state, plan(), fixed_pose=True)
        state["player"]["x"] += 3
        with self.assertRaisesRegex(ValueError, "coordinates"):
            stress.verify_snapshot(state, plan(), fixed_pose=True)

    def test_scene_and_traffic_evidence_require_real_numbers(self):
        state = snapshot()
        state["scene"]["traffic"] = None
        with self.assertRaisesRegex(ValueError, "scene"):
            stress.verify_snapshot(state, plan())
        with self.assertRaisesRegex(ValueError, "traffic count"):
            stress.verify_snapshot(snapshot(), plan(minimum_traffic_cars=1))
        state = snapshot()
        state["traffic"]["count"] = 30
        stress.verify_snapshot(state, plan(minimum_traffic_cars=20))

    def test_police_pool_counts_cannot_be_reported_as_active_chase(self):
        state = snapshot()
        state["police"] = {"count": 2, "ids": [10, 20], "count_kind": "pool",
                           "active_count": None, "active_ids": None}
        reply = {"requested_total": 2, "total": 2, "created": [20]}
        result = stress.verify_police(reply, state, 2, "pool")
        self.assertEqual(result["scenario"], "police pool load")
        with self.assertRaisesRegex(ValueError, "Active police"):
            stress.verify_police(reply, state, 2, "active")
        state = active_police_snapshot()
        proof = stress.verify_police(reply, state, 2, "active")
        self.assertEqual(len(proof["nearby_body_proof"]), 2)
        state["police"]["ids"] = [10, 10]
        with self.assertRaises(ValueError):
            stress.verify_police(reply, state, 2, "pool")

    def test_active_police_requires_nearby_unique_ids_modes_and_real_body_positions(self):
        reply = {"requested_total": 2, "total": 2}
        mutations = [lambda police: police.update(nearby_count=None, nearby_ids=None),
                     lambda police: police.update(nearby_ids=[10, 10]),
                     lambda police: police["actors"][0].update(mode=0),
                     lambda police: police["actors"][0].update(body=0),
                     lambda police: police["actors"][0].update(x=math.nan),
                     lambda police: police["actors"][0].update(x=251),
                     lambda police: police["actors"][0].update(y=61),
                     lambda police: police.update(actors=[])]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                state = active_police_snapshot()
                mutate(state["police"])
                with self.assertRaises(ValueError):
                    stress.verify_police(reply, state, 2, "active")

    def test_active_wait_accepts_later_evidence_and_timeout_retains_native_failure(self):
        ready = active_police_snapshot()
        pending = copy.deepcopy(ready)
        pending["police"].update(active_count=0, active_ids=[], nearby_count=0, nearby_ids=[])
        class Client:
            def __init__(self, states):
                self.states = iter(states)
            def command(self, command):
                self.last_command = command
                return next(self.states)
        reply = {"requested_total": 2, "total": 2}
        with patch.object(stress.time, "monotonic", side_effect=[0, .01, .02]), patch.object(stress.time, "sleep"):
            state, proof, waiting = stress.wait_active_police(Client([pending, ready]), reply, plan(police_count=2))
        self.assertEqual(state, ready)
        self.assertEqual(proof["nearby_count"], 2)
        self.assertEqual(len(waiting["attempts"]), 2)
        with patch.object(stress.time, "monotonic", side_effect=[0, 5.1]):
            with self.assertRaises(stress.EvidenceFailure) as failure:
                stress.wait_active_police(Client([pending]), reply, plan(police_count=2))
        self.assertEqual(failure.exception.evidence["last_native_snapshot"]["police"]["active_count"], 0)

    def test_positive_police_preparation_preserves_pool_and_rejects_shrink(self):
        class Client:
            def __init__(self):
                self.commands = []
            def command(self, command):
                self.commands.append(command)
                return active_police_snapshot()
        client = Client()
        preparation = stress.prepare_police_pool(client, 12)
        self.assertEqual(client.commands, ["stress snapshot"])
        self.assertIsNone(preparation["clear"])
        self.assertEqual(preparation["prior_snapshot"]["police"]["ids"], [10, 20])
        with self.assertRaisesRegex(ValueError, "cannot shrink"):
            stress.prepare_police_pool(client, 1)
        client = Client()
        stress.prepare_police_pool(client, 0)
        self.assertEqual(client.commands, ["stress clear"])

    def test_runtime_health_rejects_fatal_guest_markers_but_not_optional_bank_misses(self):
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "stderr.log"
            log.write_text("[FILE_CALLER] ERROR_PATH_NOT_FOUND optional sound bank\n[AUDIO_PROFILE] submit_failures=0\n", encoding="utf-8")
            self.assertTrue(stress.require_runtime_clean(log)["runtime_clean"])
            for marker in ("[GUEST] R6025", "- pure virtual function call", "[KERNEL] *** KeBugCheck: code=0 ***", "[FATAL] invalid state",
                           "[CRASH] code=0xC0000005", "[CRASH_NATIVE] frame=0 address=1", "[CRASH_CONTEXT] registers",
                           "[ICALL] non-code target 0x04000350", "[ICALL] Failed resolve VA 0x00123456"):
                log.write_text("normal\n" + marker, encoding="utf-8")
                with self.subTest(marker=marker), self.assertRaises(stress.EvidenceFailure) as failure:
                    stress.require_runtime_clean(log)
                self.assertFalse(failure.exception.evidence["runtime_clean"])
                self.assertEqual(failure.exception.evidence["first_fatal_log_line"], 2)

    def test_police_reuse_proves_existing_ids_are_retained(self):
        prior = active_police_snapshot()
        preparation = {"prior_snapshot": prior}
        current = copy.deepcopy(prior)
        current["police"].update(count=3, ids=[10, 20, 30])
        proof = stress.police_reuse_proof(preparation, current)
        self.assertEqual(proof["reused_ids"], [10, 20])
        self.assertEqual(proof["new_ids"], [30])
        self.assertEqual(proof["removed_ids"], [])
        self.assertFalse(proof["clear_issued"])
        current["police"]["ids"] = [20, 30]
        with self.assertRaisesRegex(ValueError, "removed previously"):
            stress.police_reuse_proof(preparation, current)

    def test_collision_unknown_and_counter_regression_are_not_fabricated_zero(self):
        before, after = snapshot(), snapshot()
        after["player"].update(x=110, heading=.2)
        after["collisions"]["total"] += 2
        result = stress.window_checks(before, after, [before, after], plan(collision_kind="total"))
        self.assertEqual(result["collision_count_delta"], 2)
        self.assertTrue(result["collisions_verified"])
        self.assertTrue(result["heading_changed"])
        self.assertTrue(result["movement_verified"])
        unknown = stress.window_checks(before, after, [before, after], plan(collision_kind="wall"))
        self.assertIsNone(unknown["collision_count_delta"])
        self.assertFalse(unknown["collision_requirement_met"])
        after["collisions"]["total"] = 1
        regressed = stress.window_checks(before, after, [before, after], plan(minimum_collisions=0, collision_kind="total"))
        self.assertIsNone(regressed["collision_count_delta"])
        self.assertFalse(regressed["collisions_verified"])
        self.assertTrue(regressed["collision_requirement_met"])

    def test_input_requires_applied_steps_and_survives_script_index_compaction(self):
        pattern = ["rt:1000", "rt+lleft:500", "rt+lright:500"]
        lines = ['PAD: live "rt:1000" at t=100 ms\n',
                 'PAD: live "rt+lleft:500" at t=1300 ms\n',
                 'PAD: live "rt+lright:500" at t=2000 ms\n',
                 'PAD: t=101 ms step 126 (digital 0x00 analog 0x80)\n',
                 'PAD: t=1301 ms step 1 (digital 0x00 analog 0x80)\n',
                 'PAD: t=2001 ms step 2 (digital 0x00 analog 0x80)\n']
        result = stress.input_evidence(lines, 1, len(lines), pattern)
        self.assertTrue(result["pattern_applied"])
        self.assertEqual(result["left_turns_applied"], 1)
        self.assertEqual(result["right_turns_applied"], 1)
        only_queued = stress.input_evidence(lines, 1, 3, pattern)
        self.assertFalse(only_queued["pattern_applied"])
        self.assertFalse(only_queued["both_directions_applied"])

    def test_attach_requires_isolated_save_and_matching_executable(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            exe = folder / "game.exe"
            exe.write_bytes(b"fixture identity")
            (folder / "save").mkdir()
            metadata = {"executable": str(exe), "sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
                        "settings": {"MM3_DEBUG_TELEPORT": "1", "MM3_SAVE_DIR": str(folder / "save"),
                                     "MM3_DISPLAY_CONFIG": str(folder / "display.ini"),
                                     "MM3_LAUNCHER_CONFIG": str(folder / "launcher.ini"),
                                     "RECOMP_PAD_LIVE": str(folder / "pad.txt"),
                                     "RECOMP_FRAME_CSV": str(folder / "frames.csv")}}
            write = lambda value: (folder / "run.json").write_text(json.dumps(value), encoding="utf-8")
            write(metadata)
            self.assertEqual(stress.load_run(folder), metadata)
            unsafe = copy.deepcopy(metadata)
            unsafe["settings"]["MM3_SAVE_DIR"] = str(folder.parent / "original-save")
            write(unsafe)
            with self.assertRaisesRegex(ValueError, "isolated"):
                stress.load_run(folder)
            write(metadata)
            exe.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "identity"):
                stress.load_run(folder)


if __name__ == "__main__":
    unittest.main()
