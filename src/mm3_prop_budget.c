/* Street-prop model queue budget: trees that vanish when many props are in range.
 *
 * Each city renderer keeps one 512-entry queue of the 3D models it will draw this
 * frame (count at owner+0x13E3C, allocator sub_00168BDD). When it is full the
 * allocator returns 0 and the prop is simply not drawn: whichever props the city
 * traversal reaches last disappear, near or far, for as long as the view stays
 * that busy. The Xbox's short draw distances rarely filled it; the longer
 * distances of the Improved look do (a live log showed 14-17 trees a frame lost).
 *
 * Props nearer than the 3D range (0x3A8C90, 125 m on the Xbox) are queued as
 * models, those between it and 0x3A8C94 (175 m) as a billboard plus a model, the
 * rest as billboards only. Billboards sit in their own list (511 entries). So
 * when the model queue overflows, this pulls both ranges in a little every frame
 * (far props become billboards instead of vanishing) and lets them out again
 * slowly once the queue has room. MM3_PROP_BUDGET=0 turns it off. */
#define WIN32_LEAN_AND_MEAN
#include "xbox_memory_layout.h"
#include "platform/xbox_winnt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_recomp_types.h"
#include "mm3_prop_budget.h"

extern void sub_0016A476_original(void);

#define MODEL_QUEUE 512u              /* entries (the allocator's 0x200) */
#define BILLBOARD_LIST 511u           /* entries (sub_00168D25's limit) */
#define SHRINK_ABOVE 480u             /* queued models: pull the range in */
#define GROW_BELOW 360u               /* queued models: let it out */
#define BILLBOARDS_FULL 440u          /* billboards: stop pulling the range in */
#define FACTOR_MIN 0.30f
#define SHRINK_STEP 0.92f             /* per frame */
#define GROW_STEP 1.015f              /* per frame */

static int s_enabled = -1;
static volatile LONG s_refused;
static float s_base_3d, s_base_billboard, s_factor = 1.0f;
static unsigned s_frames, s_refused_total, s_peak_models, s_peak_billboards;
static ULONGLONG s_second;

void mm3_prop_budget_refused(void)
{
    InterlockedIncrement(&s_refused);
}

static int guest_ok(uint32_t address, uint32_t bytes)
{
    return address >= 0x10000u && (uint64_t)address + bytes <= 0x08000000u;
}

static void apply_ranges(void)
{
    MEMF(0x003A8C90u) = s_base_3d * s_factor;
    MEMF(0x003A8C94u) = s_base_billboard * s_factor;
}

static void budget_frame(uint32_t owner)
{
    unsigned models, billboards, refused;
    ULONGLONG now;
    if (s_enabled < 0) {
        const char *value = getenv("MM3_PROP_BUDGET");
        s_enabled = !(value && !strcmp(value, "0"));
    }
    if (!s_enabled || !guest_ok(owner, 0x13E40u)) return;
    if (!s_base_3d) {
        float near_range = MEMF(0x003A8C90u), far_range = MEMF(0x003A8C94u);
        if (!isfinite(near_range) || !isfinite(far_range) || near_range <= 0.0f || far_range < near_range) {
            s_enabled = 0;
            return;
        }
        s_base_3d = near_range;
        s_base_billboard = far_range;
    }
    models = MEM32(owner + 0x13E3Cu);
    billboards = MEM32(owner + 0x112Cu);
    refused = (unsigned)InterlockedExchange(&s_refused, 0);
    if (models > s_peak_models) s_peak_models = models;
    if (billboards > s_peak_billboards) s_peak_billboards = billboards;
    s_refused_total += refused;
    ++s_frames;
    if ((refused || models >= SHRINK_ABOVE) && billboards < BILLBOARDS_FULL) {
        if (s_factor > FACTOR_MIN) {
            s_factor *= SHRINK_STEP;
            if (s_factor < FACTOR_MIN) s_factor = FACTOR_MIN;
        }
    } else if (!refused && models < GROW_BELOW && s_factor < 1.0f) {
        s_factor *= GROW_STEP;
        if (s_factor > 1.0f) s_factor = 1.0f;
    }
    apply_ranges();
    now = GetTickCount64();
    if (now - s_second >= 1000u) {
        if (s_second && (s_refused_total || s_factor < 1.0f))
            fprintf(stderr, "[MM3_PROPS] %u frames: model queue peak %u/%u, billboards peak %u/%u, "
                            "%u models refused; 3D range %.0f m, billboards from %.0f m (%.0f%% of the setting)\n",
                    s_frames, s_peak_models, MODEL_QUEUE, s_peak_billboards, BILLBOARD_LIST, s_refused_total,
                    MEMF(0x003A8C90u), MEMF(0x003A8C94u), (double)s_factor * 100.0);
        s_second = now;
        s_frames = s_refused_total = s_peak_models = s_peak_billboards = 0;
    }
}

/* The city renderer's draw of its props, once per frame and view; eax = the renderer. */
void sub_0016A476(void)
{
    budget_frame(g_eax);
    sub_0016A476_original();
}
