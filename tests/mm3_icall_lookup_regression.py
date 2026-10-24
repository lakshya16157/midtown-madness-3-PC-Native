"""Indirect-call lookup (src/mm3_runtime_entries.c): one resolved table shared
by all threads gives the manual entry if there is one, else the generated
function, exactly as RECOMP_ICALL's two lookups in order, and asks each only
once per address."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

CONTRACT = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef void (*fn_t)(void);
uint32_t g_xbox_code_lo, g_xbox_code_hi;
static unsigned manual_calls, generated_calls;
void sub_00083A6C(void) {}
void mm3_lua_ai_all_cars(void) {}
static void manual_entry(void) {}
static void generated_entry(void) {}
static void kernel_manual(void) {}
fn_t mm3_lookup_memory_tail(uint32_t va) { (void)va; return 0; }
fn_t mm3_lookup_recovered_entry(uint32_t va) { (void)va; return 0; }
fn_t recomp_lookup_manual_base(uint32_t va)
{
    ++manual_calls;
    if (va == 0x00020000u) return manual_entry;
    if (va == 0xFE000010u) return kernel_manual;
    return 0;
}
fn_t recomp_lookup(uint32_t va)
{
    ++generated_calls;
    return va == 0x00020000u || va == 0x00030000u ? generated_entry : 0;
}
fn_t recomp_lookup_manual(uint32_t va);

int main(void)
{
    unsigned i;
    /* Before the code range is known: the manual answer alone, as before. */
    assert(recomp_lookup_manual(0x00030000u) == 0 && generated_calls == 0);
    g_xbox_code_lo = 0x00012000u; g_xbox_code_hi = 0x00480000u;
    for (i = 0; i < 3; ++i) {
        assert(recomp_lookup_manual(0x00020000u) == manual_entry);       /* manual wins */
        assert(recomp_lookup_manual(0x00030000u) == generated_entry);    /* else generated */
        assert(recomp_lookup_manual(0x00040000u) == 0);                  /* neither */
        assert(recomp_lookup_manual(0x00083A6Cu) == sub_00083A6C);
        assert(recomp_lookup_manual(0xFE000010u) == kernel_manual);      /* kernel thunk */
    }
    /* Found addresses are asked once; misses are asked again (never cached). */
    assert(generated_calls == 1 + 3);
    printf("PASS icall lookup: manual first, then generated, cached once per address\n");
    return 0;
}
"""


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-icall-lookup-") as temporary:
        folder = Path(temporary)
        (folder / "contract.c").write_text(CONTRACT)
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/I" + str(ROOT / "src"), str(folder / "contract.c"),
                        str(ROOT / "src/mm3_runtime_entries.c"), "/Fo:" + str(folder) + "\\",
                        "/Fe:" + str(executable)], check=True, cwd=folder, stdout=subprocess.DEVNULL)
        subprocess.run([str(executable)], check=True, cwd=folder)


if __name__ == "__main__":
    main()
