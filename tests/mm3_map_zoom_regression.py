"""Run the actual CMake map zoom seams and native road/marker geometry offline.

Requires scripts/enter-env.ps1, generated code and the user's PAL XBE. Graphics
submission, player lookup and input are capture boundaries; native quad creation,
rotation, map scale setup and all guest calling conventions remain executable.
"""
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SEAMS = ("sub_0011CA25", "sub_0011D1E8", "sub_0011D808", "sub_0011CAE8", "sub_0011DCBF")
PRODUCER = "sub_00116535"
EMITTER_SITES = ("001169A2", "00116A7F", "00116B4A", "00116BDE", "00116C78", "00116D0E", "00116D9D", "00116E2F", "00116EBE")
MATH = ("sub_0002DC95", "sub_0006ACC3", "sub_000C6ADC",
        "sub_0006AC61", "sub_0006AE0C", "sub_0006AEE2", "sub_0001259E",
        "sub_0006AD9F", "sub_0006AE60", "sub_000629BE", "sub_0006AEB6",
        "sub_00218844", "sub_001F0399", "sub_0011DD26", "sub_0011DD00")
SQUARE_ORIGINAL = "    PUSH32(esp, 0x0011DB57u); RECOMP_ABI_CALL(0x000C6ADCu, sub_000C6ADC); /* call 0x000C6ADC */"
SQUARE_REPLACEMENT = "    if (mm3_minimap_improved()) {\n        mm3_map_square_geometry(eax, ecx, MEMF(esp));\n        mm3_map_zoom_window(ebx, MEM32(eax));\n        esp += 4; /* Inverse city-UV rotation consumes the native angle argument. */\n    } else {\n    " + SQUARE_ORIGINAL + "\n    }"
BORDER_ORIGINAL = "    PUSH32(esp, 0x0011DBB1u); RECOMP_ABI_CALL(0x001C1585u, sub_001C1585); /* call 0x001C1585 */"
BORDER_REPLACEMENT = "    if (mm3_minimap_improved()) {\n        esp += 8; /* Improved map has no MapCircle border. */\n    } else {\n    " + BORDER_ORIGINAL + "\n    }"
COMPASS_ORIGINAL = "loc_0011DBF1: ;\n    fp_push(MEMF(ebx + 0x4C)); /* fld float */"
COMPASS_REPLACEMENT = "loc_0011DBF1: ;\n    mm3_map_zoom_compass_geometry(ebx, ebp + -740);\n    fp_push(MEMF(ebx + 0x4C)); /* fld float */"
MARKER_ORIGINAL = "    fp_top() = RECOMP_FP_PC(sqrt(fp_top())); /* fsqrt */"
MARKER_REPLACEMENT = "    fp_top() = RECOMP_FP_PC(mm3_map_square_marker_distance(ebx, eax, MEMF(ebp + 0x4C), MEMF(ebp + 0x3C)));"


def generated():
    found = {}
    names = SEAMS + (PRODUCER,) + MATH
    for path in sorted((ROOT / "src/recomp/gen").glob("recomp_*.c")):
        source = path.read_text()
        for name in names:
            match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}", source, re.M | re.S)
            if match:
                assert name not in found, name
                found[name] = match.group()
    assert found.keys() == set(names), set(names) - found.keys()
    return found


def constants(source):
    xbe = (ROOT / "game_files/default.xbe").read_bytes()
    u32 = lambda offset: struct.unpack_from("<I", xbe, offset)[0]
    sections = [struct.unpack_from("<4I", xbe, u32(0x120) - u32(0x104) + i * 56 + 4)
                for i in range(u32(0x11C))]
    def word(address):
        section = next(s for s in sections if s[0] <= address <= s[0] + s[3] - 4)
        return u32(section[2] + address - section[0])
    addresses = set()
    for kind, address in re.findall(r"MEM([FD])\(0x([0-9A-Fa-f]+)\)", source):
        addresses.add(int(address, 16))
        if kind == "D":
            addresses.add(int(address, 16) + 4)
    return "\nstatic void native_constants(void) {\n" + "".join(
        f"    MEM32(0x{address:X}) = 0x{word(address):08X}u;\n"
        for address in sorted(addresses)) + "}\n"


def main():
    if not shutil.which("cl") or not shutil.which("cmake"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe and CMake are available.")
    cmake = (ROOT / "CMakeLists.txt").read_text()
    # The marker owner also has race-guidance seams; both owner groups can
    # register it before CMake removes duplicates.
    registered = set()
    for group in re.findall(r"list\(APPEND MM3_SEAM_FUNCTIONS\s+([^)]+)\)", cmake):
        registered.update(group.split())
    assert set(SEAMS) <= registered
    assert "foreach(map_owner " + " ".join(SEAMS + (PRODUCER,)) + ")" in cmake
    assert 'include("${CMAKE_CURRENT_SOURCE_DIR}/scripts/mm3_map_zoom_seam.cmake")' in cmake
    assert "mm3_add_map_zoom(distance_content ${map_owner})" in cmake
    assert "src/mm3_map_zoom.c" in cmake
    native = generated()
    with tempfile.TemporaryDirectory(prefix="mm3-map-zoom-") as temporary:
        folder = Path(temporary)
        original_path, modified_path = folder / "original.c", folder / "modified.c"
        script = folder / "seam.cmake"
        script.write_text(
            f'include("{(ROOT / "scripts/mm3_map_zoom_seam.cmake").as_posix()}")\n'
            f'include("{(ROOT / "scripts/mm3_race_guidance_seam.cmake").as_posix()}")\n'
            'file(READ "${INPUT}" content)\n'
            'foreach(stage IN LISTS PIPELINE)\n'
            '  if(stage STREQUAL "map")\n'
            '    mm3_add_map_zoom(content "${HOOKED}")\n'
            '  else()\n'
            '    mm3_add_race_guidance(content "${HOOKED}")\n'
            '  endif()\n'
            'endforeach()\n'
            'file(WRITE "${OUTPUT}" "${content}")\n')
        def transform(source, name, reject=False, order=("map",)):
            original_path.write_bytes(source.encode())
            result = subprocess.run(["cmake", f"-DINPUT={original_path}",
                                     f"-DOUTPUT={modified_path}", f"-DHOOKED={name}",
                                     "-DPIPELINE=" + ";".join(order),
                                     "-P", str(script)], capture_output=True, text=True)
            if reject:
                assert result.returncode and "MM3 map zoom" in result.stderr, result
                return None
            if result.returncode:
                raise RuntimeError(result.stderr or result.stdout or f"CMake exited {result.returncode}")
            return modified_path.read_text()
        modified = {}
        calls = {
            SEAMS[0]: "    mm3_map_zoom_reset(ecx);\n",
            SEAMS[1]: "    mm3_map_zoom_update(ecx, MEMF(esp + 0x10));\n",
            SEAMS[2]: "    mm3_map_zoom_apply(ebx, MEM32(ebp + -4));\n",
            SEAMS[3]: "    fp_top() = RECOMP_FP_PC(fp_top() * mm3_map_zoom_checkpoint_scale(ebx, MEM32(ebp + 0x40)));\n",
            SEAMS[4]: "    mm3_map_ui_capacity();\n",
        }
        labels = {SEAMS[0]: "loc_0011CA25", SEAMS[1]: "loc_0011D1E8",
                  SEAMS[2]: "loc_0011D8F6", SEAMS[3]: "loc_0011CC53", SEAMS[4]: "loc_0011DCBF"}
        instructions = {
            SEAMS[0]: "    PUSH32(esp, ebp);",
            SEAMS[1]: "    PUSH32(esp, ebp);",
            SEAMS[2]: "    PUSH32(esp, edi);",
            SEAMS[3]: "    { double _t = fp_top(); fp_push(_t); } /* fld st(0) */",
            SEAMS[4]: "    MEM32(0x3C568C) = MEM32(0x3C568C) & 0;",
        }
        for name in SEAMS:
            modified[name] = transform(native[name], name)
            assert transform(native[name].replace("\n", "\r\n"), name) == modified[name]
            assert modified[name].count(calls[name]) == 1
            restored = modified[name].replace(calls[name], "")
            if name == "sub_0011D808":
                for original, replacement in ((SQUARE_ORIGINAL, SQUARE_REPLACEMENT),
                                              (BORDER_ORIGINAL, BORDER_REPLACEMENT),
                                              (COMPASS_ORIGINAL, COMPASS_REPLACEMENT)):
                    assert modified[name].count(replacement) == 1, (name, replacement)
                    if original != COMPASS_ORIGINAL:
                        assert modified[name].count(original) == 1  # Stock retains the exact native call.
                    restored = restored.replace(replacement, original)
                    transform(native[name].replace(original, "    /* missing native call */"), name, True)
                    transform(native[name].replace(original, original + "\n" + original), name, True)
            if name == "sub_0011CAE8":
                assert restored.count(MARKER_REPLACEMENT) == 1
                restored = restored.replace(MARKER_REPLACEMENT, MARKER_ORIGINAL)
                transform(native[name].replace(MARKER_ORIGINAL, "    /* missing marker distance */"), name, True)
            assert restored[restored.index("void " + name):] == native[name]
            transform(native[name].replace(labels[name] + ": ;", labels[name] + "BAD: ;"), name, True)
            transform(native[name] + "\n" + native[name], name, True)
            label = labels[name] + ": ;\n"
            instruction = instructions[name]
            transform(native[name].replace(label, label + instruction + "\n" + label), name, True)
            unrelated = "\nvoid unrelated(void)\n{\n" + label + instruction + "\n}\n"
            separate = transform(native[name] + unrelated, name)
            assert separate.endswith(unrelated)
            assert separate.count(calls[name]) == 1
            transform(modified[name], name, True)
        producer = transform(native[PRODUCER], PRODUCER)
        assert transform(native[PRODUCER].replace("\n", "\r\n"), PRODUCER) == producer
        begin = "    uint32_t mm3_square_map = mm3_map_square_begin(ecx, MEM32(esp + 8));\n"
        publish_window = "    if (mm3_square_map) mm3_map_square_publish_window(MEMF(ebp + 0x4C), MEMF(ebp + 0x48));\n"
        city_uv = "    if (mm3_square_map) mm3_map_square_city_uv(ebx, esp);\n"
        city_pack = "    if (mm3_square_map) mm3_map_square_tag_packed(esi);\n"
        assert producer.count(begin) == producer.count(publish_window) == 1
        assert producer.count(city_uv) == producer.count(city_pack) == len(EMITTER_SITES)
        restored = producer.replace(begin, "").replace(city_uv, "").replace(city_pack, "").replace(publish_window, "")
        assert restored[restored.index("void " + PRODUCER):] == native[PRODUCER]
        transform(native[PRODUCER] + "\n" + native[PRODUCER], PRODUCER, True)
        transform(producer, PRODUCER, True)
        entry = "    PUSH32(esp, ebp);\n    ebp = esp + -116;"
        transform(native[PRODUCER].replace(entry, "    PUSH32(esp, ebp);\n    ebp = esp + -120;"), PRODUCER, True)
        transform(native[PRODUCER].replace(entry, entry + "\n" + entry), PRODUCER, True)
        viewport = "loc_00116580: ;\n    eax = MEM32(0x3C568C);"
        transform(native[PRODUCER].replace(viewport, "loc_00116580_BAD: ;\n    eax = MEM32(0x3C568C);"), PRODUCER, True)
        transform(native[PRODUCER].replace(viewport, viewport + "\n" + viewport), PRODUCER, True)
        for site in EMITTER_SITES:
            emitter = f"    PUSH32(esp, 0x{site}u); RECOMP_ABI_CALL(0x00069807u, sub_00069807); /* call 0x00069807 */"
            assert city_uv + emitter in producer
            assert emitter + "\n" + city_pack in producer
            transform(native[PRODUCER].replace(emitter, "    /* missing vertex */"), PRODUCER, True)
            transform(native[PRODUCER].replace(emitter, emitter + "\n" + emitter), PRODUCER, True)
        unrelated = "\nvoid unrelated(void)\n{\nloc_00116535: ;\n    PUSH32(esp, ebp);\n}\n"
        assert transform(native[PRODUCER] + unrelated, PRODUCER).endswith(unrelated)
        route_begin = "    uint32_t mm3_route_tag = mm3_race_route_quad_tagged(ecx, MEM32(esp + 4));\n"
        route_pack = "    if (mm3_route_tag) mm3_race_route_tag_packed(esi);\n"
        composed_bodies = []
        for order in (("map", "race"), ("race", "map")):
            composed = transform(native[PRODUCER], PRODUCER, order=order)
            assert transform(native[PRODUCER].replace("\n", "\r\n"), PRODUCER, order=order) == composed
            owner_body = composed[composed.index("void " + PRODUCER):]
            assert owner_body.count(begin) == owner_body.count(route_begin) == 1
            assert owner_body.count(publish_window) == 1
            assert owner_body.count(city_uv) == owner_body.count(city_pack) == 9 and owner_body.count(route_pack) == 3
            restored = owner_body.replace(begin, "").replace(route_begin, "").replace(city_uv, "").replace(city_pack, "").replace(route_pack, "").replace(publish_window, "")
            assert restored == native[PRODUCER]
            assert route_begin + "    PUSH32(esp, ebp);\n" + begin in owner_body
            composed_bodies.append(owner_body)
        assert composed_bodies[0] == composed_bodies[1]
        body = "\n".join(native[name] for name in MATH)
        for name, title in zip(SEAMS, ("reset", "update", "draw", "marker", "pool")):
            body += "\n" + native[name].replace("void " + name, "void retail_" + title, 1)
            body += "\n" + modified[name].replace("void " + name, "void zoom_" + title, 1)
        # Execute the literal native first textured vertex producer, including
        # resource UV scales/offsets, ARGB packing, independent mask UVs, the
        # square seam before the return push, and the real emitter argument ABI.
        prelude = producer[producer.index("{\n") + 2:producer.index("loc_00116535: ;")]
        begin_code = producer[producer.index("loc_00116535: ;"):producer.index("    ebp = esp + -116;")]
        begin_code += "    g_eax = mm3_square_map;\n    POP32(esp, ebp);\n"
        body += "\nvoid city_begin(void)\n{\n" + prelude + begin_code + producer[producer.rindex("    #undef fp_push"):producer.rindex("}")] + "}\n"
        # Execute native renderer dimensions -> half-viewport conversion at
        # the actual publication boundary. The renderer virtual lookup is
        # captured by supplying its returned dimensions pointer in eax.
        viewport_code = producer[producer.index("loc_00116554: ;"):producer.index("loc_00116580: ;")]
        viewport_code = viewport_code.replace("    if (CMP_EQ(_fa, _fb)) goto loc_00116ED6; /* je: equal / zero */", "    assert(ebx);")
        cleanup = producer[producer.rindex("    #undef fp_push"):producer.rindex("}")]
        body += "\nvoid city_window(void)\n{\n" + prelude + "    uint32_t mm3_square_map = 1;\n" + viewport_code + publish_window + "    esp += 4;\n" + cleanup + "}\n"
        first_vertex = producer[producer.index("loc_0011692B: ;"):producer.index("\nloc_001169A2: ;")]
        cleanup = producer[producer.rindex("    #undef fp_push"):producer.rindex("}")]
        body += "\nvoid city_first_vertex(void)\n{\n" + prelude + "    uint32_t mm3_square_map = 1;\n" + first_vertex + cleanup + "}\n"
        declarations = "\n".join("void " + name + "(void);" for name in MATH)
        (folder / "native_map_zoom.inc").write_text(declarations + "\n" + body + constants(body))
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        production = (ROOT / "src/mm3_map_zoom.c").read_text()
        (folder / "production_map_zoom.c").write_text(production)
        executable = folder / "contract.exe"
        command = ["cl", "/nologo", "/W3", "/WX", "/wd4102", "/Od", "/std:c11",
                   "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                   "/I" + str(ROOT / "tools/xboxrecomp/src/video"),
                   str(ROOT / "tests/mm3_map_zoom_contract.c"),
                   "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable)]
        subprocess.run(command, check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder)
        if "--check-mutants" in sys.argv:
            mutants = {
                "improved map never requests the native larger UI pool": production.replace(
                    "MEM8(0x3C5694u) = 1;", "MEM8(0x3C5694u) = 0;"),
                "UI pool reallocates on every frame": production.replace(
                    "mm3_minimap_improved() && MEM32(0x3A0EC0u) < 160000u", "mm3_minimap_improved()"),
                "Stock also requests a larger UI pool": production.replace(
                    "mm3_minimap_improved() && MEM32(0x3A0EC0u) < 160000u", "MEM32(0x3A0EC0u) < 160000u"),
                "missing common map scale": production.replace(
                    "MEMF(node + 0x50) = s_map_zoom[slot].baseline * (1.0f + MAP_SPEED_SPAN) /\n            (NV2A_MINIMAP_SCALE * s_map_zoom[slot].span);",
                    "MEMF(node + 0x50) = scale;"),
                "Ctrl restores native world scale and hides visible checkpoints": production.replace(
                    "MEMF(node + 0x50) = s_map_zoom[slot].baseline * (1.0f + MAP_SPEED_SPAN) /\n            (NV2A_MINIMAP_SCALE * s_map_zoom[slot].span);",
                    "MEMF(node + 0x50) = s_map_zoom[slot].baseline * (1.0f + MAP_SPEED_SPAN) / "
                    "((NV2A_MINIMAP_SCALE + (1 - NV2A_MINIMAP_SCALE) * map_expansion(node)) * "
                    "(s_map_zoom[slot].span + (1 - s_map_zoom[slot].span) * map_expansion(node)));"),
                "Ctrl drops speed zoom": production.replace(
                    "(NV2A_MINIMAP_SCALE * s_map_zoom[slot].span);",
                    "(NV2A_MINIMAP_SCALE * (s_map_zoom[slot].span + "
                    "(1 - s_map_zoom[slot].span) * map_expansion(node)));"),
                "immediate zoom jump": production.replace(
                    "blend = 1.0f - expf(-dt / 0.6f);", "blend = 1.0f;"),
                "missing speed target": production.replace(
                    "speed = fabsf((float)SMEM32(player_record + 4));", "speed = 0.0f;"),
                "camera tilt omits speed publication": production.replace(
                    "map_motion_publish(slot, s_square_map.window_cx + half_width,\n        s_square_map.window_cy + half_height);", "map_motion_store(slot, 0);"),
                "camera speed ignores expanded clip window": production.replace(
                    "s_square_map.window_cx = MEMF(clip) + width * .5f;",
                    "s_square_map.window_cx = MEMF(node + 0x94);"),
                "viewport X offset omitted": production.replace(
                    "s_square_map.window_cx + half_width,", "s_square_map.window_cx,"),
                "viewport Y offset omitted": production.replace(
                    "s_square_map.window_cy + half_height);", "s_square_map.window_cy);"),
                "original local-space publication restored": production.replace(
                    "s_square_map.window_cx + half_width,\n        s_square_map.window_cy + half_height);",
                    "s_square_map.window_cx, s_square_map.window_cy);"),
                "routing reads speed instead of car heading": production.replace(
                    "player_record ? MEM32(player_record) : 0x7FC00000u",
                    "player_record ? MEM32(player_record + 4) : 0x7FC00000u"),
                "checkpoint zoom cancels speed zoom": production.replace(
                    "if (!s_map_zoom[slot].baseline) s_map_zoom[slot].baseline = scale;",
                    "s_map_zoom[slot].baseline = scale;"),
                "square city turn has wrong sign": production.replace(
                    "s_square_map.s = sinf(angle);", "s_square_map.s = -sinf(angle);"),
                "square map corrupts native opacity/color": production.replace(
                    "void mm3_map_square_geometry(uint32_t vertices, uint32_t center, float angle)\n{",
                    "void mm3_map_square_geometry(uint32_t vertices, uint32_t center, float angle)\n{\n    MEMF(vertices + 0x20) = 0;"),
                "city rotation corrupts independent mask UV": production.replace(
                    "MEMF(arguments + 12) = u0 +", "MEMF(arguments + 20) = 0; MEMF(arguments + 12) = u0 +"),
                "markers retain circular clipping": production.replace(
                    "return fmaxf(fabsf(c * x + s * y), fabsf(c * y - s * x) * rx / ry);",
                    "return hypotf(x, y);"),
            }
            for label, mutant in mutants.items():
                assert mutant != production, label
                (folder / "production_map_zoom.c").write_text(mutant)
                subprocess.run(command, check=True, cwd=folder, capture_output=True)
                result = subprocess.run([str(executable)], cwd=folder, capture_output=True, text=True)
                assert result.returncode, "Regression failed to reject " + label
                print("PASS negative control:", label)
            (folder / "production_map_zoom.c").write_text(production)
            for label, replacement, original in (("missing square geometry", SQUARE_REPLACEMENT, SQUARE_ORIGINAL),
                                                 ("retained circular border", BORDER_REPLACEMENT, BORDER_ORIGINAL),
                                                 ("unscaled map compass sprite", COMPASS_REPLACEMENT, COMPASS_ORIGINAL),
                                                 ("missing native UI capacity seam", calls[SEAMS[4]], ""),
                                                 ("missing native viewport publication seam", publish_window, ""),
                                                 ("missing native city UV seam", city_uv, "")):
                assert body.count(replacement) == 1
                (folder / "native_map_zoom.inc").write_text(declarations + "\n" + body.replace(replacement, original) + constants(body))
                subprocess.run(command, check=True, cwd=folder, capture_output=True)
                result = subprocess.run([str(executable)], cwd=folder, capture_output=True, text=True)
                assert result.returncode, "Regression failed to reject " + label
                print("PASS negative control:", label)
    print("PASS map zoom/square seams: precise native calls, map/race composition in both orders, LF/CRLF and invalid anchors rejected")


if __name__ == "__main__":
    main()
