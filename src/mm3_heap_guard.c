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

/* Merging with a neighbour that is not really free.
 *
 * RtlFreeHeap joins the freed chunk with the chunk before and after it when
 * their busy flag (+5, bit 0) is clear, by taking that chunk off its free list:
 * forward = [chunk + 8], back = [chunk + 0xC] (node addresses, chunk + 8),
 * then [back] = forward and [forward + 4] = back (sub_00084709, 0x8491C and
 * 0x84898). A chunk that is really a live block whose 16-byte header was
 * overwritten with other data can look free, with the data as its links.
 * Both stress sessions on 2026-11-02 (maximum settings, repeated races)
 * crashed exactly there, writing through floats (0x3DCCCCD1 = 0.1f + 4;
 * 0x40E79CB3 = 7.24f) taken for the links, during the teardown of the
 * previous race. Who overwrites the header is not known; the block it belongs
 * to is alive and keeps its data.
 *
 * A chunk that is on a free list has non-zero links to nodes that link back to
 * it; free chunks with zero links are normal and are left to the heap (treating
 * them as damage skipped merges and, with a 1 ms retry pause, slowed loading to
 * minutes, 2026-11-02). This runs before RtlFreeHeap takes the heap's lock, so it
 * only reads: when a neighbour looks damaged, the caller skips this free (a leak
 * of one block, not a crash). Writing to another chunk's header here could race
 * another thread's allocation; a misread during such a race costs one block. */
static int guest_span(uint32_t address, uint32_t bytes)
{
    return address >= 0x00010000u && address <= 0x08000000u - bytes;
}

static int free_chunk_linked(uint32_t chunk)
{
    uint32_t node = chunk + 8u, forward = MEM32(node), back = MEM32(node + 4u);
    return guest_span(forward, 8u) && guest_span(back, 8u) && !((forward | back) & 3u) &&
           MEM32(forward + 4u) == node && MEM32(back) == node;
}

static int damaged_neighbour(uint32_t chunk)
{
    return !guest_span(chunk, 16u) ||
           ((MEM8(chunk + 5u) & 1u) == 0 && (MEM32(chunk + 8u) | MEM32(chunk + 0xCu)) != 0 &&
            !free_chunk_linked(chunk));
}

/* 1 when the chunk before or after the one being freed looks free but is not
 * on a free list (or lies outside the guest memory): the free is to be skipped. */
int mm3_heap_damaged_neighbour(uint32_t pointer)
{
    static unsigned reports;
    uint32_t chunk = pointer - 0x10u, flags, size, before, other[2];
    unsigned i;
    if (pointer < 0x00010010u || pointer >= 0x08000000u || (pointer & 7u)) return 0;
    flags = MEM8(chunk + 5u);
    if (flags & 8u) return 0;                          /* a block of its own: never merged */
    size = MEM16(chunk);
    before = MEM16(chunk + 2u);
    other[0] = before ? chunk - before * 16u : 0;
    other[1] = size && !(flags & 0x10u) ? chunk + size * 16u : 0;
    for (i = 0; i < 2; ++i) {
        if (!other[i] || !damaged_neighbour(other[i])) continue;
        if (reports < 16) {
            char text[256];
            unsigned k, found = 0;
            int at;
            ++reports;
            at = snprintf(text, sizeof text,
                          "[HEAP_NEIGHBOUR] free of %08X: the chunk %s it at %08X says free but its links %08X,%08X "
                          "are not on a free list; free skipped (one block leaked). Game code on the stack:",
                          pointer, i ? "after" : "before", other[i],
                          guest_span(other[i], 16u) ? MEM32(other[i] + 8u) : 0u,
                          guest_span(other[i], 16u) ? MEM32(other[i] + 0xCu) : 0u);
            for (k = 0; k < 48 && found < 8 && at < (int)sizeof text - 12; ++k) {
                uint32_t word = MEM32(g_esp + 4u * k);
                if (word >= 0x00012000u && word < 0x00259180u) {
                    at += snprintf(text + at, sizeof text - (size_t)at, " %08X", word);
                    ++found;
                }
            }
            fprintf(stderr, "%s\n", text);
        }
        return 1;
    }
    return 0;
}
