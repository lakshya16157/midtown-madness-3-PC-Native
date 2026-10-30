"""Run the native best-times writer and loader with more than the Xbox's 80 race tables."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WRITER, LOADER = "sub_001E8A7A", "sub_001E9060"
WRITER_GATE = ("    if ((_fa == 0)) goto loc_001E8BD1; /* je: equal / zero */",
               "    { extern int mm3_score_tables_unpadded(uint32_t tables); if ((_fa == 0) || "
               "mm3_score_tables_unpadded(MEM32(edi + 0x2C))) goto loc_001E8BD1; } /* Pad to 80 tables only from fewer. */")
SIZE_GATE = ("    if (CMP_EQ(_fa, _fb)) goto loc_001E9204; /* je: equal / zero */",
             "    { extern uint32_t mm3_score_file_tables(uint32_t data, uint32_t size, uint32_t expected, "
             "uint32_t scores); if (mm3_score_file_tables(MEM32(ebp + 0x14), eax, esi, MEM32(ebp + 0x7C))) "
             "goto loc_001E9204; } /* The Xbox's size, or more than 80 tables. */")
COUNT_GATE = ("    MEM32(ebp + 0x64) = 0x50;",
              "    { extern uint32_t mm3_score_file_table_count(void); MEM32(ebp + 0x64) = "
              "mm3_score_file_table_count(); } /* The Xbox's 0x50, or the file's count. */")
LOADER_PREAMBLE = """{
    uint32_t ebp = g_ebp;
    int _flags = 0;
    uint32_t _fa = 0, _fb = 0;
    int32_t _fas = 0, _fbs = 0;
    (void)_flags; (void)_fa; (void)_fb; (void)_fas; (void)_fbs;
    #define fp_push(v) do { double _fp_value = (v); \\
        g_fp_top = (g_fp_top + 7u) & 7u; \\
        g_fp_stack[g_fp_top] = _fp_value; } while (0)
    #define fp_pop() (g_fp_top = (g_fp_top + 1u) & 7u)
    #define fp_top() g_fp_stack[g_fp_top]
    #define fp_st(i) g_fp_stack[(g_fp_top + (i)) & 7u]
    #define fp_st1() fp_st(1)
"""


def native(name):
    owners = []
    for path in (ROOT / "src/recomp/gen").glob("recomp_*.c"):
        match = re.search(r"^void " + name + r"\(void\)\n\{.*?^\}", path.read_text(), re.M | re.S)
        if match:
            owners.append(match.group())
    assert len(owners) == 1, name
    return owners[0]


def loader_slice(body, name):
    """From the size check's setup (loc_001E91C6) to the return, as its own function."""
    tail = body[body.index("loc_001E91C6: ;"):]
    defined = set(re.findall(r"^(loc_[0-9A-F]+): ;", tail, re.M))
    assert set(re.findall(r"goto (loc_[0-9A-F]+);", tail)) <= defined
    return f"void {name}(void)\n" + LOADER_PREAMBLE + tail


def main():
    if not shutil.which("cl") or not shutil.which("cmake"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    writer, loader = native(WRITER), native(LOADER)
    with tempfile.TemporaryDirectory(prefix="mm3-score-tables-") as temporary:
        folder = Path(temporary)
        source, output, script = (folder / name for name in ("original.c", "modified.c", "transform.cmake"))
        script.write_text(
            f'include("{(ROOT / "scripts/mm3_score_tables_seam.cmake").as_posix()}")\n'
            'file(READ "${INPUT}" content)\n'
            'mm3_fix_score_tables(content ${OWNER})\n'
            'file(WRITE "${OUTPUT}" "${content}")\n')

        def transform(text, owner, reject=False):
            source.write_bytes(text.encode())
            result = subprocess.run(["cmake", f"-DINPUT={source}", f"-DOUTPUT={output}", f"-DOWNER={owner}",
                                     "-P", str(script)], capture_output=True, text=True)
            if reject:
                assert result.returncode and "MM3 score tables" in result.stderr, result.stderr
                return None
            assert not result.returncode, result.stderr
            return output.read_text()

        new_writer, new_loader = transform(writer, WRITER), transform(loader, LOADER)
        assert transform(writer.replace("\n", "\r\n"), WRITER) == new_writer
        assert transform(loader.replace("\n", "\r\n"), LOADER) == new_loader
        assert new_writer.replace(WRITER_GATE[1], WRITER_GATE[0]) == writer
        assert new_loader.replace(SIZE_GATE[1], SIZE_GATE[0]).replace(COUNT_GATE[1], COUNT_GATE[0]) == loader
        assert new_writer.count(WRITER_GATE[1]) == 1
        assert new_loader.count(SIZE_GATE[1]) == 1 and new_loader.count(COUNT_GATE[1]) == 1
        unrelated = "\n" + native("sub_001E8BDF")
        assert transform(writer + unrelated, WRITER) == new_writer + unrelated
        transform(new_writer, WRITER, True)
        transform(new_loader, LOADER, True)
        transform(writer + "\n" + writer, WRITER, True)
        for boundary in ("PUSH32(esp, 0x50);", "eax = eax - MEM32(edi + 0x2C);"):
            transform(writer.replace(boundary, boundary + "BAD"), WRITER, True)
        transform(loader.replace("MEM32(ebp + 0x64) = 0x50;", "MEM32(ebp + 0x64) = 0x51;"), LOADER, True)
        transform(loader.replace("/* cmp eax, esi (32-bit) */", "/* cmp eax, esi (32-bit) */BAD"), LOADER, True)
        transform(writer, "sub_001E8BDF", True)

        code = "\n".join([
            writer.replace(f"void {WRITER}(void)", "void original_writer(void)"),
            new_writer,
            native("sub_001E8BDF"),
            native("sub_001E4FC4"),
            native("sub_001E50F7"),
            loader_slice(loader, "original_loader"),
            loader_slice(new_loader, "modified_loader"),
        ])
        (folder / "native_scores.inc").write_text(code)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W3", "/WX", "/wd4102", "/wd4101", "/Od", "/std:c11",
                        "/I" + str(folder), "/I" + str(ROOT / "src"), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_score_tables_contract.c"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable)],
                       check=True, cwd=folder)
        subprocess.run([str(executable)], check=True, cwd=folder)
    print("PASS score tables seam: 0-80 tables byte-identical to the Xbox and loaded the same; "
          "81-256 tables saved and loaded in full (the Xbox writer runs away, its loader rejects them); "
          "bad sizes rejected; LF/CRLF, anchors and double application checked")


if __name__ == "__main__":
    main()
