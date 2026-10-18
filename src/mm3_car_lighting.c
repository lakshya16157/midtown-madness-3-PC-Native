#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_car_lighting.h"

extern void sub_000CF2DA_original(void);
extern void sub_0022FDFD_original(void);

/* Road beam: a flat spot volume. The game's spot texture is a spindle that
 * starts at the lamp, is widest about a third of the way out and fades to
 * nothing at the far end, so a 50 m box lights the road from ~4 m to ~45 m.
 * A low pitch makes the box's centre plane meet the road about 18 m ahead for
 * every lamp height from 0.3 m to 1.1 m; the 6 m thickness keeps the road,
 * kerbs, other cars and walls inside the volume across hills and bumps. */
#define CAR_BEAM_WIDTH 22.0f
#define CAR_BEAM_HEIGHT 6.0f
#define CAR_BEAM_LENGTH 50.0f
#define CAR_BEAM_PITCH 0.035f
#define CAR_BEAM_GAIN 0.26f
/* A low-energy ellipsoid fills the near field using the native smooth point
 * attenuation. It reaches from behind the bumper to 14 m ahead, follows the
 * road plane, and replaces the old overlapping pool and lamp-glow records. */
#define CAR_POOL_WIDTH 10.0f
#define CAR_POOL_HEIGHT 4.0f
#define CAR_POOL_LENGTH 18.0f
#define CAR_POOL_AHEAD 5.0f
#define CAR_POOL_DROP 0.45f
#define CAR_POOL_GAIN 0.10f
/* Records per frame are scarce (256 slots shared with every street lamp), and
 * when the queue fills the later cars lose lights from frame to frame, which
 * reads as flicker. Same-kind lamps of one car share a record; its front uses
 * just one beam and one near field, irrespective of the model's lens count. */
#define CAR_POINT_CAP 224u
#define CAR_CONE_CAP 256u

static int car_light_owner_address(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address <= 0x08000000u - size;
}

/* The race's primary driver owns its CMP graph at +8. Resolve the live
 * association rather than remembering an address across races/car swaps.
 * Native 21C6C8/21C77C use this same graph for its pose and effect mask. */
static int car_light_player(uint32_t owner)
{
    uint32_t manager = MEM32(0x003C5CDCu), race, player;
    if (!car_light_owner_address(manager, 0x58)) return 0;
    race = MEM32(manager + 0x54);
    if (!car_light_owner_address(race, 0xDC)) return 0;
    player = MEM32(race + 0xD8);
    if (!car_light_owner_address(player, 0xC) || !car_light_owner_address(owner, 8)) return 0;
    return MEM32(player + 8) == owner && MEM32(owner + 4) == player;
}

static int car_light_reclaim(uint32_t queue, uint32_t count);

/* MM3_LIGHT_DEBUG=1 (also on with MM3_LIGHT_DUMP): about every 240 lamp
 * nodes, one stderr line saying how many reached each gate and the queue, and
 * the first few queued records. Off by default; costs nothing then. */
static struct {
    unsigned calls[10], masked[10], dark[10], disabled, no_heading;
    unsigned queued[2], capped[2], duplicate[2], no_slot;
    unsigned entry_min, entry_max, window, lines, samples;
} s_dbg = { {0}, {0}, {0}, 0, 0, {0}, {0}, {0}, 0, 0xFFFFFFFFu, 0, 0, 0, 0 };
static int s_dbg_on = -1;

static int car_debug(void)
{
    if (s_dbg_on < 0) {
        const char *a = getenv("MM3_LIGHT_DEBUG"), *b = getenv("MM3_LIGHT_DUMP");
        s_dbg_on = (a && *a && strcmp(a, "0") != 0) || (b && *b);
        if (s_dbg_on) {
            fprintf(stderr, "[MM3_CARLIGHT] debug logging on\n");
            fflush(stderr);
        }
    }
    return s_dbg_on;
}

static int car_light_volume_visible(const float matrix[16], float width,
                                     float height, float length, unsigned cone)
{
    uint32_t view = MEM32(0x003C5588u), count, plane;
    float centre[3], half[3] = {width * .5f, height * .5f, length * .5f};
    unsigned i, j;
    /* Native view setup F4C71 publishes five world-space planes here. Keep
     * alternate/uninitialized views conservative, rather than losing lights. */
    if (view < 0x10000u || view > 0x08000000u - 0x1EA8u) return 1;
    count = MEM32(view + 0x1EA4);
    if (count != 5u) return 1;
    for (i = 0; i != 3; ++i)
        centre[i] = matrix[12 + i] + (cone ? matrix[8 + i] * half[2] : 0.0f);
    for (i = 0; i != count; ++i)
        for (j = 0; j != 4; ++j)
            if (!isfinite(MEMF(view + 0x1E54 + i * 16 + j * 4))) return 1;
    for (plane = view + 0x1E54, i = 0; i != count; ++i, plane += 16) {
        float n[3] = {MEMF(plane), MEMF(plane + 4), MEMF(plane + 8)};
        float distance = MEMF(plane + 12), radius = 0;
        for (j = 0; j != 3; ++j) {
            float projection = n[0] * matrix[j * 4] +
                               n[1] * matrix[j * 4 + 1] + n[2] * matrix[j * 4 + 2];
            distance += n[j] * centre[j];
            radius += half[j] * fabsf(projection);
        }
        /* Native normals point outwards. Reject the complete light volume,
         * never just its bulb/body; a car behind the camera can light the road
         * ahead. A small margin absorbs float rounding at the frustum edge. */
        if (distance > radius + .05f) return 0;
    }
    return 1;
}

/* Native local-light records contain a light-to-world matrix, three volume
 * dimensions, packed color and a point(0)/cone(1) selector. Keep the native
 * point-before-cone ordering and leave 32 slots available for road beams. */
static uint32_t car_light_queue(const float matrix[16], float width, float height,
                            float length, uint32_t color, unsigned cone,
                            unsigned priority, unsigned *slot)
{
    uint32_t queue = MEM32(0x003C5D8Cu), count, record, at;
    if (!queue || !(color & 0x00FFFFFFu)) return 0;
    if (!car_light_volume_visible(matrix, width, height, length, cone)) return 0;
    count = MEM32(queue + 0x5810);
    if (count >= (cone || priority ? CAR_CONE_CAP : CAR_POINT_CAP)) {
        if (!priority || count != CAR_CONE_CAP || !car_light_reclaim(queue, count)) {
            if (s_dbg_on > 0) ++s_dbg.capped[cone];
            return 0;
        }
        --count;
    }
    record = MEM32(queue + 0x5410 + count * 4);
    if (!record) { if (s_dbg_on > 0) ++s_dbg.no_slot; return 0; }
    if (s_dbg_on > 0) {
        ++s_dbg.queued[cone];
        if (s_dbg.samples < 6) {
            ++s_dbg.samples;
            fprintf(stderr, "[MM3_CARLIGHT] queued %s #%u: pos %.2f %.2f %.2f fwd %.2f %.2f %.2f "
                    "size %.1f x %.1f x %.1f color %08X (count was %u, record %08X)\n",
                    cone ? "cone" : "point", count, matrix[12], matrix[13], matrix[14],
                    matrix[8], matrix[9], matrix[10], width, height, length,
                    (unsigned)color, (unsigned)count, (unsigned)record);
        }
    }
    memcpy((void *)XBOX_PTR(record), matrix, 16 * sizeof(float));
    MEMF(record + 0x40) = width;
    MEMF(record + 0x44) = height;
    MEMF(record + 0x48) = length;
    MEM32(record + 0x4C) = color;
    MEM32(record + 0x50) = cone;
    at = count;
    while (at && MEM32(MEM32(queue + 0x5410 + (at - 1) * 4) + 0x50) > cone) {
        MEM32(queue + 0x5410 + at * 4) = MEM32(queue + 0x5410 + (at - 1) * 4);
        --at;
    }
    MEM32(queue + 0x5410 + at * 4) = record;
    MEM32(queue + 0x5810) = count + 1;
    if (slot) *slot = at;
    return record;
}

static float car_light_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int car_light_normalize(float v[3])
{
    float length = sqrtf(car_light_dot(v, v));
    unsigned i;
    if (!isfinite(length) || length < .0001f) return 0;
    for (i = 0; i != 3; ++i) v[i] /= length;
    return 1;
}

static void car_light_cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* Lamp nodes may be authored toed-out (Audi TT +-25 degrees, Escalade +-22,
 * Avalanche +-20, ambulances and armoured cars +-9..14). Aiming each beam
 * along its own node sent two diverging beams to the sides and left the road
 * ahead dark. Every model's straight-ahead HeadLightEnv node is aligned with
 * the car. Its traversal order varies, so cache only the current frame's
 * axis and also re-aim any front records already submitted in this frame. */
#define CAR_AXIS_SLOTS 512u
static struct { uint32_t owner; float forward[3]; } s_car_axis[CAR_AXIS_SLOTS];

static unsigned car_axis_slot(uint32_t owner)
{
    return ((owner >> 4) ^ (owner >> 11)) % CAR_AXIS_SLOTS;
}

static void car_axis_remember(uint32_t owner, const float source[12])
{
    float forward[3] = {source[6], source[7], source[8]};
    unsigned start = car_axis_slot(owner), slot = start, i;
    if (!owner || !car_light_normalize(forward)) return;
    for (i = 0; i != CAR_AXIS_SLOTS; ++i) {
        slot = (start + i) % CAR_AXIS_SLOTS;
        if (!s_car_axis[slot].owner || s_car_axis[slot].owner == owner) break;
    }
    if (i == CAR_AXIS_SLOTS) slot = start;
    s_car_axis[slot].owner = owner;
    memcpy(s_car_axis[slot].forward, forward, sizeof forward);
}

/* The vehicle's current-frame axis when it still agrees with the lamp's own
 * (within 60 degrees: guards against a reused owner address). Never borrow
 * another vehicle's axis: close traffic must not steer each other's lights.
 * Models without this node get the average of their own front-lamp axes. */
static int car_light_heading(uint32_t owner, const float source[12], float out[3])
{
    unsigned slot = car_axis_slot(owner), i;
    float own[3] = {source[6], source[7], source[8]};
    if (!car_light_normalize(own)) return 0;
    memcpy(out, own, sizeof own);
    for (i = 0; i != CAR_AXIS_SLOTS; ++i) {
        unsigned at = (slot + i) % CAR_AXIS_SLOTS;
        /* Entries are inserted by linear probing and cleared together at
         * the native reset. The first empty slot also ends a failed lookup;
         * traffic without HeadLightEnv must not scan the full table. */
        if (!s_car_axis[at].owner) break;
        if (owner && s_car_axis[at].owner == owner) {
            if (car_light_dot(s_car_axis[at].forward, own) > .5f)
                memcpy(out, s_car_axis[at].forward, sizeof own);
            break;
        }
    }
    return 1;
}

/* The source's third row is vehicle forward and its second row is up. Build a
 * fresh orthonormal frame for `heading` after adding the low-beam downward
 * pitch, retaining bank and road grade. */
static int car_light_matrix(const float source[12], const float heading[3],
                            float pitch, float out[16])
{
    float forward[3], up[3], right[3];
    unsigned i;
    for (i = 0; i != 12; ++i) if (!isfinite(source[i])) return 0;
    for (i = 0; i != 3; ++i) {
        forward[i] = heading ? heading[i] : source[6 + i];
        up[i] = source[3 + i];
    }
    if (!car_light_normalize(forward) || !car_light_normalize(up)) return 0;
    for (i = 0; i != 3; ++i) forward[i] -= pitch * up[i];
    if (!car_light_normalize(forward)) return 0;
    car_light_cross(up, forward, right);
    if (!car_light_normalize(right)) return 0;
    car_light_cross(forward, right, up);
    memset(out, 0, 16 * sizeof(float));
    for (i = 0; i != 3; ++i) {
        out[i] = right[i]; out[4 + i] = up[i]; out[8 + i] = forward[i];
        out[12 + i] = source[9 + i];
    }
    out[15] = 1;
    return 1;
}

static uint32_t car_light_color(uint32_t color, float gain)
{
    uint32_t result = color & 0xFF000000u;
    unsigned shift;
    if (!isfinite(gain) || gain <= 0) return 0;
    for (shift = 0; shift < 24; shift += 8) {
        unsigned channel = (unsigned)(((color >> shift) & 255u) * gain + .5f);
        result |= (channel > 255u ? 255u : channel) << shift;
    }
    return result;
}

/* Traffic authors brightness at .34 and above because it also sizes the
 * flare. Preserve that illumination, but let fading lamps reach zero: an
 * unconditional .8 floor held 80% power until the source vanished. The
 * smooth ramp meets the authored range without a slope discontinuity. */
static float car_light_head_strength(float brightness)
{
    float fade;
    if (!isfinite(brightness) || brightness <= 0) return 0;
    if (brightness > 1) brightness = 1;
    fade = brightness < .3f ? brightness / .3f : 1;
    fade = fade * fade * (3 - 2 * fade);
    return (.8f + .2f * brightness) * fade;
}

/* Warm halogen front lamps. Multiplication cannot add energy or introduce
 * absent channels; tail/signal/emergency hues bypass this front-only tint. */
static uint32_t car_light_front_color(uint32_t color)
{
    unsigned red = (color >> 16) & 255u;
    unsigned green = (unsigned)(((color >> 8) & 255u) * .86f + .5f);
    unsigned blue = (unsigned)((color & 255u) * .68f + .5f);
    return (color & 0xFF000000u) | (red << 16) | (green << 8) | blue;
}

/* Deduplication belongs to a vehicle and an effect kind, never a distance
 * search over the shared street/car queue. The native queue-reset hook ends
 * each cache lifetime, including frames without any car callbacks. Matching
 * groups still validate their native records before updating them in place. */
#define CAR_GROUP_SLOTS 256u
#define CAR_GROUP_PRIORITY_SLOTS 16u
#define CAR_GROUP_TOTAL_SLOTS (CAR_GROUP_SLOTS + CAR_GROUP_PRIORITY_SLOTS)
#define CAR_GROUP_HASH_SLOTS 1024u
#define CAR_GROUP_LAMPS 16u
struct car_light_group {
    uint32_t owner, color;
    unsigned kind, count, priority;
    struct { uint32_t node; float source[12], heading[3], strength; } lamp[CAR_GROUP_LAMPS];
    uint32_t record[2], image[2][0x54 / sizeof(uint32_t)];
    unsigned slot[2];
};
static struct car_light_group s_car_group[CAR_GROUP_TOTAL_SLOTS];
/* Values are group indices plus one; zero is the first unused probe. The
 * 1024-entry table includes 16 spare groups for the primary driver's front
 * colors when ordinary sources have exhausted their 256 host groups. */
static uint16_t s_car_group_hash[CAR_GROUP_HASH_SLOTS];
static unsigned s_car_groups;
static uint32_t s_car_queue, s_car_frame, s_car_count;

/* Only a primary driver's front light may replace an existing field. Prefer
 * weak traffic pools, then other points, then beams. Protect every already
 * submitted player front color. Removing/reinserting the pointer retains the
 * native point-before-cone order and all 256 reusable record allocations. */
static int car_light_reclaim(uint32_t queue, uint32_t count)
{
    unsigned char protected_record[CAR_CONE_CAP] = {0};
    unsigned char traffic_record[CAR_CONE_CAP] = {0};
    unsigned i, j, k, victim = count, best_tier = 3, best_energy = ~0u;
    uint32_t record;
    for (i = 0; i < s_car_groups; ++i) {
        struct car_light_group *group = s_car_group + i;
        for (j = 0; j < 2; ++j) {
            if (!group->record[j]) continue;
            k = group->slot[j];
            if (k >= count || MEM32(queue + 0x5410 + k * 4) != group->record[j]) {
                for (k = 0; k < count; ++k)
                    if (MEM32(queue + 0x5410 + k * 4) == group->record[j]) break;
                if (k == count) continue;
                group->slot[j] = k;
            }
            if (group->priority) protected_record[k] = 1;
            else traffic_record[k] = 1;
        }
    }
    for (i = 0; i < count; ++i) {
        uint32_t color;
        unsigned tier, energy;
        record = MEM32(queue + 0x5410 + i * 4);
        if (!record || protected_record[i]) continue;
        tier = MEM32(record + 0x50) ? 2 : traffic_record[i] ? 0 : 1;
        color = MEM32(record + 0x4C);
        energy = ((color >> 16) & 255u) + 2 * ((color >> 8) & 255u) + (color & 255u);
        if (tier < best_tier || (tier == best_tier && energy <= best_energy)) {
            victim = i; best_tier = tier; best_energy = energy;
        }
    }
    if (victim == count) return 0;
    record = MEM32(queue + 0x5410 + victim * 4);
    /* A replacement can have identical bytes. Invalidate by allocation,
     * before writing it, so a displaced vehicle cannot update this field. */
    for (i = 0; i < s_car_groups; ++i)
        for (j = 0; j < 2; ++j)
            if (s_car_group[i].record[j] == record) {
                s_car_group[i].record[j] = 0;
                s_car_group[i].slot[j] = 0;
                memset(s_car_group[i].image[j], 0, sizeof s_car_group[i].image[j]);
            }
    for (i = victim; i + 1 < count; ++i)
        MEM32(queue + 0x5410 + i * 4) = MEM32(queue + 0x5410 + (i + 1) * 4);
    MEM32(queue + 0x5410 + (count - 1) * 4) = record;
    MEM32(queue + 0x5810) = count - 1;
    return 1;
}

static void car_light_reset(uint32_t queue, uint32_t frame, uint32_t count)
{
    s_car_groups = 0;
    memset(s_car_group_hash, 0, sizeof s_car_group_hash);
    memset(s_car_axis, 0, sizeof s_car_axis);
    s_car_queue = queue;
    s_car_frame = frame;
    s_car_count = count;
}

static void car_light_frame(void)
{
    uint32_t queue = MEM32(0x003C5D8Cu);
    uint32_t count = queue ? MEM32(queue + 0x5810) : 0;
    uint32_t frame = queue ? MEM32(queue + 0x5ACC) : 0;
    /* Constant-time fallback for queue recreation/alternate entry paths.
     * Scanning every group's records here made every lamp callback (even
     * masked lamps) more expensive as traffic grew. The actual native reset
     * now invalidates the cache once, without relying on its two-bit stamp. */
    if (queue != s_car_queue || frame != s_car_frame || count < s_car_count) {
        car_light_reset(queue, frame, count);
        return;
    }
    s_car_queue = queue;
    s_car_frame = frame;
    s_car_count = count;
}

static unsigned car_light_group_bucket(uint32_t owner, unsigned kind, uint32_t color,
                                       struct car_light_group **match)
{
    uint32_t hash = (owner >> 4) ^ (owner >> 11) * 0x9E3779B1u ^
                    color * 0x85EBCA6Bu ^ kind * 0xC2B2AE35u;
    unsigned start, i;
    hash ^= hash >> 16;
    start = hash & (CAR_GROUP_HASH_SLOTS - 1);
    *match = NULL;
    for (i = 0; i != CAR_GROUP_HASH_SLOTS; ++i) {
        unsigned slot = (start + i) & (CAR_GROUP_HASH_SLOTS - 1);
        unsigned entry = s_car_group_hash[slot];
        struct car_light_group *candidate;
        /* group lookup probe */
        if (!entry) return slot;
        candidate = s_car_group + entry - 1;
        if (candidate->owner == owner && candidate->kind == kind && candidate->color == color) {
            *match = candidate;
            return slot;
        }
    }
    return CAR_GROUP_HASH_SLOTS;
}

static int car_light_group_valid(struct car_light_group *group)
{
    uint32_t count = s_car_count > 256u ? 256u : s_car_count;
    unsigned i, j;
    for (i = 0; i != 2; ++i) {
        if (!group->record[i]) continue;
        j = group->slot[i];
        /* In-place updates and most second lenses retain their native slot.
         * A new point can shift cones; repair just that matching entry then. */
        if (j >= count || MEM32(s_car_queue + 0x5410 + j * 4) != group->record[i]) {
            for (j = 0; j != count; ++j)
                if (MEM32(s_car_queue + 0x5410 + j * 4) == group->record[i]) break;
            if (j == count) return 0;
            group->slot[i] = j;
        }
        if (memcmp((const void *)XBOX_PTR(group->record[i]), group->image[i], 0x54)) return 0;
    }
    /* A missing native slot is retried under this same key rather than
     * allocating another empty group for each lens of a capped source. */
    return 1;
}

static void car_light_group_record(struct car_light_group *group, unsigned index,
                                   const float matrix[16], float width,
                                   float height, float length, uint32_t color,
                                   unsigned cone)
{
    uint32_t record = group->record[index];
    if (!(color & 0x00FFFFFFu)) return;
    if (!record) record = car_light_queue(matrix, width, height, length, color, cone,
                                         group->priority, group->slot + index);
    else {
        memcpy((void *)XBOX_PTR(record), matrix, 16 * sizeof(float));
        MEMF(record + 0x40) = width;
        MEMF(record + 0x44) = height;
        MEMF(record + 0x48) = length;
        MEM32(record + 0x4C) = color;
    }
    if (record) {
        group->record[index] = record;
        memcpy(group->image[index], (const void *)XBOX_PTR(record), 0x54);
    }
}

static void car_light_group_draw(struct car_light_group *group, float radius)
{
    float centre[12] = {0}, forward[3] = {0}, matrix[16], maximum = 0, total = 0;
    unsigned i, j;
    for (i = 0; i != group->count; ++i) total += group->lamp[i].strength;
    if (!isfinite(total) || total <= 0) return;
    for (i = 0; i != group->count; ++i) {
        float weight = group->lamp[i].strength / total;
        for (j = 0; j != 12; ++j) centre[j] += group->lamp[i].source[j] * weight;
        for (j = 0; j != 3; ++j) forward[j] += group->lamp[i].heading[j] * weight;
        if (group->lamp[i].strength > maximum) maximum = group->lamp[i].strength;
    }
    if (group->kind == 0) {
        float up[3] = {centre[3], centre[4], centre[5]}, level[3], vertical;
        if (!car_light_normalize(up) || !car_light_normalize(forward)) return;
        vertical = car_light_dot(forward, up);
        for (i = 0; i != 3; ++i) level[i] = forward[i] - vertical * up[i];
        if (!car_light_normalize(level)) return;
        if (car_light_matrix(centre, level, 0, matrix)) {
            for (i = 0; i != 3; ++i)
                matrix[12 + i] += level[i] * CAR_POOL_AHEAD - up[i] * CAR_POOL_DROP;
            car_light_group_record(group, 0, matrix, CAR_POOL_WIDTH, CAR_POOL_HEIGHT,
                                    CAR_POOL_LENGTH, car_light_color(group->color, CAR_POOL_GAIN * maximum), 0);
        }
        if (car_light_matrix(centre, forward, CAR_BEAM_PITCH, matrix))
            car_light_group_record(group, 1, matrix, CAR_BEAM_WIDTH, CAR_BEAM_HEIGHT,
                                    CAR_BEAM_LENGTH, car_light_color(group->color, CAR_BEAM_GAIN * maximum), 1);
    } else {
        memset(matrix, 0, sizeof matrix);
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
        for (i = 0; i != 3; ++i) matrix[12 + i] = centre[9 + i];
        car_light_group_record(group, 0, matrix, radius * 2, radius * 2, radius * 2,
                                car_light_color(group->color, maximum), 0);
    }
    s_car_count = MEM32(s_car_queue + 0x5810);
}

/* Distinct lamp nodes contribute once. Weight the centroid by strength, so
 * a fading lens approaches the surviving lens smoothly before it vanishes.
 * Traversal changes still produce the same final field. */
static void car_light_submit(uint32_t owner, uint32_t node, unsigned kind,
                             const float source[12], const float heading[3],
                             uint32_t color, float strength, float radius)
{
    struct car_light_group *group = NULL;
    unsigned i, j, bucket, appended = 0, priority = kind == 0 && car_light_player(owner);
    for (i = 0; i != 12; ++i) if (!isfinite(source[i])) return;
    if (!(color & 0x00FFFFFFu)) return;
    /* A source below packed-RGB precision emits no field. Do not let its
     * repeated/paired nodes consume the group budget and hide later cars. */
    if (kind == 0) {
        if (!((car_light_color(color, CAR_POOL_GAIN * strength) |
               car_light_color(color, CAR_BEAM_GAIN * strength)) & 0x00FFFFFFu)) return;
    } else if (!(car_light_color(color, strength) & 0x00FFFFFFu)) return;
    if (!s_car_queue) return;
    /* An absent owner cannot identify a shared vehicle. */
    if (!owner) owner = node;
    bucket = car_light_group_bucket(owner, kind, color, &group);
    if (bucket == CAR_GROUP_HASH_SLOTS) return;
    if (!group || !car_light_group_valid(group)) {
        /* Existing native records can still be updated when full. A new
         * group that cannot get any record must not consume host capacity. */
        if (!priority && MEM32(s_car_queue + 0x5810) >= (kind == 0 ? CAR_CONE_CAP : CAR_POINT_CAP)) {
            if (group) { group->record[0] = group->record[1] = 0; group->count = 0; }
            return;
        }
        if (!group) {
            if (s_car_groups >= (priority ? CAR_GROUP_TOTAL_SLOTS : CAR_GROUP_SLOTS)) return;
            group = s_car_group + s_car_groups++;
            s_car_group_hash[bucket] = (uint16_t)s_car_groups;
            appended = 1;
        }
        memset(group, 0, sizeof *group);
        group->owner = owner;
        group->kind = kind;
        group->color = color;
    }
    group->priority = priority;
    for (i = 0; i != group->count; ++i) if (group->lamp[i].node == node) break;
    if (i == group->count) {
        if (i == CAR_GROUP_LAMPS) return;
        ++group->count;
    } else {
        if (s_dbg_on > 0) ++s_dbg.duplicate[kind == 0];
        /* A visible draw often follows the light-only pre-cull pass. Once
         * both records exist, identical source data needs no second matrix
         * reconstruction or native record upload. Missing records still retry. */
        if (group->record[0] && (kind != 0 || group->record[1]) &&
            group->lamp[i].strength == strength &&
            !memcmp(group->lamp[i].source, source, sizeof group->lamp[i].source) &&
            !memcmp(group->lamp[i].heading, heading ? heading : source + 6,
                    sizeof group->lamp[i].heading)) return;
    }
    group->lamp[i].node = node;
    memcpy(group->lamp[i].source, source, sizeof group->lamp[i].source);
    for (j = 0; j != 3; ++j)
        group->lamp[i].heading[j] = heading ? heading[j] : source[6 + j];
    group->lamp[i].strength = strength;
    car_light_group_draw(group, radius);
    /* Frustum-rejected fields must not fill the host grouping table while
     * consuming no native records. This was the last append and drawing
     * cannot insert another group, so restoring its previously empty bucket
     * preserves linear-probe lookup. Keep genuine missing-slot retry groups. */
    if (appended && !group->record[0] && !group->record[1]) {
        uint32_t count = MEM32(s_car_queue + 0x5810);
        if (count < CAR_CONE_CAP && MEM32(s_car_queue + 0x5410 + count * 4)) {
            s_car_group_hash[bucket] = 0;
            --s_car_groups;
        }
    }
}

/* HeadLightEnv often arrives after the front lenses. Correct those same
 * native records now; waiting for the next frame made the field trail the
 * car during steering and snap when a stale axis failed the angle guard. */
static void car_light_align(uint32_t owner, const float source[12])
{
    unsigned i, j;
    car_axis_remember(owner, source);
    for (i = 0; owner && i != s_car_groups; ++i) {
        struct car_light_group *group = s_car_group + i;
        if (group->owner != owner || group->kind != 0 || !car_light_group_valid(group)) continue;
        for (j = 0; j != group->count; ++j)
            car_light_heading(owner, group->lamp[j].source, group->lamp[j].heading);
        car_light_group_draw(group, 0);
    }
}

void mm3_car_lighting_emit(uint32_t node, uint32_t mask, const float source[12])
{
    uint32_t kind = MEM32(node + 0x34), world = MEM32(0x003C55A0u);
    uint32_t color = MEM32(node + 0x64);
    uint32_t owner = MEM32(node + 4);
    float heading[3], brightness;
    int enabled = world && !MEM8(world + 0x2A8) && MEM32(world + 0x24C);
    car_light_frame();
    if (!enabled) { if (s_dbg_on > 0) ++s_dbg.disabled; return; }

    if (kind == 8) {
        car_light_align(owner, source);
        /* The Formula 1 assets retain a zero-color road-light handle. Give
         * those cars driving illumination at night without creating lenses
         * on their bodywork or changing saved effect data. */
        if (color || MEM32(0x00407C30u) != 3 || !(mask & 0x101u)) return;
        if (car_light_heading(owner, source, heading))
            car_light_submit(owner, node, 0, source, heading,
                              car_light_front_color(0xFFFFFFFFu), .9f, 0);
        return;
    }
    if (kind > 9 || kind == 7 || !(mask & (1u << kind))) {
        if (s_dbg_on > 0 && kind < 10) ++s_dbg.masked[kind];
        return;
    }
    brightness = MEMF(node + 0x54);
    if (!isfinite(brightness) || brightness <= 0) {
        if (s_dbg_on > 0) ++s_dbg.dark[kind];
        return;
    }
    if (kind == 5 || kind == 6) {
        /* Native signal lamps omit their flare during this blink phase.
         * Reuse that same clock/owner gate for their local amber spill. */
        if (MEM8(owner + 0xED) && MEMF(world + 0x1C0) * .5f > MEMF(owner + 0x12C)) return;
    }
    if (kind == 0) {
        if (car_light_heading(owner, source, heading))
            car_light_submit(owner, node, kind, source, heading,
                              car_light_front_color(color), car_light_head_strength(brightness), 0);
        else if (s_dbg_on > 0) ++s_dbg.no_heading;
        return;
    }
    if (brightness > 1) brightness = 1;
    if (kind == 1) {
        car_light_submit(owner, node, kind, source, NULL, color, brightness * .22f, 2.2f);
    } else if (kind == 2) {
        car_light_submit(owner, node, kind, source, NULL, color, brightness * .40f, 3.4f);
    } else if (kind == 3) {
        car_light_submit(owner, node, kind, source, NULL, color, brightness * .35f, 4.5f);
    } else if (kind == 4 || kind == 9) {
        car_light_submit(owner, node, kind, source, NULL, color, brightness * .25f, 3.0f);
    } else {
        car_light_submit(owner, node, kind, source, NULL, color, brightness * .18f, 2.0f);
    }
}

static void car_light_hook(void)
{
    uint32_t node = g_ecx, stack = g_esp, kind = MEM32(node + 0x34);
    uint32_t mask = MEM32(stack + 12), color = MEM32(node + 0x64);
    uint32_t transform = MEM32(stack + 4);
    float source[12];
    memcpy(source, (const void *)XBOX_PTR(transform), sizeof source);
    if (car_debug() && kind < 10) {
        uint32_t q = MEM32(0x003C5D8Cu), entry = q ? MEM32(q + 0x5810) : 0;
        ++s_dbg.calls[kind];
        if (entry < s_dbg.entry_min) s_dbg.entry_min = entry;
        if (entry > s_dbg.entry_max) s_dbg.entry_max = entry;
    }
    /* Visible lenses remain on the native draw path. The source-only model
     * pass shares these same fields and groups, so a later visible callback
     * updates the existing records instead of adding another light. */
    if (kind == 8) MEM32(stack + 12) = mask & ~0x100u;
    if (kind == 0) MEM32(node + 0x64) = car_light_color(car_light_front_color(color), .82f);
    sub_000CF2DA_original();
    if (kind == 0) MEM32(node + 0x64) = color;
    if (kind == 8) MEM32(stack + 12) = mask;
    mm3_car_lighting_emit(node, mask, source);
}

static void car_debug_report(void)
{
    unsigned k;
    if (s_dbg.lines < 40) {
        ++s_dbg.lines;
        fprintf(stderr, "[MM3_CARLIGHT] nodes/240: calls");
        for (k = 0; k != 10; ++k) if (s_dbg.calls[k]) fprintf(stderr, " k%u=%u", k, s_dbg.calls[k]);
        fprintf(stderr, " | masked");
        for (k = 0; k != 10; ++k) if (s_dbg.masked[k]) fprintf(stderr, " k%u=%u", k, s_dbg.masked[k]);
        fprintf(stderr, " | dark");
        for (k = 0; k != 10; ++k) if (s_dbg.dark[k]) fprintf(stderr, " k%u=%u", k, s_dbg.dark[k]);
        fprintf(stderr, " | world-off=%u no-heading=%u | queued points=%u cones=%u | capped points=%u cones=%u"
                " | duplicate points=%u cones=%u | no-slot=%u | queue count at node entry min=%u max=%u\n",
                s_dbg.disabled, s_dbg.no_heading, s_dbg.queued[0], s_dbg.queued[1],
                s_dbg.capped[0], s_dbg.capped[1], s_dbg.duplicate[0], s_dbg.duplicate[1],
                s_dbg.no_slot, s_dbg.entry_min == 0xFFFFFFFFu ? 0 : s_dbg.entry_min, s_dbg.entry_max);
        fflush(stderr);
    }
    memset(&s_dbg.calls, 0, sizeof s_dbg.calls);
    memset(&s_dbg.masked, 0, sizeof s_dbg.masked);
    memset(&s_dbg.dark, 0, sizeof s_dbg.dark);
    s_dbg.disabled = s_dbg.no_heading = s_dbg.no_slot = 0;
    memset(&s_dbg.queued, 0, sizeof s_dbg.queued);
    memset(&s_dbg.capped, 0, sizeof s_dbg.capped);
    memset(&s_dbg.duplicate, 0, sizeof s_dbg.duplicate);
    s_dbg.entry_min = 0xFFFFFFFFu;
    s_dbg.entry_max = 0;
    s_dbg.window = 0;
}

void sub_000CF2DA(void)
{
    car_light_hook();
    if (s_dbg_on > 0 && ++s_dbg.window >= 240) car_debug_report();
}

void sub_0022FDFD(void)
{
    /* Native reset overwrites ECX with its modulo-four stamp. Remember the
     * owner first, retain every guest register/stack effect of the original,
     * then expire host-only caches exactly once for this queue reset. */
    uint32_t queue = g_ecx;
    sub_0022FDFD_original();
    if (queue == MEM32(0x003C5D8Cu))
        car_light_reset(queue, MEM32(queue + 0x5ACC), MEM32(queue + 0x5810));
}
