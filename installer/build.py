"""Build MM3-Setup.exe: stage the payload from this checkout, then run PyInstaller.

    python installer\\build.py [--packages installer\\car-packages] [--downloads DIR]

Needs a Python with PyInstaller (pip install pyinstaller) and Pillow (the DLC
races' loading screens), a built game (build\\RelWithDebInfo\\mm3_recomp.exe)
and car packages from build_car_packages.py.
The result is one self-contained MM3-Setup.exe next to this script.
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

from build_car_packages import find_downloads, find_file, find_repo

HERE = Path(__file__).resolve().parent

# credits-file name -> an archive from that pack; the pack's readme is shipped with the cars
CREDITS = {
    "Panoz GTR-1": "!GTR-1.ar",
    "1995 McLaren F1 GTR": "!mcl_f1gtr.ar",
    "Ferrari F2007 F1": "!F2007.ar",
    "2017 Ford GT": "vpfgt17.ar",
    "McLaren Senna 2-Pack": "!mcl_senna.ar",
    "2015 Ferrari FXX K": "!FXX_K.ar",
    "2015 Koenigsegg Regera": "!Regera.ar",
    "Lamborghini Aventador LP700-4": "!lamboaven.ar",
}
CAR_MODS_FILES = ("prepare.py", "factory_specs.py", "factory_specs.json", "original_stats.py",
                  "native_overrides.py", "driving_defaults.py", "race_stability.py")
RACE_FILES = ("circuits.py", "circuits.json", "logic_circuit.lua", "dlc_icons.json",
              "dlc_loading.py", "dlc_loading.json")
# The DLC races' loading screens read the city maps (CDDS) and draw with Pillow.
TEXTURE_FILES = ("mm3_textures.py", "cdds_codebooks.json")
STDLIB_HIDDEN =("argparse", "collections", "copy", "hashlib", "math", "re", "struct", "tempfile", "zipfile", "zlib")
PILLOW_HIDDEN = ("PIL.Image", "PIL.ImageChops", "PIL.ImageDraw", "PIL.ImageFilter", "PIL.ImageFont",
                 "PIL.ImageOps", "PIL.DdsImagePlugin", "PIL.TgaImagePlugin", "PIL.PngImagePlugin")


def newest_game_exe(repo: Path) -> Path:
    """The same pick scripts/play.ps1 makes: the most recently built mm3_recomp.exe."""
    candidates = [repo / "build" / "RelWithDebInfo" / "mm3_recomp.exe",
                  repo / "build" / "Release" / "mm3_recomp.exe",
                  repo / "build" / "current" / "RelWithDebInfo" / "mm3_recomp.exe"]
    found = [p for p in candidates if p.is_file()]
    if not found:
        sys.exit("No built mm3_recomp.exe found; build the game first (scripts\\setup.ps1).")
    return max(found, key=lambda p: p.stat().st_mtime)


def make_icons(logo, branding: Path) -> None:
    """Setup's icon (the .exe, its window, the shortcuts, the Linux menu entry): the game's logo on the
    header's dark-blue tile with the gold edge.  Drawn at build time from the game's own artwork, so
    nothing of it is committed; branding/mm3.ico (all sizes) and branding/mm3.png (256 px)."""
    from PIL import Image, ImageDraw, ImageFilter
    size, radius = 256, 56
    top, bottom = (9, 16, 30), (31, 55, 102)
    tile = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(tile)
    for y in range(size):
        t = y / (size - 1)
        draw.line([(0, y), (size, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    draw.rectangle([0, size - 12, size, size], fill=(242, 183, 5, 255))        # the gold edge
    width = round(size * 0.88)
    mark = logo.resize((width, round(logo.height * width / logo.width)), Image.LANCZOS)
    left, upper = (size - width) // 2, (size - mark.height) // 2 - 6
    shadow = Image.new("RGBA", tile.size, (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 170), (left + 2, upper + 4), mark.getchannel("A"))
    tile = Image.alpha_composite(tile, shadow.filter(ImageFilter.GaussianBlur(4)))
    tile.alpha_composite(mark, (left, upper))
    rounded = Image.new("L", (size * 4, size * 4), 0)                           # smooth corners
    ImageDraw.Draw(rounded).rounded_rectangle([0, 0, size * 4 - 1, size * 4 - 1], radius * 4, fill=255)
    tile.putalpha(rounded.resize((size, size), Image.LANCZOS))
    tile.save(branding / "mm3.png")
    tile.save(branding / "mm3.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])


def stage_branding(payload: Path, repo: Path) -> None:
    """The window icon, and the game's own logo (Menu/Gfx/mm3_logo.cdds) from the maintainer's
    extracted game files. The logo is game artwork, so it is taken here and never committed; without
    game_files (or Pillow) Setup is built without it and shows plain text instead."""
    branding = payload / "branding"
    branding.mkdir(parents=True)
    shutil.copy2(repo / "src" / "mm3.ico", branding / "mm3.ico")
    archive = repo / "game_files" / "Data" / "Data_dvd.zip"
    try:
        sys.path.insert(0, str(repo / "tools" / "car-models"))
        import io
        import zipfile
        import mm3_textures
        from PIL import Image
        with zipfile.ZipFile(archive) as zipped:
            dds = mm3_textures.cdds_to_dds(zipped.read("Menu/Gfx/mm3_logo.cdds"))
        logo = Image.open(io.BytesIO(dds)).convert("RGBA")
        logo.save(branding / "mm3_logo.png")
        make_icons(logo, branding)
        print("game logo: Menu/Gfx/mm3_logo.cdds (and the icon made from it)")
    except (ImportError, OSError, KeyError, ValueError) as error:
        print(f"WARNING: no game logo in this Setup ({type(error).__name__}: {error})")
    finally:
        if sys.path and sys.path[0] == str(repo / "tools" / "car-models"):
            sys.path.pop(0)


def stage_payload(payload: Path, repo: Path, packages: Path, downloads: Path,
                  linux_binary: Path | None = None, linux_libs: Path | None = None) -> None:
    """Stage the payload.  linux_binary: the native Linux build (build_linux.py) instead of the .exe."""
    if payload.exists():
        shutil.rmtree(payload)
    exe = linux_binary or newest_game_exe(repo)
    print(f"game build: {exe}  sha256 {hashlib.sha256(exe.read_bytes()).hexdigest()[:16]}...")
    payload.mkdir(parents=True)
    if linux_binary:
        shutil.copy2(exe, payload / "mm3_recomp")
        if linux_libs and linux_libs.is_dir():
            shutil.copytree(linux_libs, payload / "lib")
    else:
        shutil.copy2(exe, payload / "mm3_recomp.exe")
        # The native build uses MSVC's shared C runtime. PyInstaller's private
        # runtime disappears when Setup closes, so the installed game needs its own
        # app-local copy even on PCs without the Visual C++ redistributable.
        runtime = Path(sys.base_prefix) / "vcruntime140.dll"
        if not runtime.is_file():
            sys.exit(f"Missing {runtime}; build with an official Windows x64 Python installation.")
        shutil.copy2(runtime, payload / runtime.name)

    stage_branding(payload, repo)
    (payload / "scripts").mkdir()
    shutil.copy2(repo / "scripts" / "extract_disc.py", payload / "scripts")
    xiso = payload / "tools" / "xboxrecomp" / "tools" / "xiso"
    xiso.mkdir(parents=True)
    # extract_disc.py imports xdvdfs and the CLI's _safe_component (__main__.py), a package.
    for name in ("__init__.py", "__main__.py", "xdvdfs.py"):
        shutil.copy2(repo / "tools" / "xboxrecomp" / "tools" / "xiso" / name, xiso)

    mods = payload / "tools" / "car-mods"
    mods.mkdir(parents=True)
    for name in CAR_MODS_FILES:
        shutil.copy2(repo / "tools" / "car-mods" / name, mods)
    # The circuit races, added by prepare.py with or without the extra cars.
    races = payload / "tools" / "races"
    races.mkdir(parents=True)
    for name in RACE_FILES:
        shutil.copy2(repo / "tools" / "races" / name, races)
    textures = payload / "tools" / "car-models"
    textures.mkdir(parents=True)
    for name in TEXTURE_FILES:
        shutil.copy2(repo / "tools" / "car-models" / name, textures)
    # cars.local.json is the maintainer's tuned setup; the tracked cars.json is the untuned preset
    # (and carries no model packages). Either way, model_package paths are ../../out/car-models/packages.
    manifest = repo / "tools" / "car-mods" / "cars.local.json"
    if not manifest.is_file():
        manifest = repo / "tools" / "car-mods" / "cars.imported.json"
    shutil.copy2(manifest, mods / "cars.local.json")
    print(f"car manifest: {manifest.name}")

    if not packages.is_dir() or not any(packages.iterdir()):
        sys.exit(f"No car packages in {packages}; run build_car_packages.py first.")
    shutil.copytree(packages, payload / "out" / "car-models" / "packages")

    credits = payload / "credits"
    credits.mkdir()
    for name, archive in CREDITS.items():
        folder = find_file(downloads, archive).parent
        readmes = sorted(p for p in folder.iterdir() if p.is_file() and p.suffix == ".txt" and "readme" in p.name.lower())
        if not readmes:
            sys.exit(f"No readme found next to {archive}")
        shutil.copy2(readmes[0], credits / f"{name}.txt")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, help="the midtown-madness-3-PC-Native checkout")
    parser.add_argument("--packages", type=Path, default=HERE / "car-packages")
    parser.add_argument("--downloads", type=Path, help="folder holding the downloaded car packs (for their readmes)")
    parser.add_argument("--work", type=Path, default=HERE / "build-work", help="staging + PyInstaller scratch")
    parser.add_argument("--keep-work", action="store_true")
    args = parser.parse_args()

    repo = args.repo or find_repo()
    repo = repo.resolve()
    args.work = args.work.resolve()
    if args.work in (repo, HERE) or args.work in repo.parents:
        parser.error("--work must be a dedicated scratch directory, not the checkout or an ancestor.")
    payload = args.work / "payload"
    stage_payload(payload, repo, args.packages, find_downloads(args.downloads))

    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile", "--windowed",
               "--name", "MM3-Setup", "--icon", str(payload / "branding" / "mm3.ico"),
               "--add-data", f"{payload};payload",
               "--distpath", str(args.work / "dist"), "--workpath", str(args.work / "pyi"),
               "--specpath", str(args.work)]
    for module in STDLIB_HIDDEN + PILLOW_HIDDEN:
        command += ["--hidden-import", module]
    command.append(str(HERE / "installer.py"))
    print("running PyInstaller...")
    subprocess.run(command, check=True)

    built = args.work / "dist" / "MM3-Setup.exe"
    final = HERE / "MM3-Setup.exe"
    shutil.copy2(built, final)
    print(f"built {final}  ({final.stat().st_size / 1e6:.0f} MB)")
    if not args.keep_work:
        shutil.rmtree(args.work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
