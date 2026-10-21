"""Recreate missing prepared model packages from the current Car Workshop install.

Workshop reapplies every imported exterior from out/car-models/packages; when
those folders are lost, any Workshop change fails. A package only overrides
visual files the Workshop would otherwise clone from the donor, so compare the
installed files with a donor-only plan: every differing car visual came from the
package. Entries keep their installed archive order. The result is accepted
only when the Workshop's plan with these packages reproduces the installed
content exactly. Existing package folders are never overwritten.
"""
from __future__ import annotations

import argparse
import copy
import io
import json
from pathlib import Path
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/car-mods"))
import prepare  # noqa: E402


def visual(alias: str, archive: str, name: str) -> bool:
    """The destinations prepare.load_model_package accepts for this car."""
    alias = re.escape(alias)
    if archive == "loose":
        return bool(re.fullmatch(r"Data/Shared/Cars/" + alias + r"(?:_lod[1-9][0-9]*)?/[A-Za-z0-9_-]+\.cmp", name)
                    or re.fullmatch(r"Data/Shared/Cars/" + alias + r"/bg\.cdds", name))
    return bool(re.fullmatch(r"Cars/" + alias + r"(?:_lod[1-9][0-9]*)?/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+\.(omb|cdds)", name)
                or re.fullmatch(r"Menu/Gfx/Vehicles/" + alias + r"(?:_Small)?\.cdds", name)
                or re.fullmatch(r"LoadingScreens/Default_" + alias + r"\.tga", name))


def entries(blob: bytes) -> dict[str, bytes]:
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        return {info.filename: archive.read(info) for info in archive.infolist()}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=ROOT / "game_files")
    parser.add_argument("--config", type=Path, default=prepare.default_config_path())
    args = parser.parse_args(argv)
    game_dir, config_dir = args.game_dir.resolve(), args.config.resolve().parent
    state, originals = prepare.read_install(game_dir)
    if not state:
        raise SystemExit("No Car Workshop installation to recover packages from")
    config = prepare.load_config(args.config)
    cars = [car for car in config["cars"] if car.get("model_package", "").strip()]
    missing = [car for car in cars if not (config_dir / car["model_package"]).exists()]
    if not missing:
        print("Every model package folder exists; nothing to recover.")
        return 0
    if len(missing) != len(cars):
        raise SystemExit("Only some package folders are missing; recover them by hand")
    donor_only = copy.deepcopy(config)
    for car in donor_only["cars"]:
        car.pop("model_package", None)
    baseline, _ = prepare.build_plan(game_dir, originals, donor_only, config_dir)
    installed = {relative: (game_dir / relative).read_bytes() for relative in state["installed"]}
    archives = {relative: (entries(installed[relative]), entries(baseline[relative]))
                for relative in prepare.ARCHIVES}
    packages = {}
    for car in cars:
        files = []
        for relative, (now, donor) in archives.items():
            name = Path(relative).name
            for entry, data in now.items():  # installed (package) order
                if visual(car["alias"], name, entry) and donor.get(entry) != data:
                    files.append((name, entry, data))
        for relative, data in installed.items():
            if visual(car["alias"], "loose", relative) and baseline.get(relative) != data:
                files.append(("loose", relative, data))
        if not any(path.endswith("/body.cmp") for _, path, _ in files):
            raise SystemExit(f"No installed custom model found for {car['alias']}")
        packages[car["alias"]] = (config_dir / car["model_package"], car, files)

    for folder, car, files in packages.values():
        manifest = []
        for archive, path, data in files:
            source = "assets/" + archive.replace(".zip", "") + "/" + path
            target = folder / source
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            manifest.append({"archive": archive, "path": path, "source": source})
        (folder / "package.json").write_text(json.dumps(
            {"format": "mm3-car-model-v1", "alias": car["alias"], "car_id": car["id"],
             "files": manifest}, indent=2) + "\n", encoding="utf-8")
        print(f"{car['alias']}: {len(files)} visual files recovered into {folder.resolve()}")

    rebuilt, _ = prepare.build_plan(game_dir, originals, config, config_dir)
    for relative, data in installed.items():
        same = rebuilt.get(relative) == data
        if not same and relative in prepare.ARCHIVES:
            same = entries(rebuilt[relative]) == entries(data)
        if not same:
            raise SystemExit(f"Recovered packages do not reproduce the installed {relative}; "
                             f"remove {Path(packages[next(iter(packages))][0]).parent} before retrying")
    print(f"PASS: Workshop plan with the recovered packages reproduces all {len(installed)} installed files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
