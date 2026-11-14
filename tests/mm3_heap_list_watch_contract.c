/* MM3_HEAP_LIST_WATCH's check (src/mm3_heap_list_watch.c) on a fake guest heap:
 * a healthy large free list passes; a forward link of zero, a misaligned one
 * (the 2026-11-13 crash: 058B86F0 -> 04026DFC) and one past guest memory are
 * found on the right node, and the report names it. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char memory[0x400000];
#include "../src/mm3_heap_list_watch.c"

#define HEAP 0x00200000u
#define HEAD (HEAP + 0x180u)
#define A 0x00221000u        /* free chunks of 0x100, 0x200 and 0x300 units, in size order */
#define B 0x00222000u
#define C 0x00224000u

static void chunk(uint32_t at, unsigned size, unsigned before)
{
    memset(memory + at, 0, 16);
    MEM16(at) = (uint16_t)size;
    MEM16(at + 2u) = (uint16_t)before;
    MEM8(at + 4u) = 2;
}

static void list_add(uint32_t at)
{
    uint32_t node = at + 8u, last = MEM32(HEAD + 4u);
    MEM32(node) = HEAD; MEM32(node + 4u) = last;
    MEM32(last) = node; MEM32(HEAD + 4u) = node;
}

static void heap(void)
{
    memset(memory + HEAP, 0, 0x30000);
    MEM32(HEAD) = HEAD; MEM32(HEAD + 4u) = HEAD;
    chunk(A, 0x100, 4); list_add(A);
    chunk(B, 0x200, 0x100); list_add(B);
    chunk(C, 0x300, 0x200); list_add(C);
}

int main(void)
{
    Mm3HeapListBadLink bad;
    g_xbox_mem_offset = (ptrdiff_t)memory;

    memset(memory + HEAP, 0, 0x200);
    MEM32(HEAD) = HEAD; MEM32(HEAD + 4u) = HEAD;
    assert(!mm3_heap_list_bad_link(HEAP, &bad));           /* empty list */
    heap();
    assert(!mm3_heap_list_bad_link(HEAP, &bad));           /* three healthy nodes */
    assert(!mm3_heap_list_bad_link(0, &bad));              /* no heap there: nothing walked */
    puts("PASS heap list: an empty and a healthy large free list pass");

    heap(); MEM32(B + 8u) = 0;                             /* B's forward link zeroed */
    assert(mm3_heap_list_bad_link(HEAP, &bad));
    assert(bad.node == B + 8u && bad.link == 0 && bad.back == A + 8u && bad.prev == A + 8u && bad.index == 1);
    heap(); MEM32(A + 8u) = 0x04026DFCu;                   /* misaligned, as in the crash */
    assert(mm3_heap_list_bad_link(HEAP, &bad));
    assert(bad.node == A + 8u && bad.link == 0x04026DFCu && bad.prev == HEAD && bad.index == 0);
    heap(); MEM32(C + 8u) = 0x3DCCCCCDu;                   /* 0.1f: past guest memory */
    assert(mm3_heap_list_bad_link(HEAP, &bad));
    assert(bad.node == C + 8u && bad.link == 0x3DCCCCCDu && bad.index == 2);
    assert(MEM32(C + 8u) == 0x3DCCCCCDu && MEM16(C) == 0x300);   /* only read */
    mm3_heap_list_report(HEAP, &bad, 12);
    puts("PASS heap list: a zero, a misaligned and an out-of-range forward link are found on their node");
    return 0;
}
