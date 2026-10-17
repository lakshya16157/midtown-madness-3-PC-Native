"""Exercise the native world-label seam and actual packed UI writer offline."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def generated_function(name):
    for path in (ROOT / "src/recomp/gen").glob("*.c"):
        match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}",
                          path.read_text(), re.M | re.S)
        if match:
            return match.group()
    raise ValueError(f"Missing generated {name}")


def main():
    cmake = ROOT / "tools/venv/Lib/site-packages/cmake/data/bin/cmake.exe"
    cmake = str(cmake) if cmake.exists() else shutil.which("cmake")
    if not shutil.which("cl") or not cmake:
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe and CMake are available.")
    owner = generated_function("sub_001093BB")
    packer = generated_function("sub_00069807")
    conversion = generated_function("sub_001F0399")
    # New native UI emission paths require review before we can safely reuse
    # their secondary UVs. Compare the actual call sites, including the
    # overlapping generated continuation, with the producer's explicit list.
    native_sites = set()
    call_pattern = (r"PUSH32\(esp, (0x[0-9A-F]+)u\); "
                    r"RECOMP_ABI_CALL\(0x00069807u, sub_00069807\)")
    for path in (ROOT / "src/recomp/gen").glob("*.c"):
        native_sites.update(re.findall(call_pattern, path.read_text()))
    producer = (ROOT / "src/mm3_name_markers.c").read_text()
    reviewed_sites = set(re.findall(r"case (0x[0-9A-F]+)u:", producer))
    assert native_sites == reviewed_sites, "Packed UI call sites changed; review UV1 ownership"
    with tempfile.TemporaryDirectory(prefix="mm3-name-markers-") as folder:
        folder = Path(folder)
        source, output = folder / "original.c", folder / "modified.c"
        script = folder / "transform.cmake"
        script.write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            f'include("{(ROOT / "scripts/mm3_name_marker_seam.cmake").as_posix()}")\n'
            'file(READ "${INPUT}" content)\n'
            'mm3_add_name_marker_seam(content ${OWNER})\n'
            'file(WRITE "${OUTPUT}" "${content}")\n')

        def transform(text, owner_name="sub_001093BB", reject=False):
            source.write_bytes(text.encode())
            result = subprocess.run([cmake, f"-DINPUT={source}", f"-DOUTPUT={output}",
                                     f"-DOWNER={owner_name}", "-P", str(script)],
                                    capture_output=True, text=True)
            if reject:
                assert result.returncode and "MM3 name markers" in result.stderr
                return None
            if result.returncode:
                raise RuntimeError(result.stderr)
            return output.read_text()

        modified = transform(owner)
        insertion = "    mm3_name_marker_projected(MEM8(eax + 0x21));\n"
        declaration = "#include <stdint.h>\nextern void mm3_name_marker_projected(uint32_t);\n"
        assert modified.count(insertion) == 1
        assert modified.removeprefix(declaration).replace(insertion, "", 1) == owner
        assert transform(owner.replace("\n", "\r\n")) == modified
        renamed = owner.replace("void sub_001093BB(void)", "void sub_001093BB_original(void)")
        assert transform(renamed, "sub_001093BB_original") == modified.replace(
            "void sub_001093BB(void)", "void sub_001093BB_original(void)")
        transform(owner.replace("loc_001093FF: ;", "loc_001093FE: ;"), reject=True)
        transform(owner + "\nloc_001093FF: ;\n", reject=True)
        transform(owner + "\n" + owner, reject=True)
        transform(modified, reject=True)
        (folder / "production_name_markers.inc").write_text(
            conversion + "\n" + packer.replace("void sub_00069807(void)",
                                              "void sub_00069807_original(void)"))
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/WX", "/wd4102", "/Od", "/std:c11",
                        "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(folder),
                        "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_name_markers_contract.c"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable)],
                       check=True, cwd=folder)
        subprocess.run([str(executable)], check=True)
    print("PASS: exact label-entry seam, definition-only rename, LF/CRLF and rejected invalid anchors")


if __name__ == "__main__":
    main()
