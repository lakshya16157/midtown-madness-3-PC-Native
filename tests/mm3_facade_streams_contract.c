/* Facade stream use (src/mm3_facade_streams.c) without the game: the two
 * native allocators are replaced by their PAL capacity checks (detail: quads
 * x 4 vertices from +0x44 against +0x48; far: from the front of +0x1F80 and
 * +0x1F84 against +0x1F88) and frames are driven by hand. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t test_clock;
#define MM3_FACADE_STREAMS_CLOCK() test_clock
#include "mm3_facade_streams.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_esi, g_ebx, g_edi, g_ebp, g_seh_ebp;
RECOMP_TLS int g_fp_top, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word, g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
static unsigned char memory[0x200000];
static const uint32_t manager = 0x100000;

void sub_00162EBA_original(void)
{
    uint32_t want = g_edx * 4u, used = MEM32(g_ecx + 0x44u);
    if (used + want > MEM32(g_ecx + 0x48u)) { g_eax = 0; return; }
    MEM32(g_ecx + 0x44u) = used + want;
    g_eax = 0x1000u + used * 32u;
}

void sub_00162EF3_original(void)
{
    uint32_t want = g_edx * 4u, front = MEM32(g_ecx + 0x1F80u), back = MEM32(g_ecx + 0x1F84u);
    if (front + back + want > MEM32(g_ecx + 0x1F88u)) { g_eax = 0; return; }
    MEM32(g_ecx + 0x1F80u) = front + want;
    g_eax = 0x2000u + front * 24u;
}

static void begin_frame(uint32_t set)
{
    MEM32(manager + 0x1F8Cu) = set;
    MEM32(manager + 0x44u) = 0;
    MEM32(manager + 0x1F80u) = MEM32(manager + 0x1F84u) = 0;
}

static uint32_t detail(uint32_t quads)
{
    g_ecx = manager;
    g_edx = quads;
    sub_00162EBA();
    return g_eax;
}

static uint32_t far_quads(uint32_t quads)
{
    g_ecx = manager;
    g_edx = quads;
    sub_00162EF3();
    return g_eax;
}

int main(void)
{
    unsigned i, granted = 0;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    MEM32(manager + 0x48u) = 0x600;                 /* 1,536 vertices: 384 quads */
    MEM32(manager + 0x1F88u) = 0x800;
    MEM32(manager + 0x1ED0u) = 0xC000;
    MEM32(manager + 0x1EC8u) = 100;
    MEM32(manager + 0x1ECCu) = 20;
    MEM32(manager + 0x1F2Cu) = 0x2000;
    MEM32(manager + 0x1F24u) = 8;
    test_clock = 1000;

    /* A frame asking 400 detail quads: 384 fit, 16 are refused, and the
     * demand counts them. */
    begin_frame(0);
    for (i = 0; i < 400; ++i) granted += detail(1) != 0;
    assert(granted == 384);
    assert(s_streams[FS_DETAIL].requests == 400 && s_streams[FS_DETAIL].refused == 16);
    assert(s_streams[FS_DETAIL].peak == 1536 && s_streams[FS_DETAIL].demand == 1536 + 64);
    assert(s_streams[FS_DETAIL].capacity == 1536);

    /* The next frame uses the other VB set: its refusals start from zero. */
    begin_frame(1);
    for (i = 0; i < 100; ++i) assert(detail(1));
    assert(s_streams[FS_DETAIL].frame_refused == 0 && s_streams[FS_DETAIL].demand == 1600);
    assert(s_streams[FS_DETAIL].peak == 1536);

    /* Far quads: a request past the pool is refused and counted. */
    assert(far_quads(500));
    assert(!far_quads(13));
    assert(s_streams[FS_FAR].refused == 1 && s_streams[FS_FAR].requests == 2);
    assert(s_streams[FS_FAR].peak == 2000 && s_streams[FS_FAR].demand == 2052);
    assert(s_streams[FS_FAR].capacity == 2048);

    /* The 28-byte streams are sampled at the same time. */
    assert(s_streams[FS_OPAQUE].peak == 120 && s_streams[FS_OPAQUE].capacity == 0xC000);
    assert(s_streams[FS_ALPHA].peak == 8 && s_streams[FS_ALPHA].capacity == 0x2000);

    /* The clock is read every 256th request. 29 s on, the checks at the
     * 512th and 768th keep the window; 31 s on, the 1024th (the 222nd below)
     * reports and starts a new window with the other 78. */
    test_clock += 29000;
    for (i = 0; i < 300; ++i) detail(0);
    assert(s_streams[FS_DETAIL].requests == 400 + 100 + 300 && s_window_start == 1000);
    test_clock += 2000;
    for (i = 0; i < 300; ++i) detail(0);
    assert(s_streams[FS_DETAIL].refused == 0 && s_streams[FS_DETAIL].requests == 78);
    assert(s_streams[FS_FAR].refused == 0 && s_streams[FS_FAR].requests == 0);
    assert(s_window_start == test_clock);

    /* A manager outside guest memory is passed through, not counted. */
    i = s_streams[FS_DETAIL].requests;
    MEM32(0x100u + 0x48u) = 0x600;
    g_ecx = 0x100;
    g_edx = 1;
    sub_00162EBA();
    assert(g_eax && s_streams[FS_DETAIL].requests == i);
    puts("PASS: facade stream use: refusals and demand per frame, VB-set frames, far pool from both ends, sampled 28-byte streams, 30 s window");
    return 0;
}
