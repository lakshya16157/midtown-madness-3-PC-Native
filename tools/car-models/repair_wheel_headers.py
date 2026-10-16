"""Restore donor wheel color/opacity headers without changing imported geometry.

Old imports of payloadless donor wheel references could inherit a glass mesh's
RGBA header. Repair prepared packages, then use Car Workshop to install them.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

from build_package import IDENTITIES, ROOT
import mm3_model


def repair(blob: bytes, donor: bytes) -> tuple[bytes, list[dict]]:
    original, model = mm3_model.read_model(donor), mm3_model.read_model(blob)
    if original.version != model.version:
        raise ValueError("Imported model and donor versions differ")
    templates = {block.name.casefold(): block for _, node in original.root.walk()
                 for block in node.meshes if block.payload}
    colors = {}
    for path, node in original.root.walk():
        wheel = next((name for name in mm3_model.WHEEL_NODES if f"/{name}/" in path), None)
        if wheel and node.meshes:
            block = templates[node.meshes[0].name.casefold()]
            colors[wheel] = mm3_model.mesh_metadata(block, original.version)["color"]
    if colors.keys() != mm3_model.WHEEL_NODES.keys():
        raise ValueError("Donor must provide four wheel headers")
    report = []
    for path, node in model.root.walk():
        wheel = next((name for name in colors if f"/{name}/" in path), None)
        if wheel is None:
            continue
        for index, block in enumerate(node.meshes):
            if not block.payload or mm3_model.is_shadow_mesh(block):
                continue
            if not block.name.rsplit("\\", 1)[-1].startswith("mm3import_"):
                continue
            old = mm3_model.mesh_metadata(block, model.version)["color"]
            target = colors[wheel]
            if old == target:
                continue
            data = bytearray(block.payload)
            data[48:48 + len(target)] = target
            node.meshes[index] = mm3_model.MeshBlock(block.name, bytes(data))
            report.append({"node": path, "from": list(struct.unpack("<" + "f" * (len(old) // 4), old)),
                           "to": list(struct.unpack("<" + "f" * (len(target) // 4), target))})
    after = model.write()
    if len(blob) != len(after):
        raise ValueError("Wheel header repair changed the model structure")
    return after, report


def prepare(folder: Path, game_data: Path):
    folder = folder.resolve()
    package = json.loads((folder / "package.json").read_text(encoding="utf-8"))
    alias, donor, _ = IDENTITIES[package["car_id"]]
    if package["alias"] != alias:
        raise ValueError("Package identity does not match its car ID")
    report_path = folder / "conversion-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    updates, count = [], 0
    for item in package["files"]:
        if item["archive"] != "loose" or not item["path"].endswith("/body.cmp"):
            continue
        path = (folder / item["source"]).resolve()
        if not path.is_relative_to(folder):
            raise ValueError("CMP path leaves its package folder")
        suffix = "_lod1" if "_lod1/" in item["path"] else ""
        before = path.read_bytes()
        after, changes = repair(before, (game_data / f"Shared/Cars/{donor}{suffix}/body.cmp").read_bytes())
        report["models"][suffix or "near"]["wheel_header_repair"] = changes
        updates.append((path, before, after))
        count += len(changes)
    if len(updates) != 2:
        raise ValueError("Expected near and far CMPs")
    updates.append((report_path, report_path.read_bytes(), (json.dumps(report, indent=2) + "\n").encode()))
    return updates, count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", type=Path)
    parser.add_argument("--game-data", type=Path, default=ROOT / "game_files/Data")
    parser.add_argument("--backup", type=Path, required=True)
    args = parser.parse_args()
    folders = ([args.packages] if (args.packages / "package.json").is_file()
               else sorted(path.parent for path in args.packages.glob("*/package.json")))
    if not folders or args.backup.exists():
        parser.error("Provide prepared packages and a new backup folder")
    prepared = [(folder.resolve(), *prepare(folder, args.game_data)) for folder in folders]
    for folder, updates, count in prepared:
        for path, before, after in updates:
            backup = args.backup / folder.name / path.relative_to(folder)
            backup.parent.mkdir(parents=True, exist_ok=True)
            backup.write_bytes(before)
    for folder, updates, count in prepared:
        for path, before, after in updates:
            temp = path.with_suffix(path.suffix + ".wheel-header.tmp")
            temp.write_bytes(after)
            temp.replace(path)
        print(f"{folder.name}: {count} wheel opacity headers corrected; geometry/lights preserved")


if __name__ == "__main__":
    main()
