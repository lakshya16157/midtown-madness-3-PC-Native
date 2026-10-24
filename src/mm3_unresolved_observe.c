#include <windows.h>
#include <stdio.h>
#include "recomp_types.h"

/* Every unresolved stub (src/recomp/gen/recomp_stubs_unresolved.c) stands in
 * for game code the translator never lifted. Most are reached by a jump from a
 * neighbouring function and only pop a return address, so the code there -
 * often an epilogue restoring callee-saved registers, sometimes a loop body -
 * never runs. 0x00241E85 was such a stub: a skipped epilogue left ESI/EDI
 * scrambled, and after a lost second circuit race no results or post-race menu
 * appeared (2026-10-23).
 *
 * The first time each stub runs it is logged as [MM3_UNRESOLVED] with the game
 * code on the guest stack, so stubs that are really reached can be lifted from
 * evidence (mm3_runtime_entry_repairs.json, scripts/Generate-MM3RuntimeEntries.py). */
#define SEEN_SLOTS 1024u

static volatile LONG s_seen[SEEN_SLOTS];

/* Record va; 1 the first time it is seen, else 0. Lock-free open addressing. */
static int first_time(uint32_t va)
{
    unsigned slot = ((va * 2654435761u) >> 22) & (SEEN_SLOTS - 1u), probe;
    for (probe = 0; probe < SEEN_SLOTS; ++probe, slot = (slot + 1u) & (SEEN_SLOTS - 1u)) {
        LONG held = s_seen[slot];
        if (held == (LONG)va) return 0;
        if (!held) {
            held = InterlockedCompareExchange(&s_seen[slot], (LONG)va, 0);
            if (!held) return 1;
            if (held == (LONG)va) return 0;
        }
    }
    return 0;
}

void mm3_unresolved_stub_reached(uint32_t va)
{
    char text[256];
    unsigned i, found = 0;
    int at;
    if (!first_time(va)) return;
    at = snprintf(text, sizeof text, "[MM3_UNRESOLVED] 0x%08X reached (stub: skips that game code); "
                  "game code on the stack:", va);
    if (g_esp >= 0x00010000u && g_esp < 0x07FFFF00u)
        for (i = 0; i < 32 && found < 6 && at < (int)sizeof text - 12; ++i) {
            uint32_t word = MEM32(g_esp + 4u * i);
            if (word >= 0x00012000u && word < 0x00259180u) {
                at += snprintf(text + at, sizeof text - (size_t)at, " %08X", word);
                ++found;
            }
        }
    fprintf(stderr, "%s\n", text);
}
