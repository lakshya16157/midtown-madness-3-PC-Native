"""Check ordinary prop/physics hooks avoid OS reads; benchmark with --benchmark.

Run after scripts/enter-env.ps1. --baseline SOURCE optionally measures the same
production hooks from an earlier source file. Indirect metadata remains guarded
with real ReadProcessMemory calls, including reserved, uncommitted guest memory.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t reads;
static BOOL counted_read(HANDLE process, const void *address, void *out, SIZE_T size, SIZE_T *copied)
{
    ++reads;
    return ReadProcessMemory(process,address,out,size,copied);
}
#define ReadProcessMemory counted_read
#define MM3_TREE_TEST_HELPERS
static void quiet_log(const char *format,...){(void)format;}
#define MM3_TREE_LOG quiet_log
#define MM3_TREE_FLUSH() ((void)0)
#include "under_test.c"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
enum { STACK=0x100000, CAR=0x110000, SIM=0x120000, BODY=0x130000, RECORD=0x140000 };
static unsigned draw_calls,contact_calls;
void sub_0016CABD(void){assert(0);}
void sub_000E1F49_original(void){assert(0);}
void sub_0016AEDB_original(void){++draw_calls;g_eax=MEM32(g_esp+4);g_esp+=8;}
void sub_001D137B_original(void){++contact_calls;++MEM32(0x3C0344);g_eax=0x2468ACE0;g_esp+=8;}
static void draw(void){g_esp=STACK;sub_0016AEDB();}
static void contact(void){g_esp=STACK;sub_001D137B();}
static void measure(const char *label,void (*run)(void),unsigned iterations)
{
    LARGE_INTEGER frequency,start,end;
    QueryPerformanceFrequency(&frequency);
    reads=0;
    QueryPerformanceCounter(&start);
    for(unsigned i=0;i<iterations;++i)run();
    QueryPerformanceCounter(&end);
    printf("%s calls=%u RPM_per_call=%.0f mean_us=%.4f\n",label,iterations,
           (double)reads/iterations,1000000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart/iterations);
}
int main(int argc,char **argv)
{
    assert(argc==3);
    int baseline=atoi(argv[1]),benchmark=atoi(argv[2]);
    unsigned char *ram=VirtualAlloc(NULL,0x800000,MEM_RESERVE,PAGE_NOACCESS);
    assert(ram&&VirtualAlloc(ram,0x400000,MEM_COMMIT,PAGE_READWRITE)==ram);
    g_xbox_mem_offset=(ptrdiff_t)ram;
    s_tree_enabled=0;
    MEM32(STACK)=0x0016D123;MEM32(STACK+4)=CAR;
    MEM32(CAR+0x3C)=SIM;MEM32(SIM+4)=BODY;
    g_ecx=0x11223344;g_edx=0x55667788;g_ebx=0x99AABBCC;
    reads=0;draw();
    assert(draw_calls==1&&g_esp==STACK+8&&g_eax==CAR);
    assert(g_ecx==0x11223344&&g_edx==0x55667788&&g_ebx==0x99AABBCC);
    assert(reads==(baseline?1u:0u));
    reads=0;contact();
    assert(contact_calls==1&&g_esp==STACK+8&&g_eax==0x2468ACE0&&MEM32(0x3C0344)==1);
    assert(g_ecx==0x11223344&&g_edx==0x55667788&&g_ebx==0x99AABBCC);
    assert(reads==(baseline?4u:0u));
    /* The detached-draw path must still reject an unreadable model pointer. */
    MEM32(STACK)=0x00167D94;MEM32(STACK+4)=0x600000;
    reads=0;draw();assert(reads>0&&g_eax==0x600000&&!mm3_tree_fall_drawing());
    /* Every collision contact reaches the classifier. Fields inside committed
     * guest RAM are copied directly; unreadable records/metadata keep guarded
     * reads and are rejected. */
    reads=0;assert(!mm3_tree_use_solid_contact(0x600000,0)&&reads>0);
    MEM16(RECORD+0x100)=2;MEM32(RECORD+0x108)=0x600000;
    reads=0;assert(!mm3_tree_use_solid_contact(RECORD,0)&&reads==1);
    /* A readable non-tree prop (empty event list, unnamed sound) costs no OS read. */
    MEM32(RECORD+0x108)=0x150000;memset((void *)XBOX_PTR(0x150000),0,52);
    reads=0;assert(!mm3_tree_use_solid_contact(RECORD,30.0f)&&reads==0);
    /* An ordinary contact cannot inspect even a deliberately unreadable car. */
    MEM32(STACK)=0x0016D123;MEM32(STACK+4)=0x600000;
    reads=0;contact();assert(!baseline?reads==0:reads>0);
    MEM32(STACK+4)=CAR;
    if(benchmark){measure("ordinary_prop_draw",draw,100000);measure("ordinary_contact",contact,100000);}
    VirtualFree(ram,0,MEM_RELEASE);
    puts("PASS: native draw/contact state, ordinary-hook OS read counts, guarded indirect pointers");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    production = args.baseline or ROOT / "src/mm3_tree_diagnostics.c"
    with tempfile.TemporaryDirectory(prefix="mm3-tree-hotpath-") as temporary:
        folder = Path(temporary)
        assert folder.resolve().is_relative_to(Path(tempfile.gettempdir()).resolve())
        (folder / "under_test.c").write_text(production.read_text())
        source, executable = folder / "contract.c", folder / "contract.exe"
        source.write_text(SOURCE)
        subprocess.run(["cl", "/nologo", "/O2", "/W4", "/WX", "/std:c11",
                        "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(ROOT / "src"),
                        "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        "/Fo:" + str(folder / "contract.obj"), "/Fe:" + str(executable),
                        str(source)], cwd=folder, check=True)
        subprocess.run([str(executable), str(int(bool(args.baseline))), str(int(args.benchmark))],
                       cwd=folder, check=True)


if __name__ == "__main__":
    main()
