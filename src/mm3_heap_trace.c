/* Opt-in diagnostics for the title's own heap (MM3_HEAP_TRACE=1): the heap behind
 * RtlAllocateHeap sub_000858F3 / RtlFreeHeap sub_000860AA at 0x04000000..0x08000000.
 * Chunk header (16 bytes, 16-byte units): +0 size, +2 size of the chunk before,
 * +5 flags (1 busy, 0x10 last in its segment), +8/+C free-list links.
 * Every allocation records who asked (guest return addresses found on the stack);
 * every free checks the chunk's header and its neighbours' back-size fields and
 * logs [HEAP_TRACE] BAD with the owner of the damaged chunk and of the chunk before
 * it. Nothing is logged or recorded unless the variable is set. */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp_types.h"

#define HEAP_LO 0x04000000u
#define HEAP_HI 0x08000000u

typedef struct { uint32_t seq, req, tid, caller[3], free_caller, free_seq; } Record;

static Record *records;
static int state = -1;
static volatile LONG sequence, reports;

int mm3_heap_trace_on(void)
{
    if (state < 0) {
        const char *value = getenv("MM3_HEAP_TRACE");
        state = (value && *value && *value != '0') ? 1 : 0;
        if (state) {
            records = (Record *)VirtualAlloc(NULL, (SIZE_T)((HEAP_HI - HEAP_LO) / 16u) * sizeof(Record),
                                            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!records) state = 0;
            else fprintf(stderr, "[HEAP_TRACE] on: allocations recorded, frees checked\n");
        }
    }
    return state;
}

static Record *record_of(uint32_t pointer)
{
    return pointer >= HEAP_LO + 16u && pointer < HEAP_HI ? &records[(pointer - HEAP_LO) / 16u] : NULL;
}

/* Guest return addresses on the stack: the byte before is the end of a call. */
static void callers(uint32_t esp, uint32_t out[3])
{
    unsigned i, n = 0;
    out[0] = out[1] = out[2] = 0;
    if (esp < 0x10000u || esp > HEAP_HI - 0x200u) return;
    for (i = 0; i < 64 && n < 3; ++i) {
        uint32_t word = MEM32(esp + 4u * i);
        if (word >= 0x12006u && word < 0x259180u &&
            (MEM8(word - 5u) == 0xE8u || MEM8(word - 2u) == 0xFFu || MEM8(word - 3u) == 0xFFu || MEM8(word - 6u) == 0xFFu))
            out[n++] = word;
    }
}

static void describe(const char *label, uint32_t pointer)
{
    const Record *r = record_of(pointer);
    if (!r || !r->seq) {
        fprintf(stderr, "[HEAP_TRACE]   %s %08X: no allocation record\n", label, pointer);
        return;
    }
    fprintf(stderr, "[HEAP_TRACE]   %s %08X: allocated #%u by thread %u size %u from %08X %08X %08X%s",
            label, pointer, r->seq, r->tid, r->req, r->caller[0], r->caller[1], r->caller[2],
            r->free_seq ? "" : "\n");
    if (r->free_seq) fprintf(stderr, ", freed #%u from %08X\n", r->free_seq, r->free_caller);
}

static void bytes(const char *label, uint32_t at, unsigned count)
{
    unsigned i;
    fprintf(stderr, "[HEAP_TRACE]   %s %08X:", label, at);
    if (at >= 0x10000u && at + count <= HEAP_HI)
        for (i = 0; i < count; ++i) fprintf(stderr, " %02X", MEM8(at + i));
    fputc('\n', stderr);
}

static unsigned check(uint32_t pointer)
{
    uint32_t hdr = pointer - 16u, size = MEM16(hdr), before = MEM16(hdr + 2u), next = hdr + size * 16u;
    unsigned bad = 0;
    if (!(MEM8(hdr + 5u) & 1u)) bad |= 1;                       /* not busy: second free */
    if (size < 2u || next + 16u > HEAP_HI) bad |= 2;            /* size runs off the heap */
    else if (!(MEM8(hdr + 5u) & 0x10u) && MEM16(next + 2u) != size) bad |= 4;   /* next chunk's back-size */
    if (before && hdr - before * 16u >= HEAP_LO && MEM16(hdr - before * 16u) != before) bad |= 8;   /* chunk before disagrees */
    return bad;
}

static void report(const char *op, uint32_t pointer, unsigned bad, uint32_t esp)
{
    uint32_t hdr = pointer - 16u, size = MEM16(hdr), before = MEM16(hdr + 2u), next = hdr + size * 16u, at;
    uint32_t stack[3];
    Record *r;
    if (InterlockedIncrement(&reports) > 24) return;
    callers(esp, stack);
    fprintf(stderr, "[HEAP_TRACE] BAD %s pointer=%08X problems=%u (1 not busy, 2 size, 4 next back-size, 8 prev) thread=%u from %08X %08X %08X\n",
            op, pointer, bad, GetCurrentThreadId(), stack[0], stack[1], stack[2]);
    bytes("header", hdr, 16);
    bytes("next  ", next, 32);
    if (before) bytes("before", hdr - before * 16u, 16);
    describe("this", pointer);
    if (next + 16u <= HEAP_HI) describe("after", next + 16u);
    /* The chunk physically before this one is what runs into its header. */
    for (at = hdr / 16u * 16u; at > HEAP_LO + 32u && hdr - at < 0x40000u; at -= 16u) {
        r = record_of(at);
        if (r && r->seq && !r->free_seq && at != pointer) {
            describe("live chunk before", at);
            break;
        }
    }
}

void mm3_heap_trace_alloc(uint32_t pointer, uint32_t size, uint32_t esp)
{
    Record *r = record_of(pointer);
    unsigned bad;
    if (!r) return;
    r->seq = (uint32_t)InterlockedIncrement(&sequence);
    r->req = size;
    r->tid = GetCurrentThreadId();
    callers(esp, r->caller);
    r->free_caller = r->free_seq = 0;
    bad = check(pointer) & ~1u;
    if (bad) report("alloc", pointer, bad, esp);
}

void mm3_heap_trace_free(uint32_t pointer, uint32_t esp)
{
    Record *r = record_of(pointer);
    unsigned bad;
    if (!r || pointer & 7u) return;
    bad = check(pointer);
    if (bad) report("free", pointer, bad, esp);
    r->free_seq = (uint32_t)InterlockedIncrement(&sequence);
    r->free_caller = MEM32(esp);
}
