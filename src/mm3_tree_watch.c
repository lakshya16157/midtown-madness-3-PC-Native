/* Tree watch: why a tree that was on screen vanished (report 2026-11-19:
 * "driving at night the trees kept disappearing").
 *
 * Trees are LocalObject street props drawn one at a time by sub_0016AEDB.
 * A visit ends in one of these outcomes:
 *   off        the prop's renderer has drawing switched off (+0xA0 byte 0)
 *   frustum    sub_001C7B15 (return 0x16AF9A) put its box outside the view
 *   size       sub_001A67BA (return 0x16AFD6), the small-prop size cull
 *   billboard  sub_0016946B (return 0x16B065) drew a billboard instead
 *   bb_full    the same, but the 511-entry billboard list (+0x112C) was full
 *   no_model   sub_00168BDD (return 0x16B116) found no model to draw
 *   drawn      the 3D model went to the queue or was drawn
 * A tree never visited in a frame was dropped earlier, by the cell pass
 * (cell frustum, occlusion) or because its cell is not loaded.
 *
 * Every tree is remembered by its instance. When a tree drawn for at least
 * TREE_STEADY frames goes missing for 1..TREE_GAP_MAX frames and comes back,
 * that is a flicker: [TREE_FLICKER] says how long, why (the first outcome
 * seen in the gap, or "not_visited"), how far and how far off-centre;
 * only trees near the middle of the view get a line, all are counted.
 * [TREE_WATCH] sums each second. MM3_TREE_WATCH=0 turns it all off. */
#define WIN32_LEAN_AND_MEAN
#include "xbox_memory_layout.h"
#include "platform/xbox_winnt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_recomp_types.h"
#include "mm3_tree_watch.h"

extern void sub_0016946B_original(void);
extern void sub_00168BDD_original(void);

#define TREE_SLOTS 16384u          /* power of two */
#define TREE_SLOT_LIMIT 12000u     /* clear the table past this (a new city) */
#define TREE_STEADY 10u
#define TREE_GAP_MAX 45u
#define TREE_EVENTS_PER_SECOND 20u
#define TREE_CENTRAL 0.8f           /* cosine to the view direction: about 37 degrees */

enum { OUT_DRAWN, OUT_BILLBOARD, OUT_OFF, OUT_FRUSTUM, OUT_SIZE, OUT_BB_FULL, OUT_NO_MODEL,
       OUT_COUNT, REASON_NONE = 255 };
static const char *const s_outcome_name[OUT_COUNT] = {
    "drawn", "billboard", "off", "frustum", "size", "bb_full", "no_model" };

typedef struct {
    uint32_t object, metadata;
    uint32_t last_drawn;           /* watch frame index, 0 = never */
    uint16_t streak;
    uint8_t gap_reason;
    float gap_distance, gap_ahead;
} TreeSlot;

static TreeSlot s_slots[TREE_SLOTS];
static unsigned s_slot_count;
static RECOMP_TLS MM3TreeVisit *s_current;
static int s_wanted = -1;
static uint32_t s_game_frame, s_frame;          /* game frame id, watch frame index */
static ULONGLONG s_second_start;
static unsigned s_frames_in_second, s_outcomes[OUT_COUNT], s_flickers[OUT_COUNT + 1];
static unsigned s_events, s_events_suppressed, s_billboards_peak, s_central;
static float s_camera[3];

static int guest_ok(uint32_t address, uint32_t bytes)
{
    return address >= 0x10000u && (uint64_t)address + bytes <= 0x08000000u;
}

int mm3_tree_watch_wanted(void)
{
    if (s_wanted < 0) {
        const char *value = getenv("MM3_TREE_WATCH");
        s_wanted = !(value && !strcmp(value, "0"));
        if (s_wanted)
            fprintf(stderr, "[TREE_WATCH] on: per-second tree outcomes and [TREE_FLICKER] when a tree drawn "
                            "%u+ frames vanishes for 1-%u frames (MM3_TREE_WATCH=0 disables)\n",
                    TREE_STEADY, TREE_GAP_MAX);
    }
    return s_wanted;
}

static TreeSlot *tree_slot(uint32_t object)
{
    uint32_t i = (object * 2654435761u) & (TREE_SLOTS - 1u);
    for (;;) {
        TreeSlot *slot = &s_slots[i];
        if (slot->object == object) return slot;
        if (!slot->object) {
            if (s_slot_count >= TREE_SLOT_LIMIT) {
                memset(s_slots, 0, sizeof s_slots);
                s_slot_count = 0;
                return tree_slot(object);
            }
            ++s_slot_count;
            slot->object = object;
            slot->gap_reason = REASON_NONE;
            return slot;
        }
        i = (i + 1u) & (TREE_SLOTS - 1u);
    }
}

/* Distance from the camera and the cosine to its view direction. The camera
 * matrix is three rows then the position at +0x24; the view looks along -row 2. */
static void tree_where(uint32_t object, uint32_t camera, float centre[3], float *distance, float *ahead)
{
    float d[3], length;
    int i;
    for (i = 0; i < 3; ++i)
        centre[i] = (MEMF(object + 0x20u + 4u * i) + MEMF(object + 0x2Cu + 4u * i)) * 0.5f;
    *distance = 0.0f;
    *ahead = 0.0f;
    if (!guest_ok(camera, 0x30)) return;
    for (i = 0; i < 3; ++i) d[i] = centre[i] - MEMF(camera + 0x24u + 4u * i);
    length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    *distance = length;
    if (length > 0.001f)
        *ahead = -(d[0] * MEMF(camera + 0x18u) + d[1] * MEMF(camera + 0x1Cu) + d[2] * MEMF(camera + 0x20u)) / length;
}

static void second_summary(ULONGLONG now)
{
    unsigned visits = 0, i;
    for (i = 0; i < OUT_COUNT; ++i) visits += s_outcomes[i];
    if (visits && s_frames_in_second) {
        fprintf(stderr, "[TREE_WATCH] %u frames, trees per frame %.1f: drawn %.1f billboard %.1f | dropped per frame: "
                        "frustum %.1f size %.2f off %.2f bb_full %.2f no_model %.2f | flickers %u (not_visited %u frustum %u "
                        "size %u off %u bb_full %u no_model %u; mid-view %u, lines dropped %u) | billboards peak %u/511 | time_of_day %u "
                        "camera (%.1f, %.1f, %.1f)\n",
                s_frames_in_second, (double)visits / s_frames_in_second,
                (double)s_outcomes[OUT_DRAWN] / s_frames_in_second, (double)s_outcomes[OUT_BILLBOARD] / s_frames_in_second,
                (double)s_outcomes[OUT_FRUSTUM] / s_frames_in_second, (double)s_outcomes[OUT_SIZE] / s_frames_in_second,
                (double)s_outcomes[OUT_OFF] / s_frames_in_second, (double)s_outcomes[OUT_BB_FULL] / s_frames_in_second,
                (double)s_outcomes[OUT_NO_MODEL] / s_frames_in_second,
                s_flickers[0] + s_flickers[OUT_FRUSTUM + 1] + s_flickers[OUT_SIZE + 1] + s_flickers[OUT_OFF + 1] +
                    s_flickers[OUT_BB_FULL + 1] + s_flickers[OUT_NO_MODEL + 1],
                s_flickers[0], s_flickers[OUT_FRUSTUM + 1], s_flickers[OUT_SIZE + 1], s_flickers[OUT_OFF + 1],
                s_flickers[OUT_BB_FULL + 1], s_flickers[OUT_NO_MODEL + 1], s_central, s_events_suppressed, s_billboards_peak,
                MEM32(0x00407C30u), s_camera[0], s_camera[1], s_camera[2]);
    }
    s_second_start = now;
    s_frames_in_second = s_events = s_events_suppressed = s_billboards_peak = s_central = 0;
    memset(s_outcomes, 0, sizeof s_outcomes);
    memset(s_flickers, 0, sizeof s_flickers);
}

static void next_frame_if_new(void)
{
    uint32_t frame_object = MEM32(0x003C5C90u), id;
    ULONGLONG now = GetTickCount64();
    if (!guest_ok(frame_object, 0x2C)) return;
    id = MEM32(frame_object + 0x28u);
    if (id == s_game_frame && s_frame) return;
    s_game_frame = id;
    ++s_frame;
    if (now - s_second_start >= 1000u) second_summary(now);
    ++s_frames_in_second;
}

void mm3_tree_watch_begin(MM3TreeVisit *visit, uint32_t object, uint32_t camera, int is_tree)
{
    memset(visit, 0, sizeof *visit);
    visit->previous = s_current;
    s_current = NULL;
    if (!is_tree || !guest_ok(object, 0xF0)) return;
    visit->owner = MEM32(object);
    if (!guest_ok(visit->owner, 0x13E3C)) return;
    visit->tracked = 1;
    visit->object = object;
    visit->camera = camera;
    visit->owner_off = !MEM8(visit->owner + 0xA0u);   /* 16AEE8: skip the whole draw */
    visit->billboards_before = MEM32(visit->owner + 0x112Cu);
    s_current = visit;
}

void mm3_tree_watch_cull(uint32_t caller, uint32_t rejected)
{
    MM3TreeVisit *visit = s_current;
    if (!visit) return;
    if (caller == 0x0016AF9Au) visit->frustum_reject = rejected != 0;
    else if (caller == 0x0016AFD6u) visit->size_reject = rejected != 0;
}

void sub_0016946B(void)
{
    MM3TreeVisit *visit = s_current;
    uint32_t caller = visit ? MEM32(g_esp) : 0;
    sub_0016946B_original();
    if (caller == 0x0016B065u) {
        visit->billboard_tested = 1;
        visit->billboard_only = (g_eax & 255u) != 0;
    }
}

void sub_00168BDD(void)
{
    MM3TreeVisit *visit = s_current;
    uint32_t caller = visit ? MEM32(g_esp) : 0;
    sub_00168BDD_original();
    if (caller == 0x0016B116u) {
        visit->model_tested = 1;
        visit->model_found = g_eax != 0;
    }
}

static void report_flicker(const TreeSlot *slot, uint32_t gap, float distance, float ahead,
                           const float centre[3], uint32_t camera)
{
    /* Trees at the screen edge come and go as the car steers; print only the
     * ones near the middle of the view (as it went, or as it came back). */
    /* fabsf: the sign of the view axis (-row 2) is unverified; drawn trees are all in front anyway. */
    int central = fabsf(slot->gap_reason == REASON_NONE ? ahead : slot->gap_ahead) > TREE_CENTRAL;
    s_flickers[slot->gap_reason == REASON_NONE ? 0 : slot->gap_reason + 1u]++;
    if (!central) return;
    ++s_central;
    if (s_events >= TREE_EVENTS_PER_SECOND) { ++s_events_suppressed; return; }
    ++s_events;
    fprintf(stderr, "[TREE_FLICKER] tree %08X (type %08X) missing %u frames after %u steady, reason %s; "
                    "back at %.1f m ahead %.2f, when it went %.1f m ahead %.2f; tree (%.1f, %.1f, %.1f) camera "
                    "(%.1f, %.1f, %.1f) time_of_day %u tick %llu\n",
            slot->object, slot->metadata, gap, slot->streak,
            slot->gap_reason == REASON_NONE ? "not_visited" : s_outcome_name[slot->gap_reason],
            distance, ahead, slot->gap_distance, slot->gap_ahead, centre[0], centre[1], centre[2],
            guest_ok(camera, 0x30) ? MEMF(camera + 0x24u) : 0.0f, guest_ok(camera, 0x30) ? MEMF(camera + 0x28u) : 0.0f,
            guest_ok(camera, 0x30) ? MEMF(camera + 0x2Cu) : 0.0f, MEM32(0x00407C30u),
            (unsigned long long)GetTickCount64());
}

void mm3_tree_watch_finish(MM3TreeVisit *visit)
{
    TreeSlot *slot;
    unsigned outcome;
    uint32_t billboards;
    float centre[3], distance, ahead;
    s_current = visit->previous;
    if (!visit->tracked) return;
    next_frame_if_new();
    billboards = MEM32(visit->owner + 0x112Cu);
    if (billboards > s_billboards_peak) s_billboards_peak = billboards;
    if (visit->owner_off) outcome = OUT_OFF;
    else if (visit->frustum_reject) outcome = OUT_FRUSTUM;
    else if (visit->size_reject) outcome = OUT_SIZE;
    else if (visit->billboard_tested && visit->billboard_only)
        outcome = billboards > visit->billboards_before ? OUT_BILLBOARD : OUT_BB_FULL;
    else if (visit->model_tested && !visit->model_found) outcome = OUT_NO_MODEL;
    else outcome = OUT_DRAWN;
    ++s_outcomes[outcome];
    tree_where(visit->object, visit->camera, centre, &distance, &ahead);
    if (guest_ok(visit->camera, 0x30)) {
        s_camera[0] = MEMF(visit->camera + 0x24u);
        s_camera[1] = MEMF(visit->camera + 0x28u);
        s_camera[2] = MEMF(visit->camera + 0x2Cu);
    }
    slot = tree_slot(visit->object);
    slot->metadata = MEM32(visit->object + 0xA0u);
    if (outcome == OUT_DRAWN || outcome == OUT_BILLBOARD) {
        if (slot->last_drawn != s_frame) {
            if (slot->last_drawn) {
                uint32_t gap = s_frame - slot->last_drawn - 1u;
                if (gap) {
                    if (gap <= TREE_GAP_MAX && slot->streak >= TREE_STEADY)
                        report_flicker(slot, gap, distance, ahead, centre, visit->camera);
                    slot->streak = 0;
                }
            }
            slot->last_drawn = s_frame;
            if (slot->streak < 0xFFFFu) ++slot->streak;
            slot->gap_reason = REASON_NONE;
        }
    } else if (slot->last_drawn && slot->last_drawn != s_frame && slot->gap_reason == REASON_NONE) {
        slot->gap_reason = (uint8_t)outcome;
        slot->gap_distance = distance;
        slot->gap_ahead = ahead;
    }
}
