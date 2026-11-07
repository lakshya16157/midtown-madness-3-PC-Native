/* Win32 calls the MM3 port uses that win32_compat.h does not provide. POSIX only.
 * Force-included into the port's own sources off Windows (CMakeLists.txt), after
 * win32_compat.h, so files that rely on <windows.h> arriving indirectly still build. */
#pragma once
#if !defined(_WIN32)
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <time.h>

#ifndef __declspec
#define __declspec(x) __mm3_declspec_##x
#define __mm3_declspec_thread __thread
#endif

typedef uint64_t DWORD64;

#ifndef EXCEPTION_IN_PAGE_ERROR
#define EXCEPTION_IN_PAGE_ERROR 0xC0000006u
#endif

#ifndef VK_F10
#define VK_F10 0x79
#define VK_F11 0x7A
#endif

/* InterlockedIncrement64/Add64/Exchange64/CompareExchange64: win32_compat.h. */

/* Returns the length copied, or the size needed (including the NUL) when it does
 * not fit, or 0 when the variable is unset: the Win32 contract. */
static inline unsigned GetEnvironmentVariableA(const char *name, char *buf, unsigned size)
{
    const char *v = getenv(name);
    if (!v) return 0;
    size_t n = strlen(v);
    if (!buf || n + 1 > size) return (unsigned)(n + 1);
    memcpy(buf, v, n + 1);
    return (unsigned)n;
}

/* CPU time of the calling thread in nanoseconds; callers only take differences. */
static inline int QueryThreadCycleTime(void *thread, uint64_t *cycles)
{
    (void)thread;
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    *cycles = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return 1;
}
#endif
