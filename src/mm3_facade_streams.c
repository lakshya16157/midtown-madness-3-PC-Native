#ifndef MM3_FACADE_STREAMS_CLOCK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define MM3_FACADE_STREAMS_CLOCK() GetTickCount64()
#endif
#include <stdint.h>
#include <stdio.h>
#include "recomp_types.h"

/* How full the facade vertex streams run. The facade builder takes its
 * vertices each frame from four fixed pools of the facade manager:
 *   detail32  +0x44 used of +0x48            (sub_00162EBA)
 *   far24     +0x1F80 + +0x1F84 of +0x1F88   (sub_00162EF3, from both ends)
 *   opaque28  +0x1EC8 + +0x1ECC of +0x1ED0   (sub_00162F75)
 *   alpha28   +0x1F24 + +0x1F28 of +0x1F2C   (sub_00162F75)
 * A request that does not fit is refused and that part of the building is
 * not drawn that frame; near the limit, which buildings miss out changes
 * with the camera, so distant facades flicker (mm3_facade_capacity.c sizes
 * the pools). Every 30 s while buildings are drawn (a race can be shorter
 * than a minute):
 *   [MM3_DISTANCE] facade streams last 30 s: detail32 peak U (demand D) of C,
 *       R refused; ...
 * peak = most vertices used in a frame, demand = that plus the vertices
 * refused in the same frame. The 28-byte streams' own allocator is wrapped by
 * the building diagnostics, so only their peak use is sampled here. Costs a
 * few loads per detail or far request. */

extern void sub_00162EBA_original(void);
extern void sub_00162EF3_original(void);

enum { FS_DETAIL, FS_FAR, FS_OPAQUE, FS_ALPHA, FS_STREAMS };

typedef struct {
    uint32_t used, capacity, peak, demand, frame_refused, refused, requests;
} FacadeStream;

static FacadeStream s_streams[FS_STREAMS];
static uint32_t s_manager, s_buffer = ~0u, s_notes;
static uint64_t s_window_start;

static void stream_use(FacadeStream *s, uint32_t used, uint32_t capacity)
{
    s->used = used;
    s->capacity = capacity;
    if (used > s->peak) s->peak = used;
    if (used + s->frame_refused > s->demand) s->demand = used + s->frame_refused;
}

static void streams_report(uint64_t now)
{
    static const char *const names[FS_STREAMS] = { "detail32", "far24", "opaque28", "alpha28" };
    unsigned k;
    fprintf(stderr, "[MM3_DISTANCE] facade streams last %.0f s:", (double)(now - s_window_start) / 1000.0);
    for (k = 0; k != FS_STREAMS; ++k) {
        const FacadeStream *s = &s_streams[k];
        if (k <= FS_FAR)
            fprintf(stderr, " %s peak %u (demand %u) of %u, %u of %u requests refused%s", names[k], s->peak,
                    s->demand, s->capacity, s->refused, s->requests, k + 1 < FS_STREAMS ? ";" : "\n");
        else
            fprintf(stderr, " %s peak %u of %u%s", names[k], s->peak, s->capacity, k + 1 < FS_STREAMS ? ";" : "\n");
    }
    fflush(stderr);
    for (k = 0; k != FS_STREAMS; ++k) {
        s_streams[k].peak = s_streams[k].demand = s_streams[k].refused = s_streams[k].requests = 0;
    }
}

/* After a detail or far request on `manager` for `quads` quads. */
static void streams_note(uint32_t manager, unsigned kind, int granted, uint32_t quads)
{
    uint32_t buffer;
    uint64_t now;
    unsigned k;
    if (manager < 0x10000u || manager > 0x08000000u - 0x2000u) return;
    /* The manager alternates two VB sets, one per frame: a new set index is
     * a new frame, whose refusals start from zero. */
    buffer = MEM32(manager + 0x1F8Cu);
    if (manager != s_manager || buffer != s_buffer) {
        for (k = 0; k != FS_STREAMS; ++k) s_streams[k].frame_refused = 0;
        s_manager = manager;
        s_buffer = buffer;
    }
    ++s_streams[kind].requests;
    if (!granted) {
        ++s_streams[kind].refused;
        s_streams[kind].frame_refused += quads * 4u;
    }
    stream_use(&s_streams[FS_DETAIL], MEM32(manager + 0x44u), MEM32(manager + 0x48u));
    stream_use(&s_streams[FS_FAR], MEM32(manager + 0x1F80u) + MEM32(manager + 0x1F84u), MEM32(manager + 0x1F88u));
    stream_use(&s_streams[FS_OPAQUE], MEM32(manager + 0x1EC8u) + MEM32(manager + 0x1ECCu), MEM32(manager + 0x1ED0u));
    stream_use(&s_streams[FS_ALPHA], MEM32(manager + 0x1F24u) + MEM32(manager + 0x1F28u), MEM32(manager + 0x1F2Cu));
    if (++s_notes & 255u) return;
    now = MM3_FACADE_STREAMS_CLOCK();
    if (!s_window_start) s_window_start = now;
    else if (now - s_window_start >= 30000u) {
        streams_report(now);
        s_window_start = now;
    }
}

void sub_00162EBA(void)
{
    uint32_t manager = g_ecx, quads = g_edx;
    sub_00162EBA_original();
    streams_note(manager, FS_DETAIL, g_eax != 0, quads);
}

void sub_00162EF3(void)
{
    uint32_t manager = g_ecx, quads = g_edx;
    sub_00162EF3_original();
    streams_note(manager, FS_FAR, g_eax != 0, quads);
}
