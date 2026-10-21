"""Guest delays (KeDelayExecutionThread) wait at Xbox precision.

Builds the toolkit's kernel_delay.h with cl and checks: a 1 ms delay takes
about 1 ms on the per-thread high-resolution timer (Win32 Sleep stretched it
to the 15.6 ms timer tick); RECOMP_PRECISE_DELAY=0 keeps Sleep; an alertable
delay returns as alerted once a user APC runs; every delay lasts at least the
Xbox's 1 ms tick; and the profiling counters add up. Run after
scripts/enter-env.ps1.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel_delay.h"

static double now_ms(void)
{
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double median_delay(LONGLONG interval_100ns, int alertable)
{
    double samples[31];
    int i;
    for (i = 0; i != 31; ++i) {
        double start = now_ms();
        assert(!kdelay_run(interval_100ns, (BOOLEAN)alertable));
        samples[i] = now_ms() - start;
    }
    qsort(samples, 31, sizeof samples[0], compare);
    return samples[15];
}

static void CALLBACK apc(ULONG_PTR value)
{
    *(int *)value = 1;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "sleep")) {
        /* RECOMP_PRECISE_DELAY=0: plain Sleep, no timer is created. */
        assert(!kdelay_precise_enabled());
        (void)median_delay(10000, 0);
        assert(s_kdelay_timer_state == 0);
        puts("PASS RECOMP_PRECISE_DELAY=0 keeps Sleep");
        return 0;
    }
    {
        double one = median_delay(10000, 0), five = median_delay(50000, 1), tiny;
        int ran = 0;
        double start;
        assert(s_kdelay_timer_state == 1);
        printf("1 ms delay: median %.2f ms; 5 ms delay: median %.2f ms\n", one, five);
        assert(one >= 0.9 && one < 5.0);
        assert(five >= 4.9 && five < 9.0);
        /* Shorter than the 1 ms tick still waits the tick. */
        tiny = median_delay(10, 0);
        assert(tiny >= 0.9 && tiny < 5.0);
        /* A queued user APC ends an alertable delay at once, as alerted. */
        assert(QueueUserAPC(apc, GetCurrentThread(), (ULONG_PTR)&ran));
        start = now_ms();
        assert(kdelay_run(500000, TRUE) == 1 && ran);
        assert(now_ms() - start < 25.0);
        /* The timer is reusable after an alerted wait. */
        assert(median_delay(10000, 1) < 5.0);
        /* Counters (RECOMP_D3D_PROFILE is set by the harness). */
        assert(s_kdelay_calls == 31 * 4 + 1);
        assert(s_kdelay_short == 31 * 3 && s_kdelay_long == 1 && s_kdelay_mid == 31);
        assert(s_kdelay_precise == s_kdelay_calls);
        assert(s_kdelay_slept > s_kdelay_requested / 2);
    }
    puts("PASS guest delays: 1 ms waits about 1 ms on the per-thread high-resolution timer, 1 ms minimum, "
         "alertable APC returns as alerted, counters add up");
    return 0;
}
'''


def main():
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe is available.")
    with tempfile.TemporaryDirectory(prefix="mm3-kernel-delay-") as temporary:
        folder = Path(temporary)
        (folder / "delay.c").write_text(SOURCE)
        exe = folder / "delay.exe"
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/O2", "/std:c11",
                        "/I" + str(ROOT / "tools/xboxrecomp/src/kernel"),
                        "/Fo:" + str(folder / "delay.obj"), "/Fe:" + str(exe),
                        str(folder / "delay.c")], check=True, cwd=folder, stdout=subprocess.DEVNULL)
        environment = dict(os.environ, RECOMP_D3D_PROFILE="1")
        environment.pop("RECOMP_PRECISE_DELAY", None)
        subprocess.run([str(exe)], check=True, cwd=folder, env=environment)
        environment["RECOMP_PRECISE_DELAY"] = "0"
        subprocess.run([str(exe), "sleep"], check=True, cwd=folder, env=environment)


if __name__ == "__main__":
    main()
