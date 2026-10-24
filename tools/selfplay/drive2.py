"""Pass all gates but the last at WAIT s each, then wait for an AI to finish and pass the last.
drive2.py NAME CITY RACE WAIT"""
import ast, json, re, sys, time
import mm3bot
name, city, race, wait = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
c = next(c for c in json.load(open(mm3bot.REPO / "tools/races/circuits.json"))
         if c["city"].strip("'") == city and str(c["race"]).strip("'") == race)
gates = ast.literal_eval(c["gates"]) if isinstance(c["gates"], str) else c["gates"]; laps = int(str(c["laps"]).strip("'"))
log = mm3bot.inst(name) / "stderr.log"
start = len(log.read_text(errors="replace"))
seq = [(lap, g) for lap in range(laps) for g in range(len(gates))]
for lap, g in seq[:-1]:
    r, w, x, y, z = gates[g]
    mm3bot.pipe(name, f"teleport {x} {z} {r}")
    time.sleep(wait)
last = len(gates) - 1
pat = re.compile(rf"gate Comp--\d {last} lap {laps}")
t0 = time.time()
while not pat.search(log.read_text(errors="replace")[start:]) and time.time() - t0 < 400:
    r, w, x, y, z = gates[len(gates) - 2]          # park at the previous gate
    time.sleep(2)
print("AI finished after", int(time.time() - t0), "s of waiting", flush=True)
time.sleep(0.5)
r, w, x, y, z = gates[last]
mm3bot.pipe(name, f"teleport {x} {z} {r}")
print("player crossed the line", flush=True)
