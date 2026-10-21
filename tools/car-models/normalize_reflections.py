"""Uniform paint, window and chrome reflections for retail MM3 car models.

Retail paint used unrelated strengths: about 24 cars (the Viper family)
166/51/128, 26 cars and most traffic 102/26/102, ten trucks, buses and
traffic vans 51/26/64 (nearly matte), and outliers such as the Koenigsegg
179/51/153, the Lotus 115/51/166 or the Washington police chrome 255/128/247.
The car shader (ObjectDemolition*.cvsx) reads four bytes per vertex, word 5
in file order: b0 baked occlusion, b1 Fresnel gain (grazing reflection), b2
constant environment reflection, b3 specular glint, which also feeds glare.

Only b1..b3 of classified material slots change. Occlusion (b0), geometry,
shadows, damage data, lamps, matte trim, rubber and every unclassified
material are kept, and each file keeps its exact length. Signed DLC (TDATA)
and Car Workshop's imported packages are never touched here.
"""
from __future__ import annotations

import argparse
import collections
import io
import json
import re
import struct
import zipfile
from pathlib import Path

import mm3_model

# Between the Viper family (E0 .20, grazing .81, glint .50) and the common
# traffic paint (E0 .10, grazing .48, glint .40): face-on 15% environment,
# 62% at 85 degrees, glint 40%.
PAINT = (128, 38, 102)
WINDOW = (51, 204, 255)   # 213 of about 250 retail window materials
CHROME = (102, 128, 230)

LAMP = re.compile(r"lamp|light|blink|lykt|indicator|signal|siren|lens|reflex", re.I)
GLASS = re.compile(r"window|rutor|ruta|glas", re.I)
METAL = re.compile(r"chrom|crome|krom|metal", re.I)
MATTE = re.compile(r"matt|black|svart|rubber|tire|tyre|tread|under|plast|interior|dash|seat|"
                   r"driver|cockpit|chassis|grill|number|plate|press|lister|^list|dark|grey|gray|"
                   r"passenger|cab$|cabin|engine|motor|exhaust|rim|wheel", re.I)
PAINT_NAME = re.compile(r"lack|paint|colou?r|body|kaross|blank|skin|gloss|shiny|textur|xray|"
                        r"standard|blinn|lod", re.I)
# See-through and deliberately semi-gloss textures keep their own values
# ("genomskinlig" = transparent, "halvblank" = semi-gloss).
SEE_THROUGH = re.compile(r"trans|genomskinlig|alpha|halvblank", re.I)
WHEEL_NODE = re.compile(r"tire|tyre|wheel|whl|rim", re.I)

# Never edited: trains and traffic models the city never spawns.
SKIP_MODELS = {"a_pmetro", "a_wmetro", "ambient_bus", "ambient_pmetro", "ambient_wmetro"}


def read_omb(data: bytes) -> list[dict]:
    if not data or data[0] != 0:
        raise ValueError("Unsupported OMB header")
    count, pos, out = struct.unpack_from("<I", data, 1)[0], 5, []
    for _ in range(count):
        end = data.index(0, pos)
        name = data[pos:end].decode("latin-1")
        pos = data.index(0, end + 1) + 1
        blue, green, red, alpha = data[pos + 1:pos + 5]
        out.append(dict(name=name, rgb=(red, green, blue), alpha=alpha))
        pos += 9
    if pos != len(data):
        raise ValueError("Unsupported OMB layout")
    return out


def slot_tuples(blob: bytes) -> tuple[dict[int, collections.Counter], dict[int, float]]:
    """Per material slot: word-5 b1..b3 counts, and the share of its vertices
    in wheel/tyre nodes (rims share paint-like bytes on some cars)."""
    model = mm3_model.read_model(blob)
    tuples = collections.defaultdict(collections.Counter)
    wheel = collections.Counter()
    for _, node in model.root.walk():
        on_wheel = bool(WHEEL_NODE.search(node.name))
        for block in node.meshes:
            if not block.payload or mm3_model.is_shadow_mesh(block):
                continue
            meta = mm3_model.mesh_metadata(block, model.version)
            payload = block.payload
            for vertex in range(meta["vertex_count"]):
                offset = meta["vertex_offset"] + vertex * 24
                slot = payload[offset + 12] // 11
                tuples[slot][tuple(payload[offset + 21:offset + 24])] += 1
                wheel[slot] += on_wheel
    return tuples, {slot: wheel[slot] / sum(counter.values()) for slot, counter in tuples.items()}


def classify(name: str, current: tuple[int, int, int], varies: bool) -> str | None:
    """Return 'paint', 'window', 'chrome' or None (keep as authored)."""
    b1, b2, b3 = current
    if LAMP.search(name):
        return None
    if GLASS.search(name):
        return "window" if b2 >= 128 and b3 >= 191 else None
    if METAL.search(name):
        # Retail chrome is 102/128/200-255; only over-bright Fresnel (the
        # Washington police trim at 255) is brought back to it.
        return "chrome" if b1 > 128 and b2 >= 100 and b3 >= 180 else None
    if b2 >= 100:
        return None                       # mirror-like special surfaces
    if MATTE.search(name) or SEE_THROUGH.search(name):
        return None
    if varies and b1 >= 10:
        return "paint"                    # its colour changes with the paint job
    if 1 <= b2 < 100 and b1 >= 26 and PAINT_NAME.search(name):
        return "paint"
    return None


TARGETS = {"paint": PAINT, "window": WINDOW, "chrome": CHROME}


def plan_model(blob: bytes, omb_sets: list[list[dict]]) -> dict[int, dict]:
    """Material slot -> decision for every slot whose bytes should change."""
    if not omb_sets:
        return {}
    first = omb_sets[0]
    plan = {}
    tuples, wheel_share = slot_tuples(blob)
    for slot, counter in tuples.items():
        if slot >= len(first) or wheel_share[slot] >= 0.5:
            continue
        current = counter.most_common(1)[0][0]
        if len(counter) != 1:
            continue                      # mixed bytes within one slot: keep as authored
        colours = {tuple(sets[slot]["rgb"]) for sets in omb_sets if slot < len(sets)}
        kind = classify(first[slot]["name"], current, len(colours) > 1)
        if kind and TARGETS[kind] != current:
            plan[slot] = dict(kind=kind, name=first[slot]["name"], before=current,
                              after=TARGETS[kind], vertices=counter[current])
    return plan


def apply_plan(blob: bytes, plan: dict[int, dict]) -> bytes:
    if not plan:
        return blob
    model = mm3_model.read_model(blob)
    targets = {slot: bytes(entry["after"]) for slot, entry in plan.items()}
    changed = 0
    for _, node in model.root.walk():
        updated = []
        for block in node.meshes:
            if not block.payload or mm3_model.is_shadow_mesh(block):
                updated.append(block)
                continue
            meta = mm3_model.mesh_metadata(block, model.version)
            payload = bytearray(block.payload)
            for vertex in range(meta["vertex_count"]):
                offset = meta["vertex_offset"] + vertex * 24
                target = targets.get(payload[offset + 12] // 11)
                if target is not None and payload[offset + 21:offset + 24] != target:
                    payload[offset + 21:offset + 24] = target
                    changed += 1
            updated.append(mm3_model.MeshBlock(block.name, bytes(payload)))
        node.meshes = updated
    after = model.write()
    if len(after) != len(blob) or mm3_model.read_model(after).write() != after:
        raise ValueError("Reflection normalisation changed the model structure")
    if not changed:
        raise ValueError("Planned reflection change touched no vertex")
    return after


def zip_ombs(archive: zipfile.ZipFile, folder: str) -> list[list[dict]]:
    names = {name.lower(): name for name in archive.namelist()}
    sets = []
    for index in range(32):
        name = names.get(f"cars/{folder.lower()}/materialset{index:02d}.omb")
        if name:
            sets.append(read_omb(archive.read(name)))
    return sets


def normalized_zip_entries(archive_bytes: bytes) -> dict[str, tuple[bytes, zipfile.ZipInfo]]:
    """Normalised traffic (Ambient_*) body.cmp entries of a pristine Data_hd.zip."""
    updates = {}
    with zipfile.ZipFile(io.BytesIO(archive_bytes)) as archive:
        for info in archive.infolist():
            parts = info.filename.split("/")
            if (len(parts) != 3 or parts[0].lower() != "cars" or parts[2].lower() != "body.cmp" or
                    not parts[1].lower().startswith("ambient_") or parts[1].lower() in SKIP_MODELS):
                continue
            blob = archive.read(info)
            plan = plan_model(blob, zip_ombs(archive, parts[1]))
            if plan:
                updates[info.filename] = (apply_plan(blob, plan), info)
    return updates


def loose_models(game: Path, owned: set[str]) -> list[Path]:
    result = []
    for folder in sorted((game / "Data/Shared/Cars").iterdir(), key=lambda p: p.name.lower()):
        path = folder / "body.cmp"
        relative = f"Data/Shared/Cars/{folder.name}/body.cmp"
        if (not path.is_file() or relative in owned or folder.name.lower() in SKIP_MODELS or
                folder.name.lower().startswith("ambient_")):
            continue
        result.append(path)
    return result


def omb_folder(folder: str) -> str:
    """Secret skins (Viper.1) keep their base car's material sets."""
    return re.sub(r"\.\d+$", "", folder)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game", type=Path, help="game_files folder")
    parser.add_argument("--apply", action="store_true", help="rewrite the retail loose models")
    parser.add_argument("--backup", type=Path, help="new folder for the original loose files (required with --apply)")
    parser.add_argument("--report", type=Path, help="write the full plan as JSON")
    args = parser.parse_args()
    game = args.game.resolve()
    state = json.loads((game / ".mm3-car-mods/state.json").read_text())
    owned = set(state.get("installed", {}))
    pristine = (game / ".mm3-car-mods/Data_hd.zip").read_bytes()
    archive = zipfile.ZipFile(io.BytesIO(pristine))
    report, writes = {}, []
    for path in loose_models(game, owned):
        folder = path.parent.name
        sets = zip_ombs(archive, omb_folder(folder)) or zip_ombs(archive, folder)
        blob = path.read_bytes()
        plan = plan_model(blob, sets)
        report[f"loose/{folder}"] = plan
        if plan:
            writes.append((path, blob, apply_plan(blob, plan)))
    for name, (data, info) in normalized_zip_entries(pristine).items():
        folder = name.split("/")[1]
        report[f"Data_hd.zip/{folder}"] = plan_model(archive.read(info), zip_ombs(archive, folder))
    summary = collections.Counter()
    for model, plan in report.items():
        for slot, entry in sorted(plan.items()):
            summary[(entry["kind"], entry["before"])] += 1
            print(f"{model:36} m{slot:<2} {entry['name'][:24]:24} {entry['kind']:6} "
                  f"{entry['before']} -> {entry['after']}  ({entry['vertices']} vertices)")
    print("\nchanges by kind and original bytes:")
    for (kind, before), count in summary.most_common():
        print(f"  {kind:6} {str(before):16} {count} materials")
    if args.report:
        args.report.write_text(json.dumps({k: {str(s): e for s, e in v.items()} for k, v in report.items()},
                                          indent=1) + "\n")
    if not args.apply:
        print(f"\nDry run: {len(writes)} loose models would change (traffic is installed by Car Workshop).")
        return 0
    if not args.backup:
        parser.error("--apply requires --backup")
    if args.backup.exists():
        parser.error("Choose a new backup folder; existing backups are preserved")
    for path, before, _ in writes:
        target = args.backup / path.relative_to(game)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(before)
    for path, _, after in writes:
        temp = path.with_suffix(".cmp.reflections.tmp")
        temp.write_bytes(after)
        temp.replace(path)
    print(f"\nRewrote {len(writes)} loose models; originals saved in {args.backup}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
