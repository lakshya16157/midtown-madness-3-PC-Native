"""Build MM3-Setup.exe: stage the payload from this checkout, then run PyInstaller.

    python installer\\build.py [--packages installer\\car-packages] [--downloads DIR]

Needs a Python with PyInstaller (pip install pyinstaller), a built game
(build\\RelWithDebInfo\\mm3_recomp.exe) and car packages from build_car_packages.py.
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
                  "native_overrides.py", "driving_defaults.py", "race_stability.py", "voice_fixes.py")
RACE_FILES = ("circuits.py", "circuits.json", "logic_circuit.lua", "dlc_icons.json")
STDLIB_HIDDEN =("argparse", "collections", "copy", "hashlib", "math", "re", "struct", "tempfile", "zipfile", "zlib")


def newest_game_exe(repo: Path) -> Path:
    """The same pick scripts/play.ps1 makes: the most recently built mm3_recomp.exe."""
    candidates = [repo / "build" / "RelWithDebInfo" / "mm3_recomp.exe",
                  repo / "build" / "Release" / "mm3_recomp.exe",
                  repo / "build" / "current" / "RelWithDebInfo" / "mm3_recomp.exe"]
    found = [p for p in candidates if p.is_file()]
    if not found:
        sys.exit("No built mm3_recomp.exe found; build the game first (scripts\\setup.ps1).")
    return max(found, key=lambda p: p.stat().st_mtime)


def stage_payload(payload: Path, repo: Path, packages: Path, downloads: Path) -> None:
    if payload.exists():
        shutil.rmtree(payload)
    exe = newest_game_exe(repo)
    print(f"game build: {exe}  sha256 {hashlib.sha256(exe.read_bytes()).hexdigest()[:16]}...")
    payload.mkdir(parents=True)
    shutil.copy2(exe, payload / "mm3_recomp.exe")
    # The native build uses MSVC's shared C runtime. PyInstaller's private
    # runtime disappears when Setup closes, so the installed game needs its own
    # app-local copy even on PCs without the Visual C++ redistributable.
    runtime = Path(sys.base_prefix) / "vcruntime140.dll"
    if not runtime.is_file():
        sys.exit(f"Missing {runtime}; build with an official Windows x64 Python installation.")
    shutil.copy2(runtime, payload / runtime.name)

    (payload / "scripts").mkdir()
    shutil.copy2(repo / "scripts" / "extract_disc.py", payload / "scripts")
    xiso = payload / "tools" / "xboxrecomp" / "tools" / "xiso"
    xiso.mkdir(parents=True)
    shutil.copy2(repo / "tools" / "xboxrecomp" / "tools" / "xiso" / "xdvdfs.py", xiso)

    mods = payload / "tools" / "car-mods"
    mods.mkdir(parents=True)
    for name in CAR_MODS_FILES:
        shutil.copy2(repo / "tools" / "car-mods" / name, mods)
    # The circuit races, added by prepare.py with or without the extra cars.
    races = payload / "tools" / "races"
    races.mkdir(parents=True)
    for name in RACE_FILES:
        shutil.copy2(repo / "tools" / "races" / name, races)
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
               "--name", "MM3-Setup", "--icon", str(repo / "src" / "mm3.ico"),
               "--add-data", f"{payload};payload",
               "--distpath", str(args.work / "dist"), "--workpath", str(args.work / "pyi"),
               "--specpath", str(args.work)]
    for module in STDLIB_HIDDEN:
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
