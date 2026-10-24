"""Pass a circuit's gates in order by teleporting: drive.py NAME CITY RACE [LAPS] [WAIT_S]"""
import ast, json, sys, time, re
from pathlib import Path
import mm3bot

name, city, race = sys.argv[1], sys.argv[2], sys.argv[3]
wait = float(sys.argv[5]) if len(sys.argv) > 5 else 1.5
circuit = next(c for c in json.load(open(mm3bot.REPO / "tools/races/circuits.json"))
               if c["city"].strip("'") == city and str(c["race"]).strip("'") == race)
gates = ast.literal_eval(circuit["gates"]) if isinstance(circuit["gates"], str) else circuit["gates"]
laps = int(sys.argv[4]) if len(sys.argv) > 4 else int(str(circuit["laps"]).strip("'"))
log = mm3bot.inst(name) / "stderr.log"


def passed():
    text = log.read_text(errors="replace")
    return len(re.findall(r"gate Human-0 \d+ lap", text))


start = passed()
for lap in range(laps):
    for i, (rotate, width, x, y, z) in enumerate(gates):
        mm3bot.pipe(name, f"teleport {x} {z} {rotate}")
        time.sleep(wait)
    print(f"lap {lap + 1} done; gate lines so far {passed() - start}", flush=True)
