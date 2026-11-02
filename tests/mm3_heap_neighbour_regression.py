"""RtlFreeHeap must not merge with a neighbour whose header was overwritten (src/mm3_heap_guard.c, sub_000860AA).

Both stress sessions on 2026-11-02 crashed in sub_00084709 (the coalescing under RtlFreeHeap) writing through
float bit patterns taken for free-list links (0x3DCCCCD1 = 0.1f + 4, 0x40E79CB3). The contract runs the guard
on a fake heap; the replay runs the real generated sub_00084709 on such a heap, crashing without the guard
and merging correctly with it; and the wrapper is pinned to call the guard around the generated free."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import cc

ROOT = Path(__file__).resolve().parents[1]

REPLAY = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "mm3_recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo=0x12000,g_xbox_code_hi=0x260000;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE],g_icall_trace_idx;
volatile uint64_t g_icall_count;
recomp_func_t recomp_lookup(uint32_t va){(void)va;return 0;}
recomp_func_t recomp_lookup_kernel(uint32_t va){(void)va;return 0;}
recomp_func_t recomp_lookup_manual(uint32_t va){(void)va;return 0;}
void recomp_icall_fail_log(uint32_t va){(void)va;exit(99);}
void recomp_icall_not_code_log(uint32_t va){(void)va;exit(99);}
int mm3_heap_damaged_neighbour(uint32_t pointer);
void sub_00084709(void);
%(body)s
#include "%(guard)s"
#define HEAP 0x200000u
#define HEAD (HEAP+0x180u)
#define P 0x221000u
#define B 0x221040u
#define N 0x221080u
#define Q 0x2210C0u
static void chunk(uint32_t at,unsigned size,unsigned before,unsigned flags){
 memset((void*)XBOX_PTR(at),0,16);MEM16(at)=(uint16_t)size;MEM16(at+2)=(uint16_t)before;MEM8(at+4)=2;MEM8(at+5)=(uint8_t)flags;}
static void list_add(uint32_t at){uint32_t node=at+8,last=MEM32(HEAD+4);
 MEM32(node)=HEAD;MEM32(node+4)=last;MEM32(last)=node;MEM32(HEAD+4)=node;}
int main(int argc,char**argv){
 /* argv[1]: healthy | damaged; argv[2]: raw | guarded */
 unsigned char*ram=(unsigned char*)calloc(0x400000,1);
 int count=0,damaged=!strcmp(argv[1],"damaged"),guarded=!strcmp(argv[2],"guarded");
 g_xbox_mem_offset=(ptrdiff_t)ram;
 MEM32(HEAD)=HEAD;MEM32(HEAD+4)=HEAD;
 chunk(P,4,0,1);chunk(B,4,4,1);chunk(N,4,4,damaged?0xCC:0);chunk(Q,4,4,1);
 if(damaged){MEM32(N+4)=0x3DCCCCCDu;MEM32(N+8)=0x3DCCCCCDu;MEM32(N+12)=0x3DCCCCCDu;}   /* 0.1f over index, flags, links */
 else list_add(N);
 g_ebx=0x12345678;g_esi=0x23456789;g_edi=0x3456789A;g_ebp=g_seh_ebp=0x456789AB;
 g_esp=0x1000;MEM32(g_esp)=0;MEM32(g_esp+4)=HEAP;MEM32(g_esp+8)=B;MEM32(g_esp+12)=0x1100;MEM32(g_esp+16)=0;
 MEM32(0x1100)=4;                                  /* the freed chunk's size in units */
 if(guarded)count=mm3_heap_damaged_neighbour(B+16);   /* the wrapper skips the free */
 if(!count)sub_00084709();
 printf("%%u eax=%%X size=%%u next-flags=%%02X next-links=%%X,%%X\n",count,g_eax,MEM32(0x1100),MEM8(N+5),MEM32(N+8),MEM32(N+12));
 return 0;
}
'''


def replay(directory):
    source = (ROOT / "src/recomp/gen/recomp_0006.c").read_text()
    body = re.search(r"void sub_00084709\(void\)\n\{.*?(?=\n/\*\*|\Z)", source, re.S).group(0)
    program = directory / "replay.c"
    (directory / "recomp_types.h").write_text('#include "mm3_recomp_types.h"\n')   # the guard's name for it
    program.write_text(REPLAY % {"body": body, "guard": (ROOT / "src/mm3_heap_guard.c").as_posix()})
    executable = directory / ("replay" + cc.exe_suffix())
    subprocess.run(cc.compile_command([program], executable,
                                      includes=[directory, ROOT / "src", ROOT / "tools/xboxrecomp/include"],
                                      std="c11", optimize="none", dynamic_crt=True,
                                      obj=directory / "replay.obj"), check=True)

    def run(state, mode):
        return subprocess.run([str(executable), state, mode], capture_output=True, text=True)

    # A genuine free neighbour on a list is merged, guarded or not.
    raw, guarded = run("healthy", "raw"), run("healthy", "guarded")
    assert raw.returncode == 0 and raw.stdout == guarded.stdout, (raw, guarded)
    assert "eax=221040 size=8" in raw.stdout and raw.stdout.startswith("0 "), raw.stdout
    # The crash: the neighbour's header holds 0.1f; the real code writes through it.
    crashed = run("damaged", "raw")
    assert crashed.returncode not in (0, 99), crashed
    # Guarded: the free is skipped; neither chunk is touched.
    safe = run("damaged", "guarded")
    assert safe.returncode == 0, safe
    assert safe.stdout.split() == ["1", "eax=0", "size=4", "next-flags=CC", "next-links=3DCCCCCD,3DCCCCCD"], safe.stdout
    print(f"PASS replay: the generated coalescing crashes on a 0.1f neighbour (exit {crashed.returncode}) "
          "and leaves it alone behind the guard; healthy merges are unchanged")


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    with tempfile.TemporaryDirectory(prefix="mm3-heap-neighbour-") as temporary:
        folder = Path(temporary)
        shutil.copyfile(ROOT / "src/mm3_recomp_types.h", folder / "recomp_types.h")
        executable = folder / "contract.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/Od", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/I" + str(folder), "/I" + str(ROOT / "tools/xboxrecomp/include"),
                        str(ROOT / "tests/mm3_heap_neighbour_contract.c"), str(ROOT / "src/mm3_heap_guard.c"),
                        "/Fo:" + str(folder) + "\\", "/Fe:" + str(executable)],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        done = subprocess.run([str(executable)], check=True, cwd=folder, capture_output=True, text=True)
        print(done.stdout.strip())
        log = done.stderr
        assert "[HEAP_NEIGHBOUR] free of 00221050: the chunk after it at 00221080 says free" in log, log
        assert "[HEAP_NEIGHBOUR] free of 00221050: the chunk before it at 00221000 says free" in log, log

    cc.require_compiler()
    scratch = ROOT / "conformance_tmp"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="heap-neighbour-", dir=scratch) as directory:
        replay(Path(directory))

    source = (ROOT / "src/recomp_manual.c").read_text()
    wrapper = source[source.index("void sub_000860AA(void)"):]
    wrapper = wrapper[:wrapper.index("\n}")]
    order = [wrapper.index(token) for token in (
        "mm3_heap_double_free(", "mm3_heap_damaged_neighbour(",
        "call_alternate_seh_preserving(sub_000860AA_gen)")]
    assert order == sorted(order), wrapper
    print("PASS wrapper: RtlFreeHeap skips a second free and a free next to a damaged chunk before the generated free")


if __name__ == "__main__":
    main()
