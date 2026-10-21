#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "mm3_world_lighting.h"
#include "mm3_view_eye.h"
#include "mm3_light_queue.h"

extern void sub_000489A6_original(void);
extern void sub_00021E18_original(void);

static int lamp_address(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address <= 0x08000000u - size;
}

/* Mounting of the fixture being processed: its locator (object-space source
 * position, then direction at +0xC in the 0x1C-byte locator table) gives the
 * height above the object's origin, which is the base of lamp posts (Y up).
 * Set per fixture by the bounds builder (E10A5 entry) and by the producer's
 * density gate (E1F49), so bounds and submitted volumes stay identical. */
static RECOMP_TLS float s_lamp_height, s_lamp_drop;

static void lamp_set_mount(uint32_t position, uint32_t direction)
{
    float height, x, y, z, length;
    s_lamp_height = s_lamp_drop = 0.0f;
    if (!lamp_address(position, 12)) return;
    height = MEMF(position + 4);
    /* Wall and ceiling fixtures on objects whose origin is not on the ground
     * read as 0 here and keep their authored reach. */
    if (!isfinite(height) || height < 1.0f || height > 12.0f) return;
    s_lamp_height = height;
    if (!lamp_address(direction, 12)) return;
    x = MEMF(direction); y = MEMF(direction + 4); z = MEMF(direction + 8);
    length = sqrtf(x * x + y * y + z * z);
    if (isfinite(length) && length > 1e-3f) s_lamp_drop = -y / length;
}

void mm3_lamp_bounds_begin(uint32_t position, uint32_t direction)
{
    lamp_set_mount(position, direction);
}

/* Street, tunnel, house and sodium lamps: neutral to warm white or amber.
 * Saturated signs, neon, emergency and signal hues keep their local reach. */
static int lamp_lights_ground(uint32_t color)
{
    unsigned red = (color >> 16) & 255u, green = (color >> 8) & 255u;
    unsigned blue = color & 255u, peak = red > green ? red : green;
    unsigned minimum = red < green ? red : green;
    if (blue > peak) peak = blue;
    if (blue < minimum) minimum = blue;
    if (!peak) return 0;
    if (minimum * 20u >= peak * 9u) return 1;
    return red == peak && green * 10u >= red * 6u && blue <= green && blue * 6u >= red;
}

/* Street lamps: lamp-coloured fixtures mounted 2-12m above their object's
 * base that light the ground below (points, or spots aimed at least 30
 * degrees down) with a lamp's reach (authored at most 15m). The PC changes
 * (every fixture lit, flare-only bulbs given a pool, longer reach, wider
 * cones, warm tint) are for these. Everything else keeps the retail density,
 * reach, cone and colour: Washington's monument floodlights (40m and 50m
 * spots with a glow, lit one in two) and their red warning lights (seven per
 * fixture) were doubled, lengthened, widened and brightened, which washed
 * the white monuments out; facade bulbs and signs likewise. Mounting is set
 * per fixture before any of these reads. */
#define LAMP_STREET_MIN_HEIGHT 2.0f
#define LAMP_STREET_MAX_RANGE 15.0f
/* Architectural fixtures at retail strength through the PC composite's
 * local-light gain (MM3_LOCAL_LIGHT_GAIN 1.3). */
#define LAMP_ARCHITECTURE_GAIN (1.0f / 1.3f)
static int lamp_is_street(uint32_t source)
{
    float range = MEMF(source + 0x4F);
    if (s_lamp_height < LAMP_STREET_MIN_HEIGHT || !lamp_lights_ground(MEM32(source + 0x2B))) return 0;
    if (!(range <= LAMP_STREET_MAX_RANGE)) return 0;
    return !MEM8(source + 0x53) || s_lamp_drop >= 0.5f;
}

/* A lamp's pool must reach the road below it. The point volume falls from
 * full at the bulb to 13/255 at its edge, so a 2-3m pool on a 3-4m post left
 * the ground under smaller lamps dark while tall lamps (7-9m authored reach)
 * lit it. Give lamp-coloured points 1.75x their mounting height (ground at
 * 57% of the radius, a pool about 1.4x the height across each side) and
 * downward spots 1.5x the distance along their axis. Fixtures that already
 * reach further keep their tuned reach. */
#define LAMP_POINT_MOUNT_REACH 1.75f
#define LAMP_SPOT_MOUNT_REACH 1.5f
static float lamp_mount_reach(uint32_t source, float reach)
{
    float floor;
    if (s_lamp_height <= 0.0f || !lamp_lights_ground(MEM32(source + 0x2B))) return reach;
    if (!MEM8(source + 0x53)) floor = LAMP_POINT_MOUNT_REACH * s_lamp_height;
    else if (s_lamp_drop >= 0.5f) floor = LAMP_SPOT_MOUNT_REACH * s_lamp_height / s_lamp_drop;
    else return reach;
    return fmaxf(reach, floor);
}

/* OTF0 LocalObject effects are packed 0x56-byte records. The same range is
 * consumed by E10A5's bounds builder and E1F49's native light producer. Never
 * change the shared descriptor: masks, breakage, flicker and source placement
 * continue through the game's normal path on every visit. */
float mm3_lamp_source_range(uint32_t source)
{
    float range = MEMF(source + 0x4F);
    float size, reach;
    if (!mm3_mod_lamp_lighting()) return range;
    if (!isfinite(range) || range < 0.0f) return 0.0f;
    reach = mm3_local_light_reach(MEM32(0x00407C30u));
    if (!lamp_is_street(source)) return range * reach;
    if (range > 0.0f) {
        /* Small indicators retain small pools; road lamps gain enough reach
         * below their mounting height. Large architectural spots add at most
         * 6m instead of multiplying their already long 40-50m throw. Keeping
         * these pools local also limits how many fixtures overlap a surface. */
        return lamp_mount_reach(source, range + fminf(range * 0.45f, 6.0f)) * reach;
    }
    size = MEMF(source + 0x49);
    if (!isfinite(size) || size <= 0.0f || !MEM8(source + 0x4D) ||
        !(MEM32(source + 0x2B) & 0x00FFFFFFu)) return 0.0f;
    /* A visible bulb must illuminate its surroundings even when the retail
     * effect provided only a flare. Derive its reach from the authored flare
     * extent and brightness: tiny bulbs about 1-2m, larger fixtures 5-10m. */
    return lamp_mount_reach(source, fminf(10.0f, (1.0f + 14.0f * fminf(size, 2.0f)) *
                        sqrtf((float)MEM8(source + 0x4D) / 255.0f))) * reach;
}

/* Retail E1F49 skips every fixture whose object hash is not a multiple of the
 * descriptor's byte 8 (2 for most Paris and Washington street lamps, so half
 * of them stay dark at night: no glow, no pool). Those fixtures now run their
 * native path in full. Street lamps are queued before vehicles in the shared
 * light queue (mm3_light_queue.c: 1024 records by default, 256 on the Xbox;
 * its frame budget then picks what is drawn). The added ones keep a quarter
 * of it (at least 112 records) free for headlights and tail lamps: 768 of
 * 1024, 144 of 256.
 *
 * Pools are chosen by distance from the camera, which is stable from frame
 * to frame: full within the current reach minus 30m, easing out to nothing
 * at the reach (100m when the queue has room). A first-come limit flickered:
 * once the queue filled, which lamps still got a pool depended on scene
 * traversal order, which changes as the camera moves. Each frame records the
 * distances of the added lamps; the next frame's reach is set so the nearest
 * ones fit about 90% of the records the lamps had. It drops at once when the
 * queue is crowded and grows back by half a metre a frame, so lamps near the
 * edge dim and brighten over seconds instead of blinking. Beyond the reach,
 * or if the hard limit is still met, a fixture keeps only its glow. */
#define LAMP_ADDED_MAX_REACH 100.0f
#define LAMP_ADDED_MIN_REACH 30.0f
#define LAMP_ADDED_FADE_BAND 30.0f
#define LAMP_REACH_GROWTH 0.5f
#define LAMP_REACH_BIN 2.0f
#define LAMP_REACH_BINS 50u
static RECOMP_TLS int s_lamp_glow_only;
static RECOMP_TLS float s_lamp_added_fade = 1.0f;
static RECOMP_TLS unsigned s_lamp_added_lit, s_lamp_added_glow, s_lamp_added_capped, s_lamp_retail_kept;
static RECOMP_TLS clock_t s_lamp_report;
static int s_lamp_report_on = -1;
/* Per-frame selection state. A frame begins when the queue's reset stamp
 * (+0x5ACC) changes or its count falls. */
static RECOMP_TLS float s_lamp_reach = LAMP_ADDED_MAX_REACH;
static RECOMP_TLS uint32_t s_lamp_frame_queue, s_lamp_frame_stamp, s_lamp_frame_count;
static RECOMP_TLS uint32_t s_lamp_frame_base, s_lamp_frame_capped, s_lamp_frame_seen;
static RECOMP_TLS uint16_t s_lamp_distance[LAMP_REACH_BINS];

static uint32_t lamp_added_limit(uint32_t queue)
{
    uint32_t capacity = mm3_light_queue_capacity(queue);
    uint32_t vehicles = capacity / 4u > 112u ? capacity / 4u : 112u;
    return capacity - vehicles;
}

/* The next frame's reach from this frame's demand. */
static void lamp_frame_end(uint32_t queue)
{
    uint32_t limit = lamp_added_limit(queue), room, fit, total = 0, bin;
    float target = LAMP_ADDED_MAX_REACH;
    if (!s_lamp_frame_seen) return;
    room = limit > s_lamp_frame_base ? limit - s_lamp_frame_base : 0;
    fit = room * 9u / 10u;
    for (bin = 0; bin != LAMP_REACH_BINS; ++bin) {
        total += s_lamp_distance[bin];
        if (total > fit) { target = (float)bin * LAMP_REACH_BIN; break; }
    }
    /* Crowded: shrink at once (one more bin if the hard limit was still met).
     * Room to spare: grow back slowly towards the reach that would fit. */
    if (target < s_lamp_reach) s_lamp_reach = target;
    else s_lamp_reach = fminf(target, s_lamp_reach + LAMP_REACH_GROWTH);
    if (s_lamp_frame_capped) s_lamp_reach -= LAMP_REACH_BIN;
    if (s_lamp_reach < LAMP_ADDED_MIN_REACH) s_lamp_reach = LAMP_ADDED_MIN_REACH;
    memset(s_lamp_distance, 0, sizeof s_lamp_distance);
    s_lamp_frame_capped = s_lamp_frame_seen = 0;
}

static void lamp_frame_check(uint32_t queue)
{
    uint32_t stamp = MEM32(queue + 0x5ACC), count = MEM32(queue + 0x5810);
    if (queue != s_lamp_frame_queue || stamp != s_lamp_frame_stamp || count < s_lamp_frame_count) {
        if (queue == s_lamp_frame_queue) {
            lamp_frame_end(queue);
        } else {
            memset(s_lamp_distance, 0, sizeof s_lamp_distance);
            s_lamp_frame_capped = s_lamp_frame_seen = 0;
        }
        s_lamp_frame_queue = queue;
        s_lamp_frame_stamp = stamp;
    }
    s_lamp_frame_count = count;
}

void mm3_lamp_density_begin(void)
{
    s_lamp_glow_only = 0;
    s_lamp_added_fade = 1.0f;
    s_lamp_height = s_lamp_drop = 0.0f;
}

/* Distance from the camera of an added fixture: native 139C8 places its
 * local position by the object's 3x4 matrix (translation at +0x24). Negative
 * without a trusted eye or position. */
static float lamp_added_distance(uint32_t matrix, uint32_t local)
{
    float eye[3], world[3], distance = 0;
    unsigned i;
    if (!mm3_view_eye(eye) || !lamp_address(matrix, 0x30) || !lamp_address(local, 12)) return -1.0f;
    for (i = 0; i != 3; ++i) {
        world[i] = MEMF(local) * MEMF(matrix + i * 4) + MEMF(local + 4) * MEMF(matrix + 0xC + i * 4) +
                   MEMF(local + 8) * MEMF(matrix + 0x18 + i * 4) + MEMF(matrix + 0x24 + i * 4);
        distance += (world[i] - eye[i]) * (world[i] - eye[i]);
    }
    distance = sqrtf(distance);
    return isfinite(distance) ? distance : -1.0f;
}

/* Pool strength (0..1) at a distance for the current reach. Without a
 * trusted distance, keep the full pool and rely on the queue limit. */
static float lamp_added_fade(float distance)
{
    float full = s_lamp_reach - LAMP_ADDED_FADE_BAND, t;
    if (distance < 0.0f || distance <= full) return 1.0f;
    if (distance >= s_lamp_reach) return 0.0f;
    t = (s_lamp_reach - distance) / LAMP_ADDED_FADE_BAND;
    return t * t * (3.0f - 2.0f * t);
}

/* With renderer profiling on (Steam launches), one line a second says how
 * many added fixtures were lit and how many fell back to glow only. */
static void lamp_density_report(void)
{
    clock_t now;
    if (s_lamp_report_on < 0) {
        const char *profile = getenv("RECOMP_D3D_PROFILE");
        s_lamp_report_on = profile && *profile && *profile != '0';
    }
    if (!s_lamp_report_on) return;
    now = clock();
    if (!s_lamp_report) { s_lamp_report = now; return; }
    if (now - s_lamp_report < CLOCKS_PER_SEC) return;
    fprintf(stderr, "[MM3_LAMPS] added street lamps lit=%u glow-only=%u capped=%u reach=%.0f m queue=%u, "
            "other fixtures left at retail density=%u per %.2f s\n",
            s_lamp_added_lit, s_lamp_added_glow, s_lamp_added_capped, s_lamp_reach,
            mm3_light_queue_capacity(MEM32(0x003C5D8Cu)), s_lamp_retail_kept,
            (double)(now - s_lamp_report) / CLOCKS_PER_SEC);
    s_lamp_report = now;
    s_lamp_added_lit = s_lamp_added_glow = s_lamp_added_capped = s_lamp_retail_kept = 0;
}

int mm3_lamp_density_skip(int skipped, uint32_t matrix, uint32_t local, uint32_t direction, uint32_t source)
{
    uint32_t queue, count;
    float distance;
    s_lamp_glow_only = 0;
    s_lamp_added_fade = 1.0f;
    lamp_set_mount(local, direction);
    mm3_light_queue_camera();
    if (!skipped || !mm3_mod_lamp_lighting() || !mm3_mod_lamp_glows()) return skipped;
    if (!lamp_address(source, 0x56) || !lamp_is_street(source)) {
        ++s_lamp_retail_kept;
        lamp_density_report();
        return skipped;
    }
    queue = MEM32(0x003C5D8Cu);
    if (!queue) {
        s_lamp_glow_only = 1;
        ++s_lamp_added_glow;
        return 0;
    }
    lamp_frame_check(queue);
    count = MEM32(queue + 0x5810);
    if (!s_lamp_frame_seen) s_lamp_frame_base = count;
    s_lamp_frame_seen = 1;
    distance = lamp_added_distance(matrix, local);
    if (distance >= 0.0f && distance < LAMP_REACH_BINS * LAMP_REACH_BIN) {
        uint32_t bin = (uint32_t)(distance / LAMP_REACH_BIN);
        if (s_lamp_distance[bin] != 0xFFFFu) ++s_lamp_distance[bin];
    }
    s_lamp_added_fade = lamp_added_fade(distance);
    s_lamp_glow_only = s_lamp_added_fade <= 0.0f;
    if (!s_lamp_glow_only && count >= lamp_added_limit(queue)) {
        s_lamp_glow_only = 1;
        s_lamp_frame_capped = 1;
        ++s_lamp_added_capped;
    }
    if (s_lamp_glow_only) ++s_lamp_added_glow;
    else ++s_lamp_added_lit;
    lamp_density_report();
    return 0;
}

float mm3_lamp_source_cone(uint32_t source)
{
    float cone = (float)MEM8(source + 0x53) * (90.0f / 255.0f);
    if (!mm3_mod_lamp_lighting() || !lamp_is_street(source)) return cone;
    /* Native spot volumes use a half-angle in degrees (clamped to 75).
     * Widen the ground footprint while retaining the authored aim/type. */
    return cone > 0.0f ? fminf(cone + 5.0f, 75.0f) : 0.0f;
}

static uint32_t lamp_color_gain(uint32_t color, float gain)
{
    unsigned red = (color >> 16) & 255u, green = (color >> 8) & 255u;
    unsigned blue = color & 255u, peak = red > green ? red : green;
    uint32_t result = color & 0xFF000000u;
    if (blue > peak) peak = blue;
    if (!peak) return color;
    /* Uniform RGB gain preserves warm/cool/signal hues and native color
     * fades. Saturate the gain as a whole, not each channel independently. */
    gain = fminf(gain, 255.0f / (float)peak);
    result |= (uint32_t)((float)red * gain + 0.5f) << 16;
    result |= (uint32_t)((float)green * gain + 0.5f) << 8;
    result |= (uint32_t)((float)blue * gain + 0.5f);
    return result;
}

static uint32_t lamp_source_color(uint32_t source, uint32_t color)
{
    unsigned red = (color >> 16) & 255u, green = (color >> 8) & 255u;
    unsigned blue = color & 255u;
    uint32_t authored = MEM32(source + 0x2B);
    unsigned source_red = (authored >> 16) & 255u;
    unsigned source_green = (authored >> 8) & 255u;
    unsigned source_blue = authored & 255u;
    unsigned peak = source_red > source_green ? source_red : source_green;
    unsigned minimum = source_red < source_green ? source_red : source_green;
    float warmth;
    float gain = MEMF(source + 0x4F) == 0.0f ?
        0.65f * (float)MEM8(source + 0x4D) / 255.0f : 0.85f;
    if (!lamp_is_street(source))
        return lamp_color_gain(color, LAMP_ARCHITECTURE_GAIN * mm3_local_light_gain(MEM32(0x00407C30u)));
    gain *= mm3_local_light_gain(MEM32(0x00407C30u));
    if (source_blue > peak) peak = source_blue;
    if (source_blue < minimum) minimum = source_blue;
    if (!peak) return color;
    /* Give neutral fixtures an amber incandescent cast. Blend continuously
     * over the nearly-white authored range. Native fades quantize RGB before
     * this call: deriving warmth from that dimmed color made some channels
     * dip as the light brightened. A fixture's tint must stay fixed throughout
     * its fade. Saturated signs and emergency hues retain authored ratios. */
    warmth = fminf(1.0f, fmaxf(0.0f,
        ((float)minimum / (float)peak - 0.75f) * 4.0f));
    return (color & 0xFF000000u) |
        ((uint32_t)((float)red * gain + 0.5f) << 16) |
        ((uint32_t)((float)green * gain * (1.0f - 0.18f * warmth) + 0.5f) << 8) |
        (uint32_t)((float)blue * gain * (1.0f - 0.40f * warmth) + 0.5f);
}

void sub_000489A6(void)
{
    if (!mm3_mod_lamp_lighting()) { sub_000489A6_original(); return; }
    uint32_t stack = g_esp;
    uint32_t color = MEM32(stack + 8);
    int world_source = MEM32(stack) == 0x000E2533u;
    int traffic_signal = MEM32(stack) == 0x0016945Au;
    if (world_source && s_lamp_glow_only) { g_esp += 20; return; } /* ret 16 */
    if (world_source) {
        uint32_t tuned = lamp_source_color(g_ebx, color);
        /* An added fixture eases its pool out between 70m and 100m. */
        MEM32(stack + 8) = s_lamp_added_fade < 1.0f ? lamp_color_gain(tuned, s_lamp_added_fade) : tuned;
    }
    if (traffic_signal) {
        /* The traffic-light producer selects only the currently lit lens,
         * tint and distance fade before its native 6m/30-degree submission.
         * Preserve that small authored pool and its signal color, at a lower
         * intensity so intersections do not accumulate oversized bright pools. */
        MEM32(stack + 8) = lamp_color_gain(color,
            0.8f * mm3_local_light_gain(MEM32(0x00407C30u)));
    }
    sub_000489A6_original();
    if (world_source || traffic_signal) MEM32(stack + 8) = color;
}

void sub_00021E18(void)
{
    if (!mm3_mod_lamp_lighting()) { sub_00021E18_original(); return; }
    uint32_t stack = g_esp;
    uint32_t color = MEM32(stack + 8);
    int world_source = MEM32(stack) == 0x000E2508u;
    if (world_source && s_lamp_glow_only) { g_esp += 16; return; } /* ret 12 */
    if (world_source) {
        uint32_t tuned = lamp_source_color(g_ebx, color);
        /* An added fixture eases its pool out between 70m and 100m. */
        MEM32(stack + 8) = s_lamp_added_fade < 1.0f ? lamp_color_gain(tuned, s_lamp_added_fade) : tuned;
    }
    sub_00021E18_original();
    if (world_source) MEM32(stack + 8) = color;
}
