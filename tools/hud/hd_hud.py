"""HD HUD: higher-resolution copies of the HUD textures, made from the player's own disc.

Setup runs this after installing (installer.py `make_hd_hud`). Game artwork is never shipped or
committed: everything here is derived on the player's machine from game_files/Data/Data_dvd.zip.

Each texture is upscaled 4x by `upscale`: premultiplied-alpha Lanczos, then iterative
back-projection (the upscaled image, scaled back down, must equal the original, which restores the
edges Lanczos softens), then crisp alpha edges where the original has a hard cut-out. The result is
saved as hd_textures/<w>x<h>_<fnv>.dds (BC3 with a mip chain, or BGRA8 when this Pillow cannot
write BC3), named after the guest texture's size and the FNV-1a 64 of its bytes, which is how the
renderer finds it (tools/xboxrecomp/src/video/nv2a_texture_pack.h).

Command line (for testing): python hd_hud.py <game_files> <out folder> [--only NAME]
"""
from __future__ import annotations

import importlib.util
import io
import struct
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
VERSION = 2                      # bump to rebuild existing installs' textures
# Not Menu/Font/: the font atlases keep the fill in blue and the outline in black pixels, and
# upscaling blends the two, so letters fill in (issue #42: i -> l, 0 -> 8, C -> G).
FOLDERS = {"Menu/Gfx/HUD/": 4}
MAX_SIZE = 4096                  # largest replacement edge
SKIP = ("_bak", "old.cdds")      # left-over artist copies the game never loads


def _textures_module():
    path = HERE.parent / "car-models" / "mm3_textures.py"
    spec = importlib.util.spec_from_file_location("mm3_textures_hd", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fnv64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def guest_bytes(dds: bytes) -> tuple[int, int, bytes] | None:
    """(width, height, the texture's bytes as the game loads them) for a DXT1/3/5 DDS."""
    if dds[:4] != b"DDS " or struct.unpack_from("<I", dds, 4)[0] != 124:
        return None
    height, width = struct.unpack_from("<II", dds, 12)
    levels = max(1, struct.unpack_from("<I", dds, 28)[0])
    fourcc = dds[84:88]
    if fourcc not in (b"DXT1", b"DXT3", b"DXT5"):
        return None
    block = 8 if fourcc == b"DXT1" else 16
    size, w, h = 0, width, height
    for _ in range(levels):
        size += max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block
        w, h = max(1, w // 2), max(1, h // 2)
    data = dds[128:128 + size]
    return (width, height, data) if len(data) == size else None


def _math(expression: str, **images):
    from PIL import ImageMath
    if hasattr(ImageMath, "lambda_eval"):           # Pillow 10.3+
        return ImageMath.unsafe_eval(expression, **images)
    return ImageMath.eval(expression, **images)


def _to_byte(channel):
    return _math("convert(min(max(a, 0), 255), 'L')", a=channel)


def upscale(image, factor: int, rounds: int = 4):
    """`image` (RGBA) made `factor` times larger: sharp where the original is sharp, smooth elsewhere."""
    from PIL import Image, ImageFilter
    image = image.convert("RGBA")
    w, h = image.size
    big = (w * factor, h * factor)
    r, g, b, a = (channel.convert("F") for channel in image.split())
    # Premultiplied colour: transparent pixels carry no colour into the edges.
    small = [_math("c * a / 255.0", c=c, a=a) for c in (r, g, b)] + [a]
    up = [channel.resize(big, Image.LANCZOS) for channel in small]
    # Back-projection: whatever the upscaled image loses when it is scaled back down is added again.
    for _ in range(rounds):
        for i, channel in enumerate(up):
            error = _math("o - d", o=small[i], d=channel.resize((w, h), Image.BOX))
            up[i] = _math("u + e", u=channel, e=error.resize(big, Image.BICUBIC))
    # Crisp cut-outs: where the original alpha jumps (an icon's edge), steepen the upscaled ramp so
    # the edge stays about one screen pixel wide instead of `factor` pixels.
    soft = up[3]
    original_alpha = image.getchannel("A")
    jump = _math("convert(mx - mn, 'L')", mx=original_alpha.filter(ImageFilter.MaxFilter(3)),
                 mn=original_alpha.filter(ImageFilter.MinFilter(3)))
    edge = jump.resize(big, Image.BILINEAR).convert("F")
    histogram = original_alpha.histogram()
    if sum(histogram[1:255]) < 0.02 * w * h:
        # One-bit alpha (DXT1 fonts): the staircase is the source's, not the shape's. Smooth it,
        # then cut at half: glyph outlines come out as clean curves.
        smooth = original_alpha.resize(big, Image.BILINEAR).filter(ImageFilter.GaussianBlur(factor * 0.55))
        alpha = _to_byte(_math("(a - 127.5) * k + 127.5", a=smooth.convert("F"), k=float(factor) * 0.9))
        # Colour bled outward from the shapes, so the new smooth rim has the shape's own colour.
        bled = image.convert("RGB").filter(ImageFilter.MaxFilter(5))
        bled.paste(image.convert("RGB"), mask=original_alpha.point(lambda v: 255 if v else 0))
        return Image.merge("RGBA", (*bled.resize(big, Image.BICUBIC).split(), alpha))
    else:
        steep = _math("(a - 127.5) * k + 127.5", a=soft, k=float(max(2, factor)) * 0.6)
        alpha = _to_byte(_math("a + (s - a) * min(max(e / 160.0 - 0.25, 0), 1)", a=soft, s=steep, e=edge))
    # Back to straight colour, against the alpha before steepening (smoother colour at the rim).
    # Ringing can push premultiplied colour below 0 or above its alpha: clamp, or rims turn dark.
    rgb = [_to_byte(_math("min(max(c, 0), max(a, 1.0)) * 255.0 / max(a, 1.0)", c=c, a=soft)) for c in up[:3]]
    return Image.merge("RGBA", (*rgb, alpha))


def _encode_level(image, bc3: bool) -> tuple[bytes, bytes]:
    """(header, data) of one DDS level."""
    out = io.BytesIO()
    if bc3:
        image.save(out, "DDS", pixel_format="DXT5")
    else:
        image.save(out, "DDS")
    raw = out.getvalue()
    return raw[:128], raw[128:]


def can_write_bc3() -> bool:
    try:
        from PIL import Image
        # Older Pillow ignores pixel_format and writes plain RGBA: check what it wrote.
        return _encode_level(Image.new("RGBA", (4, 4)), True)[0][84:88] == b"DXT5"
    except Exception:
        return False


def write_dds(path: Path, image, guest_size: tuple[int, int], bc3: bool) -> None:
    """`image` and its halves down to the guest size, as one DDS with a mip chain."""
    from PIL import Image
    levels, data, header = [], [], None
    level = image
    while True:
        levels.append(level)
        w, h = level.size
        if w // 2 < guest_size[0] or h // 2 < guest_size[1] or (bc3 and ((w // 2) % 4 or (h // 2) % 4)):
            break
        level = level.resize((w // 2, h // 2), Image.BOX)
    for i, level in enumerate(levels):
        head, body = _encode_level(level, bc3)
        if header is None:
            header = bytearray(head)
        data.append(body)
    if not bc3 and struct.unpack_from("<I", header, 92)[0] != 0x00FF0000:
        raise ValueError("this Pillow writes RGBA DDS in an unexpected channel order")
    struct.pack_into("<I", header, 8, struct.unpack_from("<I", header, 8)[0] | 0x20000)   # DDSD_MIPMAPCOUNT
    struct.pack_into("<I", header, 28, len(levels))
    if len(levels) > 1:
        struct.pack_into("<I", header, 108, struct.unpack_from("<I", header, 108)[0] | 0x400008)
    path.write_bytes(bytes(header) + b"".join(data))


def build(game_files: Path, out: Path, log=print, only: str | None = None) -> int:
    """Write the HD textures into `out`; returns how many. Skips the work when `out` is current."""
    stamp = out / "version.txt"
    dvd = Path(game_files) / "Data" / "Data_dvd.zip"
    signature = f"{VERSION} {dvd.stat().st_size} {int(dvd.stat().st_mtime)}" if dvd.is_file() else ""
    if not signature:
        log("HD HUD: no Data_dvd.zip, skipped.")
        return 0
    if not only and stamp.is_file() and stamp.read_text().strip() == signature:
        count = len(list(out.glob("*.dds")))
        log(f"HD HUD: {count} textures already made.")
        return count
    from PIL import Image
    textures = _textures_module()
    bc3 = can_write_bc3()
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.dds"):
        old.unlink()
    count = 0
    with zipfile.ZipFile(dvd) as zipped:
        for name in sorted(zipped.namelist()):
            folder = next((f for f in FOLDERS if name.startswith(f)), None)
            lower = name.lower()
            if not folder or not lower.endswith(".cdds") or any(s in lower for s in SKIP):
                continue
            if only and only.lower() not in lower:
                continue
            try:
                dds = textures.cdds_to_dds(zipped.read(name))
                guest = guest_bytes(dds)
                if not guest:
                    continue
                w, h, payload = guest
                factor = FOLDERS[folder]
                # Uncompressed is 4x the size on disk: at most 2x and 1024 px keeps it near 60 MB.
                limit = MAX_SIZE if bc3 else 1024
                if not bc3:
                    factor = min(factor, 2)
                while factor > 1 and max(w, h) * factor > limit:
                    factor //= 2
                if factor < 2:
                    continue
                image = Image.open(io.BytesIO(dds)).convert("RGBA")
                write_dds(out / f"{w}x{h}_{fnv64(payload):016x}.dds", upscale(image, factor), (w, h), bc3)
                count += 1
            except Exception as error:       # one odd texture must not stop the rest
                log(f"HD HUD: {name} skipped ({error}).")
    if not only:
        stamp.write_text(signature + "\n")
    log(f"HD HUD: made {count} higher-resolution HUD textures"
        + ("." if bc3 else " (uncompressed: this Pillow cannot write BC3)."))
    return count


if __name__ == "__main__":
    args = sys.argv[1:]
    only = None
    if "--only" in args:
        index = args.index("--only")
        only = args[index + 1]
        del args[index:index + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    build(Path(args[0]), Path(args[1]), only=only)
