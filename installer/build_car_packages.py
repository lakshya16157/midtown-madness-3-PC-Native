"""Convert the downloaded community .ar cars into car-packages/ (needed once, before build.py).

    tools\\venv\\Scripts\\python.exe installer\\build_car_packages.py [--downloads DIR] [--out DIR]

Follows "Reproduce the nine imported exteriors" in tools/car-models/README.md and checks every
.ar against the SHA-256 listed there. Download the eight packs from the MM2X listings named in
that README into one folder (any sub-folder layout works; files are found by name). Looked up in
order: --downloads, installer/downloads, out/car-models/downloads, ../Downloaded_Cars.

The converter reads the original donor cars from your extracted game_files/Data, so run it with
the repo's Python (numpy + Pillow) after scripts\\setup.ps1 has extracted the game.
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent

# (car id, source folder, package alias, archive in the staged downloads, model basename)
RECIPES = [
    (9004, "panoz", "Panoz", "panoz/!GTR-1.ar", "pgtr1"),
    (9005, "mclaren-f1", "McLaren_F1", "mclaren-f1/!mcl_f1gtr.ar", "mcl_f1gtr"),
    (9006, "ferrari-f2007", "Form1", "ferrari-f2007/!F2007.ar", "f2007"),
    (9009, "ford-gt", "Ford_GTcar", "ford-gt/vpfgt17.ar", "vpfgt17"),
    (9010, "senna", "Senna_Road", "senna/!mcl_senna.ar", "mcl_senna"),
    (9011, "senna-gtr", "Senna_GTR1", "senna/!mcl_senna.ar", "mcl_sennagtr"),
    (9012, "fxx-k", "FXXK_Racer", "fxx-k/!FXX_K.ar", "fxxk"),
    (9013, "regera", "Regera_GT1", "regera/!Regera.ar", "regera"),
    (9014, "aventador", "AventadorX", "aventador/!lamboaven.ar", "lamboaven"),
]
# archive file name -> SHA-256 the converter README expects (and the staged location it uses)
ARCHIVES = {
    "!GTR-1.ar": ("panoz", "f547db98b55cd99815d8a5506a543c943addd6c82b74f91986087fbc55a9c072"),
    "!!GTR-1_Paintjobs.ar": ("panoz", "9680a6907f95a694a1149f25ed02bfbad1ce496c2db7440c0c4574f05074adb4"),
    "!mcl_f1gtr.ar": ("mclaren-f1", "6db5d57a770aa0b46fa71b8e65e20c8bd0e3e180248bb2aaa233809f521ea82a"),
    "!F2007.ar": ("ferrari-f2007", "c1f77d7e4988e88262158371cb9a053ce0971cd6c0d68b1d80e91e604cad346c"),
    "vpfgt17.ar": ("ford-gt", "ccb468d2eaf0bcf58563cc109c14b528fcfd1257a131aaafd7330d65377cb603"),
    "!mcl_senna.ar": ("senna", "3669ba132ed524a567fb331742614297e57d7f0972b7d1ae37df0ad112ee824d"),
    "!FXX_K.ar": ("fxx-k", "c19aa6bb2b125e6007e288c85faa9433a83a574ae2eb545911dc2418c1819315"),
    "!Regera.ar": ("regera", "808d1ae149cccbaf8e2597a1c273fee60192baceb1861fb71a9aafd0c107e8f0"),
    "!lamboaven.ar": ("aventador", "3dfe8f0a4230660ea25cf772c8eb5612ee79cf4319ddc6f0c7bf738c13bae764"),
}


def find_repo() -> Path:
    """The repo root: this folder's parent, or a sibling checkout named like the project."""
    for candidate in (HERE.parent, HERE.parent / "midtown-madness-3-PC-Native"):
        if (candidate / "scripts" / "extract_disc.py").is_file():
            return candidate
    sys.exit("Cannot find the midtown-madness-3-PC-Native checkout (scripts/extract_disc.py).")


def find_downloads(explicit: Path | None = None) -> Path:
    if explicit:
        return explicit
    for candidate in (HERE / "downloads", HERE.parent / "out" / "car-models" / "downloads",
                      HERE.parent.parent / "Downloaded_Cars"):
        if candidate.is_dir():
            return candidate
    sys.exit("Cannot find the downloaded car archives; pass --downloads <folder>.")


def find_file(folder: Path, name: str) -> Path:
    """A file called `name` anywhere below `folder` (download sites wrap archives in odd folders)."""
    for path in sorted(folder.rglob("*")):
        if path.is_file() and path.name == name:
            return path
    sys.exit(f"Missing download: {name} (looked below {folder})")


def run(repo: Path, *args):
    result = subprocess.run([sys.executable, *map(str, args)], cwd=repo, capture_output=True, text=True)
    if result.returncode:
        sys.exit(f"FAILED: {args[0]} {args[1]}\n{result.stdout[-1500:]}\n{result.stderr[-2500:]}")


def stage_downloads(downloads: Path, scratch: Path) -> None:
    """Copy each archive into the folder layout the recipe expects, after checking its hash."""
    for name, (folder, expected) in ARCHIVES.items():
        source = find_file(downloads, name)
        if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            sys.exit(f"{source}: not the file the converter was verified with (hash differs)")
        (scratch / "downloads" / folder).mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, scratch / "downloads" / folder / name)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--downloads", type=Path, help="folder holding the downloaded .ar files")
    parser.add_argument("--out", type=Path, default=HERE / "car-packages", help="where to write the packages")
    args = parser.parse_args()
    repo, downloads, out = find_repo(), find_downloads(args.downloads), args.out.resolve()

    with tempfile.TemporaryDirectory(prefix="mm3-cars-") as scratch:
        scratch = Path(scratch)
        stage_downloads(downloads, scratch)
        if out.exists():
            shutil.rmtree(out)
        for car_id, source_name, alias, archive, model in RECIPES:
            started = time.time()
            archive_path = scratch / "downloads" / archive
            source, preview, package = scratch / "source" / source_name, scratch / "previews" / source_name, out / alias
            import_args = ["tools/car-models/mm2_source.py", "import", archive_path, "--model", model,
                           "--paintjob", "0", "--lod", "H", "--out", source]
            if car_id == 9004:
                import_args += ["--overlay", scratch / "downloads" / "panoz" / "!!GTR-1_Paintjobs.ar"]
            run(repo, *import_args)
            run(repo, "tools/car-models/preview.py", source / "model.json", "--out", preview, "--name", alias,
                "--include", r"(?:BODY|BREAK\d+|FNDR[01]|WHL[0-3])_H", "--icons", "--icon-prefix", "Car")
            shutil.copy(preview / "Car_Menu.png", preview / "Menu.png")
            shutil.copy(preview / "Car_Small.png", preview / "Small.png")
            build_args = ["tools/car-models/build_package.py", source / "model.json", package, "--car-id", car_id,
                          "--icons", preview, "--max-texture-size", "1024"]
            if car_id == 9004:
                build_args += ["--y-offset", "-0.36"]
            if car_id == 9006:
                build_args += ["--average-minified-repeats"]
            run(repo, *build_args)
            print(f"{car_id} {alias}: done in {time.time() - started:.0f}s", flush=True)
    print(f"Packages written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
