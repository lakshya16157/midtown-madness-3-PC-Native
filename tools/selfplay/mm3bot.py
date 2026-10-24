"""Drive isolated mm3_recomp instances for testing ("self-play"): a copy of the
saves per instance, RECOMP_PAD_LIVE button presses, RECOMP_PRESENT_CAPTURE
screenshots and the MM3_DEBUG_TELEPORT pipe. Never run it beside the user's own
session; kill the instances and delete conformance_tmp/selfplay afterwards.
drive.py / drive2.py pass circuit gates by teleport, menus.py reads the menu
manager live. Example (2026-10-23): reproducing the missing post-race menu.

  run NAME [KEY=VALUE ...]   start an instance and wait for it (run in background)
  press NAME BUTTON[:MS] [WAIT_S]
  seq NAME B1 B2 ... (each "button[:ms][@wait]")
  shot NAME [LABEL]          capture the next presented frame -> PNG path
  pipe NAME COMMAND...       teleport-pipe command (position / cars / teleport X Z [H])
  kill NAME | killall
  log NAME [PATTERN]         grep the instance's stderr
"""
import json, os, re, shutil, subprocess, sys, time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
EXE = REPO / "build" / "RelWithDebInfo" / "mm3_recomp.exe"
ROOT = REPO / "conformance_tmp" / "selfplay"


def inst(name):
    return ROOT / name


def state(name):
    return json.loads((inst(name) / "state.json").read_text())


def run(name, extra):
    d = inst(name)
    if d.exists():
        shutil.rmtree(d)
    (d / "save").mkdir(parents=True)
    for sub in ("UDATA", "TDATA"):
        shutil.copytree(REPO / "game_files" / sub, d / "save" / sub)
    (d / "shaders").mkdir()
    pad = d / "pad.txt"
    pad.write_text("")
    env = dict(os.environ)
    for k in list(env):
        if k.startswith(("MM3_", "RECOMP_")):
            del env[k]
    env.update({
        "RECOMP_PB_EXEC": "1", "RECOMP_FB_WINDOW": "1", "RECOMP_USB": "1", "RECOMP_USB_PADS": "1",
        "RECOMP_PAD_LIVE": str(pad), "RECOMP_APU_DSP_ACK": "0x80458810", "MM3_SAVE_DIR": str(d / "save"),
        "MM3_LAUNCHER": "0", "MM3_DISPLAY_CONFIG": str(d / "display.ini"),
        "MM3_LAUNCHER_CONFIG": str(d / "launcher.ini"), "RECOMP_ASPECT": "16:9",
        "RECOMP_WINDOW_SIZE": "1280x720", "RECOMP_FPS_LIMIT": "60", "MM3_DEBUG_TELEPORT": "1",
        "RECOMP_PRESENT_CAPTURE": str(d / "cap"), "RECOMP_SHADER_CACHE": "1",
        "RECOMP_SHADER_CACHE_DIR": str(d / "shaders"), "MM3_PERFORMANCE_PROFILE": "Balanced",
    })
    for kv in extra:
        k, _, v = kv.partition("=")
        env[k] = v
    with open(d / "stderr.log", "wb") as err, open(d / "stdout.log", "wb") as out:
        p = subprocess.Popen([str(EXE)], cwd=REPO, env=env, stdout=out, stderr=err)
        (d / "state.json").write_text(json.dumps({"pid": p.pid, "started": time.time()}))
        print(f"{name}: pid {p.pid}", flush=True)
        code = p.wait()
    print(f"{name}: exited {code}", flush=True)


def press(name, spec, wait=0.0):
    with open(inst(name) / "pad.txt", "a") as f:
        f.write(spec + "\n")
    time.sleep(0.25 + wait)


def shot(name, label=""):
    from PIL import Image
    d = inst(name)
    before = set(d.glob("cap-*.bmp"))
    (d / "cap.flag").write_text("1")
    end = time.time() + 15
    while time.time() < end:
        new = sorted(set(d.glob("cap-*.bmp")) - before)
        if new:
            path = new[-1]
            try:
                data = path.read_bytes()
                if len(data) > 54 and data[:2] == b"BM" and int.from_bytes(data[2:6], "little") == len(data):
                    img = Image.open(path).convert("RGB")
                    img.thumbnail((1024, 1024))
                    png = d / f"shot-{label or path.stem}.png"
                    img.save(png)
                    path.unlink()
                    print(png)
                    return str(png)
            except OSError:
                pass
        time.sleep(0.2)
    print("no capture (game not presenting?)")


def pipe(name, command):
    pid = state(name)["pid"]
    path = rf"\\.\pipe\MM3Teleport-{pid}"
    end = time.time() + 5
    while True:
        try:
            with open(path, "r+b", buffering=0) as f:
                f.write(command.encode())
                print(f.read(8192).decode(errors="replace"))
                return
        except OSError as e:
            if time.time() > end:
                print("pipe error", e)
                return
            time.sleep(0.2)


def kill(name):
    try:
        pid = state(name)["pid"]
    except FileNotFoundError:
        return
    subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)


def main(argv):
    if not argv:
        print(__doc__)
        return
    cmd, name = argv[0], (argv[1] if len(argv) > 1 else "")
    if cmd == "run":
        run(name, argv[2:])
    elif cmd == "press":
        press(name, argv[2], float(argv[3]) if len(argv) > 3 else 0.0)
    elif cmd == "seq":
        for step in argv[2:]:
            spec, _, wait = step.partition("@")
            press(name, spec, float(wait) if wait else 0.8)
    elif cmd == "shot":
        shot(name, argv[2] if len(argv) > 2 else "")
    elif cmd == "pipe":
        pipe(name, " ".join(argv[2:]))
    elif cmd == "kill":
        kill(name)
    elif cmd == "killall":
        for d in ROOT.glob("*"):
            kill(d.name)
    elif cmd == "log":
        text = (inst(name) / "stderr.log").read_text(errors="replace")
        pat = re.compile(argv[2]) if len(argv) > 2 else None
        lines = [l for l in text.splitlines() if not pat or pat.search(l)]
        print("\n".join(lines[-40:]))
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
