"""Design circuit races on a city's street graph (tools/races/roadgraph.py).

A circuit is given by the junctions at its corners (the numbers on
`design.py map`); the streets between them are the shortest road paths. The
layout puts the start/finish line on a long straight with the starting grid
behind it, gates along the course (after every turn and at least every
`GATE_SPACING` metres), opponent waypoints on every junction of the loop, and
blocks every other road leaving the loop with two staggered rows of shipping
containers.

    python tools/races/design.py check            # resolve every circuit, print stats
    python tools/races/design.py map <City> <png> # the street map with junction numbers
    python tools/races/design.py write            # tools/races/circuits.json

The street graphs (tools/races/data/<City>.json) are written by the game, once
per city, to logs/city-roads-*.json; they stay local. circuits.json, the
resolved layouts, is what the installer uses.
"""
import heapq
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import roadgraph  # noqa: E402

GATE_SPACING = 260.0       # at most this far between gates
TURN_ANGLE = 0.6           # radians; a gate follows every sharper turn
# A gate after a turn stands mid-straight, as in Midtown Madness 1 and 2
# (median 90-150 m past the corner): halfway down the road, 30-120 m on.
GATE_AFTER_TURN = (30.0, 120.0)
GRID_PLACES = 8
GRID_ROW = 5.5             # metres between grid places along the road
GRID_FRONT = 12.0          # front place to the start/finish line
GRID_LANES = (0.38, 0.14)  # the two grid columns, as shares of the road width right of its centre line
OVERHEAD_REACH = 14.0      # a road this close beside the course ...
OVERHEAD_CLEARANCE = 4.0   # ... and this much higher covers it
STACK_REACH = 7.0          # a road this close and OVERHEAD_CLEARANCE higher or lower is right over or under
GATE_SLIDE = 80.0          # a gate in a stacked stretch moves at most this far to clear it ...
GATE_CLEAR = 20.0          # ... to where the course is clear this far either side
# Past a stacked stretch longer than STACK_STRETCH the game's opponents can
# take the road over or under it for the one they are on. In Paris 29 they did
# under the Peripherique's covered section: the start/finish 90 m past its end
# was nearer, by that road, turning back round onto the ring, so every
# opponent turned round in the trench and circled the streets above. No target
# (gate, start/finish or path point) stands for STACK_SHADOW metres of course
# after such a stretch, past where the road above rejoins the course, so any
# way the game finds leads on.
STACK_STRETCH = 60.0
STACK_SHADOW = 600.0
# Shipping containers: model, spacing along their long (facing) axis and width,
# from the models' bounds in <City>.xbc ("container" 2.5 x 4.6 m, "Container2"
# 3.3 x 6.0 m) with a small gap so neighbours never start overlapping.
CONTAINERS = {"Paris": ("container", 4.8, 2.5), "Washington": ("Container2", 6.2, 3.3)}
BLOCK_CLEARANCE = 10.0     # from the loop road's edge to the first row: room to run wide
STUB_ROAD = 12.0           # a road this short only joins two parts of one junction
MIN_COURSE_WIDTH = 8.0     # courses use streets, never footpaths through parks
# The game skips a race script of 32 KB or more (circuits.SCRIPT_LIMIT); the
# compact Setup.lua takes about 60 bytes an object. Retail races stop at 165
# objects (137 props); nothing in the game caps them lower than the script
# size, so circuits block every road off the loop.
OBJECT_BUDGET = 520        # gates, every lap's opponent waypoints, props and the start strip
PROP_BUDGET = 400          # containers
GATE_BUDGET = 20
# Roads leaving the loop about as straight on as the course are blocked first.
STRAIGHT_ON = 1.0          # radians from the arrival heading
FORK_MARGIN = 0.35         # radians; as straight as the course, or nearly
START_LINE_DEPTH = 3.0     # the green strip across the road at the start/finish line
JUNCTION_MARGIN = 8.0      # gates keep this far from a junction area
# The street graph's view of an opponent's route between two targets: the
# course must beat every other way by ROUTE_MARGIN plus ROUTE_SHARE of its
# length (a check only; the game's own AI roads can differ, so opponents get
# a dense path, see PATH_*).
ROUTE_MARGIN = 30.0
ROUTE_SHARE = 0.05
PATH_TURN = math.radians(20)   # a junction where the course turns more gets a point
PATH_WIDE_JUNCTION = 45.0  # metres across: a plaza or traffic circle gets an entry and an exit point
PATH_SPACING = 150.0       # at most this far between an opponent's targets
PATH_MIN_GAP = 12.0        # no path point closer than this to a gate or another point


class Circuit:
    pass


def shortest(graph, start, goal, banned):
    """Road path (list of (road, forward)) from meeting start to goal."""
    best = {start: 0.0}
    previous = {}
    heap = [(0.0, 0, start)]
    counter = 1
    while heap:
        cost, _, node = heapq.heappop(heap)
        if node == goal:
            break
        if cost > best.get(node, 1e18):
            continue
        for road, side, other in graph.neighbours(node):
            if road in banned or other is None or graph.roads[road].width < MIN_COURSE_WIDTH:
                continue
            c = cost + graph.roads[road].length
            if c < best.get(other, 1e18):
                best[other] = c
                previous[other] = (node, road, side == 0)
                heapq.heappush(heap, (c, counter, other))
                counter += 1
    if goal not in previous and goal != start:
        raise ValueError(f"no road path to junction {graph.number[goal]}")
    path, node = [], goal
    while node != start:
        node, road, forward = previous[node]
        path.append((road, forward))
    return path[::-1]


def resolve_loop(graph, corners):
    """Corner junction numbers -> ordered [(road, forward)] closing the loop."""
    keys = [graph.ids[n] for n in corners]
    path, used = [], set()
    for a, b in zip(keys, keys[1:] + keys[:1]):
        leg = shortest(graph, a, b, used)
        path += leg
        used.update(road for road, _ in leg)
    meetings = []
    for road, forward in path:
        r = graph.roads[road]
        meetings.append(r.ends[0] if forward else r.ends[1])
    if len(set(meetings)) != len(meetings):
        repeated = [graph.number[m] for m in meetings if meetings.count(m) > 1]
        raise ValueError(f"the loop passes junction(s) {sorted(set(repeated))} twice")
    return path, meetings


def polyline(graph, path):
    """Course points with the meeting index each road starts at."""
    points, starts = [], []
    for road, forward in path:
        pts = graph.roads[road].points if forward else graph.roads[road].points[::-1]
        starts.append(len(points))
        points += pts if not points else pts[1:]
    return points, starts


def cumulative(points):
    out = [0.0]
    for a, b in zip(points, points[1:]):
        out.append(out[-1] + math.dist((a[0], a[2]), (b[0], b[2])))
    return out


def at_distance(points, dist, s):
    """Position and heading (rotation) at course distance s (wrapping)."""
    total = dist[-1]
    s %= total
    lo, hi = 0, len(dist) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if dist[mid] <= s:
            lo = mid
        else:
            hi = mid
    a, b = points[lo], points[hi]
    t = 0.0 if dist[hi] == dist[lo] else (s - dist[lo]) / (dist[hi] - dist[lo])
    p = tuple(a[i] + (b[i] - a[i]) * t for i in range(3))
    return p, roadgraph.heading(a, b)


def turn_angle(h1, h2):
    d = (h2 - h1 + math.pi) % (2 * math.pi) - math.pi
    return abs(d)


class Overhead:
    """Other roads passing over the course: a point is covered when a road
    not on the course runs within OVERHEAD_REACH metres beside it and more
    than OVERHEAD_CLEARANCE higher (a bridge, a street over a tunnel)."""

    def __init__(self, graph, course_roads):
        self.cells = {}
        for road in graph.roads:
            if road.index in course_roads:
                continue
            for a, b in zip(road.points, road.points[1:]):
                steps = max(1, int(math.dist(a, b) / 5))
                for n in range(steps + 1):
                    q = tuple(a[i] + (b[i] - a[i]) * n / steps for i in range(3))
                    key = (int(q[0] // 20), int(q[2] // 20))
                    self.cells.setdefault(key, []).append(q)

    def above(self, x, y, z, below_too=False, reach=OVERHEAD_REACH):
        cx, cz = int(x // 20), int(z // 20)
        for i in (cx - 1, cx, cx + 1):
            for j in (cz - 1, cz, cz + 1):
                for q in self.cells.get((i, j), ()):
                    rise = abs(q[1] - y) if below_too else q[1] - y
                    if rise > OVERHEAD_CLEARANCE and math.dist((q[0], q[2]), (x, z)) < reach:
                        return True
        return False


class Stacked(Overhead):
    """Roads right over or under a point (within STACK_REACH), the course's
    own included (a course that crosses itself on a bridge). The game takes
    an opponent's target there for either road: in Paris 29 opponents under
    the Peripherique's covered section turned back to climb onto the street
    above its targets. Retail races leave such stretches without targets
    (Paris Checkpoint 05 has none over the 550 m covered section; a ramp
    beside the road is no matter), and so do circuits."""

    def __init__(self, graph):
        super().__init__(graph, set())

    def at(self, x, y, z):
        return self.above(x, y, z, below_too=True, reach=STACK_REACH)


class CourseRoutes:
    """Course positions over two laps (metres from the polyline start) and the
    street routes an opponent could take between two of them."""

    def __init__(self, graph, path, meetings, dist, starts, lap):
        self.graph, self.path, self.meetings, self.lap = graph, path, meetings, lap
        self.starts = [dist[s] for s in starts]
        self.cache = {}

    def leg_start(self, leg):
        n = len(self.path)
        return self.starts[leg % n] + (leg // n) * self.lap

    def leg_of(self, s):
        leg = int(s // self.lap) * len(self.path)
        while self.leg_start(leg + 1) <= s:
            leg += 1
        while self.leg_start(leg) > s:
            leg -= 1
        return leg

    def road(self, leg):
        return self.path[leg % len(self.path)][0]

    def meeting(self, leg):
        return self.meetings[leg % len(self.meetings)]

    def distances(self, source, banned):
        key = (source, banned)
        if key not in self.cache:
            # Opponents may use any street, footpaths through parks included.
            best, heap, counter = {source: 0.0}, [(0.0, 0, source)], 1
            while heap:
                cost, _, node = heapq.heappop(heap)
                if cost > best.get(node, 1e18):
                    continue
                for road, _, other in self.graph.neighbours(node):
                    if road in banned or other is None:
                        continue
                    total = cost + self.graph.roads[road].length
                    if total < best.get(other, 1e18):
                        best[other] = total
                        heapq.heappush(heap, (total, counter, other))
                        counter += 1
            self.cache[key] = best
        return self.cache[key]

    def follows(self, a, b):
        """Does the shortest street route from course position a to b (a < b)
        follow the course, by the margin?"""
        i, j = self.leg_of(a), self.leg_of(b)
        if i == j:
            return True
        course = b - a
        out_ahead, out_back = self.leg_start(i + 1) - a, a - self.leg_start(i)
        in_start, in_end = b - self.leg_start(j), self.leg_start(j + 1) - b
        ahead, back = self.meeting(i + 1), self.meeting(i)
        start, end = self.meeting(j), self.meeting(j + 1)
        road_a, road_b = self.road(i), self.road(j)
        inf = 1e18
        others = [
            # Turning round on the first road, or reaching the last from its far end.
            out_back + self.distances(back, frozenset([road_a])).get(start, inf) + in_start,
            out_ahead + self.distances(ahead, frozenset([road_b])).get(end, inf) + in_end,
            out_back + self.distances(back, frozenset([road_a, road_b])).get(end, inf) + in_end]
        # Any other way between the roads leaves out one of the course's.
        for leg in range(i + 1, j):
            others.append(out_ahead + self.distances(ahead, frozenset([self.road(leg)])).get(start, inf) + in_start)
        return course + ROUTE_MARGIN + ROUTE_SHARE * course <= min(others)


def layout(graph, city, corners, start_road=None, laps=1):
    c = Circuit()
    path, meetings = resolve_loop(graph, corners)
    points, starts = polyline(graph, path)
    # Close the polyline.
    if math.dist(points[0], points[-1]) > 0.01:
        points.append(points[0])
    dist = cumulative(points)
    lap = dist[-1]
    widths = [graph.roads[road].width for road, _ in path]
    # Start/finish: on the longest leg (or the requested one), far enough in
    # for the whole grid to stand on that road behind the line.
    need = GRID_FRONT + GRID_ROW * (GRID_PLACES - 1) + 10.0
    candidates = []
    for i, (road, forward) in enumerate(path):
        length = graph.roads[road].length
        if start_road is not None and road != start_road:
            continue
        if length >= need + 25:
            candidates.append((length, i))
    if not candidates:
        raise ValueError("no road long enough for the starting grid")
    # Never under another road: cars placed below a street (the
    # Peripherique's covered sections) land on the street on top.
    overhead = Overhead(graph, {road for road, _ in path})
    stacked = Stacked(graph)

    # Stacked stretches of the course (start, end), metres from the polyline start.
    shadows, run = [], None
    for k in range(int(lap // 5.0) + 1):
        s = k * 5.0
        if stacked.at(*at_distance(points, dist, s)[0]):
            if run and s - run[1] <= 20.0:
                run[1] = s
            else:
                run = [s, s]
                shadows.append(run)
    shadows = [(a, b) for a, b in shadows if b - a >= STACK_STRETCH]
    c.stack_shadows = [(round(a), round(b)) for a, b in shadows]

    def shadowed(s):
        return any((s - a) % lap <= (b - a) + STACK_SHADOW for a, b in shadows)

    def clear(s):
        return not stacked.at(*at_distance(points, dist, s)[0]) and not shadowed(s)

    def grid_clear(s):
        return clear(s) and not any(overhead.above(*at_distance(points, dist, s - back)[0])
                                    for back in range(-25, int(need) + 5, 5))
    sf = None
    for _, leg in sorted(candidates, reverse=True):
        leg_start = dist[starts[leg]]
        leg_len = graph.roads[path[leg][0]].length
        first, last = need, leg_len - 25
        preferred = max(first, min(leg_len * 0.6, last))
        for s in sorted((first + k * 10.0 for k in range(int((last - first) / 10) + 1)),
                        key=lambda s: abs(s - preferred)):
            if grid_clear(leg_start + s):
                sf = leg_start + s
                break
        if sf is not None:
            break
    if sf is None:
        raise ValueError("no open road long enough for the starting grid")
    c.overhead_points = sum(overhead.above(*p) for p in points[::4])
    # Gates: after sharp turns, then fill so no two are more than GATE_SPACING
    # apart; long laps space them further to stay within GATE_BUDGET.
    marks = []
    for i in range(len(path)):
        h_in = at_distance(points, dist, dist[starts[i]] - 8.0)[1]
        h_out = at_distance(points, dist, dist[starts[i]] + 8.0)[1]
        if turn_angle(h_in, h_out) > TURN_ANGLE:
            low, high = GATE_AFTER_TURN
            marks.append(dist[starts[i]] + min(max(graph.roads[path[i][0]].length / 2, low), high))
    rel = sorted(((m - sf) % lap) for m in marks)
    rel = [r for r in rel if 40.0 < r < lap - 40.0]
    spacing, min_gap = GATE_SPACING, 60.0
    while True:
        gates = []
        last = 0.0
        for r in rel + [lap]:
            gap = r - last
            if r < lap and gap < min_gap:
                continue
            extra = math.ceil(gap / spacing) - 1
            for n in range(1, extra + 1):
                gates.append(last + gap * n / (extra + 1))
            gates.append(r)                   # the last one is the start/finish line
            last = r
        if len(gates) <= GATE_BUDGET:
            break
        spacing, min_gap = spacing * 1.2, min_gap * 1.3
    # Junctions: where the course line jumps from one road's end to the next
    # road (across the junction area: a traffic circle's island, a plaza).
    # Gates and waypoints stand on the roads, past the junction.
    junctions = [(dist[starts[i] - 1], dist[starts[i]]) for i in range(1, len(path))]
    junctions.append((dist[-2], lap))

    def off_junctions(s):
        s %= lap
        for a, b in junctions:
            if a - JUNCTION_MARGIN <= s <= b + JUNCTION_MARGIN:
                return (b + JUNCTION_MARGIN + 5.0) % lap
        return s

    def gate_clear(s):
        return all(clear(s + d) for d in (-GATE_CLEAR, 0.0, GATE_CLEAR))

    def unstack(g):
        # A gate in or by a stacked stretch (Stacked): the nearest clear spot
        # off the junctions within GATE_SLIDE, or none (the gates either side
        # of the stretch stand further apart, as retail ones do).
        if gate_clear(sf + g):
            return g
        for step in range(1, int(GATE_SLIDE / 5) + 1):
            for s in (sf + g + 5.0 * step, sf + g - 5.0 * step):
                if gate_clear(s) and off_junctions(s) == s % lap:
                    return (s - sf) % lap
        return None
    moved = sorted(m for m in (unstack((off_junctions(sf + g) - sf) % lap) for g in gates[:-1]) if m is not None)
    gates = []
    for g in moved:
        if 30.0 < g < lap - 30.0 and (not gates or g - gates[-1] >= 30.0):
            gates.append(g)
    gates.append(lap)
    c.gates = []
    for g in gates:
        p, h = at_distance(points, dist, sf + g)
        leg_index = max(k for k in range(len(starts)) if dist[starts[k]] <= (sf + g) % lap + 1e-6)
        width = graph.roads[path[leg_index][0]].width
        # The start/finish gate is wider than the others, over a green strip;
        # gates on the wide, fast roads are wider than on side streets.
        extra = 12.0 if g == lap else (14.0 if width >= 16 else 8.0)
        c.gates.append((h, max(14.0, width + extra), p[0], p[1] + 0.3, p[2]))
        if g == lap:
            c.start_line = (h, width + 2.0, START_LINE_DEPTH, p[0], p[1] + 0.1, p[2])
    # Every road leaving the loop; the ones about as straight on as the course
    # are where drivers would leave it.
    on_loop = {road for road, _ in path}
    meeting_set = set(meetings)
    exits = []
    for i, m in enumerate(meetings):
        s = dist[starts[i]]
        h_in = roadgraph.heading(at_distance(points, dist, s - 30.0)[0], at_distance(points, dist, s - 4.0)[0])
        h_out = roadgraph.heading(at_distance(points, dist, s + 4.0)[0], at_distance(points, dist, s + 30.0)[0])
        turn = turn_angle(h_in, h_out)
        # A road of a few metres (a junction made of two meeting points)
        # leads on to the roads at its far end: those are the exits.
        leaving, seen = [(road, side, other, []) for road, side, other in graph.neighbours(m)], {m}
        while leaving:
            road, side, other, before = leaving.pop()
            if road in on_loop:
                continue
            r = graph.roads[road]
            pts = before + (r.points if side == 0 else r.points[::-1])[1 if before else 0:]
            if r.length < STUB_ROAD and other is not None and other not in meeting_set and other not in seen:
                seen.add(other)
                leaving += [(road2, side2, other2, pts) for road2, side2, other2 in graph.neighbours(other)
                            if road2 != road]
                continue
            pdist = cumulative(pts)
            if pdist[-1] < 3.0:
                continue
            h_road = roadgraph.heading(at_distance(pts, pdist, min(4.0, pdist[-1] / 2))[0],
                                       at_distance(pts, pdist, min(25.0, pdist[-1]))[0])
            bend = turn_angle(h_in, h_road)
            exits.append({"loop_width": max(widths[i - 1], widths[i]), "road": r, "pts": pts, "pdist": pdist,
                          "straight_on": bend <= STRAIGHT_ON and bend < turn + FORK_MARGIN})
    # The opponents' path, as in Midtown Madness 1: its opponent files put a
    # point on the centre of every intersection along the course, so the
    # straight line from point to point runs down the streets. Here: a point
    # in the middle of every junction where the course turns (at a junction
    # area wider than PATH_WIDE_JUNCTION - a plaza, a traffic circle - on its
    # way in and on its way out instead), points on bends wherever the
    # straight line to the next target would leave the road, and more so no
    # two targets are over PATH_SPACING apart. Opponents only ever have the
    # next few armed (Scripts/logic_circuit.lua).
    def rel(s):
        return (s - sf) % lap

    def road_end(i, last):
        road, forward = path[i]
        pts = graph.roads[road].points if forward else graph.roads[road].points[::-1]
        return (pts[-1], pts[-2]) if last else (pts[0], pts[1])

    marks = [(g, k, None) for k, g in enumerate(gates)]       # (course distance from sf, gate, place)
    for i in range(len(path)):
        (end_a, before), (start_b, after) = road_end(i - 1, True), road_end(i, False)
        turn = turn_angle(roadgraph.heading(before, end_a), roadgraph.heading(start_b, after))
        if turn < PATH_TURN:
            continue
        width = min(widths[i - 1], widths[i])
        s_end = dist[starts[i] - 1] if i else lap - (dist[-1] - dist[-2])
        gap = math.dist((end_a[0], end_a[2]), (start_b[0], start_b[2]))
        if gap > PATH_WIDE_JUNCTION:
            places = [(s_end, end_a, before, end_a), (s_end + gap, start_b, start_b, after)]
        else:
            middle = tuple((a + b) / 2 for a, b in zip(end_a, start_b))
            places = [(s_end + gap / 2, middle, end_a, start_b)]
        for s, place, h_from, h_to in places:
            marks.append((rel(s), None, (roadgraph.heading(h_from, h_to), width, place)))
    marks.sort(key=lambda m: m[0])
    # Bends: the straight line between two targets stays within the road.
    vertices = [(rel(dist[k]), points[k], widths[max(j for j in range(len(path)) if starts[j] <= max(k, starts[0]))])
                for k in range(len(points) - 1)]
    vertices.sort(key=lambda v: v[0])

    def where(m):
        if m[2] is not None:
            return m[2][2]
        return at_distance(points, dist, sf + m[0])[0]
    changed = True
    while changed:
        changed = False
        for a, b in zip([(0.0, None, None)] + marks, marks):
            pa, pb = where(a), where(b)
            worst, best = 0.0, None
            for r, v, width in vertices:
                if a[0] + PATH_MIN_GAP < r < b[0] - PATH_MIN_GAP:
                    dx, dz = pb[0] - pa[0], pb[2] - pa[2]
                    length2 = dx * dx + dz * dz or 1.0
                    u = max(0.0, min(1.0, ((v[0] - pa[0]) * dx + (v[2] - pa[2]) * dz) / length2))
                    off = math.dist((v[0], v[2]), (pa[0] + u * dx, pa[2] + u * dz))
                    if off > max(3.0, width / 2 - 1.0) and off > worst:
                        worst, best = off, (r, v)
            if best:
                h = at_distance(points, dist, sf + best[0])[1]
                width = next(w for r, v, w in vertices if r == best[0])
                marks.append((best[0], None, (h, width, best[1])))
                marks.sort(key=lambda m: m[0])
                changed = True
                break
    # Straights: no more than PATH_SPACING between targets.
    filled, last = [], 0.0
    for m in marks:
        extra = math.ceil((m[0] - last) / PATH_SPACING) - 1
        filled += [(last + (m[0] - last) * n / (extra + 1), None, None) for n in range(1, extra + 1)]
        filled.append(m)
        last = m[0]
    # Points too close to a gate or to each other go (gates stay), and so do
    # points in a stacked stretch (Stacked): opponents drive through it on
    # the road they are on, to the next target past it.
    kept = []
    for m in filled:
        if m[1] is None and (m[0] < PATH_MIN_GAP or any(abs(m[0] - g) < PATH_MIN_GAP for g in gates) or
                             (kept and m[0] - kept[-1][0] < PATH_MIN_GAP) or stacked.at(*where(m)) or
                             shadowed(sf + m[0])):
            continue
        kept.append(m)
    route = CourseRoutes(graph, path, meetings, dist, starts, lap)
    c.waypoints, targets = {}, []
    for r, gate, place in kept:
        if gate is None:
            if place is None:
                p, h = at_distance(points, dist, sf + r)
                width = widths[route.leg_of(sf + r) % len(path)]
            else:
                h, width, p = place
            following = next(k for k, g in enumerate(gates) if g > r)
            # On the road's centre line, as retail targets and the opponent
            # files of Midtown Madness 1 and 2: the game's opponents keep to
            # their own lane, on a divided highway too.
            c.waypoints.setdefault(following, []).append((h, max(10.0, width / 4 + 5), p[0], p[1] + 0.3, p[2]))
        targets.append(sf + r)
    # Every opponent leg, checked on the street graph: a route between two
    # targets that could leave the course.
    c.unresolved = sum(not route.follows(a, b) for a, b in zip([sf] + targets, targets))
    c.target_gaps = [b - a for a, b in zip([sf] + targets, targets)]
    c.targets, c.shadowed = targets, shadowed
    # Grid: place 1 (the player) at the back, then forward, in two columns on
    # the right-hand half of the road (a divided highway's other half is the
    # other carriageway, behind its middle barrier).
    c.grid = []
    for k in range(GRID_PLACES):
        back = GRID_FRONT + GRID_ROW * (GRID_PLACES - 1 - k)
        p, h = at_distance(points, dist, sf - back)
        lane = graph.roads[path[leg][0]].width * GRID_LANES[k % 2]
        right = (math.cos(h), -math.sin(h))
        c.grid.append((p[0] + lane * right[0], p[1] + 0.5, p[2] + lane * right[1], h,
                       "aggressive" if k % 2 else "passive"))
    # Blocks, one kind only, as in Midtown Madness 1 and 2: a single straight
    # row of containers end to end across each road off the loop, square to
    # the road (straight-on roads first, then the cheapest), within the race's
    # object budget. Opponents' path points are made for every lap, a point on
    # every gate included.
    model, length, container_width = CONTAINERS[city]
    course = [(p[0], p[2]) for p in points[::2]]
    path_points = sum(len(v) for v in c.waypoints.values()) + len(gates)
    objects = OBJECT_BUDGET - len(gates) - path_points * laps - 1

    def course_gap(x, z):
        return min(math.dist((x, z), q) for q in course)

    def row(e):
        p, h, n_row = e["frame"][0], e["frame"][1], e["count"]
        across = (h + math.pi / 2) % (2 * math.pi)
        return [(model, p[0] + (n - (n_row - 1) / 2) * length * math.sin(across), p[1] + 0.6,
                 p[2] + (n - (n_row - 1) / 2) * length * math.cos(across), across) for n in range(n_row)]

    def place(e):
        # BLOCK_CLEARANCE beyond the loop road's edge; a row that would reach
        # onto the course (a road leaving at a sharp angle) moves further in.
        pts, pdist = e["pts"], e["pdist"]
        along = min(e["loop_width"] / 2 + BLOCK_CLEARANCE + container_width / 2, pdist[-1] * 0.5)
        while True:
            along = min(along, pdist[-1] * 0.8)
            e["frame"] = at_distance(pts, pdist, along)
            clear = all(course_gap(x, z) >= e["loop_width"] / 2 + 1.5 for _, x, _, z, _ in row(e))
            if clear or along >= pdist[-1] * 0.8:
                return
            along += 2.0

    for e in exits:
        e["count"] = max(1, math.ceil((e["road"].width + 3.0) / length))
    order = sorted(exits, key=lambda e: (not e["straight_on"], e["count"]))
    c.props, c.blocked = [], 0
    for e in order:
        if len(c.props) + e["count"] > min(objects, PROP_BUDGET):
            continue
        place(e)
        c.props += row(e)
        c.blocked += 1
    c.laps = laps
    c.open_roads = len(exits) - c.blocked
    c.points, c.dist, c.sf = points, dist, sf
    c.lap = lap
    c.meetings = [graph.number[m] for m in meetings]
    c.path = path
    return c


def summary(name, c):
    path_points = sum(len(v) for v in c.waypoints.values()) + len(c.gates)
    return (f"{name}: lap {c.lap / 1000:.2f} km, {len(c.meetings)} junctions, {len(c.gates)} gates, "
            f"{path_points} opponent targets a lap, {c.blocked} roads blocked ({c.open_roads} open) with "
            f"{len(c.props)} containers, {len(c.gates) + path_points * c.laps + len(c.props) + 1} objects"
            f", opponent targets {min(c.target_gaps):.0f}-{max(c.target_gaps):.0f} m apart"
            + (f", {c.overhead_points} covered course points" if c.overhead_points else "")
            + (f", no targets past stacked stretches {c.stack_shadows}" if c.stack_shadows else "")
            + (f", {c.unresolved} opponent routes off the course" if c.unresolved else ""))


def map_png(city, out, circuits=()):
    from PIL import Image, ImageDraw, ImageFont
    g = roadgraph.load(city)
    xs = [p[0] for r in g.roads for p in r.points]
    zs = [p[2] for r in g.roads for p in r.points]
    minx, maxx, minz, maxz = min(xs) - 50, max(xs) + 50, min(zs) - 50, max(zs) + 50
    scale = 3000 / max(maxx - minx, maxz - minz)
    img = Image.new("RGB", (int((maxx - minx) * scale), int((maxz - minz) * scale)), (255, 255, 255))
    d = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype("arial.ttf", 15)
    except OSError:
        font = ImageFont.load_default()

    def P(p):
        return ((p[0] - minx) * scale, (maxz - p[2]) * scale)
    for r in g.roads:
        d.line([P(p) for p in r.points], fill=(160, 160, 160), width=max(1, int(r.width * scale * 0.6)))
    colors = [(220, 30, 30), (30, 120, 220), (30, 160, 60), (200, 120, 0), (150, 40, 180)]
    for k, c in enumerate(circuits):
        color = colors[k % len(colors)]
        for road, _ in c.path:
            d.line([P(p) for p in g.roads[road].points], fill=color, width=5)
        for model, x, _, z, _ in c.props:
            px, py = P((x, 0, z))
            d.rectangle((px - 2, py - 2, px + 2, py + 2), fill=(0, 0, 0))
        for _, _, x, _, z in c.gates:
            px, py = P((x, 0, z))
            d.ellipse((px - 5, py - 5, px + 5, py + 5), outline=color, width=3)
    for key, pos in g.position.items():
        x, y = P(pos)
        d.text((x + 3, y - 8), str(g.number[key]), fill=(0, 0, 200), font=font)
    img.save(out)


if __name__ == "__main__":
    import circuits as definitions
    command = sys.argv[1] if len(sys.argv) > 1 else "check"
    if command == "map":
        map_png(sys.argv[2], sys.argv[3])
    else:
        resolved = []
        for spec in definitions.DESIGNS:
            graph = roadgraph.load(spec["city"])
            c = layout(graph, spec["city"], spec["corners"], spec.get("start_road"), spec["laps"])
            print(summary(f"{spec['city']} {spec['race']} {spec['name']}", c))
            resolved.append((spec, c))
        if command == "write":
            definitions.write_resolved(resolved)
        if len(sys.argv) > 2:
            for city in ("Paris", "Washington"):
                chosen = [c for s, c in resolved if s["city"] == city]
                if chosen:
                    map_png(city, sys.argv[2].replace("CITY", city), chosen)
