"""Read a circuit race back from the game log.

Scripts/logic_circuit.lua logs through src/mm3_lua_errors.c:

    [MM3_CIRCUIT] <seconds> start <City> <race> gates <n> laps <n> players <n>
    [MM3_CIRCUIT] <seconds> pos <tick> <x y z of VH-0> ... at <each opponent's next target>   (once a second)
    [MM3_CIRCUIT] <seconds> gate <player> <gate> lap <lap>
    [MM3_CIRCUIT] <seconds> stuck|reset <player> <x> <z> ...
    [MM3_CIRCUIT] <seconds> away <player> <x> <z> from WP-<k> <metres> m   (heading away from its target)

    python tools/races/trace.py logs/direct-<time>.log [race number]

For each car it maps the samples onto the course (tools/races/design.py) and
prints its speed, where it slowed down on a straight, where it left the course,
and lap times; then the places where several opponents slowed down.
"""
import json
import math
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import circuits  # noqa: E402
import design  # noqa: E402
import roadgraph  # noqa: E402

LINE = re.compile(r"^\[MM3_CIRCUIT\] (\d+(?:\.\d+)?) (.*)$")
STRAIGHT = 0.25        # radians of course heading change over STRAIGHT_SPAN either side
STRAIGHT_SPAN = 60.0
SLOWDOWN = 0.75        # a sample below this share of the car's speed a second earlier
OFF_COURSE = 12.0      # metres beyond the road's half width


def races(path):
    """[(header, [(seconds, words)])] for each race in the log."""
    out = []
    for text in Path(path).read_text(errors="replace").splitlines():
        m = LINE.match(text)
        if not m:
            continue
        seconds, words = float(m.group(1)), m.group(2).split()
        if words[0] == "start":
            out.append(({"city": words[1], "race": int(words[2]), "gates": int(words[4]),
                         "laps": int(words[6]), "players": int(words[8])}, []))
        elif out:
            out[-1][1].append((seconds, words))
    return out


class Course:
    def __init__(self, city, race):
        spec = next(s for s in circuits.DESIGNS if s["city"] == city and s["race"] == race)
        self.graph = roadgraph.load(city)
        self.c = design.layout(self.graph, city, spec["corners"], spec.get("start_road"), spec["laps"])
        self.points, self.dist = self.c.points, self.c.dist
        self.width = {}
        for road, _ in self.c.path:
            self.width[road] = self.graph.roads[road].width

    def project(self, x, z):
        """(course distance, distance from the centre line) of a position."""
        best = (1e18, 0.0)
        for i in range(len(self.points) - 1):
            ax, az = self.points[i][0], self.points[i][2]
            bx, bz = self.points[i + 1][0], self.points[i + 1][2]
            dx, dz = bx - ax, bz - az
            length2 = dx * dx + dz * dz
            t = 0.0 if not length2 else max(0.0, min(1.0, ((x - ax) * dx + (z - az) * dz) / length2))
            off = math.dist((x, z), (ax + t * dx, az + t * dz))
            if off < best[0]:
                best = (off, self.dist[i] + t * (self.dist[i + 1] - self.dist[i]))
        return best[1], best[0]

    def straight(self, s):
        h1 = design.at_distance(self.points, self.dist, s - STRAIGHT_SPAN)[1]
        h2 = design.at_distance(self.points, self.dist, s)[1]
        h3 = design.at_distance(self.points, self.dist, s + STRAIGHT_SPAN)[1]
        return design.turn_angle(h1, h2) < STRAIGHT and design.turn_angle(h2, h3) < STRAIGHT

    def near_junction(self, s):
        """(metres, number) of the junction nearest course distance s."""
        best = None
        x, _, z = design.at_distance(self.points, self.dist, s)[0]
        for key, pos in self.graph.position.items():
            d = math.dist((x, z), (pos[0], pos[2]))
            if best is None or d < best[0]:
                best = (d, self.graph.number[key])
        return best


def report(path, only=None):
    found = races(path)
    if not found:
        print("No circuit lines in", path)
        return
    for header, lines in found:
        if only is not None and header["race"] != only:
            continue
        course = Course(header["city"], header["race"])
        print(f"== {header['city']} {header['race']}: {header['gates']} gates x {header['laps']} laps, "
              f"{header['players']} cars, lap {course.c.lap:.0f} m, {len(lines)} lines")
        samples, targets = {}, {}
        for seconds, words in lines:
            if words[0] == "pos":
                # "pos <tick> x y z ... at <targets>" (or the older "x z" pairs)
                cut = words.index("at") if "at" in words else len(words)
                values = [float(v) for v in words[2:cut] if v != "-"]
                width = 3 if "at" in words else 2
                for car in range(len(values) // width):
                    samples.setdefault(car, []).append(
                        (seconds, values[width * car], values[width * car + width - 1]))
                for car, target in enumerate(words[cut + 1:], 1):
                    targets.setdefault(car, []).append((seconds, int(target)))
            elif words[0] in ("stuck", "reset", "missed", "lost", "away"):
                print("  ", words[0], " ".join(words[1:]))
        gates = {}
        for seconds, words in lines:
            if words[0] == "gate":
                gates.setdefault(words[1], []).append((seconds, int(words[2]), int(words[4])))
        slow_places = {}
        for car, rows in sorted(samples.items()):
            name = "player" if car == 0 else f"Comp--{car - 1}"
            speeds, slow, off = [], [], []
            previous_speed = None
            for (t0, x0, z0), (t1, x1, z1) in zip(rows, rows[1:]):
                if t1 <= t0:
                    continue
                speed = math.dist((x0, z0), (x1, z1)) / (t1 - t0) * 3.6
                s, offset = course.project(x1, z1)
                if speed > 5:
                    speeds.append(speed)
                if previous_speed and previous_speed > 60 and speed < SLOWDOWN * previous_speed and \
                        course.straight(s):
                    slow.append((t1, s, previous_speed, speed))
                    if car:
                        key = int(s // 50)
                        slow_places[key] = slow_places.get(key, 0) + 1
                if offset > OFF_COURSE + 10:
                    off.append((t1, s, offset))
                previous_speed = speed
            mean = sum(speeds) / len(speeds) if speeds else 0
            top = max(speeds) if speeds else 0
            print(f"  {name}: {len(rows)} s, mean {mean:.0f} km/h, top {top:.0f}, "
                  f"{len(slow)} slowdowns on straights, {len(off)} s off the course")
            for t, s, before, after in slow[:6]:
                d, junction = course.near_junction(s)
                print(f"     {t:7.1f} s at {s:6.0f} m (junction {junction}, {d:.0f} m): {before:.0f} -> {after:.0f} km/h")
            for t, s, offset in off[:3]:
                print(f"     {t:7.1f} s off the course by {offset:.0f} m near {s:.0f} m")
            if car in targets:
                # The longest an opponent kept the same next target on its path.
                held, since, last = 0.0, None, None
                for t, target in targets[car]:
                    if target != last:
                        since, last = t, target
                    held = max(held, t - since)
                print(f"     next target unchanged for at most {held:.0f} s (now target {last})")
            passes = gates.get("Human-0" if car == 0 else f"Comp--{car - 1}", [])
            laps = [t for t, gate, _ in passes if gate == header["gates"] - 1]
            if laps:
                times = [b - a for a, b in zip([passes[0][0] - 0.0] + laps, laps)]
                print("     laps (s):", " ".join(f"{x:.1f}" for x in times))
        if slow_places:
            print("  where opponents slowed on straights (course metre: count):",
                  ", ".join(f"{k * 50}-{k * 50 + 50}: {n}" for k, n in sorted(slow_places.items(), key=lambda i: -i[1])[:10]))


if __name__ == "__main__":
    report(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else None)
