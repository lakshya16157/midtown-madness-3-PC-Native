"""Pass a DLC Checkpoint race's gates in order by teleporting: drive_dlc.py NAME CITY RACE [WAIT_S] [FIRST_GATE]
CITY is Paris or Washington; RACE is the script number (10-19, menu races 11-20). The gates are
the CP-n objects of <race>Setup.lua in the installed DLC pack; each is teleported to at the
race's own heading, WAIT_S apart (default 1.5). The city only has ground near the car, so when
a gate is refused ("no usable ground") the car first hops half way towards it and tries again."""
import json
import re
import sys
import time
import mm3bot

PACKS = {"Washington": "4d53002a00000002", "Paris": "4d53002a00000003"}
name, city, race = sys.argv[1], sys.argv[2], sys.argv[3]
wait = float(sys.argv[4]) if len(sys.argv) > 4 else 1.5
first = int(sys.argv[5]) if len(sys.argv) > 5 else 0
setup = (mm3bot.REPO / "game_files" / "TDATA" / "4d53002a" / "$C" / PACKS[city] / "Scripts" / city /
         "Checkpoint" / f"{race}Setup.lua")
number = r"(-?\d+(?:\.\d+)?)"
gates = {}
for m in re.finditer(r'objectCreate\(\d+,"CP-(\d+)"\)\s*objectRotate\(' + number + r'\)\s*objectSetParam\(' + number +
                     r'\)\s*objectSetPos\(' + number + "," + number + "," + number + r"\)", setup.read_text(errors="replace")):
    gates[int(m.group(1))] = (float(m.group(2)), float(m.group(4)), float(m.group(6)))   # heading, x, z
order = [gates[i] for i in sorted(gates)]


def command(text):
    """Send a teleport-pipe command and return its JSON answer."""
    pid = mm3bot.state(name)["pid"]
    path = rf"\\.\pipe\MM3Teleport-{pid}"
    for _ in range(25):
        try:
            with open(path, "r+b", buffering=0) as pipe:
                pipe.write(text.encode())
                return json.loads(pipe.read(8192).decode(errors="replace"))
        except (OSError, ValueError):
            time.sleep(0.2)
    return {}


print(f"{city} {race}: {len(order)} gates, starting at {first}", flush=True)
for i in range(first, len(order)):
    heading, x, z = order[i]
    for attempt in range(8):
        answer = command(f"teleport {x} {z} {heading}")
        if answer.get("result") == "applied":
            break
        here = command("position")
        hx, hz = here.get("x", x), here.get("z", z)
        command(f"teleport {(hx + x) / 2} {(hz + z) / 2} {heading}")   # half way, then again
        time.sleep(1.2)
    time.sleep(wait)
    print(f"gate {i} ({x:.0f}, {z:.0f}): {answer.get('result')} after {attempt + 1} tries", flush=True)
