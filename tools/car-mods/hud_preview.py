"""Optional previews of native and configured on-screen speedometer dials.

Pillow renders configured ranges. Original artwork additionally uses the
existing local ``tools/cache/decdds/decdds.dll`` (``.so`` on other platforms).
The decoder is Daniel Stien's existing
``decdds_extract`` API, also used by tools/mm3view. This module never installs,
downloads, compiles or writes anything; unavailable dependencies simply leave
the Workshop's text description available. Importing it needs only Python's
standard library.
"""
from __future__ import annotations

import ctypes
from functools import lru_cache
import io
import math
import os
from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[2]
DECODER_PATH = ROOT / "tools" / "cache" / "decdds" / ("decdds.dll" if os.name == "nt" else "decdds.so")
# Verified against the retail texture selector in sub_0023EEC8 and its assets.
_ARTWORK = ("Speedometer", "Speedometer_Sport", "Speedometer_Diesel",
            "Speedometer_Slow", "Speedometer_Koenig")
_NATIVE_MAXIMA = ((300, 150), (300, 200), (160, 100), (220, 180), (380, 280))
# Retail angle tables used by src/mm3_speedometer.c, in screen coordinates.
_ARCS = (((125, 415), (125, 415)), ((125, 415), (109, 417.5)),
         ((125, 415), (110, 405)), ((125, 415), (155, 385)),
         ((106, 434), (156, 384)))


def _valid_range(gauge_type, metric, maximum) -> bool:
    low, high = (100, 1200) if metric is True else (60, 750)
    return (type(gauge_type) is int and 0 <= gauge_type < len(_ARTWORK)
            and type(metric) is bool and type(maximum) in (int, float)
            and low <= maximum <= high and math.isfinite(maximum))


def dial_geometry(gauge_type: int, metric: bool, maximum: float,
                  speed_kmh: float = 0.0) -> dict | None:
    """Configured dial geometry in the runtime's 132-unit square, centered at 0.

    This pure helper needs no image libraries or assets. Speed is in km/h,
    matching the native draw entry point; labels use the selected unit. Tick
    labels round halves upward as the runtime does, rather than Python's
    round-to-even rule.
    """
    if not _valid_range(gauge_type, metric, maximum) or type(speed_kmh) not in (int, float):
        return None
    try:
        value = float(speed_kmh) * (1.0 if metric else 0.6213712096214294)
    except OverflowError:
        return None
    if not math.isfinite(value) or value < 0:
        value = 0.0
    value = min(value, maximum)
    start, end = _ARCS[gauge_type][0 if metric else 1]
    ticks, labels = [], []
    for index in range(17):
        angle = math.radians(start + (end - start) * index / 16)
        c, s = math.cos(angle), math.sin(angle)
        inner = 52 if index % 2 else 50
        ticks.append(((inner * c, inner * s), (55 * c, 55 * s), .8 if index % 2 else 1.2))
        if not index % 2:
            labels.append((math.floor(maximum * index / 16 + .5), (42 * c, 42 * s)))
    needle_angle = start + (end - start) * value / maximum
    c, s = math.cos(math.radians(needle_angle)), math.sin(math.radians(needle_angle))
    return {"start": start, "end": end, "maximum": maximum, "value": value,
            "ticks": tuple(ticks), "labels": tuple(labels), "needle_angle": needle_angle,
            "needle": ((-5 * c, -5 * s), (64 * c, 64 * s), 1.6)}


@lru_cache(maxsize=32)
def _custom_dial_png(gauge_type: int, metric: bool, maximum: float) -> bytes | None:
    """Rasterize the runtime's configured dial at zero speed, without assets."""
    try:
        from PIL import Image, ImageDraw

        geometry = dial_geometry(gauge_type, metric, maximum)
        if geometry is None:
            return None
        # Four samples per output pixel keep the small native vector digits
        # legible without changing the runtime's proportions or line widths.
        image = Image.new("RGBA", (640, 640), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        scale = 640 / 132

        def polygon(points, color):
            draw.polygon([((x + 66) * scale, (y + 66) * scale) for x, y in points], fill=color)

        def box(x, y, width, height, color):
            polygon(((x, y), (x + width, y), (x + width, y + height), (x, y + height)), color)

        def line(first, last, width, color):
            x1, y1 = first
            x2, y2 = last
            dx, dy = x2 - x1, y2 - y1
            length = math.hypot(dx, dy)
            if not length:
                return
            nx, ny = dy * width * .5 / length, -dx * width * .5 / length
            polygon(((x1 + nx, y1 + ny), (x2 + nx, y2 + ny),
                     (x2 - nx, y2 - ny), (x1 - nx, y1 - ny)), color)

        background, tick_color = (9, 11, 14, 255), (186, 204, 224, 255)
        digit_color, unit_color = (237, 242, 250, 255), (158, 176, 196, 255)
        for index in range(40):
            first, last = index * math.tau / 40, (index + 1) * math.tau / 40
            polygon(((0, 0), (56 * math.cos(first), 56 * math.sin(first)),
                     (56 * math.cos(last), 56 * math.sin(last))), background)
        for first, last, width in geometry["ticks"]:
            line(first, last, width, tick_color)

        masks = (0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F)
        segments = ((0, 0, 3, .8), (2.2, .5, .8, 3), (2.2, 3.5, .8, 3),
                    (0, 6.2, 3, .8), (0, 3.5, .8, 3), (0, .5, .8, 3), (0, 3.1, 3, .8))
        for value, (x, y) in geometry["labels"]:
            text = str(value)
            x -= (len(text) * 4 - 1) * .5
            y -= 3.5
            for index, character in enumerate(text):
                mask = masks[int(character)]
                for segment, (sx, sy, width, height) in enumerate(segments):
                    if mask & (1 << segment):
                        box(x + index * 4 + sx, y + sy, width, height, digit_color)

        x, y = (-10 if metric else -7), 15
        for character in ("KM/H" if metric else "MPH"):
            if character != "/":
                line((x, y), (x, y + 5), .7, unit_color)
            if character == "K":
                line((x, y + 2.5), (x + 3, y), .7, unit_color)
                line((x, y + 2.5), (x + 3, y + 5), .7, unit_color)
            elif character == "M":
                line((x, y), (x + 1.5, y + 2), .7, unit_color)
                line((x + 1.5, y + 2), (x + 3, y), .7, unit_color)
                line((x + 3, y), (x + 3, y + 5), .7, unit_color)
            elif character == "H":
                line((x + 3, y), (x + 3, y + 5), .7, unit_color)
                line((x, y + 2.5), (x + 3, y + 2.5), .7, unit_color)
            elif character == "P":
                line((x, y), (x + 3, y), .7, unit_color)
                line((x + 3, y), (x + 3, y + 2.5), .7, unit_color)
                line((x, y + 2.5), (x + 3, y + 2.5), .7, unit_color)
            else:
                line((x, y + 5), (x + 3, y), .7, unit_color)
            x += 5

        first, last, width = geometry["needle"]
        line(first, last, width, (255, 66, 26, 255))
        box(-2, -2, 4, 4, (222, 232, 245, 255))
        image = image.resize((160, 160), Image.Resampling.LANCZOS)
        stream = io.BytesIO()
        image.save(stream, format="PNG")
        return stream.getvalue()
    except (ImportError, OSError, ValueError, AttributeError):
        return None


def load_dial(game_dir: Path, gauge_type: int, metric: bool, maximum: float) -> bytes | None:
    """Return native artwork or a configured zero-speed dial as a 160px PNG.

    Exact native maxima retain the retail texture. Other valid maxima use the
    same vector geometry as the in-game replacement and need only Pillow,
    without an archive, native decoder, downloads or filesystem writes.
    """
    if (not _valid_range(gauge_type, metric, maximum)
            or not isinstance(game_dir, (str, os.PathLike))):
        return None
    if maximum == _NATIVE_MAXIMA[gauge_type][0 if metric else 1]:
        return load_native_dial(game_dir, gauge_type, metric)
    try:
        # Keep optional-dependency failures outside the image cache so a later
        # installation can enable previews without a negative cached result.
        from PIL import Image  # noqa: F401
    except ImportError:
        return None
    return _custom_dial_png(gauge_type, metric, float(maximum))


@lru_cache(maxsize=2)
def _decoder(path: str, stamp: tuple[int, int]):
    """Open an already installed decoder; keep its native library alive."""
    library = ctypes.CDLL(path)
    extract = library.decdds_extract
    extract.argtypes = [ctypes.c_char_p, ctypes.c_uint32,
                        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                        ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    extract.restype = ctypes.c_int
    # The existing Windows decoder uses the shared Universal C Runtime. Free
    # its malloc result through that runtime, rather than leaking each image.
    runtime = ctypes.CDLL("ucrtbase.dll") if os.name == "nt" else ctypes.CDLL(None)
    release = runtime.free
    release.argtypes = [ctypes.c_void_p]
    release.restype = None
    return library, runtime, extract, release


@lru_cache(maxsize=20)
def _load_cached(archive: str, archive_stamp: tuple[int, int], gauge_type: int,
                 metric: bool, decoder_path: str, decoder_stamp: tuple[int, int]) -> bytes | None:
    from PIL import Image

    suffix = "" if metric else "_mph"
    member = f"Menu/Gfx/HUD/{_ARTWORK[gauge_type]}{suffix}.cdds"
    try:
        with zipfile.ZipFile(archive) as source:
            info = source.getinfo(member)
            # These retail textures are small; avoid decoding an unrelated or
            # corrupted oversized member through the optional native library.
            if not 4 <= info.file_size <= 256 * 1024:
                return None
            compressed = source.read(info)
        if compressed[:4] != b"\xc8\x44\x0f\x99":
            return None
        _, _, extract, release = _decoder(decoder_path, decoder_stamp)
        output = ctypes.POINTER(ctypes.c_uint8)()
        length = ctypes.c_uint32()
        try:
            result = extract(compressed, len(compressed), ctypes.byref(output), ctypes.byref(length), 0)
            if result or not output or not 128 <= length.value <= 2 * 1024 * 1024:
                return None
            decoded = ctypes.string_at(output, length.value)
        finally:
            if output:
                release(output)
        with Image.open(io.BytesIO(decoded)) as texture:
            if texture.size != (256, 256):
                return None
            # The rest of the texture contains warning lights, fuel, gears and
            # other HUD parts. Keep only the original analog dial, with alpha.
            dial = texture.convert("RGBA").crop((0, 0, 145, 145))
            dial = dial.resize((160, 160), Image.Resampling.LANCZOS)
            png = io.BytesIO()
            dial.save(png, format="PNG")
            return png.getvalue()
    except (OSError, ValueError, KeyError, EOFError, AttributeError, RuntimeError,
            zipfile.BadZipFile, Image.DecompressionBombError):
        return None


def load_native_dial(game_dir: Path, gauge_type: int, metric: bool) -> bytes | None:
    """Return a 160x160 transparent PNG of the native dial, or ``None``.

    The PNG can be passed directly as ``data`` to Tk's ``PhotoImage``. It shows
    the fixed printed artwork without inventing a live needle or speed. Reads
    are cached in memory by archive version, gauge type and units; game assets
    and optional dependencies are never modified.
    """
    if type(gauge_type) is not int or not 0 <= gauge_type < len(_ARTWORK) or type(metric) is not bool:
        return None
    try:
        # Check optional dependencies before consulting the image cache, so a
        # missing decoder can become available without restarting Workshop.
        from PIL import Image  # noqa: F401

        archive = Path(game_dir) / "Data" / "Data_dvd.zip"
        archive_info, decoder_info = archive.stat(), DECODER_PATH.stat()
        return _load_cached(str(archive.absolute()), (archive_info.st_mtime_ns, archive_info.st_size),
                            gauge_type, metric, str(DECODER_PATH),
                            (decoder_info.st_mtime_ns, decoder_info.st_size))
    except (ImportError, OSError):
        return None
