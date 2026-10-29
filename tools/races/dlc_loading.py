"""Loading screens with a route map for the downloadable races.

Every retail race has a loading screen (LoadingScreens/<Language>/<City>/
<Blitz|Checkpoint>/LoadingNN.TGA): its name, a map with its route, a
description and two hints over a picture. The DLC's Blitz and Checkpoint
races 11-20 (files 10-19) brought none, so the game showed the car's picture
(LoadingScreens/Default_<car>.tga) instead.

This makes them the way the retail ones look, from the player's own files
when the installer prepares the game (circuits.race_entries): race NN takes
the retail screen of race NN-10 in its city, mode and language (picture,
frame and hints), with its own name and description (src/mm3_dlc_races.c's)
and its own map: the city map texture (Menu/Gfx/Maps/<n>.cdds) recoloured
like the retail maps, with the route along the streets from the start
through each checkpoint in turn. The screens go into Data_dvd.zip as
LoadingNN_mm3.tga, listed in Data/mm3_dlc_loading.ini for
src/mm3_dlc_races.c. They need Pillow and the Windows fonts Arial and
Tahoma; without them the races keep the car's picture.

dlc_loading.json holds what they are drawn from (dlc_loading_data.py).
    python tools/races/dlc_loading.py preview OUT_DIR [--language English] [--race Paris/Blitz/10]
"""
import argparse
import base64
import importlib.util
import io
import json
import struct
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA_FILE = HERE / "dlc_loading.json"
LANGUAGES = ("English", "French", "German", "Italian", "Spanish")   # the game's order (0x3C5648)
# World (x, z) to the city map texture as stored (upside down): u = a*x + b,
# v = c*z + d, fitted to the street graph (tools/races/data). The texture
# is stretched; the retail maps show it at one scale both ways.
CITY_MAPS = {"Paris": ("Menu/Gfx/Maps/0.cdds", 0.232, 487.71, 0.343, 400.33),
             "Washington": ("Menu/Gfx/Maps/1.cdds", 0.1915, 656.89, 0.216, 723.44)}
MAP_PAD = 512                     # room around the texture: a map may reach past the city
PANEL = (53, 67, 330, 251)        # inside the retail map's white frame: x, y, width, height
ZOOM = (0.07, 0.26)               # px per metre; the retail maps use 0.077-0.261
ROUTE_MARGIN = 22                 # px from the route to the frame, as retail
SUPERSAMPLE = 4
LINE = (4.2, 2.8, 0.9)            # route: outline, core and highlight widths (px)
BALLS = {"start": (12.0, (70, 186, 38)), "finish": (12.0, (214, 22, 18)), "checkpoint": (9.5, (214, 22, 18))}
TITLE_AREA = (44, 24, 636, 65)    # where retail prints the race's name (up to its end) ...
TITLE_GAP = 40                    # px without a letter: the name has ended
TITLE_AT = (52, 60, 352)          # ... from x 52 on a baseline at y 60, at most 352 px wide
TITLE_FONT = ("arialbi.ttf", 28.5)  # Arial Bold Italic: 21 px capitals, as retail
TEXT_AREA = (56, 334, 381, 426)   # inside the description box
TEXT_AT = (65, 348, 17, 308)      # left, first baseline, line pitch, width
TEXT_FONT = ("tahomabd.ttf", 15.5)


def load_data():
    return json.loads(DATA_FILE.read_text(encoding="utf-8")) if DATA_FILE.is_file() else {}


def _module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _pillow():
    try:
        from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps
    except ImportError:
        return None
    return Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps


def view(points):
    """(scale, x0, z0): panel px (px, py) shows world (x0 + px/scale, z0 - py/scale),
    the whole route centred with ROUTE_MARGIN to spare where the zoom allows."""
    xs, zs = [p[0] for p in points], [p[1] for p in points]
    room_x, room_z = PANEL[2] - 2 * ROUTE_MARGIN, PANEL[3] - 2 * ROUTE_MARGIN
    scale = min(room_x / max(max(xs) - min(xs), 1.0), room_z / max(max(zs) - min(zs), 1.0))
    scale = min(max(scale, ZOOM[0]), ZOOM[1])
    centre_x, centre_z = (max(xs) + min(xs)) / 2, (max(zs) + min(zs)) / 2
    return scale, centre_x - PANEL[2] / 2 / scale, centre_z + PANEL[3] / 2 / scale


def tga(image):
    """640x480 24-bit, top row first, with the retail loading screens' header
    (which also puts 24 in the unused colour map's entry size)."""
    return struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 24, 0, 0, image.width, image.height, 24, 0x20) + \
        image.tobytes("raw", "BGR")


class Painter:
    def __init__(self, pil, data):
        self.Image, self.ImageChops, self.ImageDraw, self.ImageFilter, self.ImageFont, self.ImageOps = pil
        colours = data["colours"]
        table = base64.b64decode(colours["table"])
        size = colours["size"]
        self.colours = self.ImageFilter.Color3DLUT(
            size, [tuple(table[i + c] / 255.0 for c in range(3)) for i in range(0, len(table), 3)])
        self.title_font = self.ImageFont.truetype(*TITLE_FONT)
        self.text_font = self.ImageFont.truetype(*TEXT_FONT)
        self.balls = {kind: self._ball(*spec) for kind, spec in BALLS.items()}
        self.maps = {}

    # --- the map ----------------------------------------------------------

    def city_map(self, dvd, city):
        if city not in self.maps:
            textures = _module("mm3_textures", HERE.parent / "car-models" / "mm3_textures.py")
            dds = textures.cdds_to_dds(dvd.read(CITY_MAPS[city][0]))
            image = self.Image.open(io.BytesIO(dds)).convert("RGB")
            image = image.transpose(self.Image.Transpose.FLIP_TOP_BOTTOM)
            self.maps[city] = (self.ImageOps.expand(image, MAP_PAD, fill=(0, 0, 0)), image.height)
        return self.maps[city]

    def map_panel(self, dvd, city, place):
        texture, height = self.city_map(dvd, city)
        scale, x0, z0 = place
        _, a, b, c, d = CITY_MAPS[city]
        u0 = a * x0 + b + 0.5 + MAP_PAD
        v0 = height - 0.5 - (c * z0 + d) + MAP_PAD
        box = (u0, v0, u0 + a * PANEL[2] / scale, v0 + c * PANEL[3] / scale)
        return texture.resize(PANEL[2:], self.Image.Resampling.LANCZOS, box=box).filter(self.colours)

    def _ball(self, diameter, colour):
        """A lit red or green ball, as retail marks the start, checkpoints and finish."""
        n = round(diameter * SUPERSAMPLE)
        image = self.Image.new("RGBA", (n, n), (0, 0, 0, 0))
        pixels = image.load()
        light = (-0.5, -0.6, 0.62)
        half = (light[0], light[1], light[2] + 1.0)
        length = sum(v * v for v in half) ** 0.5
        half = tuple(v / length for v in half)
        for j in range(n):
            for i in range(n):
                x, y = (i + 0.5) / n * 2 - 1, (j + 0.5) / n * 2 - 1
                if x * x + y * y >= 1.0:
                    continue
                z = (1.0 - x * x - y * y) ** 0.5
                diffuse = max(0.0, x * light[0] + y * light[1] + z * light[2])
                shine = max(0.0, x * half[0] + y * half[1] + z * half[2]) ** 30
                pixels[i, j] = tuple(min(255, round(v * (0.28 + 0.9 * diffuse) + 230 * shine)) for v in colour) + (255,)
        return image

    def route_layer(self, race, place):
        """The route, its balls and their shadow, over a clear panel-sized layer."""
        Image, ImageDraw, ImageFilter = self.Image, self.ImageDraw, self.ImageFilter
        scale, x0, z0 = place
        k = SUPERSAMPLE
        size = (PANEL[2] * k, PANEL[3] * k)

        def at(p):
            return ((p[0] - x0) * scale * k, (z0 - p[1]) * scale * k)

        line = [at(p) for p in race["route"]]
        marks = [("start", at(race["start"]))]
        marks += [("checkpoint", at(p)) for p in race["checkpoints"][:-1]]
        marks.append(("finish", at(race["checkpoints"][-1])))
        shadow = Image.new("L", size, 0)
        draw = ImageDraw.Draw(shadow)
        drop = 1.6 * k
        draw.line([(x + drop, y + drop) for x, y in line], fill=170, width=round(LINE[0] * k), joint="curve")
        for kind, (x, y) in marks:
            r = BALLS[kind][0] * k / 2
            draw.ellipse((x + drop - r, y + drop - r, x + drop + r, y + drop + r), fill=170)
        layer = Image.new("RGBA", size, (0, 0, 0, 0))
        layer.putalpha(shadow.filter(ImageFilter.GaussianBlur(1.2 * k)))
        draw = ImageDraw.Draw(layer)
        draw.line(line, fill=(120, 8, 8, 255), width=round(LINE[0] * k), joint="curve")
        draw.line(line, fill=(228, 34, 30, 255), width=round(LINE[1] * k), joint="curve")
        draw.line([(x - 0.5 * k, y - 0.5 * k) for x, y in line], fill=(255, 120, 110, 255),
                  width=round(LINE[2] * k), joint="curve")
        for kind, (x, y) in marks:
            ball = self.balls[kind]
            layer.alpha_composite(ball, (round(x - ball.width / 2), round(y - ball.height / 2)))
        return layer.convert("RGBa").resize(PANEL[2:], Image.Resampling.LANCZOS).convert("RGBA")

    # --- text -------------------------------------------------------------

    def _fill(self, region, mask):
        """Paint over mask's pixels from the picture around them: each row
        between its nearest kept pixels, then smoothed."""
        width, height = region.size
        source, holes = region.load(), mask.load()
        out = region.copy()
        pixels = out.load()
        for y in range(height):
            x = 0
            while x < width:
                if not holes[x, y]:
                    x += 1
                    continue
                start = x
                while x < width and holes[x, y]:
                    x += 1
                left = source[start - 1, y] if start else None
                right = source[x, y] if x < width else None
                left, right = left or right, right or left
                if left is None:
                    continue
                for i in range(start, x):
                    t = (i - start + 1) / (x - start + 1)
                    pixels[i, y] = tuple(round(l + (r - l) * t) for l, r in zip(left, right))
        for radius in (4, 3, 2, 1, 1, 1):
            for _ in range(3):
                out = self.Image.composite(out.filter(self.ImageFilter.BoxBlur(radius)), region, mask)
        return out

    def _erase(self, screen, area, threshold, shadow, gap=None):
        """Take the retail text off: its bright glyphs (and the shadow
        `shadow` px below right of them), repainted from around them. With
        `gap`, the text ends where no glyph follows for that many columns."""
        glyphs = screen.crop(area).convert("L").point(lambda v: 255 if v >= threshold else 0)
        if gap:
            columns = [glyphs.crop((x, 0, x + 1, glyphs.height)).getbbox() is not None for x in range(glyphs.width)]
            end = last = next((x for x, lit in enumerate(columns) if lit), None)
            if end is None:
                return
            for x in range(last, glyphs.width):
                if columns[x]:
                    end = x
                elif x - end > gap:
                    break
            area = (area[0], area[1], min(area[2], area[0] + end + 8), area[3])
            glyphs = glyphs.crop((0, 0, area[2] - area[0], glyphs.height))
        region = screen.crop(area)
        mask = glyphs.filter(self.ImageFilter.MaxFilter(5))
        if shadow:
            below = self.Image.new("L", mask.size, 0)
            below.paste(mask.filter(self.ImageFilter.MaxFilter(3)), (shadow, shadow))
            mask = self.ImageChops.lighter(mask, below)
        if mask.getbbox():
            screen.paste(self._fill(region, mask), area[:2])

    def _text(self, screen, area, lines, font, left, baseline, pitch, drop, blur):
        """White lines with a soft dark shadow, kept inside `area`."""
        Image, ImageDraw, ImageFilter = self.Image, self.ImageDraw, self.ImageFilter
        glyphs = Image.new("L", screen.size, 0)
        draw = ImageDraw.Draw(glyphs)
        for n, line in enumerate(lines):
            draw.text((left, baseline + n * pitch), line, font=font, fill=255, anchor="ls")
        shade = Image.new("L", screen.size, 0)
        shade.paste(glyphs, (drop, drop))
        shade = shade.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: min(255, v * 2))
        inside = Image.new("L", screen.size, 0)
        inside.paste(255, area)
        screen.paste((4, 8, 22), (0, 0), self.ImageChops.multiply(shade, inside))
        screen.paste((248, 248, 250), (0, 0), self.ImageChops.multiply(glyphs, inside))

    def title(self, screen, name):
        left, baseline, width = TITLE_AT
        font = self.title_font
        text = name.upper()
        while font.getlength(text) > width and font.size > 16:
            font = font.font_variant(size=font.size - 1)
        bearing = font.getbbox(text, anchor="ls")[0]
        self._text(screen, TITLE_AREA, [text], font, left - bearing, baseline, 0, 3, 1.4)

    def description(self, screen, about):
        left, baseline, pitch, width = TEXT_AT
        lines = []
        for word in about.split():
            if lines and self.text_font.getlength(lines[-1] + " " + word) <= width:
                lines[-1] += " " + word
            else:
                lines.append(word)
        rows = (TEXT_AREA[3] - baseline) // pitch + 1
        if len(lines) > rows:
            raise ValueError(f"Description longer than the box: {about!r}")
        self._text(screen, TEXT_AREA, lines, self.text_font, left, baseline, pitch, 1, 0.8)

    # --- the screen -------------------------------------------------------

    def screen(self, template, race, language, panel):
        screen = self.Image.open(io.BytesIO(template)).convert("RGB")
        if screen.size != (640, 480):
            raise ValueError("Retail loading screens are 640x480")
        self._erase(screen, TITLE_AREA, 150, 3, TITLE_GAP)
        self._erase(screen, TEXT_AREA, 110, 0)
        # Keep the frame's rounded corners.
        corners = self.Image.new("L", PANEL[2:], 255)
        for x, y in ((0, 0), (PANEL[2] - 1, 0), (0, PANEL[3] - 1), (PANEL[2] - 1, PANEL[3] - 1)):
            for dx, dy in ((0, 0), (1, 0), (0, 1)):
                corners.putpixel((abs(x - dx), abs(y - dy)), 0)
        screen.paste(panel, PANEL[:2], corners)
        self.title(screen, race["name"])
        self.description(screen, race["about"][language])
        return screen


def screens(dvd, log=print, races=None):
    """{"LoadingScreens/<Language>/<City>/<Mode>/LoadingNN_mm3.tga": TGA} for
    every downloadable race (or those in `races`) and language with a retail
    screen to start from; {} without Pillow or the fonts."""
    data = load_data()
    pil = _pillow()
    if not data.get("races") or pil is None:
        if pil is None:
            log("DLC loading screens: Pillow is missing; the downloadable races keep the car picture.")
        return {}
    try:
        painter = Painter(pil, data)
    except OSError as error:
        log(f"DLC loading screens: {error}; the downloadable races keep the car picture.")
        return {}
    names = {name.lower(): name for name in dvd.namelist()}
    out = {}
    for key, race in sorted(data["races"].items()):
        if races and key not in races:
            continue
        city, mode, number = key.split("/")
        place = view(race["route"])
        panel = painter.map_panel(dvd, city, place)
        route = painter.route_layer(race, place)
        panel.paste(route, (0, 0), route)
        for language, language_name in enumerate(LANGUAGES):
            template = names.get(f"loadingscreens/{language_name}/{city}/{mode}/loading{int(number) - 10:02d}.tga".lower())
            if template is None:
                continue
            image = painter.screen(dvd.read(template), race, language, panel)
            out[f"LoadingScreens/{language_name}/{city}/{mode}/Loading{number}_mm3.tga"] = tga(image)
    return out


def ini(entries):
    """Data/mm3_dlc_loading.ini: the downloadable races' loading screens made."""
    lines = ["# MM3 downloadable race loading screens (<Language>/<City>/<Mode>/<NN>: LoadingNN_mm3.tga)"]
    for name in sorted(entries):
        parts = name.split("/")
        if len(parts) == 5 and parts[0] == "LoadingScreens" and parts[4].endswith("_mm3.tga"):
            lines.append("/".join(parts[1:4] + [parts[4][len("Loading"):-len("_mm3.tga")]]))
    return ("\n".join(lines) + "\n").encode("ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    preview = sub.add_parser("preview", help="write PNG previews from game_files")
    preview.add_argument("out", type=Path)
    preview.add_argument("--language", default="English", choices=LANGUAGES)
    preview.add_argument("--race", action="append", help="<City>/<Mode>/<NN> (default: all)")
    preview.add_argument("--dvd", type=Path, default=HERE.parents[1] / "game_files" / "Data" / "Data_dvd.zip")
    args = parser.parse_args()
    with zipfile.ZipFile(args.dvd) as dvd:
        made = screens(dvd, races=args.race)
    args.out.mkdir(parents=True, exist_ok=True)
    from PIL import Image
    for name, blob in made.items():
        if f"/{args.language}/" in name:
            image = Image.open(io.BytesIO(blob))
            image.save(args.out / (name.split("/", 2)[2].replace("/", "_").replace(".tga", ".png")))
    print(f"{len(made)} screens made; {args.language} previews in {args.out}")


if __name__ == "__main__":
    main()
