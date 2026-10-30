"""The police car creation seam only adds its notes to sub_0015070A and, inside
its factory call, to the factory, car init and model build."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OWNER = "sub_0015070A"
CREATED = ("loc_001507D3: ; { extern void mm3_police_created(uint32_t actor, uint32_t name); "
           "mm3_police_created(eax, MEM32(ebp + 8)); }")
BEGIN = " { extern void mm3_police_factory_begin(void); mm3_police_factory_begin(); }"
NOTE = re.compile(r" \{ extern void mm3_police_note(?:_path)?\((?:int where, uint32_t value|uint32_t text)\); "
                  r"mm3_police_note(?:_path)?\([^\n]*?\); \}")
# Owner: (notes in order, paths, a label it needs)
FACTORY = {
    "sub_0013BB36": (["0", "1", "2", "3"], 0, "loc_0013BBD0"),
    "sub_00220761": (["4", "5", "6"], 1, "loc_00220A3C"),
    "sub_002202EE": (["7", "8", "9", "10"], 2, "loc_0022037E"),
}


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
    with tempfile.TemporaryDirectory(prefix="mm3-police-create-") as temporary:
        folder = Path(temporary)
        source, output, script = (folder / name for name in ("original.c", "modified.c", "transform.cmake"))
        script.write_text(
            f'include("{(ROOT / "scripts/mm3_police_create_seam.cmake").as_posix()}")\n'
            'file(READ "${INPUT}" content)\n'
            f'if(OWNER STREQUAL "{OWNER}")\n'
            '    mm3_add_police_create_steps(content)\n'
            'else()\n'
            '    mm3_add_police_factory_notes(content ${OWNER})\n'
            'endif()\n'
            'file(WRITE "${OUTPUT}" "${content}")\n')

        def transform(owner, text, reject=False):
            source.write_bytes(text.encode())
            result = subprocess.run(["cmake", f"-DOWNER={owner}", f"-DINPUT={source}", f"-DOUTPUT={output}",
                                     "-P", str(script)], capture_output=True, text=True)
            if reject:
                assert result.returncode and "MM3 police create" in result.stderr, result.stderr
                return None
            assert not result.returncode, result.stderr
            return output.read_text()

        # Car creation: six steps, the factory start and the result.
        original = native(OWNER)
        modified = transform(OWNER, original)
        assert transform(OWNER, original.replace("\n", "\r\n")) == modified
        steps = re.findall(r"mm3_police_step\((\d)\); \}", modified)
        assert steps == ["0", "1", "2", "3", "4", "5"], steps
        assert modified.count(CREATED) == 1 and modified.count(BEGIN) == 1
        assert "loc_0015079D: ;" + BEGIN in modified
        restored = re.sub(r" \{ extern void mm3_police_step\(int step\); mm3_police_step\(\d\); \}", "", modified)
        assert restored.replace(CREATED, "loc_001507D3: ;").replace(BEGIN, "") == original
        unrelated = "\n" + native("sub_001507DA")
        assert transform(OWNER, original + unrelated) == modified + unrelated
        transform(OWNER, modified, True)
        transform(OWNER, original + "\n" + original, True)
        transform(OWNER, original.replace("loc_0015077C: ;", "loc_0015077D: ;"), True)
        transform(OWNER, original.replace("loc_0015079D: ;", "loc_0015079E: ;"), True)
        transform(OWNER, original.replace("loc_001507D3: ;\n    POP32(esp, edi);",
                                          "loc_001507D3: ;\n    POP32(esp, esi);"), True)

        # Inside the factory: each owner's notes in order, nothing else changed.
        for owner, (notes, paths, label) in FACTORY.items():
            original = native(owner)
            modified = transform(owner, original)
            assert transform(owner, original.replace("\n", "\r\n")) == modified
            found = re.findall(r"mm3_police_note\((\d+), ", modified)
            assert found == notes, (owner, found)
            assert modified.count("); mm3_police_note_path(") == paths, owner
            assert NOTE.sub("", modified) == original, owner
            assert transform(owner, original + unrelated) == modified + unrelated
            transform(owner, modified, True)
            transform(owner, original + "\n" + original, True)
            transform(owner, original.replace(label + ": ;", label + "X: ;"), True)
        transform("sub_001507DA", native("sub_001507DA"), True)
    print("PASS police create seam: six step notes, the factory start and the result report in sub_0015070A; "
          "factory, car init and model build notes in their owners only; LF/CRLF, changed labels, other "
          "functions and double application rejected")


if __name__ == "__main__":
    main()
