"""The round minimap's compass pins hide together (src/mm3_map_pins.c): just
before the ring is drawn, the retail MapCircle data gets its pin-free north
band over its west, east and south pins. Uses the retail texture from
game_files/Data/Data_dvd.zip and the local decdds decoder (tools/cache/decdds),
as tools/mm3view does."""
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/mm3view"))
import mm3view  # noqa: E402
from PIL import Image  # noqa: E402

HIDDEN_CRC = 0x36FCD944


def image(header, data):
    return Image.open(io.BytesIO(header + data)).convert("RGBA")


def pins(picture):
    data = picture.tobytes()
    return sum(1 for i in range(0, len(data), 4) if data[i + 3] > 40 and data[i] > 140 and data[i + 1] < 90)


def side_alpha(picture):
    alpha = picture.getchannel("A")
    boxes = {"north": (56, 0, 72, 12), "south": (56, 116, 72, 128), "west": (0, 56, 12, 72), "east": (116, 56, 128, 72)}
    return {side: sum(1 for v in alpha.crop(box).tobytes() if v > 100) for side, box in boxes.items()}


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    archive = zipfile.ZipFile(ROOT / "game_files/Data/Data_dvd.zip")
    dds = mm3view.cdds_to_dds(archive.read("Menu/Gfx/HUD/MapCircle.cdds"))
    header, ring = dds[:128], dds[128:128 + 16384]
    assert zlib.crc32(ring) == 0x6E109683, "not the retail MapCircle"
    before = pins(image(header, ring))
    assert before > 40, before
    with tempfile.TemporaryDirectory(prefix="mm3-map-pins-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        exe = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_map_pins_contract.c"), str(ROOT / "src/mm3_map_pins.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(exe)], check=True, cwd=folder,
                       stdout=subprocess.DEVNULL)

        def run(mode):
            (folder / "ring.bin").write_bytes(ring)
            subprocess.run([str(exe), mode], check=True, cwd=folder, stderr=subprocess.DEVNULL)
            return (folder / "out.bin").read_bytes()

        hidden = run("hide")
        picture = image(header, hidden)
        sides = side_alpha(picture)
        assert pins(picture) == 0, f"{pins(picture)} pin texels left"
        assert zlib.crc32(hidden) == HIDDEN_CRC, "%08X" % zlib.crc32(hidden)
        assert min(sides.values()) >= sides["north"] * 0.9, sides     # the ring stays whole
        assert hidden[:16 * 32 * 12] == ring[:16 * 32 * 12]            # the north band itself is untouched
        assert run("twice") == hidden                                  # once only, every later draw keeps it
        assert run("show") == ring                                     # pins shown: the retail ring
        assert run("empty") == bytes(16384)                            # pixels not streamed in yet: wait
    print(f"PASS map pins: {before} pin texels cleared from the retail ring just before its draw, ring whole "
          f"on every side {sides}; once only, untouched when shown or not yet loaded")


if __name__ == "__main__":
    main()
