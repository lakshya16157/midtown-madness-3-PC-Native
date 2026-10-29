#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "mm3_car_lighting.h"
#include "mm3_world_lighting.h"
#include "mm3_light_queue.h"
#include "mm3_lamp_glow.h"
#include "mm3_exhaust.h"

extern void sub_000CF2DA_original(void);
extern void sub_0022FDFD_original(void);
extern void sub_000DFE0E_original(void);

/* Road beam: a flat spot volume. The game's spot texture is a spindle that
 * starts at the lamp, is widest about a third of the way out and fades to
 * nothing at the far end, so a 50 m box lights the road from ~4 m to ~45 m.
 * A low pitch makes the box's centre plane meet the road about 18 m ahead for
 * every lamp height from 0.3 m to 1.1 m; the 6 m thickness keeps the road,
 * kerbs, other cars and walls inside the volume across hills and bumps.
 * The light pass has no surface angle, and the composite multiplies the light
 * by the albedo twice (4 x albedo x (base + albedo x light)), so a light
 * tunnel tile takes about ten times the light of asphalt: a 22 m wide beam
 * (+-33 degrees) lit the tunnel walls beside the car and the cars behind it
 * pure white, which flashed as the camera moved (user, 10-27). 12 m keeps a
 * low beam's +-20 degrees: both lanes ahead, walls only further on. */
#define CAR_BEAM_WIDTH 12.0f
#define CAR_BEAM_HEIGHT 6.0f
#define CAR_BEAM_LENGTH 50.0f
#define CAR_BEAM_PITCH 0.035f
#define CAR_BEAM_GAIN 0.26f
/* A low-energy ellipsoid fills the near field using the native smooth point
 * attenuation. It reaches from the lamps to 14 m ahead across the car's own
 * lane, follows the road plane, and replaces the old overlapping pool and
 * lamp-glow records. (It used to start 4 m behind the lamps and span 10 m:
 * walls and kerbs beside the doors were lit as brightly as the road ahead.) */
#define CAR_POOL_WIDTH 5.0f
#define CAR_POOL_HEIGHT 2.5f
#define CAR_POOL_LENGTH 14.0f
#define CAR_POOL_AHEAD 7.0f
#define CAR_POOL_DROP 0.45f
#define CAR_POOL_GAIN 0.10f
/* Sunlight overwhelms the rear lamps' illumination of nearby surfaces. Keep
 * only a faint, tight field in morning/noon; native visible lenses retain
 * their authored appearance and evening/night retain their wider spill. */
#define CAR_TAIL_RADIUS_DAY 0.35f
#define CAR_TAIL_RADIUS_NIGHT 2.2f
#define CAR_TAIL_GAIN_DAY 0.015f
#define CAR_TAIL_GAIN_NIGHT 0.22f
#define CAR_BRAKE_RADIUS_DAY 0.5f
#define CAR_BRAKE_RADIUS_NIGHT 3.4f
#define CAR_BRAKE_GAIN_DAY 0.04f
#define CAR_BRAKE_GAIN_NIGHT 0.40f
/* In the evening and at night a tail or brake lamp lights the road behind
 * the car, not a cube around its lens: that cube (4.4 m for a tail lamp,
 * 6.8 m for a brake lamp) reached every post, pole and wall within a couple
 * of metres of the rear, which flashed saturated red as the car passed (user,
 * 2026-10-27). The field is a low, lane-wide box along the car's rearward
 * axis from the bumper back, lowered toward the road like the reversing
 * field; daylight keeps the faint field at the lens. */
#define CAR_REAR_WIDTH 2.6f
#define CAR_REAR_HEIGHT 1.4f
#define CAR_REAR_DROP 0.35f
#define CAR_TAIL_LENGTH 3.0f
#define CAR_TAIL_BEHIND 1.0f
#define CAR_BRAKE_LENGTH 5.0f
#define CAR_BRAKE_BEHIND 1.8f
#define CAR_REAR_SHAPED_RADIUS 1.0f
/* Turn signals: a small amber flash on the kerb and nearby cars at night,
 * barely any spill in daylight (their native lenses keep blinking). */
#define CAR_SIGNAL_RADIUS_DAY 0.4f
#define CAR_SIGNAL_RADIUS_NIGHT 1.4f
#define CAR_SIGNAL_GAIN_DAY 0.03f
#define CAR_SIGNAL_GAIN_NIGHT 0.15f
/* Reversing lamps face rearward (every model turns their nodes 180 degrees).
 * A 9m cube around the bumper also lit both sides of the car and the road
 * beneath it. Instead, a ground-level field starting just ahead of the lamps
 * reaches about 4.5m behind the car and spans a lane's width -- in the
 * evening and at night. By day that white patch read as a lamp shining on the
 * road; like the tail and brake lamps, daylight keeps only a faint field at
 * the lens. */
#define CAR_REVERSE_WIDTH 3.6f
#define CAR_REVERSE_HEIGHT 3.0f
#define CAR_REVERSE_LENGTH 6.0f
#define CAR_REVERSE_BEHIND 1.5f
#define CAR_REVERSE_DROP 0.3f
#define CAR_REVERSE_GAIN_NIGHT 0.35f
#define CAR_REVERSE_RADIUS_DAY 0.4f
#define CAR_REVERSE_GAIN_DAY 0.03f
/* The lens glow (native flare) of a reversing lamp by day: a lit lens rather
 * than a lamp's halo. Evening and night keep the authored glow. */
#define CAR_REVERSE_GLOW_DAY 0.3f
/* Records per frame are shared with every street lamp (256 on the Xbox, 1024
 * queued by default here, mm3_light_queue.c, whose frame budget then draws
 * the ones that show, car lights ranked well above lamps). Same-kind lamps of
 * one car share a record; its front uses just one beam and one near field,
 * irrespective of the model's lens count. Points stop 32 records short of
 * the capacity so road beams always find room. */
#define CAR_BEAM_RESERVE 32u
static uint32_t car_cone_cap(uint32_t queue) { return mm3_light_queue_capacity(queue); }
static uint32_t car_point_cap(uint32_t queue) { return mm3_light_queue_capacity(queue) - CAR_BEAM_RESERVE; }

static int car_light_owner_address(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address <= 0x08000000u - size;
}

/* The race's primary driver owns its CMP graph at +8. Resolve the live
 * association rather than remembering an address across races/car swaps.
 * Native 21C6C8/21C77C use this same graph for its pose and effect mask.
 * The graph's own +4 is its root, i.e. the graph itself (read in play), never
 * the vehicle: requiring that made this lookup fail for every car. */
static uint32_t car_light_player_object(uint32_t owner)
{
    uint32_t manager = MEM32(0x003C5CDCu), race, player;
    if (!car_light_owner_address(manager, 0x58)) return 0;
    race = MEM32(manager + 0x54);
    if (!car_light_owner_address(race, 0xDC)) return 0;
    player = MEM32(race + 0xD8);
    if (!car_light_owner_address(player, 0x658) || !car_light_owner_address(owner, 8)) return 0;
    return MEM32(player + 8) == owner ? player : 0;
}

static int car_light_player(uint32_t owner)
{
    return car_light_player_object(owner) != 0;
}

/* Native 21C6C8 recomputes every driven vehicle's lamp mask each frame and
 * sets its bits through DFE0E with ECX = the CMP graph and EDI = the vehicle.
 * Remember that pairing: it identifies the driver's car in cruise as well as
 * in races, without relying on the race's primary-driver association. */
#define CAR_SWITCH_SLOTS 16u
static struct { uint32_t graph, vehicle; int state; } s_car_switch[CAR_SWITCH_SLOTS];
static unsigned s_car_switch_next, s_car_switch_lines;

static void car_switch_remember(uint32_t graph, uint32_t vehicle)
{
    unsigned i;
    if (!car_light_owner_address(graph, 0x128) || !car_light_owner_address(vehicle, 0x658)) return;
    for (i = 0; i != CAR_SWITCH_SLOTS; ++i)
        if (s_car_switch[i].graph == graph) {
            if (s_car_switch[i].vehicle != vehicle) s_car_switch[i].state = -1;
            s_car_switch[i].vehicle = vehicle;
            return;
        }
    i = s_car_switch_next++ % CAR_SWITCH_SLOTS;
    s_car_switch[i].graph = graph;
    s_car_switch[i].vehicle = vehicle;
    s_car_switch[i].state = -1;
}

static int car_switch_slot(uint32_t owner)
{
    unsigned i;
    for (i = 0; i != CAR_SWITCH_SLOTS; ++i)
        if (owner && s_car_switch[i].graph == owner) {
            uint32_t vehicle = s_car_switch[i].vehicle;
            if (car_light_owner_address(vehicle, 0x658) && MEM32(vehicle + 8) == owner) return (int)i;
            return -1;
        }
    return -1;
}

void sub_000DFE0E(void)
{
    if (MEM32(g_esp) == 0x0021C789u && mm3_mod_car_lighting()) car_switch_remember(g_ecx, g_edi);
    sub_000DFE0E_original();
}

/* The local driver's car (any mode), else the race's primary driver. */
static int car_light_driver(uint32_t owner)
{
    int slot = car_switch_slot(owner);
    if (slot >= 0 && MEM32(s_car_switch[slot].vehicle + 0x488) == 1u) return 1;
    return car_light_player(owner);
}

/* The driver's own headlight switch (+0x657): native 21B40A turns it on at
 * race start in evening/night and off by day for the local driver (+0x488 ==
 * 1), and 21C1F6 flips it on the Light control (pad White, which LB/L1 and
 * the Q key map to). Like a real light switch it governs that car's front
 * lenses, beams and tail lamps in every period: off, the car is dark (by day
 * its tail lenses used to stay lit); brake, reverse and signal lamps keep
 * working. Every other car keeps the time-of-day rule. */
uint32_t mm3_car_lighting_mask(uint32_t owner, uint32_t mask, uint32_t time_of_day)
{
    int slot = car_switch_slot(owner);
    uint32_t vehicle = slot >= 0 ? s_car_switch[slot].vehicle : car_light_player_object(owner);
    int on;
    if (!vehicle || MEM32(vehicle + 0x488) != 1u) return mm3_car_lighting_effect_mask(mask, time_of_day);
    on = MEM8(vehicle + 0x657) != 0;
    if (slot >= 0 && s_car_switch[slot].state != on) {
        if (s_car_switch_lines < 64) {
            ++s_car_switch_lines;
            fprintf(stderr, "[MM3_CARLIGHT] driver headlights %s (vehicle %08X)\n",
                    on ? "on" : "off", (unsigned)vehicle);
            fflush(stderr);
        }
        s_car_switch[slot].state = on;
    }
    return on ? mask : mask & ~0x103u;
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

/* Native local-light records contain a light-to-world matrix, three volume
 * dimensions, packed color and a point(0)/cone(1) selector. Keep the native
 * point-before-cone ordering and leave 32 slots available for road beams.
 *
 * Every car's fields are queued, on screen or not (the walker runs before
 * the game's body culling). A per-volume frustum test used to drop the off
 * screen ones; without it play was flicker-free and as smooth (exe 09:26:
 * about 320 car fields a frame, 58 fps, no read-back stalls; the game thread
 * waits on the renderer anyway). The frame budget (mm3_light_queue.c) ranks
 * what is drawn and the GPU clips volumes outside the screen. */
static uint32_t car_light_queue(const float matrix[16], float width, float height,
                            float length, uint32_t color, unsigned cone,
                            unsigned priority, unsigned *slot)
{
    uint32_t queue = MEM32(0x003C5D8Cu), count, record, at;
    if (!queue || !(color & 0x00FFFFFFu)) return 0;
    count = MEM32(queue + 0x5810);
    if (count >= (cone || priority ? car_cone_cap(queue) : car_point_cap(queue))) {
        if (!priority || count != car_cone_cap(queue) || !car_light_reclaim(queue, count)) {
            if (s_dbg_on > 0) ++s_dbg.capped[cone];
            mm3_light_queue_refused(cone);
            return 0;
        }
        --count;
    }
    record = MEM32(mm3_light_queue_slots(queue) + count * 4);
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
    while (at && MEM32(MEM32(mm3_light_queue_slots(queue) + (at - 1) * 4) + 0x50) > cone) {
        MEM32(mm3_light_queue_slots(queue) + at * 4) = MEM32(mm3_light_queue_slots(queue) + (at - 1) * 4);
        --at;
    }
    MEM32(mm3_light_queue_slots(queue) + at * 4) = record;
    MEM32(queue + 0x5810) = count + 1;
    mm3_light_queue_tag(record, priority ? 2u : 1u);
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

/* A rear lamp faces rearward (its node is turned 180 degrees); with the car's
 * HeadLightEnv axis known, exactly opposite that axis, so a toed-out lens
 * still lights straight back. */
static void car_light_rear_heading(uint32_t owner, const float source[12], float out[3])
{
    unsigned slot = car_axis_slot(owner), i;
    out[0] = source[6]; out[1] = source[7]; out[2] = source[8];
    if (!owner || !car_light_normalize(out)) return;
    for (i = 0; i != CAR_AXIS_SLOTS; ++i) {
        unsigned at = (slot + i) % CAR_AXIS_SLOTS;
        if (!s_car_axis[at].owner) break;
        if (s_car_axis[at].owner == owner) {
            float back[3] = {-s_car_axis[at].forward[0], -s_car_axis[at].forward[1], -s_car_axis[at].forward[2]};
            if (car_light_dot(back, out) > .5f) memcpy(out, back, sizeof back);
            break;
        }
    }
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
#define CAR_GROUP_SLOTS 512u
#define CAR_GROUP_PRIORITY_SLOTS 16u
#define CAR_GROUP_TOTAL_SLOTS (CAR_GROUP_SLOTS + CAR_GROUP_PRIORITY_SLOTS)
#define CAR_GROUP_HASH_SLOTS 2048u
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
 * 2048-entry table includes 16 spare groups for the primary driver's front
 * colors when ordinary sources have exhausted their 512 host groups (one
 * per car and lamp kind; a 512-record queue can hold more than 256). */
static uint16_t s_car_group_hash[CAR_GROUP_HASH_SLOTS];
static unsigned s_car_groups;
static uint32_t s_car_queue, s_car_frame, s_car_count;

/* Only a primary driver's front light may replace an existing field. Prefer
 * weak traffic pools, then other points, then beams. Protect every already
 * submitted player front color. Removing/reinserting the pointer retains the
 * native point-before-cone order and every reusable record allocation. */
static int car_light_reclaim(uint32_t queue, uint32_t count)
{
    unsigned char protected_record[MM3_LIGHT_QUEUE_MAXIMUM] = {0};
    unsigned char traffic_record[MM3_LIGHT_QUEUE_MAXIMUM] = {0};
    unsigned i, j, k, victim = count, best_tier = 3, best_energy = ~0u;
    uint32_t record;
    for (i = 0; i < s_car_groups; ++i) {
        struct car_light_group *group = s_car_group + i;
        for (j = 0; j < 2; ++j) {
            if (!group->record[j]) continue;
            k = group->slot[j];
            if (k >= count || MEM32(mm3_light_queue_slots(queue) + k * 4) != group->record[j]) {
                for (k = 0; k < count; ++k)
                    if (MEM32(mm3_light_queue_slots(queue) + k * 4) == group->record[j]) break;
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
        record = MEM32(mm3_light_queue_slots(queue) + i * 4);
        if (!record || protected_record[i]) continue;
        tier = MEM32(record + 0x50) ? 2 : traffic_record[i] ? 0 : 1;
        color = MEM32(record + 0x4C);
        energy = ((color >> 16) & 255u) + 2 * ((color >> 8) & 255u) + (color & 255u);
        if (tier < best_tier || (tier == best_tier && energy <= best_energy)) {
            victim = i; best_tier = tier; best_energy = energy;
        }
    }
    if (victim == count) return 0;
    record = MEM32(mm3_light_queue_slots(queue) + victim * 4);
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
        MEM32(mm3_light_queue_slots(queue) + i * 4) = MEM32(mm3_light_queue_slots(queue) + (i + 1) * 4);
    MEM32(mm3_light_queue_slots(queue) + (count - 1) * 4) = record;
    MEM32(queue + 0x5810) = count - 1;
    mm3_light_queue_tag(record, 0);
    return 1;
}

/* Renderer-profile report (RECOMP_D3D_PROFILE, as Steam launches set): how
 * often a car that stays lit from one frame to the next drops or regains one
 * kind of field, one line a second. Brake lamps follow the driver's pedal and
 * signals blink; tails and fronts of a car still in view should hold steady.
 * Fields left out later by the frame budget are counted there. */
#define CAR_WATCH_SLOTS 1024u
static struct car_watch { uint32_t owner, kinds; } s_car_watch[2][CAR_WATCH_SLOTS];
static unsigned s_car_watch_now;
static struct { unsigned off[10], on[10], cars, frames, driver_frames; double driver_distance; clock_t start; } s_car_switches;
static int s_car_report_on = -1;

static struct car_watch *car_watch_find(struct car_watch *table, uint32_t owner, int insert)
{
    unsigned slot = ((owner >> 4) * 0x9E3779B1u >> 16) % CAR_WATCH_SLOTS, i;
    for (i = 0; i != CAR_WATCH_SLOTS; ++i, slot = (slot + 1) % CAR_WATCH_SLOTS) {
        if (table[slot].owner == owner) return table + slot;
        if (!table[slot].owner) {
            if (!insert) return NULL;
            table[slot].owner = owner;
            return table + slot;
        }
    }
    return NULL;
}

static void car_watch_frame(void)
{
    static const char *const names[10] = {"front", "tail", "brake", "reverse", "siren",
                                          "signal-left", "signal-right", "", "", "siren2"};
    struct car_watch *now = s_car_watch[s_car_watch_now], *before = s_car_watch[s_car_watch_now ^ 1];
    unsigned i, k;
    clock_t clock_now;
    if (s_car_report_on < 0) {
        const char *profile = getenv("RECOMP_D3D_PROFILE");
        s_car_report_on = profile && *profile && *profile != '0';
    }
    if (!s_car_report_on) return;
    memset(now, 0, sizeof s_car_watch[0]);
    for (i = 0; i < s_car_groups; ++i) {
        const struct car_light_group *group = s_car_group + i;
        struct car_watch *watch;
        if (!group->owner || group->kind > 9 || (!group->record[0] && !group->record[1])) continue;
        if ((watch = car_watch_find(now, group->owner, 1)) != NULL) watch->kinds |= 1u << group->kind;
        /* The camera's distance from the driver's own lamps checks the eye:
         * a chase camera sits a few metres away. */
        if (group->priority && group->count) {
            float eye[3], offset = 0.0f;
            if (mm3_light_queue_last_eye(eye)) {
                for (k = 0; k != 3; ++k)
                    offset += (group->lamp[0].source[9 + k] - eye[k]) * (group->lamp[0].source[9 + k] - eye[k]);
                if (isfinite(offset)) {
                    s_car_switches.driver_distance += sqrtf(offset);
                    ++s_car_switches.driver_frames;
                }
            }
        }
    }
    for (i = 0; i != CAR_WATCH_SLOTS; ++i) {
        const struct car_watch *was;
        if (!now[i].owner || (was = car_watch_find(before, now[i].owner, 0)) == NULL) continue;
        ++s_car_switches.cars;
        for (k = 0; k != 10; ++k) {
            uint32_t bit = 1u << k;
            if (!(was->kinds & now[i].kinds & ~bit)) continue;
            if ((was->kinds & bit) && !(now[i].kinds & bit)) ++s_car_switches.off[k];
            else if (!(was->kinds & bit) && (now[i].kinds & bit)) ++s_car_switches.on[k];
        }
    }
    s_car_watch_now ^= 1u;
    ++s_car_switches.frames;
    clock_now = clock();
    if (!s_car_switches.start) { s_car_switches.start = clock_now; return; }
    if (clock_now - s_car_switches.start < CLOCKS_PER_SEC) return;
    fprintf(stderr, "[MM3_CARLIGHT] cars lit in consecutive frames %.0f; lights off/on:",
            s_car_switches.frames ? (double)s_car_switches.cars / s_car_switches.frames : 0.0);
    for (k = 0; k != 10; ++k)
        if (*names[k]) fprintf(stderr, " %s %u/%u", names[k], s_car_switches.off[k], s_car_switches.on[k]);
    if (s_car_switches.driver_frames)
        fprintf(stderr, "; camera %.1f m from the driver's lamps",
                s_car_switches.driver_distance / s_car_switches.driver_frames);
    fprintf(stderr, " per %.2f s\n", (double)(clock_now - s_car_switches.start) / CLOCKS_PER_SEC);
    memset(&s_car_switches, 0, sizeof s_car_switches);
    s_car_switches.start = clock_now;
}

static void car_light_reset(uint32_t queue, uint32_t frame, uint32_t count)
{
    car_watch_frame();
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
    /* The whole live queue: road beams sort last, so in a PC-sized queue
     * they sit past the retail 256. Searching only those lost every beam
     * once the queue passed 256, and each lens then queued another beam and
     * pool (stacked, then refused when full: the flickering beam). */
    uint32_t count = MEM32(s_car_queue + 0x5810), slots = mm3_light_queue_slots(s_car_queue);
    unsigned i, j, k, start;
    if (count > mm3_light_queue_capacity(s_car_queue)) count = mm3_light_queue_capacity(s_car_queue);
    for (i = 0; i != 2; ++i) {
        if (!group->record[i]) continue;
        j = group->slot[i];
        /* In-place updates and most second lenses retain their native slot.
         * Points queued since only move it towards the end, so search
         * forward from there first; repair just that matching entry. */
        if (j >= count || MEM32(slots + j * 4) != group->record[i]) {
            start = j < count ? j : 0;
            for (k = 0; k != count; ++k) {
                j = start + k < count ? start + k : start + k - count;
                if (MEM32(slots + j * 4) == group->record[i]) break;
            }
            if (k == count) return 0;
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
    /* No new record for a field that emits nothing; one already queued this
     * frame takes the dark color rather than keeping its earlier one. */
    if (!record && !(color & 0x00FFFFFFu)) return;
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

/* A car's fields keep the same strength at every camera distance. Each
 * frame draws a car through more than one pass (pre-cull and visible draw),
 * and a camera-distance fade evaluated per pass could leave a tail lamp lit in
 * one frame and dimmed in the next. Far and faint lights are left out by the
 * frame budget instead (mm3_light_queue.c), from one camera per frame. */
/* Development A/B through the teleport pipe ("carfields <mask>", self-play
 * with MM3_DEBUG_TELEPORT=1): which car fields are queued. Bit 0 front pools,
 * 1 front beams, 2 tail and brake fields, 3 reversing fields, 4 the other
 * lamp points, 5 the player's own car (clear: none of its fields). */
static volatile long s_car_field_mask = -1;

void mm3_car_lighting_field_mask(long mask)
{
    s_car_field_mask = mask;
}

static int car_field_on(const struct car_light_group *group, unsigned bit)
{
    long mask = s_car_field_mask;
    return (mask & (1L << bit)) && (!group->priority || (mask & 32L));
}

/* Development A/B ("carshape <beam width> <pool width> <pool height> <pool
 * length> <pool ahead>"): other front-field shapes for every car; 0 keeps
 * the constant. */
static volatile float s_car_shape[5];

void mm3_car_lighting_shape(const float shape[5])
{
    unsigned i;
    for (i = 0; i != 5; ++i) s_car_shape[i] = isfinite(shape[i]) && shape[i] > 0 ? shape[i] : 0;
}

static float car_shape(unsigned index, float fallback)
{
    float value = s_car_shape[index];
    return value > 0 ? value : fallback;
}

static void car_light_group_draw(struct car_light_group *group, float radius)
{
    float centre[12] = {0}, forward[3] = {0}, matrix[16], maximum = 0, total = 0;
    uint32_t time_of_day = MEM32(0x00407C30u);
    float reach = mm3_local_light_reach(time_of_day);
    unsigned i, j;
    for (i = 0; i != group->count; ++i) total += group->lamp[i].strength;
    if (!isfinite(total) || total <= 0) return;
    radius *= reach;
    for (i = 0; i != group->count; ++i) {
        float weight = group->lamp[i].strength / total;
        for (j = 0; j != 12; ++j) centre[j] += group->lamp[i].source[j] * weight;
        for (j = 0; j != 3; ++j) forward[j] += group->lamp[i].heading[j] * weight;
        if (group->lamp[i].strength > maximum) maximum = group->lamp[i].strength;
    }
    maximum *= mm3_local_light_gain(time_of_day);
    if (group->kind == 0) {
        float up[3] = {centre[3], centre[4], centre[5]}, level[3], vertical;
        if (!car_light_normalize(up) || !car_light_normalize(forward)) return;
        vertical = car_light_dot(forward, up);
        for (i = 0; i != 3; ++i) level[i] = forward[i] - vertical * up[i];
        if (!car_light_normalize(level)) return;
        if (car_field_on(group, 0) && car_light_matrix(centre, level, 0, matrix)) {
            for (i = 0; i != 3; ++i)
                matrix[12 + i] += level[i] * car_shape(4, CAR_POOL_AHEAD) * reach - up[i] * CAR_POOL_DROP;
            car_light_group_record(group, 0, matrix, car_shape(1, CAR_POOL_WIDTH) * reach,
                                    car_shape(2, CAR_POOL_HEIGHT), car_shape(3, CAR_POOL_LENGTH) * reach,
                                    car_light_color(group->color, CAR_POOL_GAIN * maximum), 0);
        }
        /* Height keeps the road inside the beam over bumps; only its
         * footprint (width and throw) shortens in the evening. */
        if (car_field_on(group, 1) && car_light_matrix(centre, forward, CAR_BEAM_PITCH, matrix))
            car_light_group_record(group, 1, matrix, car_shape(0, CAR_BEAM_WIDTH) * reach, CAR_BEAM_HEIGHT,
                                    CAR_BEAM_LENGTH * reach,
                                    car_light_color(group->color, CAR_BEAM_GAIN * maximum), 1);
    } else if ((group->kind == 1 || group->kind == 2) && radius >= CAR_REAR_SHAPED_RADIUS) {
        float up[3] = {centre[3], centre[4], centre[5]}, level[3], vertical;
        float length = group->kind == 2 ? CAR_BRAKE_LENGTH : CAR_TAIL_LENGTH;
        float behind = group->kind == 2 ? CAR_BRAKE_BEHIND : CAR_TAIL_BEHIND;
        if (!car_light_normalize(up) || !car_light_normalize(forward)) return;
        vertical = car_light_dot(forward, up);
        for (i = 0; i != 3; ++i) level[i] = forward[i] - vertical * up[i];
        if (!car_light_normalize(level)) return;
        if (car_field_on(group, 2) && car_light_matrix(centre, level, 0, matrix)) {
            for (i = 0; i != 3; ++i)
                matrix[12 + i] += level[i] * behind * reach - up[i] * CAR_REAR_DROP;
            car_light_group_record(group, 0, matrix, CAR_REAR_WIDTH * reach, CAR_REAR_HEIGHT,
                                    length * reach, car_light_color(group->color, maximum), 0);
        }
    } else if (group->kind == 3 && radius <= 0) {
        float up[3] = {centre[3], centre[4], centre[5]}, level[3], vertical;
        if (!car_light_normalize(up) || !car_light_normalize(forward)) return;
        vertical = car_light_dot(forward, up);
        for (i = 0; i != 3; ++i) level[i] = forward[i] - vertical * up[i];
        if (!car_light_normalize(level)) return;
        if (car_field_on(group, 3) && car_light_matrix(centre, level, 0, matrix)) {
            for (i = 0; i != 3; ++i)
                matrix[12 + i] += level[i] * CAR_REVERSE_BEHIND * reach - up[i] * CAR_REVERSE_DROP;
            car_light_group_record(group, 0, matrix, CAR_REVERSE_WIDTH * reach, CAR_REVERSE_HEIGHT,
                                    CAR_REVERSE_LENGTH * reach, car_light_color(group->color, maximum), 0);
        }
    } else if (car_field_on(group, 4)) {
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
    /* Every lamp of the driver's own car may claim a queue record: its
     * beam, tails, brakes, reverse and signals are the lights always in view. */
    unsigned i, j, bucket, appended = 0, priority = car_light_driver(owner);
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
        if (!priority && MEM32(s_car_queue + 0x5810) >= (kind == 0 ? car_cone_cap(s_car_queue) : car_point_cap(s_car_queue))) {
            if (group) { group->record[0] = group->record[1] = 0; group->count = 0; }
            mm3_light_queue_refused(kind == 0);
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
        if (count < car_cone_cap(s_car_queue) && MEM32(mm3_light_queue_slots(s_car_queue) + count * 4)) {
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
    if (!mm3_mod_car_lighting()) return;
    uint32_t kind = MEM32(node + 0x34), world = MEM32(0x003C55A0u);
    uint32_t time_of_day = MEM32(0x00407C30u);
    uint32_t color = MEM32(node + 0x64);
    uint32_t owner = MEM32(node + 4);
    float heading[3], brightness;
    int enabled = world && !MEM8(world + 0x2A8) && MEM32(world + 0x24C);
    mask = mm3_car_lighting_mask(owner, mask, time_of_day);
    /* Diagnostic (light usage report): with the driver's headlights off in
     * the evening or at night, which lights still cover the road 6m ahead
     * of its front lenses. */
    if (kind == 0 && !(mask & 1u) && mm3_car_lighting_dark_period(time_of_day) && car_light_driver(owner)) {
        float ahead[3] = {source[6], source[7], source[8]};
        if (car_light_normalize(ahead)) {
            unsigned i;
            for (i = 0; i != 3; ++i) ahead[i] = source[9 + i] + ahead[i] * 6.0f;
            mm3_light_queue_probe(ahead);
        }
    }
    car_light_frame();
    if (!enabled) { if (s_dbg_on > 0) ++s_dbg.disabled; return; }
    mm3_light_queue_camera();

    if (kind == 8) {
        if (!(mask & 0x101u)) return;
        car_light_align(owner, source);
        /* The Formula 1 assets retain a zero-color road-light handle. Give
         * those cars evening/night illumination without creating lenses
         * on their bodywork or changing saved effect data. */
        if (color) return;
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
        int dark = mm3_car_lighting_dark_period(time_of_day);
        float rear[3];
        car_light_rear_heading(owner, source, rear);
        car_light_submit(owner, node, kind, source, rear, color,
                          brightness * (dark ? CAR_TAIL_GAIN_NIGHT : CAR_TAIL_GAIN_DAY),
                          dark ? CAR_TAIL_RADIUS_NIGHT : CAR_TAIL_RADIUS_DAY);
    } else if (kind == 2) {
        int dark = mm3_car_lighting_dark_period(time_of_day);
        float rear[3];
        car_light_rear_heading(owner, source, rear);
        car_light_submit(owner, node, kind, source, rear, color,
                          brightness * (dark ? CAR_BRAKE_GAIN_NIGHT : CAR_BRAKE_GAIN_DAY),
                          dark ? CAR_BRAKE_RADIUS_NIGHT : CAR_BRAKE_RADIUS_DAY);
    } else if (kind == 3) {
        int dark = mm3_car_lighting_dark_period(time_of_day);
        car_light_submit(owner, node, kind, source, NULL, color,
                          brightness * (dark ? CAR_REVERSE_GAIN_NIGHT : CAR_REVERSE_GAIN_DAY),
                          dark ? 0 : CAR_REVERSE_RADIUS_DAY);
    } else if (kind == 4 || kind == 9) {
        car_light_submit(owner, node, kind, source, NULL, color, brightness * .25f, 3.0f);
    } else {
        int dark = mm3_car_lighting_dark_period(time_of_day);
        car_light_submit(owner, node, kind, source, NULL, color,
                          brightness * (dark ? CAR_SIGNAL_GAIN_NIGHT : CAR_SIGNAL_GAIN_DAY),
                          dark ? CAR_SIGNAL_RADIUS_NIGHT : CAR_SIGNAL_RADIUS_DAY);
    }
}

static void car_light_hook(void)
{
    uint32_t node = g_ecx, stack = g_esp, kind = MEM32(node + 0x34);
    uint32_t mask = MEM32(stack + 12), color = MEM32(node + 0x64);
    uint32_t transform = MEM32(stack + 4), time_of_day = MEM32(0x00407C30u);
    uint32_t draw_mask = mm3_car_lighting_mask(MEM32(node + 4), mask, time_of_day);
    int day_reverse = kind == 3 && !mm3_car_lighting_dark_period(time_of_day);
    float source[12];
    memcpy(source, (const void *)XBOX_PTR(transform), sizeof source);
    {
        /* Diagnostic: a lamp frame at the car's own origin, aligned with its
         * axes, is the car's local space rather than the world. Name each
         * caller once. */
        static uint32_t callers[8];
        static unsigned ncallers;
        uint32_t caller = MEM32(stack), i;
        if (fabsf(source[9]) < 16.0f && fabsf(source[10]) < 6.0f && fabsf(source[11]) < 16.0f &&
            fabsf(source[8]) > 0.999f && ncallers < 8) {
            for (i = 0; i < ncallers && callers[i] != caller; ++i) {}
            if (i == ncallers) {
                callers[ncallers++] = caller;
                fprintf(stderr, "[MM3_CARLIGHT] lamp kind %u of %08X arrives in the car's local space "
                        "(%.2f %.2f %.2f) from %08X, frame %08X\n", (unsigned)kind, MEM32(node + 4),
                        source[9], source[10], source[11], caller, transform);
            }
        }
    }
    if (car_debug() && kind < 10) {
        uint32_t q = MEM32(0x003C5D8Cu), entry = q ? MEM32(q + 0x5810) : 0;
        ++s_dbg.calls[kind];
        if (entry < s_dbg.entry_min) s_dbg.entry_min = entry;
        if (entry > s_dbg.entry_max) s_dbg.entry_max = entry;
    }
    /* Mask daytime front lenses on the native path as well as the fields.
     * Keep authored brightness/color and the caller's effect mask intact so
     * returning to an evening/night race immediately restores the lights.
     * The replacement fields always supersede the native road projector. */
    if (kind == 8) draw_mask &= ~0x100u;
    if (draw_mask != mask) MEM32(stack + 12) = draw_mask;
    if (kind == 0) MEM32(node + 0x64) = car_light_color(car_light_front_color(color), .82f);
    else if (day_reverse) MEM32(node + 0x64) = car_light_color(color, CAR_REVERSE_GLOW_DAY);
    mm3_glow_driver_scope(car_light_driver(MEM32(node + 4)));
    sub_000CF2DA_original();
    mm3_glow_driver_scope(0);
    if (kind == 0 || day_reverse) MEM32(node + 0x64) = color;
    if (draw_mask != mask) MEM32(stack + 12) = mask;
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
    /* The exhaust smoke finds each car's model and pipes from its lamps. */
    if (mm3_exhaust_enabled()) {
        uint32_t transform = MEM32(g_esp + 4u);
        if (transform >= 0x10000u && transform < 0x08000000u - 48u)
            mm3_exhaust_lamp(MEM32(g_ecx + 4u), MEM32(g_ecx + 0x34u), (const float *)XBOX_PTR(transform));
    }
    if (!mm3_mod_car_lighting()) { sub_000CF2DA_original(); return; }
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
