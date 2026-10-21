"""Give models with no front-light effects a native night-road-light handle.

Run with the game closed:
  python tools/car-models/ensure_night_lights.py game_files --backup game_files/mod-backups/night-lights

No visible lenses are invented. The native runtime supplies illumination from
this explicitly disabled road-only handle. Authored meshes/materials and all
existing nodes remain byte-identical. Existing backup folders are preserved.
Downloadable content under TDATA is signed and is never modified.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

import numpy as np
import mm3_model


def ensure_night_light(blob: bytes) -> tuple[bytes, dict | None]:
    model = mm3_model.read_model(blob)
    for _, node in model.root.walk():
        if node.kind != 4:
            continue
        at = 5 + len(node.name)
        kind = struct.unpack_from("<I", node.prefix, at)[0]
        brightness = struct.unpack_from("<f", node.prefix, at + 12)[0]
        color = struct.unpack_from("<I", node.prefix, at + 28)[0]
        if kind == 8 or (kind == 0 and brightness > 0 and color & 0xffffff):
            return blob, None
    bounds = np.array(struct.unpack_from("<6f", model.root.prefix,
                                        model.root.bounds_offset)).reshape(2, 3)
    if not np.isfinite(bounds).all() or np.any(bounds[1] <= bounds[0]):
        raise mm3_model.FormatError("Cannot place a road light from invalid car bounds")
    height = float(np.clip((bounds[1, 1] - bounds[0, 1]) * .25, .35, .9))
    relative = np.array([(bounds[0, 0] + bounds[1, 0]) * .5,
                         bounds[0, 1] + height, -bounds[1, 2] - .08])
    root = model.root.relative_matrix()
    world = root[:3, :3] @ relative + root[:3, 3]
    identity = (1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0)
    prefix = struct.pack("<I", 1) + mm3_model.cstring("NightRoadLight")
    offset = len(prefix)
    prefix += struct.pack("<I28f", 0, *(identity * 2), 0, 0, 0, 0)
    prefix += struct.pack("<i6f", 0, 0, 0, 0, 0, 0, 0)
    group = mm3_model.Node(1, "NightRoadLight", prefix,
                           transform_offset=offset, bounds_offset=offset + 120)
    group.set_translation(world, relative)
    # Zero brightness, size and ARGB keep this harmless on an unmodified
    # executable; only mm3_car_lighting's night fallback activates it.
    payload = struct.pack("<2I5fI", 8, 1, 0, 0, 0, 0, 0, 0)
    if model.version >= 114:
        payload += struct.pack("<2f", 0, 0)
    group.children.append(mm3_model.Node(4, "", struct.pack("<I", 4) + b"\0" + payload))
    original_nodes = [(path, node.write()) for path, node in model.root.walk()
                      if node is not model.root]
    model.root.children.append(group)
    output = model.write()
    checked = mm3_model.read_model(output)
    parsed_nodes = dict(checked.root.walk())
    for path, contents in original_nodes:
        assert parsed_nodes[path].write() == contents, path
    assert ensure_night_light(output)[0] == output
    return output, {"position_source_metres": world.tolist(),
                    "placement": "front center of authored model bounds, road illumination only"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_files", type=Path)
    parser.add_argument("--backup", type=Path, required=True)
    args = parser.parse_args()
    root = args.game_files.resolve()
    # Limit traversal to actual installed car roots, never prior backups.
    # Never TDATA: downloadable content is signed, and the game reports any
    # edited file there as damaged content (seen 16 and 19 October 2026).
    models = sorted((root / "Data/Shared/Cars").glob("*/body.cmp"))
    updates = []
    for path in models:
        before = path.read_bytes()
        after, evidence = ensure_night_light(before)
        if evidence is not None:
            updates.append((path, before, after, evidence))
    if not updates:
        print(f"PASS: all {len(models)} installed car models have a front-light source")
        return
    if args.backup.exists():
        parser.error("Choose a new backup folder; existing backups are preserved")
    for path, before, _, _ in updates:
        backup = args.backup / path.relative_to(root)
        backup.parent.mkdir(parents=True, exist_ok=True)
        backup.write_bytes(before)
    for path, _, after, _ in updates:
        temporary = path.with_suffix(".cmp.night-light.tmp")
        temporary.write_bytes(after)
        temporary.replace(path)
    report = [{"path": str(path.relative_to(root)), **evidence}
              for path, _, _, evidence in updates]
    (args.backup / "repair-report.json").write_text(json.dumps(report, indent=2) + "\n")
    for item in report:
        print(f"ADDED road-only source: {item['path']} at {item['position_source_metres']}")
    print(f"PASS: {len(models)} models checked; {len(updates)} missing sources restored; "
          f"originals saved in {args.backup}")


if __name__ == "__main__":
    main()
