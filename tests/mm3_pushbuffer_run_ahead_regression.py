"""Exercise the production kick's lag boundary, wrap handling and guest ABI.

Run after scripts/enter-env.ps1. The runtime acknowledgment path has its own
nv2a-wbc-wait CTest; this fixture checks when the title requests that barrier.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emmintrin.h>
#include "mm3_recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_eflags;
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
static unsigned observations, waits;
static unsigned char memory[0x400000], expected[sizeof memory];
static void mm3_producer_diagnostics_kick(void){++observations;}
static void xbox_Nv2aWbcWait(uint32_t address){
 assert(address==0x120410);
 assert(MEM32(address)==0xABCDEF00u); /* WBC was published before waiting. */
 ++waits;MEM32(address)&=~0x10000u;
}
/* NATIVE_KICK */

static void check(uint32_t current,uint32_t consumed,int should_wait){
 uint32_t before[8],after[8];double fp[8];unsigned prior_waits=waits;
 memset(memory,0x5A,sizeof memory);
 MEM32(0x351F48)=0x10000;
 MEM32(0x10000)=current;
 MEM32(0x117C8)=0x20000;MEM32(0x117C0)=0x30000;
 MEM32(0x30044)=consumed;MEM32(0x120410)=0xABCCEF00;
 memcpy(expected,memory,sizeof memory);
 g_eax=1;g_ecx=2;g_edx=3;g_esp=0x300000;g_ebx=4;g_esi=5;g_edi=6;g_ebp=7;
 g_seh_ebp=8;g_fs_base=9;g_eflags=10;g_fp_top=3;g_df=1;g_fp_cmp=2;
 g_fp_control_word=0x027F;g_fp_cc=0x4500;
 for(unsigned i=0;i<8;i++)g_fp_stack[i]=fp[i]=100.25+i;
 before[0]=g_edx;before[1]=g_ebx;before[2]=g_esi;before[3]=g_edi;
 before[4]=g_ebp;before[5]=g_seh_ebp;before[6]=g_fs_base;before[7]=g_eflags;
 sub_00344AB0();
 assert(waits==prior_waits+(unsigned)should_wait);
 assert(g_eax==0x20000 && g_ecx==0xABCDEF00 && g_esp==0x300004);
 after[0]=g_edx;after[1]=g_ebx;after[2]=g_esi;after[3]=g_edi;
 after[4]=g_ebp;after[5]=g_seh_ebp;after[6]=g_fs_base;after[7]=g_eflags;
 assert(!memcmp(before,after,sizeof before));
 assert(g_fp_top==3 && g_df==1 && g_fp_cmp==2 && g_fp_control_word==0x027F && g_fp_cc==0x4500);
 assert(!memcmp(fp,g_fp_stack,sizeof fp));
 assert(!memcmp(expected,memory,sizeof memory));
}
int main(void){
 g_xbox_mem_offset=(ptrdiff_t)memory;
 check(0x100000,0x100000,0);
 check(0x13FFFF,0x100000,0);
 check(0x140000,0x100000,0);
 check(0x140004,0x100000,1);
 check(0x110000,0x140000,1); /* Ring wrap cannot underflow into permission. */
 check(0x80400000,0x400000,0); /* Guest producer VA must be masked. */
 check(0x80440000,0x400000,0);
 check(0x80440004,0x400000,1);
 check(0x80010000,0x400000,1);
 assert(observations==9);
 puts("PASS: native kick zero/below/exact/above 256KiB, wrap, VA masking, "
      "conditional WBC publication and preserved guest registers/FP/memory");
 return 0;
}
'''


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first.")
    manual = (ROOT / "src/recomp_manual.c").read_text()
    kick = re.search(r"^void sub_00344AB0\(void\)\n\{.*?^\}", manual, re.M | re.S).group()
    with tempfile.TemporaryDirectory(prefix="mm3-pushbuffer-run-ahead-") as path:
        folder = Path(path)
        source, executable = folder / "contract.c", folder / "contract.exe"
        source.write_text(SOURCE.replace("/* NATIVE_KICK */", kick))
        for optimization in ("/Od", "/O2"):
            subprocess.run(["cl", "/nologo", "/W4", optimization, "/std:c11",
                            "/I" + str(ROOT / "src"),
                            "/I" + str(ROOT / "tools/xboxrecomp/include"),
                            "/Fo:" + str(folder / "contract.obj"),
                            "/Fe:" + str(executable), str(source)], check=True, cwd=folder)
            subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    main()
