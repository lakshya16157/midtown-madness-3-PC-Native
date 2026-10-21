"""Repeat measured driving windows through the native MM3 debug pipe.

Attaches only to an isolated benchmark run.json. It never writes process memory,
game assets, or the original save. A native stress-capable build is required.
"""
import argparse
import csv
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import statistics
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location(
    "mm3_stress_analysis", ROOT / "tools/diagnostics/analyze_stutter.py")
analysis = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(analysis)
DEFAULT_PATTERN = ["rt:4000", "rt+lleft:750", "rt:3000", "rt+lright:750"] * 3
PLAN_KEYS = {"coordinates", "scene", "repeats", "window_seconds", "warmup_seconds",
             "poll_us", "police_count", "minimum_displacement_m", "minimum_collisions",
             "minimum_traffic_cars", "collision_kind", "pattern", "sample_seconds", "police_mode", "capture",
             "render_scales", "fence_modes", "audio_wait_modes", "native_apu_modes"}


class EvidenceFailure(ValueError):
    def __init__(self, message, evidence):
        super().__init__(message)
        self.evidence = evidence


def number(value, name, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name} must be a finite number")
    if not minimum <= value <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return value


def integer(value, name, minimum, maximum):
    number(value, name, minimum, maximum)
    if int(value) != value:
        raise ValueError(f"{name} must be an integer")
    return int(value)


def pattern_duration_ms(pattern):
    # pad_live_poll adds a 200 ms release between each submitted step.
    return sum(int(step.rsplit(":", 1)[1]) for step in pattern) + 200 * (len(pattern) - 1)


def validate_plan(request):
    if not isinstance(request, dict) or set(request) - PLAN_KEYS:
        raise ValueError("Plan must be an object containing only documented stress fields")
    coordinates = request.get("coordinates")
    if not isinstance(coordinates, dict) or set(coordinates) != {"x", "z", "heading"}:
        raise ValueError("Explicit coordinates.x, coordinates.z and coordinates.heading are required")
    for key, value in coordinates.items():
        number(value, f"coordinates.{key}", -100000, 100000)
    scene = request.get("scene")
    if not isinstance(scene, dict) or not {"city", "mode", "time", "traffic", "peds"} <= set(scene):
        raise ValueError("scene requires actual native city, mode, time, traffic and peds values")
    if any(not isinstance(key, str) or not isinstance(value, (int, float)) or
           isinstance(value, bool) or not math.isfinite(value) for key, value in scene.items()):
        raise ValueError("Expected native scene fields must be finite numeric values")
    result = {"coordinates": coordinates, "scene": scene,
              "repeats": integer(request.get("repeats", 3), "repeats", 1, 20),
              "window_seconds": number(request.get("window_seconds", 30), "window_seconds", 10, 300),
              "warmup_seconds": number(request.get("warmup_seconds", 3), "warmup_seconds", 1, 30),
              "sample_seconds": number(request.get("sample_seconds", 1), "sample_seconds", .25, 5),
              "police_count": integer(request.get("police_count", 0), "police_count", 0, 16),
              "police_mode": request.get("police_mode", "active"),
              "capture": request.get("capture", False),
              "minimum_displacement_m": number(request.get("minimum_displacement_m", 1), "minimum_displacement_m", 0, 10000),
              "minimum_collisions": integer(request.get("minimum_collisions", 1), "minimum_collisions", 0, 100000),
              "minimum_traffic_cars": integer(request.get("minimum_traffic_cars", 0), "minimum_traffic_cars", 0, 10000),
              "collision_kind": request.get("collision_kind", "effects"),
              "pattern": request.get("pattern", DEFAULT_PATTERN), "poll_us": request.get("poll_us", [250]),
              "render_scales": request.get("render_scales"), "fence_modes": request.get("fence_modes"),
              "audio_wait_modes": request.get("audio_wait_modes"),
              "native_apu_modes": request.get("native_apu_modes")}
    if result["collision_kind"] not in {"total", "effects", "static", "vehicle", "wall"}:
        raise ValueError("collision_kind must be total, effects, static, vehicle or wall")
    if not isinstance(result["capture"], bool):
        raise ValueError("capture must be true or false")
    if result["police_mode"] not in {"pool", "active"}:
        raise ValueError("police_mode must be pool or active")
    if not isinstance(result["poll_us"], list) or not result["poll_us"]:
        raise ValueError("poll_us must be a nonempty list")
    result["poll_us"] = [integer(value, "poll_us", 0, 1000) for value in result["poll_us"]]
    if len(set(result["poll_us"])) != len(result["poll_us"]):
        raise ValueError("poll_us conditions must be unique")
    scales = result["render_scales"]
    native_apu = result["native_apu_modes"]
    if native_apu is not None:
        if not isinstance(native_apu, list) or not 1 <= len(native_apu) <= 2:
            raise ValueError("native_apu_modes must be a nonempty list containing 0 and/or 1")
        result["native_apu_modes"] = [integer(value, "native_apu_modes", 0, 1) for value in native_apu]
        if len(set(result["native_apu_modes"])) != len(native_apu):
            raise ValueError("native_apu_modes conditions must be unique")
    waits = result["audio_wait_modes"]
    if waits is not None:
        if not isinstance(waits, list) or not 1 <= len(waits) <= 2:
            raise ValueError("audio_wait_modes must be a nonempty list containing 0 and/or 1")
        result["audio_wait_modes"] = [integer(value, "audio_wait_modes", 0, 1) for value in waits]
        if len(set(result["audio_wait_modes"])) != len(waits):
            raise ValueError("audio_wait_modes conditions must be unique")
    fences = result["fence_modes"]
    if fences is not None:
        if not isinstance(fences, list) or not 1 <= len(fences) <= 2:
            raise ValueError("fence_modes must be a nonempty list containing 0 and/or 1")
        result["fence_modes"] = [integer(value, "fence_modes", 0, 1) for value in fences]
        if len(set(result["fence_modes"])) != len(fences):
            raise ValueError("fence_modes conditions must be unique")
    if scales is not None:
        if not isinstance(scales, list) or not 1 <= len(scales) <= 12:
            raise ValueError("render_scales must be a nonempty list of at most twelve scales")
        for scale in scales:
            number(scale, "render_scales", .5, 8)
        if len(set(scales)) != len(scales):
            raise ValueError("render_scales conditions must be unique")
    pattern = result["pattern"]
    if not isinstance(pattern, list) or not 1 <= len(pattern) <= 64 or any(
            not isinstance(step, str) or not re.fullmatch(r"rt(?:\+l(?:left|right))?:[1-9][0-9]{0,4}", step)
            for step in pattern):
        raise ValueError("pattern accepts only bounded rt, rt+lleft and rt+lright hold steps")
    if not any("lleft" in step for step in pattern) or not any("lright" in step for step in pattern):
        raise ValueError("pattern must exercise both steering directions")
    if pattern_duration_ms(pattern) > result["window_seconds"] * 1000 - 1000:
        raise ValueError("Pattern plus 200 ms releases must finish at least one second before the window ends")
    return result


def under(path, folder):
    try:
        Path(path).resolve().relative_to(Path(folder).resolve())
        return True
    except ValueError:
        return False


def load_run(folder):
    folder = Path(folder).resolve()
    metadata = json.loads((folder / "run.json").read_text(encoding="utf-8-sig"))
    settings = metadata["settings"]
    for key in ("MM3_SAVE_DIR", "MM3_DISPLAY_CONFIG", "MM3_LAUNCHER_CONFIG",
                "RECOMP_PAD_LIVE", "RECOMP_FRAME_CSV"):
        if not settings.get(key) or not under(settings[key], folder):
            raise ValueError(f"{key} must point inside the isolated run directory")
    if settings.get("MM3_DEBUG_TELEPORT") != "1":
        raise ValueError("Run must enable MM3_DEBUG_TELEPORT=1")
    if not Path(settings["MM3_SAVE_DIR"]).is_dir():
        raise ValueError("The isolated save directory is unavailable")
    executable = Path(metadata["executable"])
    actual_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
    if actual_hash.lower() != metadata["sha256"].lower():
        raise ValueError("Executable on disk no longer matches the run identity")
    return metadata


class NativeClient:
    """Local raw message-pipe client with bounded overlapped I/O."""
    def __init__(self, pid):
        if os.name != "nt":
            raise OSError("Native stress control requires Windows")
        self.pid = pid
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        k = self.kernel
        for name, args, result in (
            ("CreateFileW", [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                             wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE], wintypes.HANDLE),
            ("WaitNamedPipeW", [wintypes.LPCWSTR, wintypes.DWORD], wintypes.BOOL),
            ("CreateEventW", [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR], wintypes.HANDLE),
            ("CloseHandle", [wintypes.HANDLE], wintypes.BOOL),
            ("WaitForSingleObject", [wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
            ("CancelIoEx", [wintypes.HANDLE, ctypes.c_void_p], wintypes.BOOL),
            ("GetOverlappedResult", [wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), wintypes.BOOL], wintypes.BOOL),
            ("ReadFile", [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL),
            ("WriteFile", [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL),
            ("OpenProcess", [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            ("GetExitCodeProcess", [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            ("GetProcessTimes", [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4, wintypes.BOOL),
            ("QueryPerformanceCounter", [ctypes.POINTER(ctypes.c_longlong)], wintypes.BOOL),
            ("QueryPerformanceFrequency", [ctypes.POINTER(ctypes.c_longlong)], wintypes.BOOL),
        ):
            method = getattr(k, name)
            method.argtypes, method.restype = args, result
        self.process = k.OpenProcess(0x1000, False, pid)  # Query only; no VM write access.
        if not self.process:
            raise ctypes.WinError(ctypes.get_last_error())
        frequency = ctypes.c_longlong()
        if not k.QueryPerformanceFrequency(ctypes.byref(frequency)) or frequency.value <= 0:
            self.close()
            raise OSError("QPC frequency unavailable")
        self.frequency = frequency.value

    def close(self):
        if self.process:
            self.kernel.CloseHandle(self.process)
            self.process = None

    def qpc_ms(self):
        value = ctypes.c_longlong()
        if not self.kernel.QueryPerformanceCounter(ctypes.byref(value)):
            raise ctypes.WinError(ctypes.get_last_error())
        return value.value * 1000 / self.frequency

    def cpu_ms(self):
        code = wintypes.DWORD()
        if not self.kernel.GetExitCodeProcess(self.process, ctypes.byref(code)) or code.value != 259:
            raise RuntimeError("Game process exited during stress measurement")
        values = [wintypes.FILETIME() for _ in range(4)]
        if not self.kernel.GetProcessTimes(self.process, *(ctypes.byref(value) for value in values)):
            raise ctypes.WinError(ctypes.get_last_error())
        return sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in values[2:]) / 10000

    def _io(self, handle, buffer, length, write=False):
        class Overlapped(ctypes.Structure):
            _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                        ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD),
                        ("hEvent", wintypes.HANDLE)]
        event = self.kernel.CreateEventW(None, True, False, None)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        state, done = Overlapped(hEvent=event), wintypes.DWORD()
        try:
            method = self.kernel.WriteFile if write else self.kernel.ReadFile
            if not method(handle, buffer, length, ctypes.byref(done), ctypes.byref(state)):
                error = ctypes.get_last_error()
                if error != 997:  # ERROR_IO_PENDING
                    raise ctypes.WinError(error)
                if self.kernel.WaitForSingleObject(event, 5000) != 0:
                    self.kernel.CancelIoEx(handle, ctypes.byref(state))
                    self.kernel.GetOverlappedResult(handle, ctypes.byref(state), ctypes.byref(done), True)
                    raise TimeoutError("Native debug pipe I/O timed out")
                if not self.kernel.GetOverlappedResult(handle, ctypes.byref(state), ctypes.byref(done), False):
                    raise ctypes.WinError(ctypes.get_last_error())
            return done.value
        finally:
            self.kernel.CloseHandle(event)

    def _open_pipe(self, name):
        deadline = time.monotonic() + 3
        while True:
            self.cpu_ms()  # The owned process must remain alive on every retry.
            handle = self.kernel.CreateFileW(name, 0xC0000000, 0, None, 3, 0x40000000, None)
            if handle != ctypes.c_void_p(-1).value:
                return handle
            error = ctypes.get_last_error()
            if error not in (2, 231) or time.monotonic() >= deadline:
                raise ctypes.WinError(error)
            if error == 231:
                self.kernel.WaitNamedPipeW(name, 50)
            # The server briefly has no named instance between CloseHandle and
            # CreateNamedPipe. Retry opening only; no command has been sent.
            time.sleep(.01)

    def command(self, command):
        name = rf"\\.\pipe\MM3Teleport-{self.pid}"
        stage = "open (command unsent)"
        try:
            handle = self._open_pipe(name)
        except Exception as error:
            raise RuntimeError(f"Native command {command!r}, stage={stage}: {error}") from error
        try:
            stage = "write (mutation may be sent)"
            message = command.encode("ascii")
            outgoing = ctypes.create_string_buffer(message)
            if self._io(handle, outgoing, len(message), write=True) != len(message):
                raise OSError("Incomplete debug command write")
            stage = "read reply (command sent; no replay)"
            incoming = ctypes.create_string_buffer(65536)
            size = self._io(handle, incoming, len(incoming))
            reply = json.loads(incoming.raw[:size].decode("utf-8"))
            if not isinstance(reply, dict) or reply.get("error") or reply.get("ok") is False:
                raise RuntimeError(f"Native command {command!r} failed: {reply}")
            return reply
        except Exception as error:
            raise RuntimeError(f"Native command {command!r}, stage={stage}: {error}") from error
        finally:
            self.kernel.CloseHandle(handle)


def scene_matches(actual, expected):
    return isinstance(actual, dict) and all(
        isinstance(actual.get(key), (int, float)) and not isinstance(actual.get(key), bool) and
        math.isfinite(actual[key]) and abs(actual[key] - value) <= 1e-6
        for key, value in expected.items())


def verify_snapshot(snapshot, plan, fixed_pose=False):
    if not scene_matches(snapshot.get("scene"), plan["scene"]):
        raise ValueError(f"Actual native scene does not match plan: {snapshot.get('scene')}")
    player = snapshot.get("player", {})
    if not player.get("available"):
        raise ValueError("No active native player car")
    for key in ("x", "y", "z", "heading", "speed"):
        if not isinstance(player.get(key), (int, float)) or not math.isfinite(player[key]):
            raise ValueError(f"Native player.{key} is unavailable")
    if fixed_pose:
        pose = plan["coordinates"]
        if math.hypot(player["x"] - pose["x"], player["z"] - pose["z"]) > 2:
            raise ValueError("Native reset did not establish the requested fixed coordinates")
        error = math.remainder(player["heading"] - pose["heading"], 2 * math.pi)
        if abs(error) > .05:
            raise ValueError("Native reset did not establish the requested heading")
    minimum = plan["minimum_traffic_cars"]
    traffic = snapshot.get("traffic", {}).get("count")
    if minimum and (not isinstance(traffic, int) or isinstance(traffic, bool) or traffic < minimum):
        raise ValueError("Requested actual traffic count is unavailable or insufficient")


def verify_police(reply, snapshot, count, mode="active"):
    police = snapshot.get("police") or {}
    ids, observed = police.get("ids"), police.get("count")
    if (reply.get("requested_total") != count or reply.get("total") != count or
        not isinstance(observed, int) or isinstance(observed, bool) or observed != count or
        not isinstance(ids, list) or len(ids) != count or
        any(not isinstance(actor, int) or isinstance(actor, bool) or actor <= 0 for actor in ids) or
        len(set(ids)) != count):
        raise ValueError("Police pool total is not verified by actual native IDs and count")
    proof = []
    if mode == "active":
        active, active_ids = police.get("active_count"), police.get("active_ids")
        nearby, nearby_ids = police.get("nearby_count"), police.get("nearby_ids")
        def valid_ids(values, reported):
            return (isinstance(reported, int) and not isinstance(reported, bool) and reported >= count and
                    isinstance(values, list) and len(values) == reported and
                    all(isinstance(actor, int) and not isinstance(actor, bool) and actor > 0 for actor in values) and
                    len(set(values)) == len(values) and set(values) <= set(ids))
        if not valid_ids(active_ids, active):
            raise ValueError("Active police cars are unavailable; pool count alone does not prove an active chase")
        if not valid_ids(nearby_ids, nearby) or not set(nearby_ids) <= set(active_ids):
            raise ValueError("Nearby active police IDs and count are unavailable or insufficient")
        actors = police.get("actors")
        if not isinstance(actors, list) or any(not isinstance(actor, dict) for actor in actors):
            raise ValueError("Active police body positions are unavailable")
        actor_ids = [actor.get("id") for actor in actors]
        if any(not isinstance(actor, int) or isinstance(actor, bool) for actor in actor_ids) or len(set(actor_ids)) != len(actor_ids):
            raise ValueError("Police actor position IDs are invalid or duplicated")
        indexed = {actor["id"]: actor for actor in actors}
        player = snapshot.get("player") or {}
        for key in ("x", "y", "z"):
            if not isinstance(player.get(key), (int, float)) or not math.isfinite(player[key]):
                raise ValueError("Player position is unavailable for police distance proof")
        for actor_id in nearby_ids:
            actor = indexed.get(actor_id, {})
            if (not isinstance(actor.get("mode"), int) or isinstance(actor.get("mode"), bool) or actor["mode"] == 0 or
                not isinstance(actor.get("body"), int) or isinstance(actor.get("body"), bool) or actor["body"] <= 0):
                raise ValueError("Nearby police actor has no active native mode or validated body")
            for key in ("x", "y", "z"):
                if (not isinstance(actor.get(key), (int, float)) or isinstance(actor.get(key), bool) or
                    not math.isfinite(actor[key])):
                    raise ValueError("Nearby police body position is unavailable or non-finite")
            horizontal = math.hypot(actor["x"] - player["x"], actor["z"] - player["z"])
            vertical = abs(actor["y"] - player["y"])
            # Printed guest float coordinates can round at the range boundary.
            if horizontal > 150.001 or vertical > 50.001:
                raise ValueError("Nearby police body is outside 150 m horizontal / 50 m vertical limits")
            proof.append({"id": actor_id, "body": actor["body"], "mode": actor["mode"],
                          "x": actor["x"], "y": actor["y"], "z": actor["z"],
                          "horizontal_distance_m": horizontal, "vertical_distance_m": vertical})
    return {"requested_total": count, "observed_pool_count": observed, "count_kind": police.get("count_kind"),
            "active_count": police.get("active_count"), "nearby_count": police.get("nearby_count"),
            "nearby_body_proof": proof, "distance_limits_m": {"horizontal": 150, "vertical": 50,
                                                               "serialized_coordinate_tolerance": .001},
            "scenario": "police pool load" if mode == "pool" else "nearby active police cars"}


def wait_active_police(client, reply, plan):
    begin = time.monotonic()
    attempts = []
    while True:
        snapshot = client.command("stress snapshot")
        verify_snapshot(snapshot, plan)
        try:
            proof = verify_police(reply, snapshot, plan["police_count"], "active")
            attempts.append({"qpc_ms": snapshot.get("qpc_ms"), "verified": True})
            return snapshot, proof, {"elapsed_ms": (time.monotonic() - begin) * 1000, "attempts": attempts}
        except ValueError as error:
            police = snapshot.get("police") or {}
            attempts.append({"qpc_ms": snapshot.get("qpc_ms"), "verified": False,
                             "active_count": police.get("active_count"), "nearby_count": police.get("nearby_count"),
                             "error": str(error)})
            if time.monotonic() - begin >= 5:
                raise EvidenceFailure(
                    f"Nearby active police were not verified within 5 seconds: {error}",
                    {"phase": "police_activation", "spawn_reply": reply,
                     "last_native_snapshot": snapshot, "attempts": attempts}) from None
            time.sleep(.05)


def prepare_police_pool(client, requested):
    if not requested:
        return {"operation": "clear_zero_baseline", "clear": client.command("stress clear"),
                "prior_snapshot": None}
    snapshot = client.command("stress snapshot")
    count = (snapshot.get("police") or {}).get("count")
    if not isinstance(count, int) or isinstance(count, bool) or count < 0:
        raise ValueError("Existing police pool total is unavailable")
    verify_police({"requested_total": count, "total": count}, snapshot, count, "pool")
    if count > requested:
        raise ValueError("Positive police scenarios cannot shrink an existing pool; use a fresh isolated session")
    return {"operation": "preserve_existing_pool", "clear": None, "prior_snapshot": snapshot}


def police_reuse_proof(preparation, snapshot):
    prior = preparation.get("prior_snapshot")
    if prior is None:
        return None
    previous = prior["police"]
    current = snapshot["police"]
    previous_ids, current_ids = set(previous["ids"]), set(current["ids"])
    removed = sorted(previous_ids - current_ids)
    if removed:
        raise ValueError("Police total request removed previously verified pool IDs")
    return {"previous_pool_count": previous["count"], "previous_pool_ids": previous["ids"],
            "observed_pool_count": current["count"], "reused_ids": sorted(previous_ids & current_ids),
            "new_ids": sorted(current_ids - previous_ids), "removed_ids": removed,
            "clear_issued": False}


def window_checks(before, after, snapshots, plan):
    first, last = before["player"], after["player"]
    displacement = math.hypot(last["x"] - first["x"], last["z"] - first["z"])
    path = sum(math.hypot(b["player"]["x"] - a["player"]["x"],
                          b["player"]["z"] - a["player"]["z"])
               for a, b in zip(snapshots, snapshots[1:]))
    kind = plan["collision_kind"]
    initial = before.get("collisions", {}).get(kind)
    final = after.get("collisions", {}).get(kind)
    delta = (final - initial if isinstance(initial, int) and not isinstance(initial, bool) and
             isinstance(final, int) and not isinstance(final, bool) and final >= initial else None)
    turn = max((abs(math.remainder(s["player"]["heading"] - first["heading"], 2 * math.pi))
                for s in snapshots), default=0)
    return {"horizontal_displacement_m": displacement, "sampled_horizontal_path_m": path,
            "movement_verified": displacement >= plan["minimum_displacement_m"],
            "heading_excursion_rad": turn, "heading_changed": turn >= .05,
            "collision_kind": kind, "collision_count_delta": delta,
            "collisions_verified": delta is not None and delta >= plan["minimum_collisions"],
            "collision_requirement_met": plan["minimum_collisions"] == 0 or
                                         (delta is not None and delta >= plan["minimum_collisions"]),
            "actual_traffic_count_available": isinstance(before.get("traffic", {}).get("count"), int),
            "actual_police_count_available": isinstance(before.get("police", {}).get("count"), int)}


def input_evidence(lines, first_line, last_line, pattern):
    scheduled, applied = [], []
    for number, line in enumerate(lines, 1):
        if not first_line <= number <= last_line:
            continue
        match = re.search(r'PAD: live "([^"]+)" at t=(\d+) ms', line)
        if match and match[1] in pattern:
            scheduled.append({"command": match[1], "runtime_ms": int(match[2]), "log_line": number})
        match = re.search(r"PAD: t=(\d+) ms step (\d+) ", line)
        if match:
            tick = int(match[1])
            for step in scheduled:
                duration = int(step["command"].rsplit(":", 1)[1])
                if step["runtime_ms"] <= tick < step["runtime_ms"] + duration:
                    applied.append({"command": step["command"], "runtime_ms": tick,
                                    "log_line": number, "step": int(match[2])})
                    break
    expected_left = sum("lleft" in step for step in pattern)
    expected_right = sum("lright" in step for step in pattern)
    return {"scheduled": scheduled, "applied": applied,
            "left_turns_applied": sum("lleft" in step["command"] for step in applied),
            "right_turns_applied": sum("lright" in step["command"] for step in applied),
            "pattern_applied": [step["command"] for step in applied] == pattern,
            "both_directions_applied": sum("lleft" in step["command"] for step in applied) == expected_left and
                                       sum("lright" in step["command"] for step in applied) == expected_right}


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def line_count(path):
    return len(analysis.complete_lines(path)[0]) if Path(path).exists() else 0


def runtime_health(path):
    fatal = re.compile(r"\bR6025\b|\bpure virtual function call\b|\bKeBugCheck\b|\bFATAL\b|\[CRASH(?:_[A-Z]+)*\]|\[ICALL\] (?:non-code target|Failed resolve)", re.IGNORECASE)
    lines = Path(path).read_bytes().decode("utf-8", errors="replace").splitlines() if Path(path).exists() else []
    records = [{"log_line": number, "message": line.rstrip()}
               for number, line in enumerate(lines, 1) if fatal.search(line)]
    return {"runtime_clean": not records, "fatal_records": records,
            "first_fatal_log_line": records[0]["log_line"] if records else None}


def require_runtime_clean(path):
    health = runtime_health(path)
    if not health["runtime_clean"]:
        raise EvidenceFailure("Fatal guest/runtime diagnostics invalidate further stress windows",
                              {"phase": "runtime_health", **health})
    return health


def apply_poll(client, cadence):
    deadline = time.monotonic() + 3
    while True:
        reply = client.command(f"stress poll {cadence}")
        if reply.get("effective_us") == cadence:
            return reply
        if time.monotonic() >= deadline:
            raise ValueError(f"Readback polling change was not applied: {reply}")
        time.sleep(.05)


def apply_scale(client, scale):
    deadline = time.monotonic() + 3
    while True:
        reply = client.command(f"stress scale {scale:.9g}")
        effective = reply.get("effective_scale")
        if isinstance(effective, (int, float)) and math.isfinite(effective) and abs(effective - scale) < .0001:
            return reply
        if time.monotonic() >= deadline:
            raise ValueError(f"Rendering scale change was not applied: {reply}")
        time.sleep(.05)


def apply_fence(client, fence):
    deadline = time.monotonic() + 3
    while True:
        reply = client.command(f"stress fence {fence}")
        if reply.get("effective_fence") == fence:
            return reply
        if time.monotonic() >= deadline:
            raise ValueError(f"GPU completion mechanism change was not applied: {reply}")
        time.sleep(.05)


def apply_audio_wait(client, mode):
    deadline = time.monotonic() + 3
    while True:
        reply = client.command(f"stress audio_wait {mode}")
        if reply.get("effective_audio_wait") == mode:
            return reply
        if time.monotonic() >= deadline:
            raise ValueError(f"Audio producer wait change was not applied: {reply}")
        time.sleep(.05)


def apply_native_apu(client, mode):
    reply = client.command(f"stress native_apu {mode}")
    if (reply.get("ok") is not True or reply.get("requested_native_apu") != mode or
            type(reply.get("effective_native_apu")) is not int or reply["effective_native_apu"] != mode):
        raise ValueError(f"Native APU access change was not applied: {reply}")
    return reply


def check_native_apu(client, mode):
    reply = client.command("stress native_apu")
    if (reply.get("ok") is not True or type(reply.get("effective_native_apu")) is not int or
            reply["effective_native_apu"] != mode):
        raise ValueError(f"Native APU access condition changed or is unavailable: {reply}")
    return reply


def ordered_conditions(plan, repeat, include_native_apu=False):
    # Preserve the original public 4-tuple API. Only the measured runner opts
    # into the extra condition; legacy callers see their existing dimensions.
    modes = (plan.get("native_apu_modes") or [None]) if include_native_apu else [None]
    conditions = [(cadence, scale, fence, wait, native_apu) for native_apu in modes
                  for wait in (plan["audio_wait_modes"] or [None])
                  for fence in (plan["fence_modes"] or [None])
                  for scale in (plan["render_scales"] or [None])
                  for cadence in plan["poll_us"]]
    if not include_native_apu:
        conditions = [condition[:4] for condition in conditions]
    return conditions if repeat % 2 == 0 else list(reversed(conditions))


def capture_frame(client, prefix, folder):
    prefix = Path(prefix)
    if not under(prefix, folder):
        raise ValueError("Capture prefix must be inside the isolated run")
    existing = list(prefix.parent.glob(prefix.name + "-*.bmp"))
    ordinal = max((int(path.stem.rsplit("-", 1)[1]) for path in existing), default=-1) + 1
    destination = prefix.parent / f"{prefix.name}-{ordinal:03d}.bmp"
    Path(str(prefix) + ".flag").write_text("1", encoding="ascii")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        client.cpu_ms()
        try:
            with destination.open("rb") as stream:
                header = stream.read(54)
            if (len(header) == 54 and header[:2] == b"BM" and
                int.from_bytes(header[2:6], "little") == destination.stat().st_size):
                return {"file": str(destination), "qpc_ms": client.qpc_ms()}
        except OSError:
            pass
        time.sleep(.1)
    raise TimeoutError("No complete native present capture arrived")


def snapshot_after(client, qpc_ms):
    deadline = time.monotonic() + 2
    while True:
        snapshot = client.command("stress snapshot")
        sampled = snapshot.get("qpc_ms")
        if isinstance(sampled, (int, float)) and math.isfinite(sampled) and sampled >= qpc_ms:
            return snapshot
        if time.monotonic() >= deadline:
            raise TimeoutError("Native copied snapshot did not advance past the capture boundary")
        time.sleep(.025)


def run_stress(folder, plan, client=None, stationary_seconds=0):
    folder, plan = Path(folder).resolve(), validate_plan(plan)
    stationary_seconds = integer(stationary_seconds, "stationary_seconds", 0, 120)
    metadata = load_run(folder)
    if (folder / "stress-report.json").exists():
        raise ValueError("This run already has a stress report; use a fresh isolated session")
    if (folder / "windows.json").exists() and json.loads((folder / "windows.json").read_text(encoding="utf-8-sig")):
        raise ValueError("Run already contains measurement windows; use a fresh isolated session")
    owned_client = client is None
    client = client or NativeClient(int(metadata["process_id"]))
    pad, frames = (Path(metadata["settings"][key]) for key in ("RECOMP_PAD_LIVE", "RECOMP_FRAME_CSV"))
    log = folder / "stderr.log"
    capture_prefix = metadata["settings"].get("RECOMP_PRESENT_CAPTURE")
    if plan["capture"] and (not capture_prefix or not under(capture_prefix, folder)):
        if owned_client:
            client.close()
        raise ValueError("Capture-enabled plan requires an isolated RECOMP_PRESENT_CAPTURE prefix")
    report = {"plan": plan, "metadata": metadata, "windows": [], "results": [], "completed": False,
              "runtime_clean": True,
              "all_windows_verified": False,
              "notes": ["Each measured window resets the native car to the same explicit coordinates/heading.",
                        "World traffic and shader state can evolve within one process; alternating order does not make them deterministic.",
                        "Unknown native counts/classifications remain unavailable; no menu setting proves a police spawn or collision.",
                        "Input sequence includes the native live-input parser's 200 ms release gaps.",
                        "Counters and interval overlaps establish observations, not automatic causal attribution."]}
    write_json(folder / "stress-plan.json", plan)
    try:
        verify_snapshot(client.command("stress snapshot"), plan)
        if stationary_seconds:
            # A no-input control precedes spawned police/driving. Do not call
            # it a collision test or infer a frozen/deterministic traffic world.
            cadence, scale, fence, audio_wait, native_apu = ordered_conditions(plan, 0, include_native_apu=True)[0]
            apply_poll(client, cadence)
            if scale is not None:
                apply_scale(client, scale)
            if fence is not None:
                apply_fence(client, fence)
            if audio_wait is not None:
                apply_audio_wait(client, audio_wait)
            native_apu_reply = apply_native_apu(client, native_apu) if native_apu is not None else None
            pose = plan["coordinates"]
            reset = client.command(f"teleport {pose['x']:.9g} {pose['z']:.9g} {pose['heading']:.9g}")
            if reset.get("result") != "applied":
                raise ValueError(f"Stationary native reset failed: {reset}")
            time.sleep(plan["warmup_seconds"])
            before = snapshot_after(client, client.qpc_ms())
            verify_snapshot(before, plan, fixed_pose=True)
            native_apu_before = check_native_apu(client, native_apu) if native_apu is not None else None
            cpu_start, begin = client.cpu_ms(), client.qpc_ms()
            snapshots = [before]
            deadline = begin + stationary_seconds * 1000
            while client.qpc_ms() < deadline:
                time.sleep(min(.5, max(0, deadline - client.qpc_ms()) / 1000))
                snapshots.append(client.command("stress snapshot"))
            end, cpu_end = client.qpc_ms(), client.cpu_ms()
            native_apu_after = check_native_apu(client, native_apu) if native_apu is not None else None
            require_runtime_clean(log)
            for sample in snapshots:
                verify_snapshot(sample, plan)
            displacement = max(math.hypot(s["player"]["x"]-before["player"]["x"],
                                          s["player"]["z"]-before["player"]["z"]) for s in snapshots)
            report["stationary_control"] = {
                "window": {"start": begin, "end": end, "cpu_ms": cpu_end-cpu_start},
                "snapshots": snapshots,
                "input": "No input submitted during this control; traffic simulation remains live.",
                "spawned_police": False,
                "native_apu": native_apu_reply,
                "native_apu_checks": {"before": native_apu_before, "after": native_apu_after},
                "max_horizontal_displacement_m": displacement,
                "position_stayed_within_half_metre": displacement <= .5,
            }
            write_json(folder / "stress-report.json", report)
            print(f"Stationary control completed: {stationary_seconds}s", flush=True)
        for repeat in range(plan["repeats"]):
            for cadence, scale, fence, audio_wait, native_apu in ordered_conditions(plan, repeat, include_native_apu=True):
                name = f"stress-{repeat + 1:02d}-{cadence}us"
                if scale is not None:
                    name += "-scale" + f"{scale:g}".replace(".", "p")
                if fence is not None:
                    name += f"-fence{fence}"
                if audio_wait is not None:
                    name += f"-audio{audio_wait}"
                if native_apu is not None:
                    name += f"-nativeapu{native_apu}"
                print(f"Preparing {name}", flush=True)
                require_runtime_clean(log)
                pool_preparation = prepare_police_pool(client, plan["police_count"])
                clear_reply = pool_preparation["clear"]
                pose = plan["coordinates"]
                reset = client.command(f"teleport {pose['x']:.9g} {pose['z']:.9g} {pose['heading']:.9g}")
                if reset.get("result") != "applied":
                    raise ValueError(f"Native reset failed: {reset}")
                poll_reply = apply_poll(client, cadence)
                scale_reply = apply_scale(client, scale) if scale is not None else None
                fence_reply = apply_fence(client, fence) if fence is not None else None
                audio_wait_reply = apply_audio_wait(client, audio_wait) if audio_wait is not None else None
                native_apu_reply = apply_native_apu(client, native_apu) if native_apu is not None else None
                time.sleep(plan["warmup_seconds"])
                spawn = client.command(f"stress police {plan['police_count']}") if plan["police_count"] else None
                before = client.command("stress snapshot")
                police_proof = None
                police_wait = None
                post_spawn_reset = None
                if spawn:
                    if plan["police_mode"] == "active":
                        before, police_proof, police_wait = wait_active_police(client, spawn, plan)
                    else:
                        police_proof = verify_police(spawn, before, plan["police_count"], "pool")
                    # Natural activation can move the stationary player before
                    # the complete police body proof arrives. Reset only after
                    # that proof, then validate both live police and fixed pose
                    # again; record the native mutation explicitly.
                    post_spawn_reset = client.command(
                        f"teleport {pose['x']:.9g} {pose['z']:.9g} {pose['heading']:.9g}")
                    if post_spawn_reset.get("result") != "applied":
                        raise ValueError(f"Post-spawn native reset failed: {post_spawn_reset}")
                    before = snapshot_after(client, client.qpc_ms())
                    police_proof = verify_police(spawn, before, plan["police_count"], plan["police_mode"])
                else:
                    police_proof = verify_police(clear_reply, before, 0, "pool")
                reuse_proof = police_reuse_proof(pool_preparation, before)
                verify_snapshot(before, plan, fixed_pose=True)
                before_capture = capture_frame(client, capture_prefix, folder) if plan["capture"] else None
                if before_capture:
                    # The capture wait is outside the measured interval and
                    # must not inflate its contact/effect counter delta.
                    before = snapshot_after(client, before_capture["qpc_ms"])
                    verify_snapshot(before, plan, fixed_pose=True)
                    if spawn:
                        police_proof = verify_police(spawn, before, plan["police_count"], plan["police_mode"])
                require_runtime_clean(log)
                native_apu_before = check_native_apu(client, native_apu) if native_apu is not None else None
                record = {"name": name, "reset": reset, "poll": poll_reply, "spawn": spawn, "clear": clear_reply,
                          "post_spawn_reset": post_spawn_reset,
                          "police_pool_preparation": pool_preparation,
                          "police_reuse_proof": reuse_proof,
                          "scale": scale_reply,
                          "fence": fence_reply,
                          "audio_wait": audio_wait_reply,
                          "native_apu": native_apu_reply,
                          "native_apu_checks": {"before": native_apu_before},
                          "condition_settings": ({"MM3_NATIVE_APU_MMIO": str(native_apu_before["effective_native_apu"])}
                                                 if native_apu_before else {}),
                          "police_proof": police_proof,
                          "police_activation_wait": police_wait,
                          "before_capture": before_capture,
                          "snapshots": [before], "first_log_line": line_count(log) + 1}
                cpu_start, begin = client.cpu_ms(), client.qpc_ms()
                with pad.open("a", encoding="ascii", newline="") as stream:
                    stream.write("\n".join(plan["pattern"]) + "\n")
                deadline = begin + plan["window_seconds"] * 1000
                next_sample = begin + plan["sample_seconds"] * 1000
                while client.qpc_ms() < deadline:
                    now = client.qpc_ms()
                    time.sleep(max(0, min(next_sample, deadline) - now) / 1000)
                    if client.qpc_ms() >= deadline:
                        break
                    snapshot = client.command("stress snapshot")
                    verify_snapshot(snapshot, plan)
                    record["snapshots"].append(snapshot)
                    next_sample += plan["sample_seconds"] * 1000
                end, cpu_end = client.qpc_ms(), client.cpu_ms()
                record["native_apu_checks"]["after"] = check_native_apu(client, native_apu) if native_apu is not None else None
                after = client.command("stress snapshot")
                verify_snapshot(after, plan)
                record["snapshots"].append(after)
                record["after_capture"] = capture_frame(client, capture_prefix, folder) if plan["capture"] else None
                window = {"name": name, "start": begin, "end": end, "cpu_ms": cpu_end - cpu_start,
                          "logical_processors": metadata.get("logical_processors", os.cpu_count())}
                record.update({"window": window, "checks": window_checks(before, after, record["snapshots"], plan),
                               "counter_sample_bounds_qpc_ms": [before.get("qpc_ms"), after.get("qpc_ms")],
                               "counter_scope": "Deltas span the copied native sample timestamps, which can differ from frame-window bounds by the native sample cadence.",
                               "last_log_line": line_count(log)})
                report["windows"].append(window)
                report["results"].append(record)
                record["runtime_health_after_window"] = runtime_health(log)
                record["runtime_clean"] = record["runtime_health_after_window"]["runtime_clean"]
                report["runtime_clean"] = record["runtime_clean"]
                write_json(folder / "windows.json", report["windows"])
                write_json(folder / "stress-report.json", report)
                print(f"Measured {name}: movement={record['checks']['horizontal_displacement_m']:.1f} m, "
                      f"contacts={record['checks']['collision_count_delta']}", flush=True)
                require_runtime_clean(log)
        # CSV is buffered by the game; do not summarize or terminate early.
        limit = time.monotonic() + 15
        while True:
            samples, _ = analysis.read_frames(frames)
            last_qpc = max((row["end_qpc_ms"] for row in samples["frame_ms"]), default=0)
            if last_qpc >= report["windows"][-1]["end"]:
                break
            if time.monotonic() >= limit:
                raise TimeoutError("Frame CSV has not persisted the complete final measurement window")
            client.cpu_ms()
            time.sleep(.25)
        lines = analysis.complete_lines(log)[0]
        if "stationary_control" in report:
            control = report["stationary_control"]
            window = control["window"]
            control["frame_and_hitch_analysis"] = analysis.analyze(frames, log, window["start"], window["end"], top=10)
            control["process_cpu_percent"] = window["cpu_ms"] / (window["end"]-window["start"]) / metadata.get("logical_processors", os.cpu_count()) * 100
        for index, record in enumerate(report["results"]):
            # The following window's first log boundary captures records flushed
            # late without attributing another window's actual input to this one.
            stop = (report["results"][index + 1]["first_log_line"] - 1
                    if index + 1 < len(report["results"]) else len(lines))
            record["input_evidence"] = input_evidence(lines, record["first_log_line"], stop, plan["pattern"])
            window = record["window"]
            metrics = analysis.analyze(frames, log, window["start"], window["end"], top=5)
            record["frame_and_hitch_analysis"] = metrics
            record["process_cpu_percent"] = (window["cpu_ms"] / (window["end"] - window["start"]) /
                                               window["logical_processors"] * 100)
            record["audio_records"] = []
            for number_, line in enumerate(lines, 1):
                if not re.search(r"\[(?:APU|XA2|DSP_PROFILE|AUDIO)[^\]]*\]", line):
                    continue
                fields = dict(analysis.FIELDS.findall(line))
                try:
                    timestamp = analysis.finite_number(fields["qpc_ms"])
                except (KeyError, ValueError):
                    continue
                if window["start"] <= timestamp <= window["end"]:
                    record["audio_records"].append({"log_line": number_, "message": line.rstrip(), "fields": fields})
            record["verified"] = record.get("runtime_clean", True) and all(record["checks"][key] for key in
                ("movement_verified", "heading_changed", "collision_requirement_met")) and record["input_evidence"]["pattern_applied"]
        report["audio_limit"] = "Only audio records carrying QPC timestamps are assigned to windows. Missing records do not prove healthy audio."
        report["completed"] = True
        report["all_windows_verified"] = report["runtime_clean"] and all(record["verified"] for record in report["results"])
        write_json(folder / "stress-report.json", report)
        return report
    except Exception as error:
        # A fatal game failure can first surface as a stale native snapshot or
        # a closed pipe. Preserve the crash evidence even in that error path.
        health = runtime_health(log)
        report["runtime_clean"] = health["runtime_clean"]
        if not health["runtime_clean"]:
            report["failure_evidence"] = {"phase": "runtime_health", **health}
        report["error"] = f"{type(error).__name__}: {error}"
        import traceback
        report["error_traceback"] = traceback.format_exc()
        if isinstance(error, EvidenceFailure):
            report["failure_evidence"] = error.evidence
            if error.evidence.get("phase") == "runtime_health":
                report["runtime_clean"] = False
        write_json(folder / "stress-report.json", report)
        raise
    finally:
        if owned_client:
            client.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_directory", type=Path, nargs="?")
    parser.add_argument("--plan", type=Path, required=True, help="Validated fixed-scene stress plan JSON")
    parser.add_argument("--validate-plan", action="store_true", help="Validate and print a plan without opening a game")
    parser.add_argument("--stationary-seconds", type=int, default=0, help="Optional no-input control before police spawning/driving (0..120 seconds)")
    args = parser.parse_args(argv)
    try:
        plan = validate_plan(json.loads(args.plan.read_text(encoding="utf-8-sig")))
        if args.validate_plan:
            print(json.dumps(plan, indent=2, allow_nan=False))
            return 0
        if args.run_directory is None:
            raise ValueError("run_directory is required unless --validate-plan is used")
        report = run_stress(args.run_directory, plan, stationary_seconds=args.stationary_seconds)
    except (OSError, ValueError, RuntimeError, TimeoutError, KeyError) as error:
        print(f"MM3 stress: {error}", file=sys.stderr)
        return 2
    print(f"Stress report: {args.run_directory / 'stress-report.json'}", flush=True)
    return 0 if report["all_windows_verified"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
