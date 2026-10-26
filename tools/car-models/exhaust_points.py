"""Exhaust pipes of every installed car model, for the exhaust smoke mod.

Writes src/mm3_exhaust_models.h: per model, its lamp nodes (effect kind,
model-space rotation rows and position, as stored in body.cmp) and its
exhaust pipes (model-space position and outward direction). The game matches
each car it draws to a model by the lamps it reports (sub_000CF2DA), which
also give the model's place in the world, and puffs smoke from those pipes.

Pipes come from, in order:
  1. the ExhaustSmoke emitter nodes the original artists placed (a transform
     node with an empty kind-5 child; the retail game never used them);
  2. the model's exhaust pipe parts (exhaust, exaustpipe, muffler...): the
     tips of each part's mesh at its rear end, one pipe per tip (a part
     running across the car often ends in a tip each side); the part's box
     if its mesh cannot be read;
  3. a distant-detail model (name_lod1) takes the pipes of its full model
     when their lamps agree on the model's frame, so its pipes do not move
     when the game switches models;
  4. otherwise the rear of the body just under the bumper, on the left.

Usage: tools\\venv\\Scripts\\python.exe tools\\car-models\\exhaust_points.py
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
import mm3_model  # noqa: E402

PIPE_PART = re.compile(r"exhaust|exaust|muffler|tailpipe|loosepipe", re.I)
EMITTER = re.compile(r"exhaustsmoke", re.I)
OUTPUT = ROOT / "src" / "mm3_exhaust_models.h"


def world(node):
    if node.transform_offset is None:
        return None
    return struct.unpack_from("<12f", node.prefix, node.transform_offset + 4)


def bounds(node):
    if node.bounds_offset is None:
        return None
    return struct.unpack_from("<6f", node.prefix, node.bounds_offset)


def mesh_points(block, version):
    """A mesh's vertex positions in its node's frame: three signed fields
    (11, 11 and 10 bits) of the 24-byte packed vertex, scaled by the size of
    the mesh's stored bounds."""
    meta = mm3_model.mesh_metadata(block, version)
    lo, hi = struct.unpack_from("<3f", block.payload, 4), struct.unpack_from("<3f", block.payload, 16)
    size = [b - a for a, b in zip(lo, hi)]

    def field(value, bits):
        value &= (1 << bits) - 1
        return value - (1 << bits) if value >= 1 << (bits - 1) else value

    points = []
    for i in range(meta["vertex_count"]):
        word = struct.unpack_from("<I", block.payload, meta["vertex_offset"] + i * 24)[0]
        points.append((field(word, 11) / 1024 * size[0], field(word >> 11, 11) / 1024 * size[1],
                       field(word >> 22, 10) / 512 * size[2]))
    return points


def pipe_tips(node, matrix, version):
    """The tips of an exhaust part's mesh, in model space: its rear-most
    vertices (within 6 cm of its rear end), grouped by gaps across the car
    of more than 12 cm. None if the part has no readable mesh."""
    for child in node.children:
        if child.kind != 2 or not child.meshes or not child.meshes[0].payload:
            continue
        try:
            local = mesh_points(child.meshes[0], version)
        except (mm3_model.FormatError, struct.error):
            return None
        if not local or matrix is None:
            return None
        points = [tuple(p[0] * matrix[k] + p[1] * matrix[3 + k] + p[2] * matrix[6 + k] + matrix[9 + k]
                        for k in range(3)) for p in local]
        rear = min(p[2] for p in points)
        tail = sorted((p for p in points if p[2] < rear + 0.06), key=lambda p: p[0])
        groups, group = [], [tail[0]]
        for p in tail[1:]:
            if p[0] - group[-1][0] > 0.12:
                groups.append(group)
                group = []
            group.append(p)
        groups.append(group)
        return [(sum(p[0] for p in g) / len(g), sum(p[1] for p in g) / len(g), rear) for g in groups]
    return None


def model_points(model):
    lamps, emitters, parts, tires = [], [], [], []
    body = root_bounds = None

    def walk(node, matrix):
        nonlocal body, root_bounds
        own = world(node)
        if own is not None:
            matrix = own
        if node.kind == 4 and matrix is not None:
            kind = struct.unpack_from("<I", node.prefix, 5 + len(node.name))[0]
            lamps.append((kind, matrix))
        box = bounds(node)
        if box is not None:
            if root_bounds is None:
                root_bounds = box
            if node.kind == 1 and node.name.lower() == "body" and body is None:
                body = box
            if node.kind == 1 and node.name.lower().startswith("tire"):
                tires.append(box)
            if node.kind == 1 and PIPE_PART.search(node.name) and not EMITTER.search(node.name):
                parts.append((box, pipe_tips(node, matrix, model.version)))
        if node.kind == 1 and EMITTER.search(node.name) and own is not None and \
                any(child.kind == 5 for child in node.children):
            emitters.append(own)
        for child in node.children:
            walk(child, matrix)

    walk(model.root, None)
    pipes = []
    for matrix in emitters:
        pipes.append((matrix[9:12], matrix[6:9]))
    if not pipes and parts:
        # The pipe ends: the rear-most parts only (a bend further forward is
        # the same pipe), each part's mesh tips; without a readable mesh, the
        # part's rear end, and one wide part centred on the car is a pipe
        # each side.
        rear = min(box[2] for box, _ in parts)
        for (x0, y0, z0, x1, y1, z1), tips in parts:
            if z0 > rear + 0.1:
                continue
            y = (y0 + y1) / 2
            # A mesh whose rear end is not the part's own (a frame the box
            # does not share) is not trusted: the box rule instead.
            if tips and abs(tips[0][2] - z0) < 0.05:
                pipes += [(tip, (0.0, 0.0, -1.0)) for tip in tips]
            elif x1 - x0 > 0.45 and abs(x0 + x1) < 0.2:
                pipes += [((x0 + 0.06, y, z0), (0.0, 0.0, -1.0)), ((x1 - 0.06, y, z0), (0.0, 0.0, -1.0))]
            else:
                pipes.append((((x0 + x1) / 2, y, z0), (0.0, 0.0, -1.0)))
    source = "emitters" if emitters else "parts" if pipes else "body"
    if not pipes:
        box = body or root_bounds
        if box is None:
            return lamps, [], "none"
        ground = min(t[1] for t in tires) if tires else (root_bounds or box)[1]
        y = max(ground + 0.18, box[1] + 0.04)
        pipes.append(((box[0] * 0.45, y, box[2] + 0.06), (0.0, 0.0, -1.0)))
    return lamps, pipes[:4], source


def base_name(name):
    """The full model a distant-detail or paint variant belongs to."""
    return re.sub(r"(_lod\d+)?(\.\d+)?$", "", name, flags=re.I).lower()


def same_frame(lamps, full):
    """Whether most lamps of a distant-detail model sit where its full
    model has a lamp of the same kind (within 2 cm): one model frame."""
    def near(kind, m):
        return any(k == kind and sum((m[9 + i] - f[9 + i]) ** 2 for i in range(3)) < 0.02 ** 2 for k, f in full)
    return lamps and sum(near(kind, m) for kind, m in lamps) * 2 >= len(lamps)


def main():
    paths = sorted((ROOT / "game_files/Data/Shared/Cars").glob("*/body.cmp"))
    paths += sorted((ROOT / "game_files/TDATA").rglob("body.cmp"))
    models, seen, sources = [], {}, {}
    found = [(path.parent.name, *model_points(mm3_model.read_model(path.read_bytes()))) for path in paths]
    authored = {}
    for name, lamps, pipes, source in found:
        if source in ("emitters", "parts") and not re.search(r"_lod\d", name, re.I):
            authored.setdefault(base_name(name), (lamps, pipes))
    for name, lamps, pipes, source in found:
        full = authored.get(base_name(name))
        # A distant-detail model smokes where its full model does, so the
        # pipes do not move when the game switches between them.
        if (source == "body" or re.search(r"_lod\d", name, re.I)) and full and full[1] != pipes and \
                same_frame(lamps, full[0]):
            pipes, source = full[1], "full model"
        if not lamps or not pipes:
            continue
        key = (tuple((k, tuple(round(v, 4) for v in m)) for k, m in lamps),
               tuple((tuple(round(v, 4) for v in p), tuple(round(v, 4) for v in d)) for p, d in pipes))
        if key in seen:                 # paint variants share their geometry
            continue
        seen[key] = name
        sources[source] = sources.get(source, 0) + 1
        models.append((name, lamps, pipes))
    lines = [
        "/* Generated by tools/car-models/exhaust_points.py from the installed car",
        " * models (body.cmp): each model's lamp nodes and exhaust pipes in model space,",
        " * as stored (rows right, up, forward, then position). Do not edit. */",
        "#ifndef MM3_EXHAUST_MODELS_H",
        "#define MM3_EXHAUST_MODELS_H",
        "#include <stdint.h>",
        "typedef struct { uint32_t kind; float m[12]; } Mm3ExhaustLamp;",
        "typedef struct { float p[3], d[3]; } Mm3ExhaustPipe;",
        "typedef struct { const char *name; uint16_t lamp, lamps, pipe, pipes; } Mm3ExhaustModel;",
        "static const Mm3ExhaustLamp mm3_exhaust_lamps[] = {",
    ]
    f = lambda v: "%.5ff" % v  # noqa: E731
    lamp_index = pipe_index = 0
    table = []
    for name, lamps, pipes in models:
        for kind, matrix in lamps:
            lines.append("    {%u, {%s}}," % (kind, ", ".join(f(v) for v in matrix)))
        table.append((name, lamp_index, len(lamps), pipe_index, len(pipes)))
        lamp_index += len(lamps)
        pipe_index += len(pipes)
    lines += ["};", "static const Mm3ExhaustPipe mm3_exhaust_pipes[] = {"]
    for name, lamps, pipes in models:
        for position, direction in pipes:
            lines.append("    {{%s}, {%s}}," % (", ".join(f(v) for v in position), ", ".join(f(v) for v in direction)))
    lines += ["};", "static const Mm3ExhaustModel mm3_exhaust_models[] = {"]
    for name, lamp, lamps, pipe, pipes in table:
        lines.append('    {"%s", %u, %u, %u, %u},' % (name, lamp, lamps, pipe, pipes))
    lines += ["};", "#endif", ""]
    OUTPUT.write_text("\n".join(lines), encoding="ascii", newline="\r\n")
    print(f"{len(models)} distinct models from {len(paths)} files; pipes from {sources}; "
          f"{lamp_index} lamps, {pipe_index} pipes -> {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
