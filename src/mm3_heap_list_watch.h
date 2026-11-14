#ifndef MM3_HEAP_LIST_WATCH_H
#define MM3_HEAP_LIST_WATCH_H
/* MM3_HEAP_LIST_WATCH diagnostic (src/mm3_heap_list_watch.c). */
#include <stdint.h>

typedef struct {
    uint32_t node, link, back, prev;   /* the node (chunk + 8), its bad forward link, its back link,
                                          and the node before it on the list */
    unsigned index;                    /* its position on the list */
} Mm3HeapListBadLink;

/* 1 and *out filled when a node on the heap's large free list (heap + 0x180)
 * links forward to something that is not a node: outside guest memory or not
 * 8-aligned. Only reads. */
int mm3_heap_list_bad_link(uint32_t heap, Mm3HeapListBadLink *out);
/* The [HEAP_LIST] report for one bad link. */
void mm3_heap_list_report(uint32_t heap, const Mm3HeapListBadLink *bad, unsigned long long ms);
/* Starts the watch thread when MM3_HEAP_LIST_WATCH is set. */
void mm3_heap_list_watch_start(void);

#endif
