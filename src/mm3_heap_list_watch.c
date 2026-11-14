/* Diagnostic: catch damage to the game heap's large free list when it happens,
 * not at the next allocation that walks it.
 *
 *   MM3_HEAP_LIST_WATCH=1          watch the game heap at 0x04000000
 *   MM3_HEAP_LIST_WATCH=<hex VA>   watch another heap
 *
 * Off unless set. The heap's chunks have 16-byte headers (+0 size and +2 size
 * of the chunk before, in 16-byte units, +4 index, +5 flags; see
 * mm3_heap_guard.c) and free chunks link through +8 (forward) and +C (back);
 * links are node addresses, chunk + 8. Chunks of 0x80 units and more sit on
 * one list headed at heap + 0x180, kept sorted by size: RtlAllocateHeap's
 * insert walks it (0x84A5C reads MEM16(node - 8) for each node's size). A
 * forward link that is not a node there (zero, or a pointer into the middle of
 * something) crashes that walk later, far from whoever wrote it.
 *
 * A thread walks the list every millisecond. A forward link that is outside
 * guest memory or not 8-aligned, seen on the same node in two walks in a row
 * (the game may be mid-update on one), is reported once per node as
 * [HEAP_LIST] lines: the node, its link and back link, the list neighbour, and
 * the chunk's bytes with 64 bytes either side and the chunk before it. The
 * overwritten bytes, and the time of the report against the game's own log,
 * point at the writer. The walk only reads.
 *
 * Found with it (2026-11-13, a Linux fork's test boot that started a race
 * while the game was still booting): chunk 058B86F0's forward link was
 * 04026DFC, which crashed the allocator about two seconds later. */
#include "platform/xbox_winnt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_heap_list_watch.h"

#define LIST_HEAD 0x180u
#define MAX_NODES 8192u
#define MAX_REPORTS 16u

static int guest_span(uint32_t va, uint32_t bytes)
{
    return va >= 0x00010000u && va <= 0x08000000u - bytes;
}

int mm3_heap_list_bad_link(uint32_t heap, Mm3HeapListBadLink *out)
{
    uint32_t head = heap + LIST_HEAD, prev = head, node;
    unsigned i;
    if (!guest_span(head, 8)) return 0;
    node = MEM32(head);
    for (i = 0; i < MAX_NODES && node != head; ++i) {
        uint32_t next;
        if (!guest_span(node, 8) || (node & 7u)) return 0;     /* the head itself, mid-update */
        next = MEM32(node);
        if (next != head && (!guest_span(next, 8) || (next & 7u))) {
            out->node = node;
            out->link = next;
            out->back = MEM32(node + 4u);
            out->prev = prev;
            out->index = i;
            return 1;
        }
        prev = node;
        node = next;
    }
    return 0;
}

static void dump(const char *what, uint32_t va, unsigned words)
{
    char line[512];
    int at;
    unsigned i;
    if (!guest_span(va, words * 4u)) {
        fprintf(stderr, "[HEAP_LIST] %s %08X: outside guest memory\n", what, va);
        return;
    }
    at = snprintf(line, sizeof line, "[HEAP_LIST] %s %08X:", what, va);
    for (i = 0; i < words && at < (int)sizeof line - 10; ++i)
        at += snprintf(line + at, sizeof line - (size_t)at, " %08X", MEM32(va + 4u * i));
    fprintf(stderr, "%s\n", line);
}

void mm3_heap_list_report(uint32_t heap, const Mm3HeapListBadLink *bad, unsigned long long ms)
{
    uint32_t chunk = bad->node - 8u;
    uint32_t before = chunk - 16u * (uint32_t)MEM16(chunk + 2u);
    fprintf(stderr, "[HEAP_LIST] heap %08X: free chunk %08X (%u units) links forward to %08X, not a node "
                    "(back %08X, list neighbour before it %08X, item %u, %llu ms after the watch started)\n",
            heap, chunk, MEM16(chunk), bad->link, bad->back, bad->prev, bad->index, ms);
    dump("chunk - 64", chunk - 64u, 16);
    dump("chunk", chunk, 16);
    dump("chunk + 64", chunk + 64u, 16);
    dump("the chunk before it (by its size field)", before, 8);
    dump("the list neighbour's chunk", bad->prev - 8u, 8);
    fflush(stderr);
}

static uint32_t s_heap;

static DWORD WINAPI watch(LPVOID unused)
{
    Mm3HeapListBadLink seen = {0}, now;
    uint32_t reported[MAX_REPORTS];
    unsigned count = 0, i;
    ULONGLONG start = GetTickCount64();
    (void)unused;
    for (;;) {
        if (mm3_heap_list_bad_link(s_heap, &now)) {
            if (now.node == seen.node && now.link == seen.link) {
                for (i = 0; i < count && reported[i] != now.node; ++i) {}
                if (i == count && count < MAX_REPORTS) {
                    reported[count++] = now.node;
                    mm3_heap_list_report(s_heap, &now, (unsigned long long)(GetTickCount64() - start));
                }
            }
            seen = now;
        } else {
            seen.node = 0;
        }
        Sleep(1);
    }
    return 0;
}

void mm3_heap_list_watch_start(void)
{
    const char *setting = getenv("MM3_HEAP_LIST_WATCH");
    HANDLE thread;
    if (!setting || !*setting || !strcmp(setting, "0")) return;
    s_heap = strcmp(setting, "1") ? (uint32_t)strtoul(setting, NULL, 16) : 0x04000000u;
    if (!guest_span(s_heap + LIST_HEAD, 8)) {
        fprintf(stderr, "[HEAP_LIST] MM3_HEAP_LIST_WATCH=%s: not a guest heap address\n", setting);
        return;
    }
    thread = CreateThread(NULL, 0, watch, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    fprintf(stderr, "[HEAP_LIST] watching the large free list of the heap at %08X%s\n",
            s_heap, thread ? "" : ": no thread, not watching");
}
