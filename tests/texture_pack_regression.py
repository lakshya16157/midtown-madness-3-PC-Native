"""HD HUD: Setup's generator (tools/hud/hd_hud.py) and the renderer's loader
(tools/xboxrecomp/src/video/nv2a_texture_pack.h) agree on names, hashes and levels."""
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    from PIL import Image, ImageDraw
    hd = module("hd_hud_test", ROOT / "tools/hud/hd_hud.py")
    textures = module("mm3_textures_test", ROOT / "tools/car-models/mm3_textures.py")
    assert hd.fnv64(b"") == 0xCBF29CE484222325 and hd.fnv64(b"a") == 0xAF63DC4C8601EC8C
    with tempfile.TemporaryDirectory(prefix="mm3-texture-pack-") as temporary:
        folder = Path(temporary)
        # A made-up 64x32 HUD picture (no game art), as the disc stores it: CDDS in Data_dvd.zip.
        picture = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
        ImageDraw.Draw(picture).ellipse((4, 4, 40, 28), fill=(255, 255, 255, 255), outline=(200, 0, 0, 255))
        dds = io.BytesIO()
        picture.save(dds, "DDS", pixel_format="DXT5")
        (folder / "game_files/Data").mkdir(parents=True)
        with zipfile.ZipFile(folder / "game_files/Data/Data_dvd.zip", "w") as zipped:
            zipped.writestr("Menu/Gfx/HUD/Test.cdds", textures.dds_to_cdds(dds.getvalue()))
            zipped.writestr("Menu/Gfx/HUD/Test_BAK.cdds", textures.dds_to_cdds(dds.getvalue()))
        lines = []
        out = folder / "hd_textures"
        assert hd.build(folder / "game_files", out, lines.append) == 1, lines
        assert hd.build(folder / "game_files", out, lines.append) == 1 and "already" in lines[-1]
        w, h, payload = hd.guest_bytes(textures.cdds_to_dds(textures.dds_to_cdds(dds.getvalue())))
        (folder / "guest.bin").write_bytes(payload)
        made = sorted(p.name for p in out.glob("*.dds"))
        assert made == [f"64x32_{hd.fnv64(payload):016x}.dds"], made
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(ROOT / "tools/xboxrecomp/src"), str(ROOT / "tests/texture_pack_contract.c"),
                        "/Fe:" + str(folder / "contract.exe")], check=True, cwd=folder, capture_output=True)
        environment = dict(os.environ, RECOMP_TEXTURE_PACK_DIR=str(out))
        # 4x = 256 wide with levels 256/128/64: scale 1 takes the guest size, 2.5 the 256 one
        # (128 is narrower than 160), 8 the largest there is.
        done = subprocess.run([str(folder / "contract.exe"), "64", "32", str(folder / "guest.bin"),
                               "64", "256", "256"], cwd=folder, env=environment, capture_output=True, text=True)
        assert done.returncode == 0, done.stdout + done.stderr
    print("PASS texture pack: Setup's HD HUD files load in the renderer at the level that fits the screen")


if __name__ == "__main__":
    main()
