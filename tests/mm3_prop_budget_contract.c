/* Street-prop model queue budget: the 3D and billboard ranges (0x3A8C90/94) are pulled in while
 * the 512-entry model queue overflows, never below 30%, not while the billboard list is nearly
 * full, and let out again once the queue has room. Guest memory is a plain array. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;

static unsigned char memory[0x400000];
static const uint32_t owner = 0x100000;
static unsigned original_calls;

void sub_0016A476_original(void) { ++original_calls; }

#include "../src/mm3_prop_budget.c"

static void frame(unsigned models, unsigned billboards, unsigned refused)
{
    unsigned i;
    MEM32(owner + 0x13E3C) = models;
    MEM32(owner + 0x112C) = billboards;
    for (i = 0; i < refused; ++i) mm3_prop_budget_refused();
    g_eax = owner;
    sub_0016A476();
    assert(g_eax == owner);                       /* the draw gets its register back untouched */
}

static float near_range(void) { return MEMF(0x003A8C90u); }
static float far_range(void) { return MEMF(0x003A8C94u); }

int main(void)
{
    unsigned i, calls;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    MEMF(0x003A8C90u) = 250.0f;
    MEMF(0x003A8C94u) = 350.0f;

    frame(100, 20, 0);                            /* plenty of room: nothing moves */
    assert(near_range() == 250.0f && far_range() == 350.0f && original_calls == 1);

    frame(512, 20, 5);                            /* overflow: both ranges pull in together */
    assert(near_range() < 250.0f * 0.93f && near_range() > 250.0f * 0.91f);
    assert(far_range() / near_range() > 1.399f && far_range() / near_range() < 1.401f);

    for (i = 0; i < 200; ++i) frame(512, 20, 9);  /* keeps overflowing: it stops at 30% */
    assert(near_range() >= 250.0f * 0.299f && near_range() <= 250.0f * 0.301f);

    for (i = 0; i < 400; ++i) frame(100, 20, 0);  /* room again: back to the setting, never past it */
    assert(near_range() == 250.0f && far_range() == 350.0f);

    for (i = 0; i < 30; ++i) frame(512, 450, 9);  /* billboard list nearly full: no further pulling in */
    assert(near_range() == 250.0f);

    frame(500, 20, 0);                            /* nearly full but nothing refused yet: still pulls in */
    assert(near_range() < 250.0f);
    calls = original_calls;
    frame(420, 20, 0);                            /* between the thresholds: holds */
    {
        float held = near_range();
        frame(420, 20, 0);
        assert(near_range() == held);
    }
    assert(original_calls == calls + 2);
    puts("PASS: prop budget pulls the ranges in on overflow, stops at 30% and when billboards are full, recovers, keeps the draw's register");
    return 0;
}
