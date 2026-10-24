#include <stdio.h>
#include "recomp_types.h"

/* RtlFreeHeap (sub_000860AA: stdcall heap, flags, pointer). Its block header is
 * 16 bytes before the pointer: size in 16-byte units at +0, flags at +5 with
 * 0x01 = busy (read from live blocks 2026-10-23). Freeing a block that is not
 * busy is a second free of it: it would sit on a free list twice, and two later
 * allocations would share it. Replays of a lost circuit race after an earlier
 * one showed that kind of damage: a menu screen's vtable zeroed through a
 * foreign object in a widget link (no results or post-race menu), then a
 * request for 4 GB from the heap. The caller skips such a free; it is logged
 * as [HEAP_DOUBLE_FREE] with the game code on the stack. */
int mm3_heap_double_free(uint32_t heap, uint32_t caller, uint32_t pointer)
{
    static unsigned reports;
    uint32_t entry = pointer - 0x10u;
    if (pointer < 0x00010010u || pointer >= 0x08000000u || (pointer & 7u) || (MEM8(entry + 5u) & 0x01u))
        return 0;
    if (reports < 32) {
        char text[256];
        unsigned i, found = 0;
        int at = snprintf(text, sizeof text,
                          "[HEAP_DOUBLE_FREE] heap=%08X pointer=%08X header=%08X,%08X caller=%08X; free skipped. "
                          "Game code on the stack:", heap, pointer, MEM32(entry), MEM32(entry + 4u), caller);
        for (i = 4; i < 48 && found < 8 && at < (int)sizeof text - 12; ++i) {
            uint32_t word = MEM32(g_esp + 4u * i);
            if (word >= 0x00012000u && word < 0x00259180u) {
                at += snprintf(text + at, sizeof text - (size_t)at, " %08X", word);
                ++found;
            }
        }
        ++reports;
        fprintf(stderr, "%s\n", text);
    }
    return 1;
}
