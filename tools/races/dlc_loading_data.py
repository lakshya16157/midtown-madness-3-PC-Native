"""What dlc_loading.py draws the downloadable races' loading screens from
(dlc_loading.json). Development only: needs numpy and the game's files.

    python tools/races/dlc_loading_data.py routes [--tdata game_files/TDATA]
        each race's start and checkpoints (its scripts, only read: the DLC
        is signed), the streets between them in turn (tools/races/data), and
        its name and descriptions from src/mm3_dlc_races.c
    python tools/races/dlc_loading_data.py colours [--game game_files]
        the map colours: the retail loading screens' maps are the city map
        texture recoloured. Each of the 40 retail race maps is placed by its
        checkpoint balls (the race scripts give their positions), and the
        colour table maps the texture's colours to what retail shows there.
"""
import argparse
import base64
import heapq
import io
import json
import math
import re
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import dlc_icons  # noqa: E402
import dlc_loading  # noqa: E402
import roadgraph  # noqa: E402

TITLE = "4d53002a"
CITIES = {"1": "Paris", "2": "Washington"}
FOOTPATH = 8.0             # roads narrower than this (paths through parks) ...
FOOTPATH_COST = 1.6        # ... count this many times their length
JUNCTION_GAP = 35.0        # metres: a lone road end this near other road ends joins them
# A leg whose street route is DETOUR times the straight line plus DETOUR_EXTRA
# metres goes straight instead, where the map shows open ground the whole way:
# at most CUT_BLOCKED of it blocks, buildings or water and never CUT_RUN m on end.
DETOUR, DETOUR_EXTRA = 2.0, 100.0
CUT_BLOCKED, CUT_RUN = 0.15, 12.0
SIMPLIFY = 1.0             # metres a route may stray from the street centre line
COLOUR_GRID = 17
BALL_FIT_LIMIT = 12.0      # px: retail maps whose balls fit worse are left out ...
MATCH_LIMIT = 0.3          # ... and those whose outlines then match the texture's worse


# --- names and descriptions (src/mm3_dlc_races.c) ----------------------------

def c_string(body):
    out, i = [], 0
    while i < len(body):
        if body[i] != "\\":
            out.append(body[i])
            i += 1
        elif body[i + 1] == "x":
            j = i + 2
            while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(chr(int(body[i + 2:j], 16)))
            i = j
        else:
            out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"'}[body[i + 1]])
            i += 2
    return "".join(out)


def race_texts(source):
    """{"<City>/<Mode>/<NN>": (name, [description in each language])}, the
    way mm3_dlc_race_label words them."""
    def strings(name):
        body = re.search(r"\b" + name + r"(?:\[\w*\])+\s*=\s*\{(.*?)\};", source, re.S).group(1)
        return [c_string(s) for s in re.findall(r'"((?:[^"\\]|\\.)*)"', body)]

    languages = len(dlc_loading.LANGUAGES)
    seasons, times, weathers = strings("seasons"), strings("times"), strings("weathers")
    blitz, checkpoint = strings("blitz_text"), strings("checkpoint_text")
    enums = {}
    for body in re.findall(r"enum\s*\{([^}]*)\}", source):
        for n, name in enumerate(v.strip() for v in body.split(",")):
            enums[name] = n
    texts = {}
    for city, mode, race, gates, seconds, season, time, weather, name in re.findall(
            r'\{(\d), (\d), (\d+), (\d+), (\d+), (\w+), (\w+), (\w+), "([^"]*)"\}', source):
        gates, seconds = int(gates), int(seconds)
        about = []
        for lang in range(languages):
            text = (checkpoint[lang] % gates if mode == "1" else
                    blitz[lang] % (gates, seconds // 60, seconds % 60))
            text += " %s, %s, %s." % (seasons[lang * 4 + enums[season]], times[lang * 4 + enums[time]],
                                      weathers[lang * 3 + enums[weather]])
            about.append(text)
        key = f"{CITIES[city]}/{('Blitz', 'Checkpoint')[int(mode)]}/{int(race) - 1:02d}"
        texts[key] = (c_string(name), about)
    return texts


# --- routes ------------------------------------------------------------------

class Streets:
    """A city's road centre lines as a graph, for the shortest way along the
    streets between two points."""

    def __init__(self, city):
        graph = roadgraph.load(city)
        self.xyz, self.links = [], []
        a_ids, b_ids, costs, meeting = [], [], [], {}
        for road in graph.roads:
            factor = 1.0 if road.width >= FOOTPATH else FOOTPATH_COST
            ids = [self._node(p) for p in road.points]
            for a, b in zip(ids, ids[1:]):
                self._link(a, b, factor)
                a_ids.append(a)
                b_ids.append(b)
                costs.append(factor)
            for side, end in ((0, ids[0]), (1, ids[-1])):
                key = road.ends[side]
                if key not in meeting:
                    meeting[key] = self._node(graph.position[key])
                self._link(end, meeting[key], 1.0)
        # The graph misses some junctions: a road ends alone a few metres
        # from where others end (Paris road 32 at the Etoile side, 26 m).
        # Join such an end to the road ends around it at its height.
        places = {key: np.array(graph.position[key]) for key in meeting}
        for key, members in graph.meetings.items():
            if len(members) != 1:
                continue
            for other, at in places.items():
                gap = np.abs(at - places[key])
                if other != key and math.hypot(gap[0], gap[2]) <= JUNCTION_GAP and gap[1] <= 3.0 and \
                        other != graph.roads[members[0][0]].ends[1 - members[0][1]]:
                    self._link(meeting[key], meeting[other], 1.0)
        xyz = np.array(self.xyz)
        self.a, self.b, self.cost = np.array(a_ids), np.array(b_ids), np.array(costs)
        self.a_xyz, self.b_xyz = xyz[self.a], xyz[self.b]

    def _node(self, p):
        self.xyz.append(tuple(p))
        self.links.append([])
        return len(self.xyz) - 1

    def _link(self, a, b, factor):
        cost = math.dist(self.xyz[a], self.xyz[b]) * factor
        self.links[a].append((b, cost))
        self.links[b].append((a, cost))

    def snap(self, p):
        """(segment, point) of the street nearest p; height counts double, so
        a checkpoint on a bridge keeps to the bridge."""
        weight = np.array([1.0, 2.0, 1.0])
        p = np.array(p, float)
        ab = self.b_xyz - self.a_xyz
        t = np.clip(((p - self.a_xyz) * ab * weight ** 2).sum(1) /
                    np.maximum((ab * ab * weight ** 2).sum(1), 1e-9), 0.0, 1.0)
        q = self.a_xyz + ab * t[:, None]
        i = int(np.argmin((((q - p) * weight) ** 2).sum(1)))
        return i, tuple(q[i])

    def route(self, p, q):
        """[(x, y, z)] from p to q along the streets."""
        (si, ps), (sj, qs) = self.snap(p), self.snap(q)
        if si == sj:
            return [tuple(p), ps, qs, tuple(q)]
        ends = lambda s, x: {int(n): math.dist(x, self.xyz[n]) * self.cost[s] for n in (self.a[s], self.b[s])}  # noqa: E731
        start, goal = ends(si, ps), ends(sj, qs)
        dist, previous = dict(start), {}
        heap = [(c, n) for n, c in start.items()]
        heapq.heapify(heap)
        best, last = math.inf, None
        while heap:
            c, n = heapq.heappop(heap)
            if c > dist.get(n, math.inf):
                continue
            if c >= best:
                break
            if n in goal and c + goal[n] < best:
                best, last = c + goal[n], n
            for m, w in self.links[n]:
                if c + w < dist.get(m, math.inf):
                    dist[m] = c + w
                    previous[m] = n
                    heapq.heappush(heap, (c + w, m))
        if last is None:
            raise ValueError(f"No street route from {p} to {q}")
        path = [last]
        while path[-1] in previous:
            path.append(previous[path[-1]])
        return [tuple(p), ps] + [self.xyz[n] for n in reversed(path)] + [qs, tuple(q)]


class Ground:
    """What the city map texture shows along a straight line: may a car cut
    across? (The street graph has no paths across parks and plazas: in
    Constitution Gardens and round the Etoile it went a kilometre round
    checkpoints 14-100 m apart.)"""

    def __init__(self, city, dvd):
        from PIL import Image
        textures = dlc_loading._module("mm3_textures", HERE.parent / "car-models" / "mm3_textures.py")
        name, self.a, self.b, self.c, self.d = dlc_loading.CITY_MAPS[city]
        rgb = np.asarray(Image.open(io.BytesIO(textures.cdds_to_dds(dvd.read(name)))).convert("RGB")).astype(int)
        r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        water = (b - r > 60) & (b - g > 40)
        landmark = (r - b > 80) & (g > 100) & (r > 180)                                  # orange buildings
        block = (abs(r - g) < 14) & (abs(g - b) < 18) & (r >= 150) & (r <= 205)        # grey city blocks
        outside = r + g + b < 40                                                          # beyond the city
        self.blocked = water | landmark | block | outside

    def clear(self, p, q):
        n = max(2, int(math.dist(p, q) / 2.0) + 1)
        t = np.linspace(0.0, 1.0, n)
        u = np.clip(np.rint(self.a * (p[0] + (q[0] - p[0]) * t) + self.b), 0, self.blocked.shape[1] - 1).astype(int)
        v = np.clip(np.rint(self.c * (p[1] + (q[1] - p[1]) * t) + self.d), 0, self.blocked.shape[0] - 1).astype(int)
        hit = self.blocked[v, u]
        run = longest = 0
        for h in hit:
            run = run + 1 if h else 0
            longest = max(longest, run)
        return hit.mean() <= CUT_BLOCKED and longest * math.dist(p, q) / (n - 1) <= CUT_RUN

    def cut(self, p, q):
        """[p, q], or [p, bend, q] round a pond or a corner, over open ground; or None."""
        if self.clear(p, q):
            return [p, q]
        length = math.dist(p, q)
        if length < 1.0:
            return None
        side = (-(q[1] - p[1]) / length, (q[0] - p[0]) / length)
        for offset in (20.0, 40.0, 60.0, 90.0, 120.0):
            bends = []
            for sign in (1.0, -1.0):
                for along in (0.3, 0.5, 0.7):
                    bend = (round(p[0] + (q[0] - p[0]) * along + side[0] * offset * sign, 1),
                            round(p[1] + (q[1] - p[1]) * along + side[1] * offset * sign, 1))
                    if self.clear(p, bend) and self.clear(bend, q):
                        bends.append((math.dist(p, bend) + math.dist(bend, q), bend))
            if bends:
                return [p, min(bends)[1], q]
        return None


def simplify(points, tolerance):
    """Douglas-Peucker on [x, z]."""
    if len(points) < 3:
        return points
    (x1, z1), (x2, z2) = points[0], points[-1]
    dx, dz = x2 - x1, z2 - z1
    length = math.hypot(dx, dz)
    far, index = -1.0, 0
    for i, (x, z) in enumerate(points[1:-1], 1):
        d = abs(dz * (x - x1) - dx * (z - z1)) / length if length > 1e-9 else math.hypot(x - x1, z - z1)
        if d > far:
            far, index = d, i
    if far <= tolerance:
        return [points[0], points[-1]]
    return simplify(points[:index + 1], tolerance)[:-1] + simplify(points[index:], tolerance)


def routes(tdata, source, dvd):
    texts = race_texts(source)
    races, streets, grounds = {}, {}, {}
    for setup_path in sorted((tdata / TITLE).glob("$C/*/Scripts/*/*/1?Setup.lua")):
        city, mode = setup_path.parent.parent.name, setup_path.parent.name
        if mode not in ("Blitz", "Checkpoint"):
            continue
        number = setup_path.name[:2]
        key = f"{city}/{mode}/{number}"
        logic = setup_path.with_name(number + "Logic.lua").read_bytes().decode("latin1")
        points = dlc_icons.race_points(setup_path.read_bytes().decode("latin1"), logic)
        if len(points) < 2 or key not in texts:
            raise ValueError(f"{setup_path}: no route or no name in src/mm3_dlc_races.c")
        if city not in streets:
            streets[city], grounds[city] = Streets(city), Ground(city, dvd)
        line, cuts = [], 0
        for p, q in zip(points, points[1:]):
            leg = [(round(v[0], 1), round(v[2], 1)) for v in streets[city].route(p, q)]
            ends = (leg[0], leg[-1])
            along = sum(math.dist(leg[i], leg[i + 1]) for i in range(len(leg) - 1))
            if along > DETOUR * math.dist(*ends) + DETOUR_EXTRA:
                cut = grounds[city].cut(*ends)
                if cut and sum(math.dist(cut[i], cut[i + 1]) for i in range(len(cut) - 1)) < along:
                    leg, cuts = cut, cuts + 1
            leg = simplify(leg, SIMPLIFY)
            line += leg[1:] if line else leg
        name, about = texts[key]
        races[key] = {"name": name, "about": about,
                      "start": [round(points[0][0], 1), round(points[0][2], 1)],
                      "checkpoints": [[round(p[0], 1), round(p[2], 1)] for p in points[1:]],
                      "route": [list(p) for p in line]}
        print(f"{key}: {name}, {len(points) - 1} checkpoints, {len(line)} route points"
              + (f", {cuts} straight across open ground" if cuts else ""))
    return races


# --- colours -----------------------------------------------------------------

def ball_fit(panel, points):
    """(scale, x0, z0, worst px) placing the race's start and checkpoints on
    the green and red balls of a retail map, or None."""
    from PIL import Image, ImageFilter
    r, g, b = panel[..., 0], panel[..., 1], panel[..., 2]
    height, width = r.shape

    def blobs(mask, opening):
        image = Image.fromarray(mask.astype(np.uint8) * 255)
        if opening:
            image = image.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.MaxFilter(5))
        a = np.asarray(image) > 0
        seen, out = np.zeros_like(a), []
        for y, x in zip(*np.nonzero(a)):
            if seen[y, x]:
                continue
            stack, members = [(y, x)], []
            seen[y, x] = True
            while stack:
                cy, cx = stack.pop()
                members.append((cy, cx))
                for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                    if 0 <= ny < height and 0 <= nx < width and a[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
            m = np.array(members, float)
            out.append((m[:, 1].mean() + 0.5, m[:, 0].mean() + 0.5, len(members)))
        return out

    green = [blob for blob in blobs((g > 100) & (g - r > 25) & (g - b > 40), True) if blob[2] >= 12]
    # A ball's shine is pink-white; the route line's is red.
    red_area = (r > 90) & (r - g > 60) & (r - b > 60)
    yy, xx = np.mgrid[0:height, 0:width]
    red = []
    for hx, hy, _ in blobs((r > 235) & (g > 85) & (b > 85) & (r - b > 25), False):
        near = red_area & ((xx + 0.5 - hx) ** 2 + (yy + 0.5 - hy) ** 2 < 36)
        if near.sum() >= 8:
            red.append((xx[near].mean() + 0.5, yy[near].mean() + 0.5))
    if len(green) != 1 or not red:
        return None
    start = np.array([points[0][0], points[0][2]])
    gates = np.array([[p[0], p[2]] for p in points[1:]])
    balls = np.array(red)
    gx, gy = green[0][:2]
    best = None
    for scale in np.arange(0.06, 0.40, 0.0005):
        x0, z0 = start[0] - gx / scale, start[1] + gy / scale
        at = np.c_[(gates[:, 0] - x0) * scale, (z0 - gates[:, 1]) * scale]
        miss = np.minimum(np.sqrt(((at[:, None] - balls[None]) ** 2).sum(2)).min(1), 12).mean()
        if best is None or miss < best[0]:
            best = (miss, scale, x0, z0)
    _, scale, x0, z0 = best
    at = np.c_[(gates[:, 0] - x0) * scale, (z0 - gates[:, 1]) * scale]
    nearest = np.sqrt(((at[:, None] - balls[None]) ** 2).sum(2)).argmin(1)
    world = np.r_[start[None], gates]
    seen = np.r_[[[gx, gy]], balls[nearest]]
    a = np.zeros((2 * len(world), 3))
    a[0::2, 0], a[0::2, 1] = world[:, 0], -1.0     # px = s*x - s*x0
    a[1::2, 0], a[1::2, 2] = -world[:, 1], 1.0     # py = -s*z + s*z0
    target = seen.reshape(-1)
    (scale, sx0, sz0), *_ = np.linalg.lstsq(a, target, rcond=None)
    worst = float(np.abs(a @ np.array([scale, sx0, sz0]) - target).max())
    return float(scale), float(sx0 / scale), float(sz0 / scale), worst


def outlines(grey):
    """Gradient strength inside the frame (8 px in), flattened."""
    gx, gy = np.zeros_like(grey), np.zeros_like(grey)
    gx[:, 1:-1], gy[1:-1] = grey[:, 2:] - grey[:, :-2], grey[2:] - grey[:-2]
    return np.hypot(gx, gy)[8:-8, 8:-8].ravel()


def colours(game):
    from PIL import Image, ImageFilter
    hd = zipfile.ZipFile(game / "Data" / "Data_hd.zip")
    dvd = zipfile.ZipFile(game / "Data" / "Data_dvd.zip")
    names = {name.lower(): name for name in dvd.namelist()}
    identity = {"colours": {"size": 2, "table": base64.b64encode(bytes(
        [v for b in (0, 255) for g in (0, 255) for r in (0, 255) for v in (r, g, b)])).decode()}}
    painter = dlc_loading.Painter(dlc_loading._pillow(), identity)
    x, y, w, h = dlc_loading.PANEL
    sources, targets, used = [], [], 0
    for city in dlc_loading.CITY_MAPS:
        for mode in ("Blitz", "Checkpoint"):
            for number in range(10):
                screen = names.get(f"loadingscreens/english/{city}/{mode}/loading{number:02d}.tga".lower())
                script = f"Scripts/{city}/{mode}/{number:02d}"
                if not screen:
                    continue
                points = dlc_icons.race_points(hd.read(script + "Setup.lua").decode("latin1"),
                                               hd.read(script + "Logic.lua").decode("latin1"))
                retail = np.asarray(Image.open(io.BytesIO(dvd.read(screen))).convert("RGB"))[y:y + h, x:x + w].astype(int)
                fit = ball_fit(retail, points)
                if not fit or fit[3] > BALL_FIT_LIMIT or len(points) < 6:
                    print(f"{city} {mode} {number:02d}: balls not placed, left out")
                    continue
                scale, x0, z0, _ = fit
                # Outlines, not brightness: retail draws outside the city light,
                # where the texture is black.
                lines = outlines(retail.mean(2))
                best = None
                for zoom in (0.995, 1.0, 1.005):
                    for dx in np.arange(-2.0, 2.1, 0.5):
                        for dy in np.arange(-2.0, 2.1, 0.5):
                            place = (scale * zoom, x0 + dx / (scale * zoom), z0 - dy / (scale * zoom))
                            texture = outlines(np.asarray(painter.map_panel(dvd, city, place).convert("L"), float))
                            match = np.corrcoef(texture, lines)[0, 1]
                            if best is None or match > best[0]:
                                best = (match, place)
                match, place = best
                if match < MATCH_LIMIT:
                    print(f"{city} {mode} {number:02d}: matches the texture only {match:.2f}, left out")
                    continue
                texture = np.asarray(painter.map_panel(dvd, city, place)).astype(int)
                route = (retail[..., 0] - retail[..., 2] > 30) | (retail[..., 1] - retail[..., 2] > 20) | (retail.sum(2) < 90)
                keep = ~(np.asarray(Image.fromarray(route.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(11))) > 0)
                keep[:3], keep[-3:], keep[:, :3], keep[:, -3:] = False, False, False, False
                sources.append(texture[keep])
                targets.append(retail[keep])
                used += 1
                print(f"{city} {mode} {number:02d}: {place[0]:.3f} px/m, match {match:.2f}")
    source, target = np.concatenate(sources), np.concatenate(targets)
    n = COLOUR_GRID
    cell = np.rint(source / 255.0 * (n - 1)).astype(int)
    flat = cell[:, 0] + n * (cell[:, 1] + n * cell[:, 2])          # red fastest, as Color3DLUT
    table = np.full((n ** 3, 3), np.nan)
    order = np.argsort(flat, kind="stable")
    for index, members in zip(range(n ** 3), np.split(order, np.cumsum(np.bincount(flat, minlength=n ** 3))[:-1])):
        if len(members) >= 3:
            table[index] = np.median(target[members], axis=0)
    known = ~np.isnan(table[:, 0])
    coords = np.stack([np.arange(n ** 3) % n, np.arange(n ** 3) // n % n, np.arange(n ** 3) // (n * n)], 1)
    for index in np.nonzero(~known)[0]:
        d = np.sqrt(((coords[known] - coords[index]) ** 2).sum(1))
        near = np.argsort(d)[:4]
        weight = 1.0 / (d[near] + 1e-3)
        table[index] = (table[known][near] * weight[:, None]).sum(0) / weight.sum()
    print(f"colour table from {used} retail maps, {len(source)} pixels, {int(known.sum())} of {n ** 3} cells seen")
    return {"size": n, "table": base64.b64encode(np.clip(np.rint(table), 0, 255).astype(np.uint8).tobytes()).decode(),
            "maps": used}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    routes_parser = sub.add_parser("routes")
    routes_parser.add_argument("--tdata", type=Path, default=ROOT / "game_files" / "TDATA")
    routes_parser.add_argument("--game", type=Path, default=ROOT / "game_files")
    colours_parser = sub.add_parser("colours")
    colours_parser.add_argument("--game", type=Path, default=ROOT / "game_files")
    args = parser.parse_args()
    data = dlc_loading.load_data()
    if args.command == "routes":
        with zipfile.ZipFile(args.game / "Data" / "Data_dvd.zip") as dvd:
            data["races"] = routes(args.tdata, (ROOT / "src" / "mm3_dlc_races.c").read_text(encoding="latin1"), dvd)
    else:
        data["colours"] = colours(args.game)
    dlc_loading.DATA_FILE.write_text(json.dumps(data, indent=1, sort_keys=True, ensure_ascii=False) + "\n",
                                     encoding="utf-8")
    print(f"wrote {dlc_loading.DATA_FILE}")


if __name__ == "__main__":
    main()
