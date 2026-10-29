"""Circuit races for Midtown Madness 3: lapped Checkpoint races 20-29 per city.

The race list scans Scripts/<City>/Checkpoint for NNConfig files and keys each
race by NN, after the retail 00-09 and the DLC 10-19. Each circuit is three
script files plus a menu picture; tools/car-mods/prepare.py appends them to the
archives (`race_entries`) and writes the names to Data/mm3_circuits.ini
(`names_ini`), which src/mm3_circuit_races.c shows in the race list.
Scripts/logic_circuit.lua (beside this file) adds laps and strict gate order
on top of the retail checkpoint logic.

DESIGNS below name each circuit's corners on the city's street graph;
`python tools/races/design.py write` resolves them (start/finish and grid,
gates, opponent waypoints, containers blocking every road off the loop) into
circuits.json, which this module turns into race files. Circuits run without
traffic, pedestrians or police.
"""
import base64
import importlib.util
import io
import json
import math
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
RESOLVED = HERE / "circuits.json"
FIRST_RACE = 20
CITY_IDS = {"Paris": 1, "Washington": 2}
SEASONS = {"Spring", "Summer", "Fall", "Winter"}
TIMES = {"Morning", "Noon", "Evening", "Night"}
WEATHERS = {"Clear", "Rain", "Snow"}
GATE_TYPE, WAYPOINT_TYPE, RECT_ZONE_TYPE, PROP_TYPE = 1, 5, 10, 9
START_ZONE = "ZN-START"
# Opponents race with the retail top personalities (04_*, from the race-09
# ramp in Scripts/logic_circuit.lua), unchanged, aggressive and passive in
# turn on the grid as in the retail race 09 (all aggressive fought each other
# in packs).
OPPONENT_WINDOW = 3        # targets an opponent has armed ahead (circuit_window)


def _design(city, race, name, laps, season, time, weather, corners, about):
    return {"city": city, "race": race, "name": name, "laps": laps, "season": season,
            "time": time, "weather": weather, "corners": corners, "about": about}


# Corners are junction numbers on `design.py map <City>`. Laps make about
# 4-12 km of racing; the last race of each city tours the city at night in snow.
# Opponents catch up sooner race by race (Scripts/logic_circuit.lua).
DESIGNS = [
    _design("Washington", 20, "Circuit: Downtown Blocks", 5, "Spring", "Morning", "Clear", [64, 63, 169, 138],
            "Fast laps around a tight downtown block. Short straights and hard corners: brake late, stay clean."),
    _design("Washington", 21, "Circuit: Georgetown", 3, "Fall", "Noon", "Rain", [353, 351, 362, 361, 340, 347, 346],
            "The old streets of Georgetown in the autumn rain. Narrow, twisty and slippery all the way round."),
    _design("Washington", 22, "Circuit: The Ellipse", 2, "Summer", "Evening", "Clear", [63, 302, 300, 48],
            "Long sweeping bends past the monuments into the summer sunset. Smooth lines win this one."),
    _design("Washington", 23, "Circuit: Chinatown", 5, "Summer", "Noon", "Clear", [200, 205, 203, 230],
            "A short, punchy loop through Chinatown. Every corner counts: one mistake and the pack is gone."),
    _design("Washington", 24, "Circuit: Lakeside", 2, "Spring", "Evening", "Rain", [302, 284, 294, 144, 106],
            "Fast stretches along the water, then a twisty run back through the city, on a rainy spring evening."),
    _design("Washington", 25, "Circuit: Avenue Sprint", 3, "Fall", "Morning", "Clear", [29, 39, 232, 213, 228],
            "Built around the broad avenues: flat out on the long straights, then stand on the brakes."),
    _design("Washington", 26, "Circuit: Southeast Sweep", 2, "Summer", "Night", "Clear", [196, 231, 266, 275, 237, 317],
            "Long laps through the southeast of the city on a warm summer night. High speeds, wide turns."),
    _design("Washington", 27, "Circuit: West End", 5, "Fall", "Evening", "Rain", [33, 8, 14, 34],
            "The shortest circuit in Washington, in the evening rain. Close racing from start to finish."),
    # The last two races tour the whole city on its highways: the southern
    # freeway loop and the west parkway, with the big avenues between.
    _design("Washington", 28, "Circuit: Grand Tour", 2, "Spring", "Noon", "Clear",
            [34, 86, 6, 17, 27, 22, 18, 39, 29, 71, 229, 220, 196, 298, 281, 280, 278, 144, 145, 148, 155,
             163, 149, 1, 112, 342, 246],
            "Across the city centre, down the long avenue, then flat out on the southern freeway and up the "
            "west parkway. A big tour with no second chances."),
    _design("Washington", 29, "Circuit: Midnight Snow Tour", 2, "Winter", "Night", "Snow",
            [5, 43, 42, 83, 72, 47, 39, 218, 215, 232, 259, 262, 256, 253, 266, 275, 237, 319, 317, 295, 296,
             297, 298, 281, 280, 278, 144, 145, 148, 155, 163, 149, 1, 112, 342, 223, 331, 30, 389, 392, 3, 4, 73],
            "The final challenge: all the way round Washington on its highways and outer avenues, at midnight "
            "in the snow."),
    # Paris: courses keep clear of the Etoile's star of avenues and of the
    # 4 m links (Concorde, the Peripherique's ramps). The last race is the
    # Peripherique itself, all the way round the city.
    _design("Paris", 20, "Circuit: Left Bank Blocks", 5, "Spring", "Morning", "Clear",
            [364, 365, 395, 397, 398, 410, 347, 391],
            "Short, tight laps round the blocks behind the left bank. Hard braking, quick corners, no room for mistakes."),
    _design("Paris", 21, "Circuit: Montmartre", 3, "Fall", "Noon", "Rain",
            [50, 19, 60, 18, 29, 31, 30, 25, 26, 53],
            "Narrow streets below the hill of Montmartre in the autumn rain. Slippery, twisty and unforgiving."),
    _design("Paris", 22, "Circuit: Seine Loop", 3, "Summer", "Evening", "Clear",
            [470, 444, 446, 317, 300, 376, 364, 391],
            "Along both banks of the Seine and over two bridges, into the summer sunset."),
    _design("Paris", 23, "Circuit: Le Marais", 5, "Summer", "Noon", "Clear",
            [103, 117, 127, 115, 126, 134, 132, 133, 116],
            "A short, punchy loop through the old streets of the Marais. Every corner counts."),
    _design("Paris", 24, "Circuit: Champs-Elysees", 4, "Spring", "Night", "Clear",
            [475, 447, 448, 466, 467, 449, 455, 442, 445, 444, 471, 470],
            "Flat out down the widest avenue in Paris at night, then back along the river."),
    _design("Paris", 25, "Circuit: Bastille", 3, "Fall", "Morning", "Rain",
            [145, 147, 156, 160, 148, 150, 178, 187, 188, 111, 87, 89, 86, 97, 91, 85, 88, 154],
            "Wide boulevards round the east of the city on a wet autumn morning."),
    _design("Paris", 26, "Circuit: Left Bank Diagonals", 3, "Summer", "Night", "Clear",
            [371, 387, 372, 405, 411, 341, 342, 363, 362, 340, 392, 396, 343, 399, 397, 398, 352, 413, 406, 358],
            "Two long diagonal avenues across the left bank on a warm summer night. High speeds, sharp ends."),
    _design("Paris", 27, "Circuit: Grands Boulevards", 2, "Fall", "Evening", "Rain",
            [473, 461, 390, 463, 434, 125, 103, 117, 127, 118, 123, 144, 88, 154, 145, 16, 66, 48, 51, 61, 27, 28,
             64, 39, 15, 18, 60, 19, 50, 429, 368, 428, 357, 456, 426, 425],
            "The great boulevards across the north of Paris in the evening rain. Long, fast and wide."),
    _design("Paris", 28, "Circuit: Grand Tour", 2, "Spring", "Noon", "Clear",
            [461, 390, 463, 434, 125, 103, 117, 127, 118, 123, 144, 88, 154, 145, 147, 156, 148, 150, 178, 236,
             238, 276, 309, 305, 311, 310, 274, 285, 275, 287, 295, 294, 302, 300, 370, 376, 377, 364, 391, 470,
             475, 447, 448, 466, 460, 459],
            "Boulevards, the Bastille, the south bank and the Champs-Elysees: one big tour of Paris."),
    _design("Paris", 29, "Circuit: Midnight Snow Tour", 2, "Winter", "Night", "Snow",
            [9, 12, 67, 71, 72, 73, 76, 80, 81, 84, 226, 229, 220, 221, 318, 321, 322, 326, 415, 416, 257, 290,
             301, 335, 5, 8],
            "The final challenge: all the way round Paris on the Peripherique, at midnight in the snow."),
]


ICON_SIZE = 64            # the race list's pictures: 64 x 64, a white line of the route
ICON_SCALE = 8            # drawn this much larger, then scaled down (smooth edges)
ICON_LINE = 3.4           # line width in icon pixels, like the retail route pictures
ICON_MARGIN = 14.5        # the route about 38 px across, as the retail pictures (median 39 px)


def outline(points, step):
    """The course as [x, z] about every `step` metres (a closed loop)."""
    out, travelled = [[round(points[0][0], 1), round(points[0][2], 1)]], 0.0
    for a, b in zip(points, points[1:]):
        travelled += math.dist((a[0], a[2]), (b[0], b[2]))
        if travelled >= step:
            out.append([round(b[0], 1), round(b[2], 1)])
            travelled = 0.0
    return out


def icon_cdds(course, closed=True):
    """The race list's picture of a circuit, as in the retail ones: its route
    as a white line on a clear 64 x 64, north up (needs Pillow and
    tools/car-models/mm3_textures.py; run by design.py write). closed=False
    draws a start-to-finish route (dlc_icons.py)."""
    import importlib.util
    from PIL import Image, ImageDraw
    spec = importlib.util.spec_from_file_location(
        "mm3_textures", HERE.parent / "car-models" / "mm3_textures.py")
    textures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(textures)
    xs, zs = [p[0] for p in course], [p[1] for p in course]
    span = max(max(xs) - min(xs), max(zs) - min(zs)) or 1.0
    room = (ICON_SIZE - 2 * ICON_MARGIN) * ICON_SCALE
    size = ICON_SIZE * ICON_SCALE
    scale = room / span
    left = (size - (max(xs) - min(xs)) * scale) / 2
    top = (size - (max(zs) - min(zs)) * scale) / 2
    place = [(left + (x - min(xs)) * scale, top + (max(zs) - z) * scale) for x, z in course]
    image = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    draw = ImageDraw.Draw(image)
    width = round(ICON_LINE * ICON_SCALE)
    draw.line(place + place[:2] if closed else place, fill=(255, 255, 255, 255), width=width, joint="curve")
    for x, y in place:
        draw.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=(255, 255, 255, 255))
    small = image.resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)
    png = io.BytesIO()
    small.save(png, "PNG")
    png.seek(0)
    return textures.image_to_menu_cdds(png)


def _round(values):
    return [round(v, 3) if isinstance(v, float) else v for v in values]


def write_resolved(resolved):
    """design.py: store the resolved layouts (sorted by city and race)."""
    out = []
    for spec, c in sorted(resolved, key=lambda item: (item[0]["city"], item[0]["race"])):
        validate(spec)
        out.append({key: spec[key] for key in ("city", "race", "name", "laps", "season", "time", "weather", "about")} | {
            "lap_m": round(c.lap), "corners": spec["corners"],
            "gates": [_round(g) for g in c.gates],
            "waypoints": {str(k): [_round(w) for w in v] for k, v in sorted(c.waypoints.items())},
            "grid": [_round(g) for g in c.grid],
            "start_line": _round(c.start_line),
            "icon": base64.b64encode(icon_cdds(outline(c.points, max(10.0, c.lap / 150)))).decode("ascii"),
            "props": [_round(p) for p in c.props]})
    RESOLVED.write_text(json.dumps(out, indent=1) + "\n")
    print(f"wrote {RESOLVED} ({len(out)} circuits)")


def circuits():
    return json.loads(RESOLVED.read_text()) if RESOLVED.is_file() else []


def validate(circuit):
    if circuit["city"] not in CITY_IDS:
        raise ValueError(f"Unknown city {circuit['city']}")
    if not FIRST_RACE <= circuit["race"] < FIRST_RACE + 10:
        raise ValueError(f"{circuit['name']}: circuit races are numbered {FIRST_RACE}-{FIRST_RACE + 9}")
    if not 1 <= circuit["laps"] <= 5:
        raise ValueError(f"{circuit['name']}: 1 to 5 laps")
    if circuit["season"] not in SEASONS or circuit["time"] not in TIMES or circuit["weather"] not in WEATHERS:
        raise ValueError(f"{circuit['name']}: season, time of day or weather not known to the game")
    name = circuit["name"]
    if not name.startswith("Circuit: ") or len(name) > 40 or not all(" " <= ch <= "~" for ch in name):
        raise ValueError(f"{name}: names start with 'Circuit: ', are printable ASCII and fit 40 characters")
    info = info_text(circuit)
    if not 20 <= len(info) <= 300 or not all(" " <= ch <= "~" for ch in info):
        raise ValueError(f"{name}: the race info is 20-300 printable ASCII characters")


def info_text(circuit):
    """The race list's info box: the description, laps and opponents."""
    laps, km = circuit["laps"], circuit.get("lap_m", 0) / 1000
    tier = circuit["race"] - FIRST_RACE
    level = "hard" if tier < 4 else "very hard" if tier < 8 else "extreme"
    lap_text = f"1 lap of {km:.1f} km" if laps == 1 else f"{laps} laps of {km:.1f} km"
    return f"{circuit['about']} {lap_text}. Opponents: {level}."


def config_text(circuit):
    lines = [f"Season\t\t{circuit['season']}", f"TimeOfDay\t{circuit['time']} ",
             f"Weather\t\t{circuit['weather']} ", "AmbientDensity\tNone None ", "CopsCount\t0 ", ""]
    return "\r\n".join(lines).encode("ascii")


# The game reads a script's size as a signed 16-bit number (sub_00214FC5)
# and silently skips any script of 32 KB or more: the race then has no
# objects and its logic never arms a gate. Setup scripts make each object in
# one short call to stay well under that.
SCRIPT_LIMIT = 32767
SCRIPT_BUDGET = 30000
SETUP_HELPERS = [
    "function MkObj(kind, name, rotate, param, x, y, z)",
    "\tobjectCreate(kind, name)", "\tobjectRotate(rotate)", "\tobjectSetParam(param)", "\tobjectSetPos(x, y, z)",
    "end",
    "function MkProp(name, rotate, x, y, z)",
    "\tMkObj(9, name, rotate, 0, x, y, z)", "\tobjectLoadModel(circuit_prop_model)", "end",
    "function MkZone(name, rotate, across, along, x, y, z, model)",
    "\tobjectCreate(10, name)", "\tobjectRotate(rotate)", "\tobjectSetParam(across, along)",
    "\tobjectSetPos(x, y, z)", "\tobjectLoadModel(model)", "end"]


def _n(value, places=2):
    text = f"{value:.{places}f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def opponent_route(circuit):
    """One lap of the opponents' targets in driving order (Scripts/logic_circuit.lua):
    [(rotation, reach, x, y, z, gate)] - the path points before each gate, then
    the gate (gate number; -1 for a path point)."""
    route = []
    for gate, (rotate, width, x, y, z) in enumerate(circuit["gates"]):
        route += [tuple(w) + (-1,) for w in circuit["waypoints"].get(str(gate), [])]
        route.append((rotate, width / 2, x, y, z, gate))
    if len(route) <= OPPONENT_WINDOW + 1:
        raise ValueError(f"{circuit['name']}: the opponents' path needs over {OPPONENT_WINDOW + 1} targets a lap")
    return route


def setup_text(circuit):
    gates, props, route = circuit["gates"], circuit["props"], opponent_route(circuit)
    models = {model for model, *_ in props}
    if len(models) > 1 or not all(model.replace("_", "").isalnum() for model in models):
        raise ValueError(f"{circuit['name']}: one prop model with a plain name, not {sorted(models)}")
    lines = SETUP_HELPERS + [f'circuit_prop_model = "{next(iter(models), "container")}"', "function onInit()",
                             f"objectCount({len(gates) + len(route) * circuit['laps'] + len(props) + 9})"]
    for index, (rotate, param, x, y, z) in enumerate(gates):
        lines.append(f'MkObj({GATE_TYPE},"CP-{index}",{_n(rotate, 4)},{_n(param)},{_n(x)},{_n(y)},{_n(z)})')
    # Opponents' waypoints: every lap its own, so none is ever armed twice.
    for lap in range(circuit["laps"]):
        for index, (rotate, reach, x, y, z, _) in enumerate(route):
            lines.append(f'MkObj({WAYPOINT_TYPE},"WP-{lap * len(route) + index}",{_n(rotate, 4)},{_n(reach)},'
                         f'{_n(x)},{_n(y)},{_n(z)})')
    for index, (_, x, y, z, rotate) in enumerate(props):
        lines.append(f'MkProp("PH-{index}",{_n(rotate, 3)},{_n(x)},{_n(y)},{_n(z)})')
    # A green strip across the road at the start/finish line (a zone shown,
    # never armed).
    rotate, width, depth, x, y, z = circuit["start_line"]
    lines.append(f'MkZone("{START_ZONE}",{_n(rotate, 4)},{_n(width)},{_n(depth)},{_n(x)},{_n(y)},{_n(z)},"RZoneGreen")')
    lines += ["end", ""]
    text = "\r\n".join(lines).encode("ascii")
    if len(text) > SCRIPT_BUDGET:
        raise ValueError(f"{circuit['name']}: Setup.lua is {len(text)} bytes; the game skips scripts of "
                         f"{SCRIPT_LIMIT + 1} bytes or more")
    return text


def logic_text(circuit, original_cars=()):
    gates, route = circuit["gates"], opponent_route(circuit)
    out = ["-- " + "*" * 80,
           "-- * Midtown Madness 3 PC circuit race (tools/races/circuits.py)",
           f"-- * \"{circuit['name']}\", {circuit['laps']} laps of {circuit['lap_m']} m, {len(gates)} gates",
           "-- " + "*" * 80,
           'gameLoadScript("logic_general.lua")',
           "if gameIsMulti() == 1 then",
           '\tgameLoadScript("logic_multi_checkpoint.lua")',
           "else",
           '\tgameLoadScript("logic_checkpoint.lua")',
           '\tgameLoadScript("logic_circuit.lua")',
           "end",
           f"total_gates = {len(gates)}",
           f"laps = {circuit['laps']}",
           f"circuit_tier = {circuit['race'] - FIRST_RACE}",
           f'circuit_start_zone = "{START_ZONE}"',
           "circuit_original_cars = {" + "".join(f'"{car.lower()}", ' for car in original_cars) + ";}",
           "if gameIsMulti() == 1 then",
           "\ttotal_targets = total_gates",
           "else",
           "\ttotal_targets = total_gates * laps",
           "end",
           "spawn_location = {}"]
    for index, (x, y, z, a, personality) in enumerate(circuit["grid"], 1):
        extra = "" if index == 1 else f', "", "{personality}"'
        out.append(f"\tspawn_location[{index}] = {{{x:.6f},{y:.6f},{z:.6f}, {a:.6f}{extra};}}")
    # The opponents' path (Scripts/logic_circuit.lua): one lap of targets in
    # driving order, each with its position, how near counts as reached, and
    # its gate.
    out.append(f"circuit_route_count = {len(route)}")
    out.append("circuit_route_pos = {" + "".join(f"{{{x:.1f}, {z:.1f}, {reach:.1f};}}, "
                                                 for _, reach, x, _, z, _ in route) + ";}")
    out.append("circuit_route_gate = {" + "".join(f"{gate}, " for *_, gate in route) + ";}")
    out.append("ai_target_orders = 1")
    out.append(f"ai_targets[1] = {{{len(route)}, " + ",".join(f'"WP-{k}"' for k in range(len(route))) + ";}")
    out.append("")
    return "\r\n".join(out).encode("ascii")


def race_entries(hd, dvd, original_cars=()):
    """{"Data_hd.zip": {name: bytes}, "Data_dvd.zip": {name: bytes}} for every circuit.

    An archive without the retail checkpoint logic (not a game archive) gets
    no races."""
    if "Scripts/logic_checkpoint.lua" not in set(hd.namelist()):
        return {"Data_hd.zip": {}, "Data_dvd.zip": {}}
    lua = (HERE / "logic_circuit.lua").read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
    hd_entries, dvd_entries, seen = {"Scripts/logic_circuit.lua": lua}, {}, set()
    pictures = set(dvd.namelist())
    for circuit in circuits():
        validate(circuit)
        key = (circuit["city"], circuit["race"])
        if key in seen:
            raise ValueError(f"Two circuits share {key}")
        seen.add(key)
        if not 3 <= len(circuit["gates"]) <= 60 or len(circuit["grid"]) != 8:
            raise ValueError(f"{circuit['name']}: 3 to 60 gates and eight grid places")
        base = f"Scripts/{circuit['city']}/Checkpoint/{circuit['race']:02d}"
        hd_entries[base + "Config"] = config_text(circuit)
        hd_entries[base + "Setup.lua"] = setup_text(circuit)
        hd_entries[base + "Logic.lua"] = logic_text(circuit, original_cars)
        # The race list's picture: the circuit's own route (icon_cdds).
        target = f"Menu/Gfx/races/{circuit['city']}/Checkpoint/{circuit['race']:02d}.cdds"
        if circuit.get("icon"):
            dvd_entries[target] = base64.b64decode(circuit["icon"])
        elif "Menu/Gfx/races/NoIcon.cdds" in pictures:
            dvd_entries[target] = dvd.read("Menu/Gfx/races/NoIcon.cdds")
    # The downloadable races' own route pictures (dlc_icons.py), beside the
    # DLC's copies of races 1-10: src/mm3_dlc_races.c asks for <NN>_mm3.
    for key, icon in dlc_icons().items():
        dvd_entries[f"Menu/Gfx/races/{key}_mm3.cdds"] = base64.b64decode(icon)
    # And their loading screens with a route map (dlc_loading.py), drawn from
    # the retail screens and city maps in this archive.
    dvd_entries.update(dlc_loading_screens(dvd))
    return {"Data_hd.zip": hd_entries, "Data_dvd.zip": dvd_entries}


DLC_ICONS = HERE / "dlc_icons.json"


def dlc_icons():
    """{"<City>/<Mode>/<NN>": base64 CDDS} for the downloadable races 10-19."""
    return json.loads(DLC_ICONS.read_text()) if DLC_ICONS.exists() else {}


def _dlc_loading():
    path = HERE / "dlc_loading.py"
    if not path.is_file():
        return None
    spec = importlib.util.spec_from_file_location("mm3_dlc_loading", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def dlc_loading_screens(dvd):
    """{Data_dvd.zip name: TGA} of the downloadable races' loading screens."""
    module = _dlc_loading()
    return module.screens(dvd) if module else {}


def dlc_loading_ini(dvd_entries):
    """Data/mm3_dlc_loading.ini: which loading screens race_entries made."""
    module = _dlc_loading()
    return module.ini(dvd_entries) if module else b""


def dlc_icons_ini():
    """Data/mm3_dlc_icons.ini: which downloadable races have a picture."""
    lines = ["# MM3 downloadable races with their own route picture (<City>/<Mode>/<NN>_mm3.cdds)"]
    lines.extend(sorted(dlc_icons()))
    return ("\n".join(lines) + "\n").encode("ascii")


def names_ini():
    """Data/mm3_circuits.ini for src/mm3_circuit_races.c."""
    lines = ["# MM3 circuit races: city (1 Paris, 2 Washington) race name<TAB>info box text"]
    for circuit in circuits():
        lines.append(f"{CITY_IDS[circuit['city']]} {circuit['race']} {circuit['name']}\t{info_text(circuit)}")
    return ("\n".join(lines) + "\n").encode("ascii")


if __name__ == "__main__":
    for circuit in circuits():
        print(f"{circuit['city']} {circuit['race']} {circuit['name']}: {circuit['laps']} x {circuit['lap_m']} m, "
              f"{len(circuit['gates'])} gates, {len(circuit['props'])} containers")
