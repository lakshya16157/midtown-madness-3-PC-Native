"""The "Opponents use full-detail cars" seam (MM3_AI_FULL_DETAIL) adds one jump to
the car init sub_00220761: past the "_lod1" rename of a computer-driven car, to
where the player's car goes. It composes with the police factory notes."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OWNER = "sub_00220761"
JUMP = "loc_0022085A: ; if (mm3_ai_full_detail()) goto loc_00220885;"
EXTERN = "extern int mm3_ai_full_detail(void);\n"


def native(name):
    owners = []
    for path in (ROOT / "src/recomp/gen").glob("recomp_*.c"):
        match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}", path.read_text(), re.M | re.S)
        if match:
            owners.append(match.group())
    assert len(owners) == 1, name
    return owners[0]


def main():
    if not shutil.which("cmake"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-ai-detail-") as temporary:
        folder = Path(temporary)
        source, output, script = (folder / name for name in ("original.c", "modified.c", "transform.cmake"))
        script.write_text(
            f'include("{(ROOT / "scripts/mm3_ai_detail_seam.cmake").as_posix()}")\n'
            f'include("{(ROOT / "scripts/mm3_police_create_seam.cmake").as_posix()}")\n'
            'file(READ "${INPUT}" content)\n'
            'mm3_add_ai_full_detail(content)\n'
            'if(POLICE)\n'
            f'    mm3_add_police_factory_notes(content {OWNER})\n'
            'endif()\n'
            'file(WRITE "${OUTPUT}" "${content}")\n')

        def transform(text, reject=False, police=False):
            source.write_bytes(text.encode())
            result = subprocess.run(["cmake", f"-DINPUT={source}", f"-DOUTPUT={output}",
                                     f"-DPOLICE={'1' if police else '0'}", "-P", str(script)],
                                    capture_output=True, text=True)
            if reject:
                assert result.returncode and "MM3 AI detail" in result.stderr, result.stderr
                return None
            assert not result.returncode, result.stderr
            return output.read_text()   # CMake writes CRLF on Windows; compare lines

        original = native(OWNER)
        # The jump the type check skips to stays the native one: types 2/3 rename, then reach it.
        assert re.search(r"if \(CMP_EQ\(_fa, _fb\)\) goto loc_0022085A;.*?"
                         r"if \(CMP_NE\(_fa, _fb\)\) goto loc_00220885;", original, re.S)
        modified = transform(original)
        assert modified.startswith(EXTERN) and modified.count(JUMP) == 1
        assert modified[len(EXTERN):].replace(JUMP, "loc_0022085A: ;") == original
        assert transform(original.replace("\n", "\r\n")) == modified
        unrelated = "\n" + native("sub_002202EE")
        assert transform(original + unrelated) == modified + unrelated
        # With the police factory notes on the same body, both stay.
        both = transform(original, police=True)
        assert both.count(JUMP) == 1 and re.findall(r"mm3_police_note\((\d+), ", both) == ["4", "5", "6"]
        transform(modified[len(EXTERN):], True)                     # applied twice
        transform(original + "\n" + original, True)                  # two owners
        transform(original.replace("loc_0022085A: ;", "loc_0022085B: ;"), True)
        transform(original.replace("PUSH32(esp, 0x3B59AC);", "PUSH32(esp, 0x3B59AD);"), True)
        transform(original.replace("loc_00220885: ;", "loc_00220886: ;"), True)
        transform(original.replace("eax = MEM32(ebx + 0x488);", "eax = MEM32(ebx + 0x48C);"), True)
        transform(unrelated, True)
    print("PASS AI detail seam: one jump past the _lod1 rename of sub_00220761 to the player's path; "
          "LF/CRLF, police notes alongside, double application, other functions and changed code rejected")


if __name__ == "__main__":
    main()
