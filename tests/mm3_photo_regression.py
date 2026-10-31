"""Photo mode: F2 holds the race (src/mm3_pause_clock.c), F3 and the controller's
A save PNG pictures (src/mm3_photo.c), key thread and free camera buttons
(src/mm3_cheat_keys.c, src/mm3_free_camera.c), against pretend keys and a
renderer stub. The pictures are decoded and compared pixel by pixel."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def run(executable, folder, *arguments, env=None):
    subprocess.run([str(executable), *arguments], check=True, cwd=folder, env={**os.environ, **(env or {})})


def check_pictures(pictures):
    files = sorted(pictures.glob("MM3-*.png"))
    assert len(files) == 3, files
    for path in files:
        assert len(path.stem) == len("MM3-20261030-181530-123"), path.name   # MM3-yyyymmdd-hhmmss-mmm
        with Image.open(path) as image:
            assert image.format == "PNG" and image.size == (64, 32), (image.format, image.size)
            rgb = image.convert("RGB")
            for y in range(32):
                for x in range(64):
                    blue, green, red = (x * 4) & 255, (y * 8) & 255, ((x + y) * 2) & 255
                    assert rgb.getpixel((x, y)) == (red, green, blue), (path.name, x, y, rgb.getpixel((x, y)))
    print("PASS pictures: three PNGs, 64x32, every pixel as handed over (BGRA in, alpha dropped, RGB out)")


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-photo-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/DXInputGetState=test_XInputGetState",
                        "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_photo_contract.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(executable),
                        "user32.lib", "shell32.lib", "ole32.lib", "windowscodecs.lib", "uuid.lib"],
                       check=True, cwd=folder)
        for name, args, env in (("on", (), {}),
                                ("off", ("off",), {"MM3_PHOTO_MODE": "0"}),
                                ("net", ("net",), {"RECOMP_NET": "nat"})):
            pictures = folder / ("pictures-" + name)
            pictures.mkdir()
            run(executable, folder, *args, env={"MM3_PHOTO_DIR": str(pictures), **env})
            if name == "on":
                check_pictures(pictures)
            else:
                assert not list(pictures.glob("MM3-*.png"))


if __name__ == "__main__":
    main()
