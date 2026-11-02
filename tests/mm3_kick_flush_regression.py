"""Exercise the production XDK cache-flush handshake with an acknowledging thread.

The title's sub_00344AB0 kick is extracted from src/recomp_manual.c and runs
against a fixture xbox_Nv2aWbcWait that spins on the flush word until an
acknowledging thread clears it, as the runtime's does. The port bounds the
producer's run-ahead (MM3_PRODUCER_LAG_KB, default 256 KiB) instead of waiting at
every kick like the fork's xbox_Nv2aWbcFlush: it waits for the acknowledgement
exactly when the producer is further ahead than the bound (a ring wrap counts as
unbounded), and a kick inside the bound neither sets nor waits on the flush word.
"""
from pathlib import Path
import subprocess
import tempfile
import cc

ROOT = Path(__file__).resolve().parents[1]


def main():
    cc.require_compiler()
    source = (ROOT / "src/recomp_manual.c").read_text(encoding="utf-8")
    kick = source.index("void sub_00344AB0(void)\n{")
    end = source.index("\n}\n", kick) + 3
    fixture = r'''
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <immintrin.h>
static unsigned char ram[0x360000];
static ptrdiff_t g_xbox_mem_offset;
ptrdiff_t xbox_GetMemoryOffset(void) { return g_xbox_mem_offset; }
#define MEM32(a) (*(volatile uint32_t *)(ram + (a)))
static uint32_t g_eax, g_ecx, g_esp;
static unsigned watch_calls;
void recomp_sampler_watch_current_thread(void) { ++watch_calls; }
void mm3_producer_diagnostics_kick(void) {}
/* The kick's run-ahead state with renderer profiling off (the default). */
static uint32_t s_kick_lag = 0x40000u;
static int s_kick_stats, s_kick_wrap_sync;
static int64_t s_kick_wait_ticks, s_kick_wait_max;
static unsigned s_kick_count, s_kick_waits, s_kick_wraps;
static void mm3_kick_report(int64_t now) { (void)now; assert(!"report without profiling"); }
/* The runtime's wait, minus its executor thread: spin until the flush word is
 * acknowledged (bounded, so a missing acknowledgement fails the test). */
static unsigned wait_calls;
void xbox_Nv2aWbcWait(uint32_t register_va)
{
    ULONGLONG start = GetTickCount64();
    ++wait_calls;
    while ((*(volatile uint32_t *)(ram + register_va) & 0x10000u) && GetTickCount64()-start < 2000) Sleep(0);
    assert(!(*(volatile uint32_t *)(ram + register_va) & 0x10000u));
}
''' + source[kick:end] + r'''
static volatile LONG observed;
static DWORD WINAPI acknowledge(void *unused)
{
    ULONGLONG start = GetTickCount64();
    (void)unused;
    while (!(MEM32(0x110410) & 0x10000u) && GetTickCount64()-start < 500) Sleep(0);
    if (MEM32(0x110410) & 0x10000u) {
        InterlockedExchange(&observed, 1);
        MEM32(0x110410) &= ~0x10000u;
    }
    return 0;
}
int main(void)
{
    /* PUT, GET, then whether the kick waits at the default 256 KiB bound and
     * at a zero bound: zero lag, small lag, large lag, and a ring wrap (GET
     * ahead of PUT with no ring layout) which is never inside a bound. */
    const struct { uint32_t put, get; int waits_default, waits_zero_bound; } positions[] = {
        {0x20000,0x20000,0,0},{0x30000,0x20000,0,1},{0x90000,0x10000,1,1},{0x10000,0x90000,1,1}};
    unsigned bound, i;
    g_xbox_mem_offset = (ptrdiff_t)ram;
    MEM32(0x351F48)=0x2000; MEM32(0x2000+0x17C8)=0x10000;
    MEM32(0x2000+0x17C0)=0x3000;
    for (bound=0; bound<2; ++bound) {
        s_kick_lag = bound ? 0 : 0x40000u;
        for (i=0; i<sizeof positions/sizeof *positions; ++i) {
            HANDLE thread;
            int expect_wait = bound ? positions[i].waits_zero_bound : positions[i].waits_default;
            unsigned before = wait_calls;
            MEM32(0x2000)=positions[i].put; MEM32(0x3044)=positions[i].get;
            MEM32(0x110410)=0x15; observed=0; g_esp=0x400;
            thread=CreateThread(NULL,0,acknowledge,NULL,0,NULL); assert(thread);
            sub_00344AB0();
            if (!expect_wait) {
                /* Inside the bound: the flush word is never raised. */
                assert(MEM32(0x110410)==0x15); assert(wait_calls==before);
            }
            assert(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0); CloseHandle(thread);
            if (expect_wait) {
                assert(observed==1); assert(MEM32(0x110410)==0x15); assert(wait_calls==before+1);
            } else assert(observed==0);
            assert(g_esp==0x404 && g_eax==0x10000 && g_ecx==0x10015);
        }
    }
    assert(watch_calls==1);
    puts("PASS: the kick waits for the XDK flush acknowledgement exactly when the producer is beyond the run-ahead bound (large lag, wrap, or any lag at a zero bound)");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="mm3-kick-flush-") as folder:
        folder = Path(folder)
        cfile = folder / "flush.c"
        cfile.write_text(fixture, encoding="utf-8")
        exe = folder / "flush.exe"
        includes = [ROOT / "tools/xboxrecomp/src/kernel", ROOT / "tools/xboxrecomp/src",
                    ROOT / "tools/xboxrecomp/src/nv2a", ROOT / "tools/xboxrecomp/templates/runtime",
                    ROOT / "tools/xboxrecomp/include"]
        subprocess.run(cc.compile_command([cfile], exe, std="c11", optimize="none",
                        warnings="high", includes=includes, obj=folder / "flush.obj"),
                       check=True, cwd=folder)
        subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == "__main__":
    main()
