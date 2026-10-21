/* Full generated-leaf differential, including all popped x87 slots, every
 * guest RAM byte, mask mutations, frame bridge and host SSE exception flags. */
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

#include "masked_visibility_oracle.inc"
static unsigned fallback_calls;
void sub_001C7B15_generated(void) { ++fallback_calls; masked_visibility_oracle(); }
#include "../src/mm3_masked_visibility.c"

/* Compile the actual existing wrapper too; its diagnostics must still see
 * the optimized result and route exceptional calls into the retained body. */
typedef struct {
    int frustum_reject;
    unsigned cell_frustum_calls, cell_frustum_rejects;
} BuildingFrame;
static BuildingFrame *s_active;
static uint32_t read_word(uint32_t address) { return MEM32(address); }
#include "masked_visibility_wrapper.inc"

#define BASE 0x10000u
#define BOX (BASE + 512)
#define PLANES (BASE + 1024)
#define MASK (BASE + 2048)
#define STACK (BASE + 4096)
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
    unsigned n, kind = test % (native ? 2 : 11), count = test % 49;
    uint32_t box = BOX, planes = PLANES, mask = MASK;
    for (n = 0; n < sizeof memory; ++n) memory[n] = (unsigned char)next_random();
    for (n = 448; n < 2336; n += 4) MEMF(BASE + n) = (float)random_number();
    for (n = 3904; n < 4928; n += 4) MEMF(BASE + n) = (float)random_number();
    if (kind == 1) planes = box + 16; /* Read-only input aliases remain valid. */
    if (kind == 2) mask = box + 4;
    if (kind == 3) mask = planes + 12;
    if (kind == 4) mask = STACK - 12;
    if (kind == 5) box = STACK - 48;
    if (kind == 6) planes = STACK - 24;
    if (kind == 7) mask = STACK + 4;
    if (kind == 8) mask = STACK;
    if (kind == 9) box = STACK - 44, mask = box + 36;
    if (kind == 10) count = 33 + test % 16;
    if (native) count = test % 33;

    if (test % 5 == 0) {
        float distance = test % 3 == 0 ? 1.0f : test % 3 == 1 ?
            nextafterf(1.0f, 0.0f) : nextafterf(1.0f, 2.0f);
        for (n = 0; n < 15; ++n) MEMF(box + n * 4) = 0.0f;
        MEMF(box) = MEMF(box + 16) = MEMF(box + 32) = 1.0f;
        MEMF(box + 48) = MEMF(box + 52) = MEMF(box + 56) = 1.0f;
        for (n = 0; n < count; ++n) {
            MEMF(planes + n * 16) = n % 3 == 0 ? 1.0f : 0.0f;
            MEMF(planes + n * 16 + 4) = n % 3 == 1 ? 1.0f : 0.0f;
            MEMF(planes + n * 16 + 8) = n % 3 == 2 ? 1.0f : 0.0f;
            MEMF(planes + n * 16 + 12) = test & 1 ? distance : -distance;
        }
    }
    MEM32(mask) = (test / 11) % 5 == 0 ? 0 :
        (test / 11) % 5 == 1 ? 0xffffffffu :
        (test / 11) % 5 == 2 ? 0xaaaaaaaau :
        (test / 11) % 5 == 3 ? 0x80000001u : next_random();
    if (!native && test % 17 == 0) {
        static const uint32_t edge[] = {
            0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc12345,
            1, 0x80000001, 0x7f7fffff, 0xff7fffff, 0x00800000,
            0x3f800001, 0x3f7fffff, 0x7fa12345
        };
        MEM32(box + (next_random() % 15) * 4) = edge[(test / 17) % 13];
        MEM32(planes + (next_random() % 64) * 4) = edge[(test / 221) % 13];
    }
    if (!native && test % 19 == 0) {
        static const uint32_t limits[] = {
            0x53800000, 0xd3800000, 0x537fffff, 0x53800001,
            0, 0x80000000, 1, 0x80000001, 0x00800000, 0x80800000
        };
        for (n = 0; n < 15; ++n)
            MEM32(box + n * 4) = limits[(n + test / 19) % 10];
        for (n = 0; n < 64; ++n)
            MEM32(planes + n * 4) = limits[(n * 3 + test / 190) % 10];
    }
    g_eax = next_random(); g_ecx = box; g_edx = planes; g_esp = STACK;
    g_ebx = mask; g_esi = next_random(); g_edi = next_random();
    g_ebp = next_random(); g_seh_ebp = next_random();
    g_fp_top = test % 8; g_fp_cmp = (int)(test % 4) - 1;
    g_fp_cc = (uint16_t)next_random();
    g_fp_control_word = native ?
        (uint16_t)(0x3f | ((test / 8) % 2) * 0x200 | ((test / 16) % 4) * 0x400) :
        (uint16_t)(0x3f | ((test / 8) % 16) * 0x100);
    g_df = (test / 3) & 1;
    for (n = 0; n < 8; ++n) g_fp_stack[n] = random_number();
    if (!native && test % 23 == 0) {
        static const uint64_t fp_edge[] = {
            0, UINT64_C(0x8000000000000000), UINT64_C(0x7ff0000000000000),
            UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8123456789abc),
            UINT64_C(0x7ff0123456789abc), 1, UINT64_C(0x8000000000000001)
        };
        memcpy(&g_fp_stack[(test / 23) % 8], &fp_edge[(test / 184) % 8], 8);
    }
    MEM32(STACK) = 0x600000;
    MEM32(STACK + 4) = count; /* Set after alias fixture initialization. */
}

static int invalid_span_contract(void)
{
    State start, end;
    unsigned n;
    static const uint32_t bad[] = {0, 0xffc, 0xffff, 0x07fffffe,
                                  0x08000000, 0xfe800000, 0xffffffff};
    for (n = 0; n < sizeof bad / sizeof bad[0]; ++n) {
        seed(1, 0); start = snapshot();
        g_esp = bad[n];
        if (masked_visibility_fast_inputs()) return 0;
        g_esp = start.sp;
        g_ecx = bad[n];
        if (masked_visibility_fast_inputs()) return 0;
        g_ecx = start.cx;
        g_edx = bad[n];
        if (masked_visibility_fast_inputs()) return 0;
        g_edx = start.dx;
        g_ebx = bad[n];
        if (masked_visibility_fast_inputs()) return 0;
        g_ebx = start.bx;
        end = snapshot();
        if (memcmp(&start, &end, sizeof start)) return 0;
    }
    /* Zero planes must not touch either the plane-list or mask pointer. */
    seed(0, 1); g_edx = g_ebx = 0xfe800000;
    if (!masked_visibility_fast_inputs()) return 0;
    return 1;
}

static void benchmark_fixture(unsigned kind)
{
    unsigned n;
    seed(0, 1);
    g_fp_control_word = kind < 5 ? 0x23f : 0x3f;
    kind %= 5;
    MEM32(STACK + 4) = 6;
    for (n = 0; n < 15; ++n) MEMF(BOX + n * 4) = 0;
    MEMF(BOX) = MEMF(BOX + 16) = MEMF(BOX + 32) = 1;
    MEMF(BOX + 48) = MEMF(BOX + 52) = MEMF(BOX + 56) = 1;
    for (n = 0; n < 6; ++n) {
        MEMF(PLANES + n * 16) = n % 3 == 0 ? 1.0f : 0.0f;
        MEMF(PLANES + n * 16 + 4) = n % 3 == 1 ? 1.0f : 0.0f;
        MEMF(PLANES + n * 16 + 8) = n % 3 == 2 ? 1.0f : 0.0f;
        MEMF(PLANES + n * 16 + 12) = kind == 1 ? 2.0f : kind == 2 ? -2.0f : 0.0f;
    }
}

#define REQUIRE(condition) do { if (!(condition)) { \
    printf("FAIL: path contract at line %u: %s\n", __LINE__, #condition); return 0; \
} } while (0)
static int path_contract(void)
{
    unsigned kind;
    BuildingFrame frame;
    fallback_calls = 0;
    benchmark_fixture(0); MEM32(STACK + 4) = 0;
    g_eax = 0xa1b2c3d4; g_fp_cmp = -1; g_fp_cc = 0x100;
    sub_001C7B15_original();
    REQUIRE(g_eax == 0xa1b2c300 && g_edx == PLANES && g_esp == STACK + 8);
    REQUIRE(g_fp_cmp == -1 && g_fp_cc == 0x100 && MEM32(MASK) == 0);
    for (kind = 0; kind != 2; ++kind) {
        benchmark_fixture(0); MEM32(MASK) = kind ? 0x80000000u : 0;
        g_fp_cmp = -1; g_fp_cc = 0x100;
        sub_001C7B15_original();
        REQUIRE(g_eax == 0 && g_edx == PLANES + 96 && g_esp == STACK + 8);
        REQUIRE(g_fp_cmp == -1 && g_fp_cc == 0x100);
        REQUIRE(MEM32(MASK) == (kind ? 0x80000000u : 0));
    }
    benchmark_fixture(1); MEM32(MASK) = 0x3f; g_fp_top = 0; g_eax = 0xa1b2c3d4;
    sub_001C7B15_original();
    REQUIRE(g_eax == 0xa1b22001 && g_edx == PLANES && MEM32(MASK) == 0x3f);
    REQUIRE(g_fp_top == 0 && g_fp_cmp == 1 && g_fp_cc == 0);
    benchmark_fixture(2); MEM32(MASK) = 0x3f;
    sub_001C7B15_original();
    REQUIRE(g_eax == 0 && g_edx == PLANES + 96 && MEM32(MASK) == 0);
    benchmark_fixture(0); MEM32(MASK) = 0x3f;
    sub_001C7B15_original();
    REQUIRE(g_eax == 0 && g_edx == PLANES + 96 && MEM32(MASK) == 0x3f);
    benchmark_fixture(0); MEM32(MASK) = 0x80000000u; MEM32(STACK + 4) = 32;
    MEMF(PLANES + 31 * 16) = 1; MEMF(PLANES + 31 * 16 + 4) = 0;
    MEMF(PLANES + 31 * 16 + 8) = 0; MEMF(PLANES + 31 * 16 + 12) = 0;
    sub_001C7B15_original();
    REQUIRE(g_eax == 0 && g_edx == PLANES + 512 && MEM32(MASK) == 0x80000000u);
    for (kind = 0; kind != 6; ++kind) {
        float distance = kind < 3 ? 1.0f : -1.0f;
        if (kind % 3 == 1) distance = nextafterf(distance, 0.0f);
        if (kind % 3 == 2) distance = nextafterf(distance, kind < 3 ? 2.0f : -2.0f);
        benchmark_fixture(0); MEM32(MASK) = 1; MEM32(STACK + 4) = 1;
        MEMF(PLANES + 12) = distance;
        sub_001C7B15_original();
        REQUIRE(LO8(g_eax) == (distance > 1.0f));
        REQUIRE(MEM32(MASK) == (distance < -1.0f ? 0u : 1u));
    }
    REQUIRE(fallback_calls == 0); /* Every named path above must use scalars. */

    memset(&frame, 0, sizeof frame); s_active = &frame;
    benchmark_fixture(1); MEM32(MASK) = 0x3f; MEM32(STACK) = 0x00168A92;
    sub_001C7B15();
    REQUIRE(frame.frustum_reject == 1 && g_esp == STACK + 8);
    benchmark_fixture(1); MEM32(MASK) = 0x3f; MEM32(STACK) = 0x0016886A;
    sub_001C7B15();
    REQUIRE(frame.cell_frustum_calls == 1 && frame.cell_frustum_rejects == 1);
    benchmark_fixture(0); MEM32(MASK) = 0; MEM32(STACK) = 0x0016886A;
    MEM32(STACK + 4) = 33; fallback_calls = 0;
    sub_001C7B15();
    REQUIRE(fallback_calls == 1 && frame.cell_frustum_calls == 2);
    REQUIRE(frame.cell_frustum_rejects == 1 && g_edx == PLANES + 33 * 16);
    s_active = NULL;
    benchmark_fixture(0); MEM32(MASK) = 0; sub_001C7B15();
    REQUIRE(g_eax == 0 && g_esp == STACK + 8);
    return 1;
}
static double benchmark(void (*function)(void), unsigned iterations, unsigned kind)
{
    LARGE_INTEGER start, end, frequency;
    unsigned i;
    benchmark_fixture(kind);
    kind %= 5;
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
    for (i = 0; i < iterations; ++i) {
        g_ecx = BOX; g_edx = PLANES; g_esp = STACK; g_ebx = MASK;
        MEM32(MASK) = kind == 3 ? 0 : kind == 4 ? 0x15u : 0x3fu;
        function();
    }
    QueryPerformanceCounter(&end);
    return (end.QuadPart - start.QuadPart) * 1e9 /
        (double)frequency.QuadPart / iterations;
}
static void write_file(const char *folder, unsigned test, const char *suffix,
                       const void *data, size_t size)
{
    char path[1024]; FILE *out;
    sprintf(path, "%s/%03u.%s", folder, test, suffix);
    out = fopen(path, "wb");
    if (!out || fwrite(data, 1, size, out) != size) exit(2);
    fclose(out);
}
int main(int argc, char **argv)
{
    unsigned i, initial_mxcsr = _mm_getcsr();
    g_xbox_mem_offset = (ptrdiff_t)memory - BASE;
    if (argc == 3 && !strcmp(argv[1], "--raw")) {
        for (i = 0; i < 256; ++i) {
            uint32_t regs[10], original_bp;
            seed(i, 1); original_bp = g_ebp;
            regs[0] = g_eax; regs[1] = g_ecx; regs[2] = g_edx; regs[3] = g_esp;
            regs[4] = g_ebx; regs[5] = g_esi; regs[6] = g_edi; regs[7] = g_ebp;
            regs[8] = g_df; regs[9] = g_fp_control_word;
            write_file(argv[2], i, "before", memory, sizeof memory);
            write_file(argv[2], i, "registers", regs, sizeof regs);
            _mm_setcsr((initial_mxcsr & ~0x6000u) |
                       (((g_fp_control_word >> 10) & 3u) << 13));
            sub_001C7B15_original();
            regs[0] = g_eax; regs[1] = g_ecx; regs[2] = g_edx; regs[3] = g_esp;
            regs[4] = g_ebx; regs[5] = g_esi; regs[6] = g_edi; regs[7] = original_bp;
            write_file(argv[2], i, "after", memory, sizeof memory);
            write_file(argv[2], i, "expected", regs, sizeof regs);
        }
        return 0;
    }
    if (!invalid_span_contract()) { puts("FAIL: speculative span/read contract"); return 1; }
    if (!path_contract()) return 1;
    fallback_calls = 0;
    for (i = 0; i < 200000; ++i) {
        State start, expected, actual;
        seed(i, 0);
        _mm_setcsr((initial_mxcsr & ~(0x6000u | 0x8040u | 0x3fu)) |
                   (((i / 128) % 4) << 13) | (((i / 512) % 2) << 15) |
                   (((i / 1024) % 2) << 6) | (i % 29 == 0 ? 0x3fu : 0));
        start = snapshot(); memcpy(initial, memory, sizeof memory);
        masked_visibility_oracle(); expected = snapshot();
        memcpy(expected_memory, memory, sizeof memory);
        restore(start); memcpy(memory, initial, sizeof memory);
        sub_001C7B15_original(); actual = snapshot();
        if (memcmp(&expected, &actual, sizeof expected) ||
            memcmp(expected_memory, memory, sizeof memory)) {
            unsigned n;
            printf("FAIL case %u: control=%04X top=%d alias=%u count=%u\n",
                   i, start.cw, start.top, i % 11,
                   *(uint32_t *)(initial + STACK - BASE + 4));
            for (n = 0; n < sizeof expected; ++n)
                if (((unsigned char *)&expected)[n] != ((unsigned char *)&actual)[n])
                    printf("state byte %u: %02X != %02X\n", n,
                           ((unsigned char *)&expected)[n], ((unsigned char *)&actual)[n]);
            for (n = 0; n < sizeof memory; ++n)
                if (expected_memory[n] != memory[n]) {
                    printf("RAM byte %u: %02X != %02X\n", n, expected_memory[n], memory[n]);
                    break;
                }
            return 1;
        }
    }
    _mm_setcsr(initial_mxcsr);
    if (fallback_calls < 10000 || fallback_calls > 190000) {
        puts("FAIL: fast and generated fallback coverage"); return 1;
    }
    printf("PASS: 200000 complete state/RAM/MXCSR cases (%u fast / %u fallback), all mask/count/alias/PC/RC/TOP/DF paths\n",
           200000 - fallback_calls, fallback_calls);
    if (argc > 1) {
        static const char *names[] = {"six intersecting", "first rejected", "six cleared", "all skipped", "sparse"};
        unsigned kind;
        for (kind = 0; kind != 10; ++kind) {
            for (i = 0; i < 5; ++i) {
                double original, optimized;
                if (i & 1) {
                    optimized = benchmark(sub_001C7B15_original, 500000, kind);
                    original = benchmark(masked_visibility_oracle, 500000, kind);
                } else {
                    original = benchmark(masked_visibility_oracle, 500000, kind);
                    optimized = benchmark(sub_001C7B15_original, 500000, kind);
                }
                printf("BENCH PC%u %s: original %.3f ns; optimized %.3f ns; %.3fx\n",
                       (g_fp_control_word >> 8) & 3, names[kind % 5],
                       original, optimized, original / optimized);
            }
        }
    }
    return 0;
}
