/* Execute MM3's XACT engine clock (sub_00357B67) and its native 64-bit
 * helpers with the guest ABI against the APU sample counter (XGSCNT). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;
static unsigned char memory[0x10000];
static uint32_t xgscnt;
static unsigned raised, lowered, reads;
static const uint32_t stack = 0xC000, engine = 0x5000, out = 0x6000;

uint32_t mm3_apu_mmio_read32(uint32_t address)
{
    assert(address == 0xFE80200Cu);
    ++reads;
    return xgscnt;
}
/* The engine lock raises IRQL and saves the old one at [ecx]; leaving the
 * flag at ebp-4 clear makes the native epilogue skip its lower. */
static void sub_0035320E(void) { ++raised; MEM8(g_ecx) = 0; g_esp += 4; }
static void lower_irql(void) { ++lowered; g_esp += 4; }

#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
#include "native_clock.inc"
#undef eax
#undef ecx
#undef edx
#undef esp
#undef ebx
#undef esi
#undef edi

/* XACT time in 100 ns units, as the native clock reports it. */
static uint64_t xact_time(void)
{
    unsigned before = reads;
    g_esp = stack; g_esi = engine; g_ebx = 0xA001; g_edi = 0xA002; g_ebp = 0xA003;
    g_esp -= 4; MEM32(g_esp) = out;
    g_esp -= 4; MEM32(g_esp) = 0xDEADBEEFu;
    sub_00357B67();
    assert(reads == before + 1);
    assert(g_esp == stack && g_esi == engine && g_ebx == 0xA001 && g_edi == 0xA002);
    return (uint64_t)MEM32(out) | (uint64_t)MEM32(out + 4) << 32;
}

/* What the guest's own arithmetic makes of a sample total. */
static uint64_t samples_to_time(uint64_t samples) { return samples * 10000u / 48000u * 1000u; }

int main(void)
{
    uint64_t t0, t, total;
    unsigned frame, step, calls = 0;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    memset(memory, 0, sizeof memory);

    /* DirectSound clears the counter when it starts the APU; then one second
     * of output is 1500 voice-processor frames of 32 samples. */
    xgscnt = 0;
    assert(xact_time() == 0); ++calls;
    for (frame = 0; frame < 1500; ++frame) xgscnt += 32;
    assert(xact_time() == 10000000u); ++calls;

    /* Read once per game frame at an uneven 41-58 fps for 20 more seconds:
     * the time follows the audio produced, whatever the cadence. */
    total = 48000;
    for (step = 0; total < 48000u * 21u; ++step) {
        unsigned frames = 26 + (step * 7) % 11; /* 17.3-24 ms of output */
        xgscnt += frames * 32u; total += frames * 32u;
        t = xact_time(); ++calls;
        assert(t == samples_to_time(total));
        assert(t + 1000u >= total * 10000000u / 48000u && t <= total * 10000000u / 48000u);
    }

    /* After 2^32 samples (24.9 hours) the counter wraps; the clock adds only
     * the samples produced across the wrap. */
    memset(memory, 0, sizeof memory);
    xgscnt = 0xFFFFFF00u;
    t0 = xact_time(); ++calls;
    xgscnt += 16u * 32u;
    assert(xgscnt == 0x100u);
    t = xact_time(); ++calls;
    assert(t - t0 >= 106000u && t - t0 <= 107000u); /* 512 samples = 10.67 ms */

    /* The former XGSCNT, host time in 100 ns units (10 MHz): one second of it
     * read as 208 seconds of XACT time. */
    memset(memory, 0, sizeof memory);
    xgscnt = 0;
    t0 = xact_time(); ++calls;
    xgscnt = 10000000u;
    t = xact_time(); ++calls;
    assert(t - t0 > 200ull * 10000000u && t - t0 < 210ull * 10000000u);

    assert(raised == calls && lowered == 0);
    printf("PASS native XACT clock over %u reads: 48 kHz samples give real time (10000000 per second), "
           "uneven call cadence exact, wrap adds 512 samples; 10 MHz counter read as %.1f s per second\n",
           calls, (double)(t - t0) / 1e7);
    return 0;
}
