"""Route pictures for the downloadable Blitz and Checkpoint races.

The DLC's race list icons for races 11-20 are copies of races 1-10, so the
menu repeated them. This draws each downloadable race's own route (its start,
then its checkpoints in order) the way circuits.py draws a circuit, and stores
them in dlc_icons.json; circuits.race_entries puts them into Data_dvd.zip as
Menu/Gfx/races/<City>/<Mode>/<NN>_mm3.cdds for src/mm3_dlc_races.c.

The DLC is only read, never changed (it is signed). Usage:
  python tools/races/dlc_icons.py [--tdata game_files/TDATA]
"""
import argparse
import base64
import json
import re
from pathlib import Path

import circuits

HERE = Path(__file__).resolve().parent
TITLE = "4d53002a"


def race_points(setup, logic):
    """[x, y, z] for the start and every CP-<n> gate, in order."""
    spawn = re.search(r"spawn_location\[1\]\s*=\s*\{\s*([-\d.]+)\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)", logic)
    gates = {}
    for name, body in re.findall(r'objectCreate\(\s*\d+\s*,\s*"CP-(\d+)"\s*\)(.*?)(?=objectCreate|\Z)', setup, re.S):
        position = re.search(r"objectSetPos\(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)\s*\)", body)
        if position:
            gates[int(name)] = [float(v) for v in position.groups()]
    points = [[float(v) for v in spawn.groups()]] if spawn else []
    return points + [gates[k] for k in sorted(gates)]


def generate(tdata):
    icons = {}
    for setup_path in sorted((tdata / TITLE).glob("$C/*/Scripts/*/*/1?Setup.lua")):
        city, mode = setup_path.parent.parent.name, setup_path.parent.name
        if mode not in ("Blitz", "Checkpoint"):
            continue
        number = setup_path.name[:2]
        logic = setup_path.with_name(number + "Logic.lua").read_bytes().decode("latin1")
        points = race_points(setup_path.read_bytes().decode("latin1"), logic)
        if len(points) < 3:
            raise ValueError(f"{setup_path}: too few points for a route")
        icons[f"{city}/{mode}/{number}"] = base64.b64encode(
            circuits.icon_cdds([[p[0], p[2]] for p in points], closed=False)).decode("ascii")
    return icons


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tdata", type=Path, default=HERE.parents[1] / "game_files" / "TDATA")
    args = parser.parse_args()
    icons = generate(args.tdata)
    if not icons:
        raise SystemExit(f"No downloadable races under {args.tdata}")
    (HERE / "dlc_icons.json").write_text(json.dumps(icons, indent=1, sort_keys=True) + "\n")
    print(f"{len(icons)} route pictures -> {HERE / 'dlc_icons.json'}")


if __name__ == "__main__":
    main()
