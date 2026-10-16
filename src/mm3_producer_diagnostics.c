#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"
#include "mm3_producer_diagnostics.h"

#ifndef MM3_PRODUCER_LOG
#define MM3_PRODUCER_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

typedef struct ProducerSample {
    uint64_t qpc, cycles;
    XBOX_FILE_READ_STATS reads;
    int valid, cycles_valid;
} ProducerSample;

static int s_producer_initialized, s_producer_enabled;
static uint64_t s_producer_frequency, s_producer_gap_ticks;
static __declspec(thread) ProducerSample s_producer_previous;

void mm3_producer_diagnostics_init(void)
{
    int saved_errno = errno;
    DWORD saved_error = GetLastError();
    const char *setting;
    LARGE_INTEGER frequency;
    if (s_producer_initialized) goto done;
    s_producer_initialized = 1;
    setting = getenv("RECOMP_PRODUCER_TRACE");
    if (setting && strcmp(setting, "1") == 0 &&
        QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
        s_producer_frequency = (uint64_t)frequency.QuadPart;
        s_producer_gap_ticks = s_producer_frequency / 25 +
            (s_producer_frequency % 25 != 0);
        s_producer_enabled = 1;
    }
done:
    errno = saved_errno;
    SetLastError(saved_error);
}

void mm3_producer_diagnostics_kick(void)
{
    int saved_errno;
    DWORD saved_error;
    LARGE_INTEGER now;
    ProducerSample current = {0}, previous;
    uint64_t gap_ticks;
    double milliseconds;
    fenv_t saved_fp;
    int saved_fp_valid = 0;
    if (!s_producer_enabled) return;
    saved_errno = errno;
    saved_error = GetLastError();
    if (!QueryPerformanceCounter(&now) || now.QuadPart < 0) {
        /* A failed or wrapped wall clock cannot describe an interval. Require
         * a fresh successful baseline instead of reporting a fabricated gap. */
        s_producer_previous.valid = 0;
        goto done;
    }
    current.qpc = (uint64_t)now.QuadPart;
    current.valid = 1;
    current.cycles_valid = QueryThreadCycleTime(GetCurrentThread(), &current.cycles);
    xbox_GetFileReadStats(&current.reads);
    previous = s_producer_previous;
    s_producer_previous = current;
    if (!previous.valid || current.qpc < previous.qpc) goto done;
    gap_ticks = current.qpc - previous.qpc;
    if (gap_ticks >= s_producer_gap_ticks) {
        int cpu_valid = current.cycles_valid && previous.cycles_valid &&
            current.cycles >= previous.cycles;
        int reads_valid = current.reads.qpc_frequency != 0 &&
            current.reads.qpc_frequency == previous.reads.qpc_frequency;
        uint64_t read_count = reads_valid ?
            current.reads.read_count - previous.reads.read_count : 0;
        uint64_t read_ticks = reads_valid ?
            current.reads.read_qpc_ticks - previous.reads.read_qpc_ticks : 0;
        /* The usual kick path uses integer arithmetic only. Formatting and
         * conversion on a slow gap must preserve FP status and unmasked traps. */
        saved_fp_valid = feholdexcept(&saved_fp) == 0;
        milliseconds = 1000.0 / (double)s_producer_frequency;
        /* Cycle counts are deliberately raw: Windows does not guarantee a
         * constant CPU timer frequency, so converting them to CPU ms is unsafe.
         * Read counters use unsigned subtraction to support cumulative wrap. */
        MM3_PRODUCER_LOG(
            "[HITCH] phase=producer_gap qpc_ms=%.3f start_qpc_ms=%.3f "
            "duration_ms=%.3f thread=%lu cpu_cycle_delta=%llu cpu_cycles_valid=%d "
            "read_count_delta=%llu read_wall_ticks_delta=%llu read_wall_ms=%.3f "
            "read_stats_valid=%d\n",
            current.qpc * milliseconds, previous.qpc * milliseconds,
            gap_ticks * milliseconds, (unsigned long)GetCurrentThreadId(),
            (unsigned long long)(cpu_valid ? current.cycles - previous.cycles : 0),
            cpu_valid, (unsigned long long)read_count, (unsigned long long)read_ticks,
            reads_valid ? (double)read_ticks * 1000.0 /
                (double)current.reads.qpc_frequency : 0.0, reads_valid);
    }
done:
    if (saved_fp_valid) fesetenv(&saved_fp);
    errno = saved_errno;
    SetLastError(saved_error);
}
