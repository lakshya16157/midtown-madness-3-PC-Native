/* Based on MattSG's collision-cell guard (9ad1762, bounded in 342b68f).
 * Repair at cache insertion, never in the per-contact hot path. */
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#include "recomp_types.h"

extern void sub_0015A929_original(void);

/* One bounded process-wide scratch buffer, never per-cell or per-thread.
 * Static storage needs no allocation/OOM fallback or thread-exit cleanup.
 * Each packed word holds the nearest ORIGINAL valid list position at/before
 * (low16) and at/after (high16); FFFF is outside the maximum65535-slot list. */
static uint32_t collision_bounds[65535];
#ifdef _WIN32
static SRWLOCK collision_lock = SRWLOCK_INIT;
static volatile LONG collision_reports;
#define COLLISION_LOCK() AcquireSRWLockExclusive(&collision_lock)
#define COLLISION_UNLOCK() ReleaseSRWLockExclusive(&collision_lock)
#define COLLISION_REPORT_COUNT() ((unsigned)InterlockedIncrement(&collision_reports))
#else
static pthread_mutex_t collision_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned collision_reports;
#define COLLISION_LOCK() ((void)pthread_mutex_lock(&collision_lock))
#define COLLISION_UNLOCK() ((void)pthread_mutex_unlock(&collision_lock))
#define COLLISION_REPORT_COUNT() (__atomic_add_fetch(&collision_reports, 1u, __ATOMIC_RELAXED))
#endif

static int collision_range(uint32_t address, uint32_t bytes)
{
    return address >= 0x10000u && address < 0x08000000u &&
           bytes <= 0x08000000u - address;
}

static int collision_overlap(uint32_t a, uint32_t a_bytes, uint32_t b, uint32_t b_bytes)
{
    return a_bytes && b_bytes && a < b + b_bytes && b < a + a_bytes;
}

static void collision_sanitize(uint32_t header)
{
    uint32_t faces, list, length, grid, cells = 1, i, keep = 0;
    unsigned repaired = 0;
    int damaged = 0;
    if ((header & 3u) || !collision_range(header, 0x50u)) return;
    faces = MEM32(header + 4u);
    list = MEM32(header + 8u);
    length = MEM32(header + 12u);
    grid = MEM32(header + 16u);
    for (i = 0; i < 3; ++i) {
        uint32_t dimension = MEM32(header + 0x44u + 4u * i);
        if (!dimension || dimension > 0x10000u / cells) return;
        cells *= dimension;
    }
    if (!faces || faces > 0xFFFFu || length > 0xFFFFu ||
        (length && ((list & 1u) || !collision_range(list, 2u * length))) ||
        (grid & 1u) ||
        !collision_range(grid, 4u * cells)) return;
    /* Aliased metadata cannot be repaired in place: a grid-range write could
     * otherwise turn a repaired face index back into an out-of-range value. */
    if (collision_overlap(header, 0x50u, list, 2u * length) ||
        collision_overlap(header, 0x50u, grid, 4u * cells) ||
        collision_overlap(list, 2u * length, grid, 4u * cells)) return;

    /* Clean streaming cells need one list scan plus bounds clamping, with no
     * scratch writes, scratch lock, or list writes. */
    for (i = 0; i < length; ++i)
        if (MEM16(list + 2u * i) >= faces) { damaged = 1; break; }
    /* Trim every range to its first/last originally valid list position.
     * Replacing interior invalid entries with a preceding valid entry then
     * introduces only duplicates already belonging to that same range, even
     * when cells overlap. Invalid-only ranges become empty. Valid ranges and
     * every valid list value remain byte-for-byte unchanged. */
    if (damaged) {
        uint32_t previous = 0xFFFFu, next = 0xFFFFu;
        COLLISION_LOCK();
        for (i = 0; i < length; ++i) {
            if (MEM16(list + 2u * i) < faces) previous = i;
            collision_bounds[i] = previous;
        }
        for (i = length; i > 0; --i) {
            if (MEM16(list + 2u * (i - 1u)) < faces) next = i - 1u;
            collision_bounds[i - 1u] |= next << 16;
        }
    }
    for (i = 0; i < cells; ++i) {
        uint32_t address = grid + 4u * i;
        uint32_t original_lo = MEM16(address), original_hi = MEM16(address + 2u);
        uint32_t lo = original_lo, hi = original_hi;
        if (hi > length) hi = length;
        if (lo > hi) lo = hi;
        if (damaged && lo < hi) {
            uint32_t first = collision_bounds[lo] >> 16;
            if (first >= hi) lo = hi; /* Includes the FFFF sentinel. */
            else {
                uint32_t last = collision_bounds[hi - 1u] & 0xFFFFu;
                lo = first; hi = last + 1u;
            }
        }
        if (lo != original_lo) { MEM16(address) = (uint16_t)lo; ++repaired; }
        if (hi != original_hi) { MEM16(address + 2u) = (uint16_t)hi; ++repaired; }
    }
    /* Repair the shared list once after the original-position bounds have
     * been used. Empty lists touch no list memory; all-invalid lists fill with
     * face0 but every grid range is empty, so no geometry is manufactured. */
    for (i = 0; damaged && i < length; ++i)
        if (MEM16(list + 2u * i) < faces) {
            keep = MEM16(list + 2u * i);
            break;
        }
    for (i = 0; damaged && i < length; ++i) {
        uint32_t address = list + 2u * i;
        if (MEM16(address) < faces) keep = MEM16(address);
        else { MEM16(address) = (uint16_t)keep; ++repaired; }
    }
    if (repaired) {
        if (COLLISION_REPORT_COUNT() <= 8)
            fprintf(stderr, "[COLLISION] repaired %u invalid entries in cell %08X\n", repaired, header);
    }
    if (damaged) COLLISION_UNLOCK();
}

void sub_0015A929(void)
{
    uint32_t cell = g_esi;
    uint32_t before = collision_range(cell, 4u) ? MEM32(cell) : 0;
    sub_0015A929_original();
    if (collision_range(cell, 4u) && (g_eax & 0xFFu) && MEM32(cell) != before)
        collision_sanitize(MEM32(cell));
}
