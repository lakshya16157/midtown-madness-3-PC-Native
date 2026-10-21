/* Compare the complete machine state and guest RAM against the actual
 * generated retail leaf, including popped x87 scratch slots and aliasing. */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;

#include "matrix_oracle.inc"
static unsigned fallback_calls;
void sub_00259982_original(void) { ++fallback_calls; matrix_oracle(); }
#include "../src/mm3_matrix.c"

static unsigned char memory[8192], initial[8192], expected_memory[8192];
static uint32_t rng = 0x12345678;
static uint32_t next_random(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
static double random_number(void)
{
    return ((double)(int)(next_random() % 200001) - 100000) / 257.0;
}
typedef struct {
    uint32_t ax, cx, dx, sp, bx, si, di, bp, seh;
    double fp[8];
    int top, cmp, df;
    uint16_t cc, cw;
    unsigned mxcsr;
} State;
static State snapshot(void)
{
    State state;
    memset(&state, 0, sizeof state);
    state.ax = g_eax; state.cx = g_ecx; state.dx = g_edx; state.sp = g_esp;
    state.bx = g_ebx; state.si = g_esi; state.di = g_edi;
    state.bp = g_ebp; state.seh = g_seh_ebp;
    memcpy(state.fp, g_fp_stack, sizeof state.fp);
    state.top = g_fp_top; state.cmp = g_fp_cmp; state.df = g_df;
    state.cc = g_fp_cc; state.cw = g_fp_control_word;
    state.mxcsr = _mm_getcsr();
    return state;
}
static void restore(State state)
{
    g_eax = state.ax; g_ecx = state.cx; g_edx = state.dx; g_esp = state.sp;
    g_ebx = state.bx; g_esi = state.si; g_edi = state.di;
    g_ebp = state.bp; g_seh_ebp = state.seh;
    memcpy(g_fp_stack, state.fp, sizeof state.fp);
    g_fp_top = state.top; g_fp_cmp = state.cmp; g_df = state.df;
    g_fp_cc = state.cc; g_fp_control_word = state.cw;
    _mm_setcsr(state.mxcsr);
}
static void seed(unsigned test, int native)
{
    unsigned n, kind = test % (native ? 8 : 12);
    uint32_t left = 512, right = 768, out = 1024;
    for (n = 0; n < sizeof memory; ++n) memory[n] = (unsigned char)next_random();
    for (n = 448; n < 1120; n += 4) MEMF(n) = (float)random_number();
    for (n = 3904; n < 4192; n += 4) MEMF(n) = (float)random_number();
    if (kind == 1) out = left;
    if (kind == 2) out = right;
    if (kind == 3) out = left = right;
    if (kind == 4) out = left + 4;
    if (kind == 5) out = right - 4;
    if (kind == 6) right = left + 4;
    if (kind == 7) out = left + 12, right = left + 28;
    if (kind == 8) out = 4092 - 64; /* Overwrite saved guest registers. */
    if (kind == 9) left = 4092 - 64;
    if (kind == 10) out = left = right = 4092 - 64;
    if (kind == 11) left = 4096 - 80, right = 4096 - 32;
    if (!native && test % 17 == 0) {
        static const uint32_t edge[] = {
            0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc12345,
            1, 0x80000001, 0x7f7fffff, 0xff7fffff, 0x00800000,
            0x3f800001, 0x3f7fffff, 0x7fa12345
        };
        MEM32(left + (next_random() % 16) * 4) = edge[(test / 17) % 13];
        MEM32(right + (next_random() % 16) * 4) = edge[(test / 221) % 13];
    }
    if (!native && test % 19 == 0) {
        static const uint32_t limits[] = {
            0x5d800000, 0xdd800000, 0x5d7fffff, 0x5d800001,
            0, 0x80000000, 1, 0x80000001, 0x00800000, 0x80800000
        };
        for (n = 0; n < 16; ++n) {
            MEM32(left + n * 4) = limits[(n + test / 19) % 10];
            MEM32(right + n * 4) = limits[(n * 3 + test / 190) % 10];
        }
    }
    g_eax = next_random(); g_ecx = next_random(); g_edx = next_random();
    g_esp = 4096; g_ebx = next_random(); g_esi = next_random();
    g_edi = next_random(); g_ebp = next_random(); g_seh_ebp = next_random();
    g_fp_top = test % 8; g_fp_cmp = (int)(test % 4) - 1;
    g_fp_cc = (uint16_t)next_random();
    g_fp_control_word = native ? 0x23f : (uint16_t)(0x3f | ((test / 8) % 16) * 0x100);
    g_df = (test / 3) & 1;
    for (n = 0; n < 8; ++n) g_fp_stack[n] = random_number();
    MEM32(4096) = 0x600000;
    MEM32(4100) = out; MEM32(4104) = left; MEM32(4108) = right;
}
static double benchmark(void (*function)(void), unsigned iterations)
{
    LARGE_INTEGER start, end, frequency;
    unsigned i;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (i = 0; i < iterations; ++i) {
        g_esp = 4096;
        MEM32(4100) = 1024; MEM32(4104) = 512; MEM32(4108) = 768;
        function();
    }
    QueryPerformanceCounter(&end);
    return (end.QuadPart - start.QuadPart) * 1e9 /
        (double)frequency.QuadPart / iterations;
}
static void write_file(const char *folder, unsigned test, const char *suffix,
                       const void *data, size_t size)
{
    char path[1024];
    FILE *out;
    sprintf(path, "%s/%03u.%s", folder, test, suffix);
    out = fopen(path, "wb");
    if (!out || fwrite(data, 1, size, out) != size) exit(2);
    fclose(out);
}
int main(int argc, char **argv)
{
    unsigned i, initial_mxcsr = _mm_getcsr();
    g_xbox_mem_offset = (ptrdiff_t)memory;
    if (argc == 3 && !strcmp(argv[1], "--raw")) {
        for (i = 0; i < 128; ++i) {
            uint32_t regs[10], original_bp;
            seed(i, 1); original_bp = g_ebp;
            regs[0] = g_eax; regs[1] = g_ecx; regs[2] = g_edx; regs[3] = g_esp;
            regs[4] = g_ebx; regs[5] = g_esi; regs[6] = g_edi; regs[7] = g_ebp;
            regs[8] = g_df; regs[9] = g_fp_control_word;
            write_file(argv[2], i, "before", memory, sizeof memory);
            write_file(argv[2], i, "registers", regs, sizeof regs);
            sub_00259982();
            regs[0] = g_eax; regs[1] = g_ecx; regs[2] = g_edx; regs[3] = g_esp;
            regs[4] = g_ebx; regs[5] = g_esi; regs[6] = g_edi; regs[7] = original_bp;
            write_file(argv[2], i, "after", memory, sizeof memory);
            write_file(argv[2], i, "expected", regs, sizeof regs);
        }
        return 0;
    }
    for (i = 0; i < 200000; ++i) {
        State start, expected, actual;
        seed(i, 0);
        /* Cross the host SSE rounding modes with both FTZ/DAZ choices too.
         * Compare sticky exception flags as well as every guest FP value. */
        _mm_setcsr((initial_mxcsr & ~(0x6000u | 0x8040u | 0x3fu)) |
                   (((i / 128) % 4) << 13) |
                   (((i / 512) % 2) << 15) |
                   (((i / 1024) % 2) << 6));
        start = snapshot(); memcpy(initial, memory, sizeof memory);
        matrix_oracle(); expected = snapshot();
        memcpy(expected_memory, memory, sizeof memory);
        restore(start); memcpy(memory, initial, sizeof memory);
        sub_00259982(); actual = snapshot();
        if (memcmp(&expected, &actual, sizeof expected) ||
            memcmp(expected_memory, memory, sizeof memory)) {
            unsigned n;
            printf("FAIL case %u: control=%04X top=%d alias=%u df=%u\n",
                   i, start.cw, start.top, i % 12, start.df);
            for (n = 0; n < sizeof expected; ++n)
                if (((unsigned char *)&expected)[n] != ((unsigned char *)&actual)[n])
                    printf("state byte %u: %02X != %02X\n", n,
                           ((unsigned char *)&expected)[n], ((unsigned char *)&actual)[n]);
            for (n = 0; n < sizeof memory; ++n)
                if (expected_memory[n] != memory[n]) {
                    printf("memory byte %u: %02X != %02X\n", n, expected_memory[n], memory[n]);
                    break;
                }
            return 1;
        }
    }
    _mm_setcsr(initial_mxcsr);
    if (fallback_calls < 10000 || fallback_calls > 190000) {
        puts("FAIL: regression did not exercise both native fast path and original fallback");
        return 1;
    }
    printf("PASS: 200000 exact matrix state/memory cases (%u fast / %u fallback; all aliases, partial/stack overlaps, PC/RC, DF, bounds, x87 slots and host MXCSR)\n",
           200000 - fallback_calls, fallback_calls);
    if (argc > 1) {
        for (i = 0; i < 9; ++i) {
            double original, optimized;
            seed(0, 1); g_df = 0;
            g_fp_control_word = i < 3 ? 0x3f : 0x23f;
            if (i & 1) {
                optimized = benchmark(sub_00259982, 1000000);
                original = benchmark(matrix_oracle, 1000000);
            } else {
                original = benchmark(matrix_oracle, 1000000);
                optimized = benchmark(sub_00259982, 1000000);
            }
            printf("PC=%u: original %.3f ns; optimized %.3f ns; %.3fx\n",
                   (g_fp_control_word >> 8) & 3, original, optimized, original / optimized);
        }
    }
    return 0;
}
