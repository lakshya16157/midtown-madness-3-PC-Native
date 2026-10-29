"""Loading screens with a route map for the downloadable races (tools/races/dlc_loading.py).

The data (dlc_loading.json) must say what src/mm3_dlc_races.c says, and each
route must run from the start through every checkpoint in order. With the
game's files present, the screens are drawn from the pristine Data_dvd.zip
and checked against their retail templates: same format, the hints and the
rest of the picture untouched, a new title, description and map with the
route on it.
"""
import base64
import importlib.util
import io
import math
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RETAIL_HEADER = bytes.fromhex("0000020000000018000000008002e0011820")   # LoadingScreens/English/Paris/Blitz/Loading00.TGA


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def check_data(loading, data_tool):
    data = loading.load_data()
    races = data["races"]
    expected = {f"{city}/{mode}/{n}" for city in ("Paris", "Washington") for mode in ("Blitz", "Checkpoint")
                for n in range(10, 20)}
    assert set(races) == expected, sorted(set(races) ^ expected)
    texts = data_tool.race_texts((ROOT / "src" / "mm3_dlc_races.c").read_text(encoding="latin1"))
    for key, race in races.items():
        name, about = texts[key]
        assert race["name"] == name and race["about"] == about, f"{key}: run dlc_loading_data.py routes again"
        assert len(about) == len(loading.LANGUAGES)
        route = [tuple(p) for p in race["route"]]
        points = [tuple(race["start"])] + [tuple(p) for p in race["checkpoints"]]
        at = 0
        for point in points:
            at = route.index(point, at)
        assert route[0] == points[0] and route[-1] == points[-1], key
        for a, b in zip(points, points[1:]):
            i, j = route.index(a), route.index(b, route.index(a))
            along = sum(math.dist(route[k], route[k + 1]) for k in range(i, j))
            assert along <= 4 * math.dist(a, b) + 300, (key, a, b, along)
    table = base64.b64decode(data["colours"]["table"])
    assert len(table) == 3 * data["colours"]["size"] ** 3 and data["colours"]["maps"] >= 30
    print(f"PASS: {len(races)} downloadable races: names and descriptions as src/mm3_dlc_races.c words them, "
          "routes along the streets through every checkpoint in order")


def pristine_dvd():
    for path in (ROOT / "game_files" / ".mm3-car-mods" / "Data_dvd.zip", ROOT / "game_files" / "Data" / "Data_dvd.zip"):
        if path.is_file():
            return path
    return None


def check_screens(loading):
    path = pristine_dvd()
    if not path or loading._pillow() is None:
        print("SKIP: screens (no game files or no Pillow)")
        return
    from PIL import Image, ImageChops
    keys = ["Paris/Blitz/10", "Washington/Checkpoint/19"]
    with zipfile.ZipFile(path) as dvd:
        made = loading.screens(dvd, races=keys)
        names = {name.lower(): name for name in dvd.namelist()}
        assert len(made) == len(keys) * len(loading.LANGUAGES), sorted(made)
        for name, blob in made.items():
            _, language, city, mode, file = name.split("/")
            number = int(file[len("Loading"):len("Loading") + 2])
            template = dvd.read(names[f"loadingscreens/{language}/{city}/{mode}/loading{number - 10:02d}.tga".lower()])
            # Top row first, as English Paris's retail screens (the others are
            # stored bottom up with a TGA 2.0 footer; the game reads both).
            assert len(blob) == 18 + 640 * 480 * 3 and blob[:18] == RETAIL_HEADER, name
            new, old = Image.open(io.BytesIO(blob)).convert("RGB"), Image.open(io.BytesIO(template)).convert("RGB")
            changed = ImageChops.difference(new, old).convert("L").point(lambda v: 255 if v > 2 else 0)
            # Only the title, the map and the description change.
            title, panel, text = loading.TITLE_AREA, loading.PANEL, loading.TEXT_AREA
            keep = Image.new("L", (640, 480), 255)
            for box in (title, (panel[0], panel[1], panel[0] + panel[2], panel[1] + panel[3]), text):
                keep.paste(0, box)
            assert ImageChops.multiply(changed, keep).getbbox() is None, (name, ImageChops.multiply(changed, keep).getbbox())
            for box, least in ((title, 1500), (text, 1500)):
                assert sum(changed.crop(box).histogram()[1:]) >= least, (name, box)
            map_box = (panel[0], panel[1], panel[0] + panel[2], panel[1] + panel[3])
            raw = new.crop(map_box).tobytes()
            pixels = list(zip(raw[0::3], raw[1::3], raw[2::3]))
            red = sum(1 for r, g, b in pixels if r > 180 and g < 70 and b < 70)
            green = sum(1 for r, g, b in pixels if g > 150 and r < 120 and b < 90)
            blue = sum(1 for r, g, b in pixels if b > r + 20 and b > 120)
            assert red > 400 and green > 20 and blue > len(pixels) // 2, (name, red, green, blue)
    listed = loading.ini(made).decode("ascii").splitlines()
    assert listed[0].startswith("#") and sorted(listed[1:]) == sorted(
        f"{language}/{key.rsplit('/', 1)[0]}/{key.rsplit('/', 1)[1]}" for key in keys for language in loading.LANGUAGES)
    print(f"PASS: {len(made)} loading screens in the retail format: new title, map with the route and "
          "description; the picture and hints untouched; Data/mm3_dlc_loading.ini lists them")


def main():
    loading = load("mm3_dlc_loading", ROOT / "tools" / "races" / "dlc_loading.py")
    data_tool = load("mm3_dlc_loading_data", ROOT / "tools" / "races" / "dlc_loading_data.py")
    check_data(loading, data_tool)
    check_screens(loading)


if __name__ == "__main__":
    main()
