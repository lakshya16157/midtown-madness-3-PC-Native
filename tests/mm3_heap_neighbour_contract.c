/* RtlFreeHeap's coalescing against a neighbour whose header was overwritten
 * (src/mm3_heap_guard.c), on a fake guest heap. The headers are 16 bytes: +0
 * size and +2 size of the chunk before (16-byte units), +4 index, +5 flags
 * (1 busy, 8 own block, 0x10 last in its segment), +8/+C free-list links, which
 * are node addresses (chunk + 8) of a doubly linked list. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
uint32_t g_xbox_code_lo = 0x00012000u, g_xbox_code_hi = 0x00259180u;
static unsigned char memory[0x400000];

int mm3_heap_double_free(uint32_t heap, uint32_t caller, uint32_t pointer);
int mm3_heap_damaged_neighbour(uint32_t pointer);

#define HEAP 0x00200000u
#define HEAD (HEAP + 0x180u)          /* a free-list head node: [0] forward, [4] back */
#define P 0x00221000u                 /* chunk before */
#define B 0x00221040u                 /* the chunk being freed; its block is B + 16 */
#define N 0x00221080u                 /* chunk after */
#define Q 0x002210C0u                 /* the one after that */
#define FLOATS_A 0x3DCCCCCDu          /* 0.1f: both links of the first crash */
#define FLOATS_B 0x3F266666u          /* 0.65f and 7.24f: the second */
#define FLOATS_C 0x40E79CAFu

static void chunk(uint32_t at, unsigned size, unsigned before, unsigned flags)
{
    memset(memory + at, 0, 16);
    MEM16(at) = (uint16_t)size;
    MEM16(at + 2) = (uint16_t)before;
    MEM8(at + 4) = 2;
    MEM8(at + 5) = (uint8_t)flags;
}

/* Puts the chunk on the list headed at HEAD (alone, or after `after`'s node). */
static void list_add(uint32_t at)
{
    uint32_t node = at + 8u, last = MEM32(HEAD + 4u);
    MEM32(node) = HEAD; MEM32(node + 4u) = last;
    MEM32(last) = node; MEM32(HEAD + 4u) = node;
}

static void heap(void)
{
    memset(memory + P, 0, 0x1000);
    MEM32(HEAD) = HEAD; MEM32(HEAD + 4u) = HEAD;
    chunk(P, 4, 0, 1);
    chunk(B, 4, 4, 1);
    chunk(N, 4, 4, 1);
    chunk(Q, 4, 4, 1);
}

static void trample(uint32_t at, uint32_t a, uint32_t b, uint32_t c)
{
    MEM32(at + 4u) = a; MEM32(at + 8u) = b; MEM32(at + 0xCu) = c;   /* a vec3 over index, flags and links */
}

int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)memory;
    g_esp = 0x0000FE00u;

    /* A healthy heap: a free neighbour on a list, a busy one, none. */
    heap();
    assert(mm3_heap_damaged_neighbour(B + 16u) == 0);
    heap(); chunk(N, 4, 4, 0); list_add(N);
    assert(mm3_heap_damaged_neighbour(B + 16u) == 0);
    heap(); chunk(P, 4, 0, 0); list_add(P); chunk(N, 4, 4, 0); list_add(N);
    assert(mm3_heap_damaged_neighbour(B + 16u) == 0);
    puts("PASS heap neighbours: free neighbours on a list, and busy ones, are left to the heap");

    /* The first crash: the chunk after is all 0.1f from +4 on (flags CC: free). */
    heap(); trample(N, FLOATS_A, FLOATS_A, FLOATS_A);
    assert(mm3_heap_damaged_neighbour(B + 16u) == 1);
    assert(MEM8(N + 5u) == 0xCCu && MEM32(N + 8u) == FLOATS_A);         /* only read, nothing written */
    /* ...and the owner's own free of it is skipped as a second free. */
    assert(mm3_heap_double_free(HEAP, 0x00012345u, N + 16u));
    puts("PASS heap neighbours: a chunk after with 0.1f links makes the free skip, untouched");

    /* The second crash: links 0.65f and 7.24f (the faulting store went to 0x40E79CB3). */
    heap(); trample(N, 0x1234u, FLOATS_B, FLOATS_C);
    MEM8(N + 5u) = 0x66u;
    assert(mm3_heap_damaged_neighbour(B + 16u) == 1 && MEM8(N + 5u) == 0x66u);
    puts("PASS heap neighbours: the second crash's links make the free skip");

    /* Links inside the heap that do not lead back are damage as well. */
    heap(); chunk(N, 4, 4, 0); list_add(N); MEM32(N + 8u) = Q + 8u;     /* Q + 8 does not point back at N */
    assert(mm3_heap_damaged_neighbour(B + 16u) == 1);
    heap(); chunk(N, 4, 4, 0); MEM32(N + 8u) = 0x00221001u; MEM32(N + 0xCu) = HEAD;   /* unaligned */
    assert(mm3_heap_damaged_neighbour(B + 16u) == 1);
    /* Zero links are a normal free chunk (2026-11-02 loading slowdown: they were treated as damage). */
    heap(); chunk(N, 4, 4, 0); MEM32(N + 8u) = 0; MEM32(N + 0xCu) = 0;
    assert(mm3_heap_damaged_neighbour(B + 16u) == 0);
    puts("PASS heap neighbours: links that do not lead back or are unaligned are damage; zero links are not");

    /* The chunk before. */
    heap(); chunk(P, 4, 0, 0); trample(P, FLOATS_A, FLOATS_A, FLOATS_A); MEM8(P + 5u) = 0xCCu;
    assert(mm3_heap_damaged_neighbour(B + 16u) == 1);
    puts("PASS heap neighbours: the chunk before is checked too");

    /* Not looked at: the chunk after a segment's last chunk, and an own block. */
    heap(); MEM8(B + 5u) = 0x11u; trample(N, FLOATS_A, FLOATS_A, FLOATS_A);
    assert(mm3_heap_damaged_neighbour(B + 16u) == 0);
    heap(); MEM8(B + 5u) = 0x09u; trample(N, FLOATS_A, FLOATS_A, FLOATS_A);
    assert(mm3_heap_damaged_neighbour(B + 16u) == 0);
    puts("PASS heap neighbours: a last chunk and an own block are not merged, so not checked");

    /* A chunk before that lies below the guest's first page cannot be made safe: the free is skipped. */
    {
        uint32_t low = 0x00011020u;
        chunk(low - 16u, 4, 0x1000, 1);
        assert(mm3_heap_damaged_neighbour(low) == 1);
        puts("PASS heap neighbours: a neighbour outside the guest memory skips the free");
    }

    /* Pointers the heap code would not take are none of its business. */
    assert(mm3_heap_damaged_neighbour(0) == 0);
    assert(mm3_heap_damaged_neighbour(B + 17u) == 0);
    return 0;
}
