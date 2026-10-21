#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "recomp_types.h"
#include "mm3_light_queue.h"
#include "mm3_view_eye.h"
#include "mm3_darkness_monitor.h"

/* PC-sized local-light queue. Street lamps, headlights, tail and brake
 * lamps, signals and the reverse pool all share one queue per frame; on the
 * Xbox it held 256 records. At night in dense traffic with every street lamp
 * lit, that filled in about three seconds of four and lamps lost their pools
 * from frame to frame (flicker).
 *
 * The manager is allocated once by its static initializer (0x5B20 bytes,
 * 0x254FDD) and every native user reaches records only through its pointer
 * array, never by inline index: push (22FF8B, a sorted insert), point and
 * spot submission (23006E, 22FFD1, which also check the capacity) and the
 * consumer (230970, which walks `count` pointers, batching volumes 64 units
 * at a time and resetting the count). Asking for more bytes keeps the
 * manager intact and places a larger pointer array and the extra records
 * after it; those five functions then use this array and capacity. The
 * retail records stay where they were (their constructor is a no-op, so new
 * records only need zeroing). Any other manager, or a failed larger
 * allocation, keeps the retail array and 256.
 *
 * MM3_LIGHT_QUEUE=256..1024 (default 1024; 256 is retail) sizes this queue,
 * which only collects the frame's lights: queuing costs a 0x54-byte copy.
 * Drawing is what costs (one volume build on the game thread and one pass
 * over the light buffer each), so the frame budget below decides which of
 * them are drawn. */
#define LIGHT_MANAGER_BYTES 0x5B20u
#define LIGHT_RECORD_BYTES 0x54u
#define LIGHT_INLINE_RECORDS 0x10u
#define LIGHT_INLINE_SLOTS 0x5410u
#define LIGHT_COUNT 0x5810u
#define LIGHT_MANAGER_POINTER 0x003C5D8Cu

static uint32_t s_requested, s_allocation, s_queue, s_slots, s_capacity = MM3_LIGHT_QUEUE_RETAIL;

static uint32_t light_queue_requested(void)
{
    if (!s_requested) {
        const char *setting = getenv("MM3_LIGHT_QUEUE");
        long value = setting && *setting ? strtol(setting, NULL, 10) : (long)MM3_LIGHT_QUEUE_MAXIMUM;
        if (value < (long)MM3_LIGHT_QUEUE_RETAIL) value = MM3_LIGHT_QUEUE_RETAIL;
        if (value > (long)MM3_LIGHT_QUEUE_MAXIMUM) value = MM3_LIGHT_QUEUE_MAXIMUM;
        s_requested = (uint32_t)value;
    }
    return s_requested;
}

static uint32_t light_queue_extra(uint32_t capacity)
{
    return capacity * 4u + (capacity - MM3_LIGHT_QUEUE_RETAIL) * LIGHT_RECORD_BYTES;
}

uint32_t mm3_light_queue_alloc_size(void)
{
    uint32_t capacity = light_queue_requested();
    return capacity > MM3_LIGHT_QUEUE_RETAIL ? LIGHT_MANAGER_BYTES + light_queue_extra(capacity)
                                             : LIGHT_MANAGER_BYTES;
}

void mm3_light_queue_allocated(uint32_t address)
{
    s_allocation = address;
}

void mm3_light_queue_attach(void)
{
    uint32_t queue = MEM32(LIGHT_MANAGER_POINTER), capacity = light_queue_requested(), i;
    uint32_t slots, records;
    if (capacity <= MM3_LIGHT_QUEUE_RETAIL || !queue || queue != s_allocation ||
        queue > 0x08000000u - LIGHT_MANAGER_BYTES - light_queue_extra(capacity)) {
        if (capacity > MM3_LIGHT_QUEUE_RETAIL)
            fprintf(stderr, "[MM3_LIGHTS] light queue kept at the retail 256 records\n");
        return;
    }
    slots = queue + LIGHT_MANAGER_BYTES;
    records = slots + capacity * 4u;
    memset((void *)XBOX_PTR(records), 0, (capacity - MM3_LIGHT_QUEUE_RETAIL) * LIGHT_RECORD_BYTES);
    /* The constructor filled the retail array; keep its order for the first
     * 256 slots and give the new slots the new records. */
    for (i = 0; i != MM3_LIGHT_QUEUE_RETAIL; ++i)
        MEM32(slots + i * 4u) = MEM32(queue + LIGHT_INLINE_SLOTS + i * 4u);
    for (; i != capacity; ++i)
        MEM32(slots + i * 4u) = records + (i - MM3_LIGHT_QUEUE_RETAIL) * LIGHT_RECORD_BYTES;
    s_queue = queue;
    s_slots = slots;
    s_capacity = capacity;
    fprintf(stderr, "[MM3_LIGHTS] light queue %u records (retail 256), %u extra bytes\n",
            capacity, light_queue_extra(capacity));
}

uint32_t mm3_light_queue_slots(uint32_t queue)
{
    return queue && queue == s_queue ? s_slots : queue + LIGHT_INLINE_SLOTS;
}

uint32_t mm3_light_queue_capacity(uint32_t queue)
{
    return queue && queue == s_queue ? s_capacity : MM3_LIGHT_QUEUE_RETAIL;
}

/* Frame light budget. Producers queue in scene order: every street lamp
 * before any car, so with a first-come queue a dense junction left the cars
 * waiting at its lights without tail lamps or beams. Instead the consumer
 * (230970) first ranks the whole frame and draws only the lights that show:
 *
 *  - the driver's own car, always;
 *  - every other light by how large and bright it looks: volume radius over
 *    the distance from the camera to the volume (so near, wide pools rank
 *    high), times 0.2..1 for its brightest channel; car lights count 8x, as
 *    they move and sit close to the player;
 *  - world lights (lamps, signs, signals) whose pool would look tiny fade
 *    out (from 0.02 to 0.012: a 3 m bulb pool by about 75-125 m, a street
 *    lamp only beyond 400 m) while their glow sprites stay; dark records go;
 *  - above MM3_LIGHT_BUDGET (default 512 drawn, 64..1024; 0 turns all of
 *    this off and draws every queued light, as before) the lowest-ranked go. The cut-off follows demand quickly when it rises and
 *    eases back over about a second, and lights within 30% of it fade, so a
 *    light leaving the budget dims instead of blinking. A sudden surge may
 *    overshoot the budget by a quarter for a few frames rather than pop.
 *
 * Dropped records move behind the drawn ones (the pointer array stays a
 * permutation of the allocated records), points still precede spots. */
#define LIGHT_BUDGET_DEFAULT 512u
#define LIGHT_CAR_WEIGHT 8.0f
#define LIGHT_FLOOR_GONE 0.012f
#define LIGHT_FLOOR_FULL 0.02f
#define LIGHT_FADE_BAND 0.3f
#define LIGHT_TIGHTEN 0.35f
#define LIGHT_RELAX 0.05f

#define LIGHT_TAG_SLOTS (2u * MM3_LIGHT_QUEUE_MAXIMUM)
static uint32_t s_tag_record[LIGHT_TAG_SLOTS], s_tag_frame[LIGHT_TAG_SLOTS], s_frame = 1;
static uint8_t s_tag_class[LIGHT_TAG_SLOTS], s_record_class[MM3_LIGHT_QUEUE_MAXIMUM];
static float s_score[MM3_LIGHT_QUEUE_MAXIMUM], s_fade[MM3_LIGHT_QUEUE_MAXIMUM];
static float s_rank[MM3_LIGHT_QUEUE_MAXIMUM];
static uint32_t s_kept[MM3_LIGHT_QUEUE_MAXIMUM], s_dropped[MM3_LIGHT_QUEUE_MAXIMUM];
static uint32_t s_budget = ~0u;
static float s_threshold;
static struct { uint32_t cars, tiny, over, fading; } s_select;

/* With renderer profiling on (Steam launches), one line a second: lights
 * queued and drawn per frame, what the budget left out, frames that filled
 * the queue, and car lights refused for lack of room. */
static struct {
    uint32_t frames, peak, drawn_peak, full, no_camera, refused[2];
    uint64_t total, drawn, cars, tiny, over, fading;
    clock_t start;
} s_usage;
static int s_usage_on = -1;

/* This frame's tags by record address (open addressing; entries from earlier
 * frames count as empty, so nothing needs clearing). */
static uint32_t light_tag_slot(uint32_t record)
{
    uint32_t slot = ((record / 4u * 0x9E3779B1u) >> 16) % LIGHT_TAG_SLOTS, i;
    for (i = 0; i != LIGHT_TAG_SLOTS; ++i, slot = (slot + 1u) % LIGHT_TAG_SLOTS)
        if (s_tag_frame[slot] != s_frame || s_tag_record[slot] == record) return slot;
    return LIGHT_TAG_SLOTS;
}

void mm3_light_queue_tag(uint32_t record, unsigned light_class)
{
    uint32_t slot = record ? light_tag_slot(record) : LIGHT_TAG_SLOTS;
    if (slot == LIGHT_TAG_SLOTS) return;
    s_tag_record[slot] = record;
    s_tag_frame[slot] = s_frame;
    s_tag_class[slot] = (uint8_t)(light_class < 3u ? light_class : 0u);
}

static unsigned light_record_class(uint32_t record)
{
    uint32_t slot = record ? light_tag_slot(record) : LIGHT_TAG_SLOTS;
    return slot != LIGHT_TAG_SLOTS && s_tag_frame[slot] == s_frame ? s_tag_class[slot] : 0u;
}

static uint32_t light_queue_budget(void)
{
    if (s_budget == ~0u) {
        const char *setting = getenv("MM3_LIGHT_BUDGET");
        long value = setting && *setting ? strtol(setting, NULL, 10) : (long)LIGHT_BUDGET_DEFAULT;
        if (value > 0 && value < 64) value = 64;
        if (value > (long)MM3_LIGHT_QUEUE_MAXIMUM) value = MM3_LIGHT_QUEUE_MAXIMUM;
        s_budget = value > 0 ? (uint32_t)value : 0u;
    }
    return s_budget;
}

static float light_smoothstep(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* Apparent size and brightness of a record (see above); points span W/H/L
 * around their position, spots 0..L along their forward row. */
static float light_score(uint32_t record, const float eye[3])
{
    float width = MEMF(record + 0x40), height = MEMF(record + 0x44), length = MEMF(record + 0x48);
    float radius = 0.5f * fmaxf(width, fmaxf(height, length)), distance = 0.0f, score;
    uint32_t color = MEM32(record + 0x4C), spot = MEM32(record + 0x50) == 1u, i;
    unsigned red = (color >> 16) & 255u, green = (color >> 8) & 255u, blue = color & 255u;
    unsigned peak = red > green ? red : green;
    if (blue > peak) peak = blue;
    for (i = 0; i != 3; ++i) {
        float centre = MEMF(record + 0x30 + i * 4) +
                       (spot ? MEMF(record + 0x20 + i * 4) * length * 0.5f : 0.0f);
        distance += (centre - eye[i]) * (centre - eye[i]);
    }
    distance = sqrtf(distance) - radius;
    if (!(distance > 0.0f)) distance = 0.0f;
    score = radius / (distance + 1.0f) * (0.2f + 0.8f * (float)peak / 255.0f);
    /* A malformed record is left to the native path as it was. */
    return isfinite(score) && score >= 0.0f ? score : FLT_MAX;
}

/* The k-th largest of `count` values (1 <= k <= count); reorders them. */
static float light_kth_largest(float *values, int count, int k)
{
    int left = 0, right = count - 1, target = k - 1;
    while (left < right) {
        float pivot = values[left + (right - left) / 2], swap;
        int i = left, j = right;
        while (i <= j) {
            while (values[i] > pivot) ++i;
            while (values[j] < pivot) --j;
            if (i <= j) { swap = values[i]; values[i] = values[j]; values[j] = swap; ++i; --j; }
        }
        if (target <= j) right = j;
        else if (target >= i) left = i;
        else return values[target];
    }
    return values[target];
}

static uint32_t light_queue_select(uint32_t queue, uint32_t slots, uint32_t count,
                                   uint32_t budget, const float eye[3])
{
    uint32_t i, kept = 0, dropped = 0, driver = 0, ranked = 0, want, hard;
    float need = 0.0f, hard_need = 0.0f;
    memset(&s_select, 0, sizeof s_select);
    for (i = 0; i != count; ++i) {
        uint32_t record = MEM32(slots + i * 4u);
        unsigned light_class = light_record_class(record);
        s_record_class[i] = (uint8_t)light_class;
        if (light_class) ++s_select.cars;
        if (light_class == 2u) { s_score[i] = FLT_MAX; s_fade[i] = 1.0f; ++driver; continue; }
        if (!record || !(MEM32(record + 0x4C) & 0x00FFFFFFu)) {
            s_score[i] = 0.0f;
            s_fade[i] = record ? 0.0f : 1.0f;
            continue;
        }
        s_score[i] = light_score(record, eye) * (light_class ? LIGHT_CAR_WEIGHT : 1.0f);
        s_fade[i] = light_class ? 1.0f : light_smoothstep((s_score[i] - LIGHT_FLOOR_GONE) /
                                                          (LIGHT_FLOOR_FULL - LIGHT_FLOOR_GONE));
        if (s_fade[i] > 0.0f) s_rank[ranked++] = s_score[i];
    }
    want = budget > driver ? budget - driver : 0u;
    hard = want + want / 4u;
    if (!want) need = hard_need = FLT_MAX;
    else {
        if (ranked > hard) hard_need = light_kth_largest(s_rank, (int)ranked, (int)hard);
        if (ranked > want) need = light_kth_largest(s_rank, (int)ranked, (int)want);
    }
    s_threshold += (need - s_threshold) * (need > s_threshold ? LIGHT_TIGHTEN : LIGHT_RELAX);
    if (s_threshold < hard_need) s_threshold = hard_need;
    if (s_threshold < 1e-6f) s_threshold = 0.0f;
    for (i = 0; i != count; ++i) {
        uint32_t record = MEM32(slots + i * 4u);
        float fade = s_fade[i];
        if (s_record_class[i] != 2u && fade > 0.0f && s_threshold > 0.0f) {
            if (s_score[i] < s_threshold) {
                fade = 0.0f;
                ++s_select.over;
            } else {
                fade = fminf(fade, light_smoothstep((s_score[i] - s_threshold) /
                                                    (LIGHT_FADE_BAND * s_threshold)));
            }
        } else if (fade <= 0.0f) {
            ++s_select.tiny;
        }
        if (fade > 0.0f && fade < 1.0f && record) {
            uint32_t color = MEM32(record + 0x4C), faded = color & 0xFF000000u;
            unsigned shift;
            for (shift = 0; shift < 24; shift += 8)
                faded |= (uint32_t)((float)((color >> shift) & 255u) * fade + 0.5f) << shift;
            MEM32(record + 0x4C) = faded;
            if (!(faded & 0x00FFFFFFu)) fade = 0.0f;
            else ++s_select.fading;
        }
        if (fade > 0.0f) s_kept[kept++] = record;
        else s_dropped[dropped++] = record;
    }
    for (i = 0; i != kept; ++i) MEM32(slots + i * 4u) = s_kept[i];
    for (i = 0; i != dropped; ++i) MEM32(slots + (kept + i) * 4u) = s_dropped[i];
    MEM32(queue + LIGHT_COUNT) = kept;
    return kept;
}

static int light_queue_reporting(void)
{
    if (s_usage_on < 0) {
        const char *profile = getenv("RECOMP_D3D_PROFILE");
        s_usage_on = profile && *profile && *profile != '0';
    }
    return s_usage_on;
}

void mm3_light_queue_refused(unsigned beam)
{
    if (s_usage_on > 0 && beam < 2) ++s_usage.refused[beam];
}

/* The frame's camera. When the consumer runs, the current view (0x3C5588)
 * no longer describes the scene camera, so the first trusted eye seen while
 * this frame's lights are produced (street lamps, cars) is kept for it. */
static float s_eye[3];
static uint32_t s_eye_frame;

void mm3_light_queue_camera(void)
{
    float eye[3];
    if (s_eye_frame == s_frame || !mm3_view_eye(eye)) return;
    memcpy(s_eye, eye, sizeof eye);
    s_eye_frame = s_frame;
}

int mm3_light_queue_last_eye(float eye[3])
{
    if (!s_eye_frame) return 0;
    memcpy(eye, s_eye, sizeof s_eye);
    return 1;
}

static int light_queue_eye(float eye[3])
{
    if (s_eye_frame == s_frame) {
        memcpy(eye, s_eye, sizeof s_eye);
        return 1;
    }
    return mm3_view_eye(eye);
}

/* Probe (see the header): the drawn lights covering a point on the road ahead
 * of the driver's car with its headlights off, counted per frame by class
 * (0 world, 1 another car, 2 the driver's own car), and the strongest of the
 * second, reported with the usage line. */
static float s_probe_point[3];
static uint32_t s_probe_frame;
static struct {
    uint32_t frames, covered[3], best_class, best_cone, best_color;
    float best, best_size[3], best_offset[3];
} s_probe;

void mm3_light_queue_probe(const float point[3])
{
    memcpy(s_probe_point, point, sizeof s_probe_point);
    s_probe_frame = s_frame;
}

/* How much of a record's volume lies over `point`: 1 at its centre to 0 at
 * its edge (a spot: the middle of its box along the axis), times its
 * brightest channel. Points span W/H/L around their position along the
 * record's rows, spots 0..L along the forward row. */
static float light_cover(uint32_t record, const float point[3], float offset[3])
{
    float half[3] = {MEMF(record + 0x40) * .5f, MEMF(record + 0x44) * .5f, MEMF(record + 0x48) * .5f};
    float local[3], spread = 0.0f;
    uint32_t color = MEM32(record + 0x4C), i, j;
    unsigned peak = (color >> 16) & 255u;
    if (((color >> 8) & 255u) > peak) peak = (color >> 8) & 255u;
    if ((color & 255u) > peak) peak = color & 255u;
    for (i = 0; i != 3; ++i) offset[i] = point[i] - MEMF(record + 0x30 + i * 4);
    for (i = 0; i != 3; ++i) {
        if (!(half[i] > 0.0f)) return 0.0f;
        local[i] = 0.0f;
        for (j = 0; j != 3; ++j) local[i] += offset[j] * MEMF(record + i * 16 + j * 4);
    }
    if (MEM32(record + 0x50) == 1u) local[2] -= half[2];
    for (i = 0; i != 3; ++i) spread += (local[i] / half[i]) * (local[i] / half[i]);
    return spread < 1.0f ? (1.0f - spread) * (float)peak / 255.0f : 0.0f;
}

static void light_queue_probe(uint32_t queue, uint32_t drawn)
{
    uint32_t slots = mm3_light_queue_slots(queue), i, k;
    unsigned seen[3] = {0, 0, 0};
    ++s_probe.frames;
    for (i = 0; i != drawn; ++i) {
        uint32_t record = MEM32(slots + i * 4u);
        float offset[3], cover;
        unsigned light_class;
        if (!record) continue;
        cover = light_cover(record, s_probe_point, offset);
        if (!(cover > 0.0f)) continue;
        light_class = light_record_class(record);
        seen[light_class] = 1;
        if (cover > s_probe.best) {
            s_probe.best = cover;
            s_probe.best_class = light_class;
            s_probe.best_cone = MEM32(record + 0x50) == 1u;
            s_probe.best_color = MEM32(record + 0x4C) & 0x00FFFFFFu;
            for (k = 0; k != 3; ++k) {
                s_probe.best_size[k] = MEMF(record + 0x40 + k * 4);
                s_probe.best_offset[k] = offset[k];
            }
        }
    }
    for (k = 0; k != 3; ++k) s_probe.covered[k] += seen[k];
}

/* The consumer passes each frame's queue before drawing it: select what is
 * drawn, then forget this frame's car/driver tags. Without a trusted camera
 * every queued light is drawn, as before. */
void mm3_light_queue_frame(uint32_t queue)
{
    uint32_t count, capacity, budget, drawn;
    float eye[3];
    clock_t now;
    if (queue < 0x10000u || queue > 0x08000000u - LIGHT_MANAGER_BYTES) return;
#ifdef MM3_DARKNESS_MONITOR
    mm3_darkness_frame(queue);
#endif
    count = MEM32(queue + LIGHT_COUNT);
    capacity = mm3_light_queue_capacity(queue);
    if (count > capacity) count = capacity;
    budget = light_queue_budget();
    drawn = count;
    memset(&s_select, 0, sizeof s_select);
    if (count && budget && light_queue_eye(eye))
        drawn = light_queue_select(queue, mm3_light_queue_slots(queue), count, budget, eye);
    else if (count && budget)
        ++s_usage.no_camera;
    if (s_probe_frame == s_frame && light_queue_reporting()) light_queue_probe(queue, drawn);
    if (!++s_frame) s_frame = 1;
    if (!light_queue_reporting()) return;
    if (count) {
        ++s_usage.frames;
        s_usage.total += count;
        s_usage.drawn += drawn;
        s_usage.cars += s_select.cars;
        s_usage.tiny += s_select.tiny;
        s_usage.over += s_select.over;
        s_usage.fading += s_select.fading;
        if (count > s_usage.peak) s_usage.peak = count;
        if (drawn > s_usage.drawn_peak) s_usage.drawn_peak = drawn;
        if (count >= capacity) ++s_usage.full;
    }
    now = clock();
    if (!s_usage.start) { s_usage.start = now; return; }
    if (now - s_usage.start < CLOCKS_PER_SEC) return;
    if (s_usage.frames) {
        double frames = (double)s_usage.frames;
        fprintf(stderr, "[MM3_LIGHTS] per frame queued mean=%.0f peak=%u of %u, drawn mean=%.0f peak=%u "
                "budget=%u (car lights %.0f; left out: tiny/far %.0f, over budget %.0f; fading %.0f), "
                "full=%u no-camera=%u of %u frames, car lights refused pools=%u beams=%u per %.2f s\n",
                (double)s_usage.total / frames, s_usage.peak, capacity,
                (double)s_usage.drawn / frames, s_usage.drawn_peak, budget,
                (double)s_usage.cars / frames, (double)s_usage.tiny / frames,
                (double)s_usage.over / frames, (double)s_usage.fading / frames,
                s_usage.full, s_usage.no_camera, s_usage.frames, s_usage.refused[0], s_usage.refused[1],
                (double)(now - s_usage.start) / CLOCKS_PER_SEC);
    }
    if (s_probe.frames) {
        static const char *const classes[3] = {"a world light", "another car's light", "the driver's own light"};
        fprintf(stderr, "[MM3_LIGHTS] headlights off: the road 6 m ahead of the driver was lit in %u of %u frames "
                "by the driver's own lights, %u by other cars', %u by world lights",
                s_probe.covered[2], s_probe.frames, s_probe.covered[1], s_probe.covered[0]);
        if (s_probe.best > 0.0f)
            fprintf(stderr, "; strongest %s (%s %.1f x %.1f x %.1f m, colour %06X, %.0f%% strength there, "
                    "centre %.1f m away)", classes[s_probe.best_class < 3 ? s_probe.best_class : 0],
                    s_probe.best_cone ? "beam" : "pool", s_probe.best_size[0], s_probe.best_size[1],
                    s_probe.best_size[2], (unsigned)s_probe.best_color, s_probe.best * 100.0f,
                    sqrtf(s_probe.best_offset[0] * s_probe.best_offset[0] +
                          s_probe.best_offset[1] * s_probe.best_offset[1] +
                          s_probe.best_offset[2] * s_probe.best_offset[2]));
        fputc('\n', stderr);
        memset(&s_probe, 0, sizeof s_probe);
    }
    memset(&s_usage, 0, sizeof s_usage);
    s_usage.start = now;
}
