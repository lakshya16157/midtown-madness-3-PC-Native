"""Attach installed imported cars' lamps to their bodies.

Imported exteriors keep the donor's loose parts only as invisible stubs, so
lamps hung from them wobbled or flew off in crashes while the visible lenses
stayed put (mm3_lights.anchor_lights_to_body). New packages are built with the
fix; this repairs cars already installed by Car Workshop. Each changed CMP is
backed up first, and the Workshop install state receives the new digests so
reapplying or restoring still works. Running it again changes nothing.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import tempfile

import mm3_lights

ROOT = Path(__file__).resolve().parents[2]
STATE = Path(".mm3-car-mods/state.json")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".mm3-lamps-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def default_config() -> Path:
    local = ROOT / "tools/car-mods/cars.local.json"
    return local if local.is_file() else ROOT / "tools/car-mods/cars.json"


def imported_aliases(config: Path) -> list[str]:
    manifest = json.loads(config.read_text(encoding="utf-8-sig"))
    return [car["alias"] for car in manifest["cars"] if car.get("model_package")]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, default=ROOT / "game_files")
    parser.add_argument("--config", type=Path, default=default_config())
    parser.add_argument("--backup-dir", type=Path,
                        default=ROOT / "backups" / ("lamp-anchors-" + datetime.now().strftime("%Y%m%d-%H%M%S")))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    state_path = args.game_dir / STATE
    state = json.loads(state_path.read_text()) if state_path.is_file() else None
    cars = args.game_dir / "Data/Shared/Cars"
    changes = []
    for alias in imported_aliases(args.config):
        for folder in sorted(cars.glob(alias + "*")):
            suffix = folder.name[len(alias):]
            if suffix and not (suffix.startswith("_lod") and suffix[4:].isdigit()):
                continue
            path = folder / "body.cmp"
            if not path.is_file():
                continue
            relative = path.relative_to(args.game_dir).as_posix()
            before = path.read_bytes()
            recorded = state and state["installed"].get(relative)
            if recorded and recorded != digest(before):
                raise SystemExit(f"{relative} changed outside Car Workshop; resolve that first")
            after, report = mm3_lights.anchor_lights_to_body(before)
            print(f"{relative}: {len(report['anchored'])} lamps attached to the body"
                  + (f", {len(report['kept'])} kept on visible parts" if report["kept"] else ""))
            if after != before:
                changes.append((path, relative, before, after))
    if args.dry_run or not changes:
        print("No files changed." if not changes else f"Dry run: {len(changes)} files would change.")
        return 0
    for path, relative, before, _ in changes:
        backup = args.backup_dir / relative
        if backup.exists():
            raise SystemExit(f"Backup already exists: {backup}")
        atomic_write(backup, before)
    for path, relative, _, after in changes:
        atomic_write(path, after)
        if state and relative in state["installed"]:
            state["installed"][relative] = digest(after)
    if state:
        atomic_write(state_path, (json.dumps(state, indent=2) + "\n").encode())
    print(f"Changed {len(changes)} files; originals saved in {args.backup_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
