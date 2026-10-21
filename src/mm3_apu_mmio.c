/* Sound updates repeatedly access fixed APU registers. Routing a known MOV
 * through the Windows access-violation decoder costs an exception per access.
 * The checked generation seam instead calls the identical register backend.
 * Unknown/unaligned addresses, disabled audio, and an explicit comparison mode
 * retain the original volatile memory access and its exception/fallback path. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fenv.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "apu.h"
#include "mm3_apu_mmio.h"

extern MCPXAPUState *g_apu_state;
extern ptrdiff_t g_xbox_mem_offset;

static INIT_ONCE s_config_once = INIT_ONCE_STATIC_INIT;
static volatile LONG s_enabled;
static int s_trace_requested;

static BOOL CALLBACK configure_native_apu(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const char *setting = getenv("MM3_NATIVE_APU_MMIO");
    (void)once; (void)parameter; (void)context;
    /* The legacy tracing mode includes extra register observations. Keep its
     * existing access-violation path intact when explicitly requested. */
    s_trace_requested = getenv("RECOMP_APU_TRACE") != NULL;
    s_enabled = (!setting || strcmp(setting, "0") != 0) && !s_trace_requested;
    return TRUE;
}

int mm3_apu_mmio_set_enabled(int enabled)
{
    int saved_errno = errno, ok = 0;
    DWORD saved_error = GetLastError();
    InitOnceExecuteOnce(&s_config_once, configure_native_apu, NULL, NULL);
    if ((enabled == 0 || enabled == 1) &&
        (!enabled || (!s_trace_requested && g_apu_state))) {
        InterlockedExchange(&s_enabled, enabled);
        ok = 1;
    }
    errno = saved_errno;
    SetLastError(saved_error);
    return ok;
}

int mm3_apu_mmio_get_enabled(void)
{
    int saved_errno = errno;
    DWORD saved_error = GetLastError();
    InitOnceExecuteOnce(&s_config_once, configure_native_apu, NULL, NULL);
    int enabled = s_enabled && g_apu_state;
    errno = saved_errno;
    SetLastError(saved_error);
    return enabled;
}

static int use_native_apu(uint32_t address)
{
    InitOnceExecuteOnce(&s_config_once, configure_native_apu, NULL, NULL);
    return s_enabled && g_apu_state && address >= 0xFE800000u &&
           address <= 0xFE87FFFCu && (address & 3u) == 0;
}

uint32_t mm3_apu_mmio_read32(uint32_t address)
{
    int saved_errno = errno;
    DWORD saved_error = GetLastError();
    uint32_t value;
    if (use_native_apu(address)) {
        /* Exception dispatch normally restores the interrupted host floating
         * point context. Preserve that boundary on the direct path as well. */
        fenv_t saved_fp;
        int have_fp = feholdexcept(&saved_fp) == 0;
        value = (uint32_t)mcpx_apu_mmio_read(g_apu_state, address - 0xFE800000u, 4);
        if (have_fp) fesetenv(&saved_fp);
    } else {
        value = *(volatile uint32_t *)((uintptr_t)g_xbox_mem_offset + address);
    }
    errno = saved_errno;
    SetLastError(saved_error);
    return value;
}

void mm3_apu_mmio_write32(uint32_t address, uint32_t value)
{
    int saved_errno = errno;
    DWORD saved_error = GetLastError();
    if (use_native_apu(address)) {
        fenv_t saved_fp;
        int have_fp = feholdexcept(&saved_fp) == 0;
        mcpx_apu_mmio_write(g_apu_state, address - 0xFE800000u, value, 4);
        if (have_fp) fesetenv(&saved_fp);
    } else {
        *(volatile uint32_t *)((uintptr_t)g_xbox_mem_offset + address) = value;
    }
    errno = saved_errno;
    SetLastError(saved_error);
}
