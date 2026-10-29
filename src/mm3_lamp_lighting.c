#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static RECOMP_TLS float s_lamp_height, s_lamp_drop, s_lamp_side;
static RECOMP_TLS float s_lamp_locator_y;   /* the locator's height as authored, for the log */
/* World position of the fixture's object (its post's axis for lamp posts),
 * set with the mounting by the producer's density gate. */
static RECOMP_TLS float s_lamp_object[3];
static RECOMP_TLS int s_lamp_object_known;

static void lamp_set_mount(uint32_t position, uint32_t direction)
{
    float height, x, y, z, length;
    s_lamp_height = s_lamp_drop = s_lamp_side = s_lamp_locator_y = 0.0f;
    /* The aim counts on any mount: tunnel and wall lamps are told apart by it. */
    if (lamp_address(direction, 12)) {
        x = MEMF(direction); y = MEMF(direction + 4); z = MEMF(direction + 8);
        length = sqrtf(x * x + y * y + z * z);
        if (isfinite(length) && length > 1e-3f) s_lamp_drop = -y / length;
    }
    if (!lamp_address(position, 12)) return;
    /* How far out from the object's vertical axis (a lamp post's axis). */
    x = MEMF(position); z = MEMF(position + 8);
    s_lamp_side = sqrtf(x * x + z * z);
    if (!isfinite(s_lamp_side)) s_lamp_side = 0.0f;
    height = MEMF(position + 4);
    if (isfinite(height)) s_lamp_locator_y = height;
    /* Wall and ceiling fixtures on objects whose origin is not on the ground
     * read as 0 here and keep their authored reach. */
    if (isfinite(height) && height >= 1.0f && height <= 12.0f) s_lamp_height = height;
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
 * cones, warm tint) are for these. Everything else keeps the retail reach,
 * cone and colour, and the retail density unless lamp_is_restored (tunnel,
 * wall and low road lamps) below: Washington's monument floodlights (40m and 50m
 * spots with a glow, lit one in two) and their red warning lights (seven per
 * fixture) were doubled, lengthened, widened and brightened, which washed
 * the white monuments out; facade bulbs and signs likewise. Mounting is set
 * per fixture before any of these reads. */
#define LAMP_STREET_MIN_HEIGHT 2.0f
#define LAMP_STREET_MAX_RANGE 15.0f
/* The lamps beside the road matched to the lamps in its middle (user, 10-29:
 * "there are two types of street lights, the lights in the middle and the
 * lights at both extreme ends of the road, please match both of their light
 * intensity on the road"; "decrease the normal road light brightness as
 * well"). On the Xbox both put about 0.145 of the lamp colour on the road
 * under them: a middle double lamp's authored 0.148 at the foot of its post,
 * a side lantern's pool spot 0.148 near its pool's centre. Here the middle
 * ones (D326CD11, lamp_spreads) give about 0.09 (1.27x the falloff's centre,
 * half the colour, net retail through the 1.3 composite gain), the side pool
 * spots gave 0.39 (LAMP_STREET_POOL was 0.30 of the lamp colour, 10-27) and
 * street-mounted lamps 0.12 (0.85 x 1.3). A pool spot now lights its pool at
 * LAMP_STREET_POOL of the lamp colour (0.075 x 1.3 = 0.0975), and street
 * lamps at LAMP_STREET_DIM of their former strength (0.8: 0.093). */
#define LAMP_STREET_POOL 0.075f
#define LAMP_STREET_DIM 0.8f
static float s_lamp_pool_share = LAMP_STREET_POOL, s_lamp_street_scale = LAMP_STREET_DIM;
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

/* A street lamp's pool spot: a lamp-coloured spot aimed at least 30 degrees
 * down, of a lamp's reach, in a fixture with a glowing bulb, set out from its
 * post. Paris hangs it 1.7-2.3 m up and 1-2.5 m out (E9002163, 0D17322D),
 * Washington's sodium cobra heads 2.2 m up and 2.18 m out; its pool (about
 * 12 m across at the retail 7 m and 60 degrees) lies beside the post, and its
 * intensity byte made that pool 15-20% of the lamp's colour. A spot inside a
 * globe on top of its post (Washington's 74606803, E9005B70) shines down
 * the post itself, so it keeps the retail strength. */
#define LAMP_POOL_MIN_SIDE 0.3f
static int lamp_fixture_has_bulb(uint32_t source);
static int lamp_is_pool_spot(uint32_t source)
{
    float range = MEMF(source + 0x4F);
    return range > 0.0f && range <= LAMP_STREET_MAX_RANGE && MEM8(source + 0x53) && s_lamp_drop >= 0.5f &&
           s_lamp_side >= LAMP_POOL_MIN_SIDE && lamp_lights_ground(MEM32(source + 0x2B)) &&
           lamp_fixture_has_bulb(source);
}

/* Lamps that light the ground but are not street lamps by their mounting:
 * tunnel and underpass ceiling lights and wall lamps on objects whose origin
 * is not at the road (their height reads 0 or above 12m), and low road lights
 * under 2m. Retail thinned them like street lamps (byte 8), so every other
 * one stayed dark, glow included. Bright lamp colours (peak 0xC0 or more:
 * not the dim 805A3C/5A5A46 monument floods) that are warm, or white with at
 * most a slight cool cast (tunnel D2E6F5; not Paris's pink FF96DC signs),
 * lamp-sized (authored reach at most 15m), all round, aimed at least 30
 * degrees down, or aimed up like the Peripherique tunnels' wall uplighters
 * (lit one in three, so most of a tunnel's lamps stayed dark; user,
 * 2026-10-27), whose road pool lamp_uplighter_spill adds. They light again
 * at their retail reach, cone and colour; signs, neon, warning lights,
 * floodlights and sideways spots keep the retail density. */
#define LAMP_RESTORED_MIN_PEAK 0xC0u
#define LAMP_UPLIGHT_MIN 0.3f
static int lamp_is_restored(uint32_t source)
{
    uint32_t color = MEM32(source + 0x2B);
    unsigned red = (color >> 16) & 255u, green = (color >> 8) & 255u, blue = color & 255u;
    unsigned peak = red > green ? red : green, minimum = red < green ? red : green;
    float range = MEMF(source + 0x4F);
    if (blue > peak) peak = blue;
    if (blue < minimum) minimum = blue;
    if (peak < LAMP_RESTORED_MIN_PEAK || !lamp_lights_ground(color)) return 0;
    if (blue > green && minimum * 20u < peak * 17u) return 0;
    if (!(range <= LAMP_STREET_MAX_RANGE)) return 0;
    return !MEM8(source + 0x53) || s_lamp_drop >= 0.5f || s_lamp_drop <= -LAMP_UPLIGHT_MIN;
}

/* Restored lamps with no street mounting (their light at the foot of a post,
 * on a wall, in a tunnel or a highway lamp head) spread the same light wider:
 * LAMP_SOFT_SPREAD times the reach at 1/SPREAD^2 of the colour. All of them
 * are lit now (one in two or three on the Xbox) and reach LAMP_AREA_REACH
 * further, and some stand where cars pass: the Peripherique's double lamps
 * (D326CD11) hold their 9 m light at the foot of the post in the central
 * barrier, beside the inner lane, so a car flashed white at every post and
 * black between them (user, 10-29: "on the highway there are other types of
 * lamps as well"). The falloff's tail is the colour times the reach squared,
 * so the road keeps the same light and far reach while the peak halves and
 * neighbouring pools meet. Pool spots and lanterns on short posts (mounted
 * 1-2 m) keep their street pools. */
#define LAMP_SOFT_SPREAD 1.4142136f
static int lamp_spreads(uint32_t source)
{
    return s_lamp_height <= 0.0f && MEMF(source + 0x4F) > 0.0f && lamp_is_restored(source) &&
           !lamp_is_pool_spot(source);
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
/* Every lamp-coloured, lamp-sized light (street lamps and their pool spots,
 * tunnel, wall and low road lamps, the tunnels' uplighter pools) reaches this
 * much further (user, 2026-10-28: "increase the area of illumination of all
 * types of street, road, underground lamps a bit more"). With the renderer's
 * inverse-square falloff (mm3_world_lighting.c), which fades a pool's outer
 * ring faster than the authored profile, 1.3 lights about a quarter more
 * ground than before. Floodlights, signs, neon and signals keep their
 * reach. MM3_LAMP_REACH (1..2) tunes it. */
#define LAMP_AREA_REACH 1.3f
static float s_lamp_area;       /* 0 until MM3_LAMP_REACH is read */
/* Development tuning from the teleport pipe (`lamptune`): restored lamps'
 * strength, the uplighters' road pools and the reach above. */
static float s_lamp_restored_scale = 1.0f, s_lamp_spill_scale = 1.0f;

void mm3_lamp_lighting_tune(float restored, float spill, float reach)
{
    if (restored >= 0.05f && restored <= 2.0f) s_lamp_restored_scale = restored;
    if (spill >= 0.0f && spill <= 2.0f) s_lamp_spill_scale = spill;
    if (reach >= 1.0f && reach <= 2.0f) s_lamp_area = reach;
}
static float lamp_area_reach(void)
{
    if (!s_lamp_area) {
        const char *setting = getenv("MM3_LAMP_REACH");
        char *end = NULL;
        float value = setting && *setting ? strtof(setting, &end) : LAMP_AREA_REACH;
        if (setting && *setting && (end == setting || *end || !(value >= 1.0f && value <= 2.0f))) {
            fprintf(stderr, "[MM3_LAMPS] invalid MM3_LAMP_REACH; expected 1..2, using %.1f\n", LAMP_AREA_REACH);
            value = LAMP_AREA_REACH;
        }
        s_lamp_area = value;
    }
    return s_lamp_area;
}

/* In the evening (`evening` below 1) pools shrink, but never short of the
 * road: multiplying the mounted reach as well left the ground under a 9m lamp
 * at 95% of its pool's radius, where the point volume has faded out, so the
 * street lamps looked off. At least 0.8 of the mounted reach (1.4x a point's
 * height: the ground at 71% of the radius, about half the night pool's
 * area) keeps a smaller, dimmer pool on the road. */
#define LAMP_EVENING_MOUNT 0.8f
static float lamp_mount_reach(uint32_t source, float reach, float evening)
{
    float floor;
    if (s_lamp_height <= 0.0f || !lamp_lights_ground(MEM32(source + 0x2B))) return reach * evening;
    if (!MEM8(source + 0x53)) floor = LAMP_POINT_MOUNT_REACH * s_lamp_height;
    else if (s_lamp_drop >= 0.5f) floor = LAMP_SPOT_MOUNT_REACH * s_lamp_height / s_lamp_drop;
    else return reach * evening;
    reach = fmaxf(reach, floor);
    return evening < 1.0f ? fmaxf(reach * evening, floor * LAMP_EVENING_MOUNT) : reach;
}

/* LocalObject.bin (OTF0 version 7) stays in memory as loaded: "OTF0", u16
 * version, u16 fixture types, u16 0x56 (effect size), u16 0x34 (type record
 * size), u32 table size; then a (key, offset) pair per type, and at each
 * offset a type record whose first byte counts the effects after it. The
 * table holding an effect is found by searching back from it, once a table. */
#define LAMP_TABLE_MAGIC 0x3046544Fu
#define LAMP_TABLE_SEARCH 0x20000u
static uint32_t lamp_effect_table(uint32_t effect)
{
    static uint32_t base, end;
    uint32_t at, stop;
    if (base && effect >= base && effect < end && MEM32(base) == LAMP_TABLE_MAGIC) return base;
    if (!lamp_address(effect, 0x56)) return 0;
    stop = effect > 0x10000u + LAMP_TABLE_SEARCH ? effect - LAMP_TABLE_SEARCH : 0x10000u;
    for (at = effect; at >= stop; --at) {
        if (MEM32(at) == LAMP_TABLE_MAGIC && MEM16(at + 4) == 7 && MEM16(at + 8) == 0x56 &&
            MEM16(at + 10) == 0x34 && lamp_address(at, MEM32(at + 12)) && effect < at + MEM32(at + 12)) {
            base = at;
            end = at + MEM32(at + 12);
            return at;
        }
    }
    return 0;
}

/* The fixture type record holding an effect descriptor (0 if none), with the
 * type's key and the effect's index in it. */
static uint32_t lamp_fixture_type(uint32_t source, uint32_t *key, uint32_t *index)
{
    uint32_t table = lamp_effect_table(source), count, i;
    if (!table) return 0;
    count = MEM16(table + 6);
    for (i = 0; i < count; i++) {
        uint32_t type = table + MEM32(table + 0x10 + i * 8 + 4), first = type + 0x34, effects = MEM8(type);
        if (source < first || source >= first + effects * 0x56u || (source - first) % 0x56u) continue;
        if (key) *key = MEM32(table + 0x10 + i * 8);
        if (index) *index = (source - first) / 0x56u;
        return type;
    }
    return 0;
}

/* Whether a glow-only bulb's fixture also has a lamp light of its own (a pool
 * spot or point of lamp colour, authored reach at most 15 m): Paris's lantern
 * posts (FFCA85 bulb and a 7 m 60-degree spot under it, or a 9 m point) and
 * most of Washington's. 0 when the fixture is not found. */
static int lamp_fixture_has_light(uint32_t source)
{
    static struct { uint32_t source, table, stamp; int lit; } cache[64];
    uint32_t table = lamp_effect_table(source), slot = (source / 0x56u) & 63u, type, j;
    int lit = 0;
    if (!table) return 0;
    if (cache[slot].source == source && cache[slot].table == table && cache[slot].stamp == MEM32(table + 4))
        return cache[slot].lit;
    type = lamp_fixture_type(source, NULL, NULL);
    for (j = 0; type && j < MEM8(type); j++) {
        uint32_t effect = type + 0x34 + j * 0x56u;
        float range = MEMF(effect + 0x4F);
        if (effect != source && range > 0.0f && range <= LAMP_STREET_MAX_RANGE &&
            lamp_lights_ground(MEM32(effect + 0x2B))) lit = 1;
    }
    cache[slot].source = source; cache[slot].table = table;
    cache[slot].stamp = MEM32(table + 4); cache[slot].lit = lit;
    return lit;
}

/* Whether an effect's fixture shows a lamp bulb: a lamp-coloured glow (flare
 * size and brightness), in the effect itself or beside it. 0 when the fixture
 * is not found. */
static int lamp_fixture_has_bulb(uint32_t source)
{
    static struct { uint32_t source, table, stamp; int bulb; } cache[64];
    uint32_t table = lamp_effect_table(source), slot = (source / 0x56u) & 63u, type, j;
    int bulb = 0;
    if (!table) return 0;
    if (cache[slot].source == source && cache[slot].table == table && cache[slot].stamp == MEM32(table + 4))
        return cache[slot].bulb;
    type = lamp_fixture_type(source, NULL, NULL);
    for (j = 0; type && j < MEM8(type); j++) {
        uint32_t effect = type + 0x34 + j * 0x56u;
        float size = MEMF(effect + 0x49);
        if (size > 0.0f && MEM8(effect + 0x4D) && lamp_lights_ground(MEM32(effect + 0x2B))) bulb = 1;
    }
    cache[slot].source = source; cache[slot].table = table;
    cache[slot].stamp = MEM32(table + 4); cache[slot].bulb = bulb;
    return bulb;
}

/* MM3_LAMP_SURVEY=1: each fixture effect the producer meets is described
 * once (up to 512): its type and index, authored light, locator (object
 * space), the object's origin and the light's world position. */
static void lamp_survey(uint32_t matrix, uint32_t local, uint32_t direction, uint32_t source)
{
    static int on = -1;
    static uint32_t seen_key[512], seen_index[512];
    static unsigned count;
    uint32_t key = 0, index = 0;
    float world[3];
    unsigned i;
    int new_type = 1;
    if (on < 0) {
        const char *setting = getenv("MM3_LAMP_SURVEY");
        on = setting && *setting && *setting != '0';
    }
    if (!on || count >= 512u || !lamp_address(source, 0x56) || !lamp_address(matrix, 0x30) ||
        !lamp_address(local, 12) || !lamp_address(direction, 12) || !lamp_fixture_type(source, &key, &index))
        return;
    for (i = 0; i < count; ++i) {
        if (seen_key[i] == key && seen_index[i] == index) return;
        if (seen_key[i] == key) new_type = 0;
    }
    seen_key[count] = key; seen_index[count++] = index;
    for (i = 0; i != 3; ++i)
        world[i] = MEMF(local) * MEMF(matrix + i * 4) + MEMF(local + 4) * MEMF(matrix + 0xC + i * 4) +
                   MEMF(local + 8) * MEMF(matrix + 0x18 + i * 4) + MEMF(matrix + 0x24 + i * 4);
    fprintf(stderr, "[MM3_LAMP_SURVEY] type %08X effect %u: colour %06X range %.1f cone %u intensity %u "
            "density %u brightness %u flare %.2f | locator %.2f %.2f %.2f aim %.2f %.2f %.2f | object %.2f %.2f %.2f "
            "up %.2f %.2f %.2f | light %.2f %.2f %.2f\n",
            (unsigned)key, (unsigned)index, (unsigned)(MEM32(source + 0x2B) & 0x00FFFFFFu), MEMF(source + 0x4F),
            (unsigned)MEM8(source + 0x53), (unsigned)MEM8(source + 0x11), (unsigned)MEM8(source + 8),
            (unsigned)MEM8(source + 0x4D), MEMF(source + 0x49), MEMF(local), MEMF(local + 4), MEMF(local + 8),
            MEMF(direction), MEMF(direction + 4), MEMF(direction + 8), MEMF(matrix + 0x24), MEMF(matrix + 0x28),
            MEMF(matrix + 0x2C), MEMF(matrix + 0xC), MEMF(matrix + 0x10), MEMF(matrix + 0x14),
            world[0], world[1], world[2]);
    /* The object's locators, if they are a 0x1C-byte table with one entry
     * per effect (position at +0, aim at +0xC): this effect's minus its index. */
    if (new_type && local == direction - 0xC) {
        uint32_t type = lamp_fixture_type(source, NULL, NULL), j;
        for (j = 0; type && j < MEM8(type); j++) {
            uint32_t at = local + (j - index) * 0x1Cu;
            if (!lamp_address(at, 0x18)) break;
            fprintf(stderr, "[MM3_LAMP_SURVEY]   type %08X locator %u: %.2f %.2f %.2f aim %.2f %.2f %.2f%s\n",
                    (unsigned)key, (unsigned)j, MEMF(at), MEMF(at + 4), MEMF(at + 8), MEMF(at + 0xC),
                    MEMF(at + 0x10), MEMF(at + 0x14), j == index ? " (this effect)" : "");
        }
    }
}

/* Names each kind of bulb left at its glow once (up to 8). */
static void lamp_bulb_note(uint32_t source)
{
    static uint32_t seen[8];
    static unsigned count;
    uint32_t key = (MEM32(source + 0x2B) & 0x00FFFFFFu) ^ ((uint32_t)MEM8(source + 0x4D) << 24);
    unsigned i;
    for (i = 0; i < count && seen[i] != key; ++i) {}
    if (i < count || count >= 8u) return;
    seen[count++] = key;
    fprintf(stderr, "[MM3_LAMPS] bulb %06X (flare %.2f, brightness %u, mounted %.1f m) keeps only its glow: "
            "its fixture has a lamp light of its own\n", (unsigned)(key & 0x00FFFFFFu), MEMF(source + 0x49),
            (unsigned)MEM8(source + 0x4D), s_lamp_height);
}

/* Names each kind of lamp that spreads its light once (up to 8). */
static void lamp_spread_note(uint32_t source, float reach)
{
    static uint32_t seen[8];
    static unsigned count;
    uint32_t key = (MEM32(source + 0x2B) & 0x00FFFFFFu) ^ ((uint32_t)(MEMF(source + 0x4F) * 16.0f) << 24);
    unsigned i;
    for (i = 0; i < count && seen[i] != key; ++i) {}
    if (i < count || count >= 8u) return;
    seen[count++] = key;
    fprintf(stderr, "[MM3_LAMPS] lamp %06X (%.0f m %s, light %.1f m %s its object) spreads its light: %.1f m at "
            "%.0f%% of the colour\n", (unsigned)(key & 0x00FFFFFFu), MEMF(source + 0x4F),
            !MEM8(source + 0x53) ? "all round" : s_lamp_drop >= 0.5f ? "spot aimed down" : "spot aimed up",
            fabsf(s_lamp_locator_y), s_lamp_locator_y < 0.0f ? "below" : "above", reach,
            100.0f / (LAMP_SOFT_SPREAD * LAMP_SOFT_SPREAD));
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
    if (!lamp_is_street(source)) {
        /* Lamp-sized lamp-coloured fixtures (tunnel, wall and low road
         * lights) keep their authored reach in the evening: shortened, a
         * ceiling lamp's pool stopped short of the road under it. */
        if (lamp_lights_ground(MEM32(source + 0x2B)) && range <= LAMP_STREET_MAX_RANGE) {
            if (!lamp_spreads(source)) return range * lamp_area_reach();
            lamp_spread_note(source, range * lamp_area_reach() * LAMP_SOFT_SPREAD);
            return range * lamp_area_reach() * LAMP_SOFT_SPREAD;
        }
        return range * reach;
    }
    if (range > 0.0f) {
        /* Small indicators retain small pools; road lamps gain enough reach
         * below their mounting height. Large architectural spots add at most
         * 6m instead of multiplying their already long 40-50m throw. Keeping
         * these pools local also limits how many fixtures overlap a surface. */
        return lamp_area_reach() * lamp_mount_reach(source, range + fminf(range * 0.45f, 6.0f), reach);
    }
    size = MEMF(source + 0x49);
    if (!isfinite(size) || size <= 0.0f || !MEM8(source + 0x4D) ||
        !(MEM32(source + 0x2B) & 0x00FFFFFFu)) return 0.0f;
    /* A bulb whose fixture has its own lamp light keeps only its glow, as on
     * the Xbox. A pool centred in the lantern lit the post under it from top
     * to bottom (the volume has no surface direction), while the road 5 m
     * below stayed out of reach: a bright line down every lamp post (user,
     * 2026-10-27). The fixture's own light already sits where the designers
     * aimed it at the road. */
    if (lamp_fixture_has_light(source)) {
        lamp_bulb_note(source);
        return 0.0f;
    }
    /* A visible bulb must illuminate its surroundings even when the retail
     * effect provided only a flare. Derive its reach from the authored flare
     * extent and brightness: tiny bulbs about 1-2m, larger fixtures 5-10m. */
    return lamp_area_reach() * lamp_mount_reach(source, fminf(10.0f, (1.0f + 14.0f * fminf(size, 2.0f)) *
                        sqrtf((float)MEM8(source + 0x4D) / 255.0f)), reach);
}

/* Retail E1F49 skips every fixture whose object hash is not a multiple of the
 * descriptor's byte 8 (2 for most Paris and Washington street lamps, so half
 * of them stay dark at night: no glow, no pool). Those fixtures now run their
 * native path in full, and so do thinned tunnel, wall and low road lamps
 * (lamp_is_restored, at retail strength). Street lamps are queued before vehicles in the shared
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
static RECOMP_TLS unsigned s_lamp_restored_lit, s_lamp_restored_glow;
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
    s_lamp_object_known = 0;
}

/* A pool spot hangs 1-2.5 m out from its post (Paris E9002163 1.06 m,
 * 0D17322D 2.49 m; Washington's sodium cobra heads 2.18 m), so the beam took
 * in the post's lower half, and a pale post lit as brightly as the road read
 * as a bright stripe beside every lamp. Its apex moves out to at least
 * LAMP_POOL_OFFSET from the post, along the direction it was already set out
 * (towards the road): the post stands at the dim edge of its own pool. */
#define LAMP_POOL_OFFSET 3.5f
static float lamp_pool_offset(void)
{
    static float offset = -1.0f;
    if (offset < 0.0f) {
        const char *setting = getenv("MM3_LAMP_POOL_OFFSET");
        offset = setting && *setting ? (float)atof(setting) : LAMP_POOL_OFFSET;
        if (!(offset >= 0.0f) || offset > 10.0f) offset = LAMP_POOL_OFFSET;
    }
    return offset;
}

static void lamp_pool_spot_place(uint32_t record)
{
    float dx = MEMF(record + 0x30) - s_lamp_object[0], dz = MEMF(record + 0x38) - s_lamp_object[2];
    float side = sqrtf(dx * dx + dz * dz), want = lamp_pool_offset();
    if (!s_lamp_object_known || !(side > 0.3f) || side >= want || side > 6.0f) return;
    MEMF(record + 0x30) += dx * (want / side - 1.0f);
    MEMF(record + 0x38) += dz * (want / side - 1.0f);
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
            "tunnel/wall/road lamps lit=%u glow-only=%u, other fixtures left at retail density=%u "
            "(time of day %u) per %.2f s\n",
            s_lamp_added_lit, s_lamp_added_glow, s_lamp_added_capped, s_lamp_reach,
            mm3_light_queue_capacity(MEM32(0x003C5D8Cu)), s_lamp_restored_lit, s_lamp_restored_glow,
            s_lamp_retail_kept, (unsigned)MEM32(0x00407C30u),
            (double)(now - s_lamp_report) / CLOCKS_PER_SEC);
    s_lamp_report = now;
    s_lamp_added_lit = s_lamp_added_glow = s_lamp_added_capped = s_lamp_retail_kept = 0;
    s_lamp_restored_lit = s_lamp_restored_glow = 0;
}

/* With the report on, each kind of fixture left at the retail density is
 * named once (up to 24 kinds), so a lamp a player finds dark can be told
 * apart from the ones the PC lights. */
static void lamp_retail_note(uint32_t source)
{
    static uint32_t seen[24][3];
    static unsigned count;
    uint32_t key[3];
    unsigned i;
    if (s_lamp_report_on <= 0 || count >= 24u) return;
    key[0] = MEM32(source + 0x2B) & 0x00FFFFFFu;
    key[1] = MEM32(source + 0x4F);
    key[2] = (uint32_t)MEM8(source + 0x53) | (uint32_t)MEM8(source + 8) << 8 |
             (s_lamp_drop >= 0.5f ? 1u : s_lamp_drop <= -LAMP_UPLIGHT_MIN ? 2u : 0u) << 16;
    for (i = 0; i < count; ++i)
        if (!memcmp(seen[i], key, sizeof key)) return;
    memcpy(seen[count++], key, sizeof key);
    fprintf(stderr, "[MM3_LAMPS] left at retail density (one in %u): colour %06X, %.1f m %s, mounted %.1f m\n",
            (unsigned)MEM8(source + 8), (unsigned)key[0], MEMF(source + 0x4F),
            !MEM8(source + 0x53) ? "all round" : s_lamp_drop >= 0.5f ? "spot aimed down" :
            s_lamp_drop <= -LAMP_UPLIGHT_MIN ? "spot aimed up" : "spot aimed sideways", s_lamp_height);
}

int mm3_lamp_density_skip(int skipped, uint32_t matrix, uint32_t local, uint32_t direction, uint32_t source)
{
    uint32_t queue, count;
    float distance;
    int street;
    s_lamp_glow_only = 0;
    s_lamp_added_fade = 1.0f;
    lamp_set_mount(local, direction);
    s_lamp_object_known = lamp_address(matrix, 0x30);
    if (s_lamp_object_known) {
        s_lamp_object[0] = MEMF(matrix + 0x24);
        s_lamp_object[1] = MEMF(matrix + 0x28);
        s_lamp_object[2] = MEMF(matrix + 0x2C);
    }
    lamp_survey(matrix, local, direction, source);
    mm3_light_queue_camera();
    if (!skipped || !mm3_mod_lamp_lighting() || !mm3_mod_lamp_glows()) return skipped;
    street = lamp_address(source, 0x56) && lamp_is_street(source);
    if (!street && !(lamp_address(source, 0x56) && lamp_is_restored(source))) {
        ++s_lamp_retail_kept;
        lamp_density_report();
        if (lamp_address(source, 0x56)) lamp_retail_note(source);
        return skipped;
    }
    queue = MEM32(0x003C5D8Cu);
    if (!queue) {
        s_lamp_glow_only = 1;
        ++*(street ? &s_lamp_added_glow : &s_lamp_restored_glow);
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
    if (street) ++*(s_lamp_glow_only ? &s_lamp_added_glow : &s_lamp_added_lit);
    else ++*(s_lamp_glow_only ? &s_lamp_restored_glow : &s_lamp_restored_lit);
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
    float warmth, top, pool = 0.0f;
    float gain = (MEMF(source + 0x4F) == 0.0f ?
        0.65f * (float)MEM8(source + 0x4D) / 255.0f : 0.85f) * s_lamp_street_scale;
    /* A pool spot's intensity byte (+0x11) scales its colour before this call
     * (0x26 = 15% for Paris's lanterns). Its pool reaches s_lamp_pool_share
     * (LAMP_STREET_POOL) of the lamp's colour; native fades stay in `color`. */
    if (lamp_is_pool_spot(source) && MEM8(source + 0x11) && MEM8(source + 0x11) < 255u)
        pool = s_lamp_pool_share * 256.0f / (float)MEM8(source + 0x11);
    if (!lamp_is_street(source))
        return lamp_color_gain(color, (pool > 0.0f ? pool : LAMP_ARCHITECTURE_GAIN) *
                               mm3_local_light_gain(MEM32(0x00407C30u)) *
                               (lamp_is_restored(source) ? s_lamp_restored_scale : 1.0f) /
                               (lamp_spreads(source) ? LAMP_SOFT_SPREAD * LAMP_SOFT_SPREAD : 1.0f));
    gain *= (pool > 0.0f ? pool : 1.0f) * mm3_local_light_gain(MEM32(0x00407C30u));
    if (source_blue > peak) peak = source_blue;
    if (source_blue < minimum) minimum = source_blue;
    if (!peak) return color;
    top = (float)(red > green ? (red > blue ? red : blue) : (green > blue ? green : blue));
    if (top * gain > 255.0f) gain = 255.0f / top;
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

/* Tunnel and underpass walls carry uplighters: lamp-coloured spots aimed up
 * at the ceiling (the Peripherique tunnels: city fixtures 3 m up, 45 degrees
 * up, 7 m long, very dim), so the road under them stayed dark (user,
 * 2026-10-24). Such a fixture also lights a pool of its own hue around it that
 * reaches the road, through the native point submission with the producer's
 * world matrix (aimed at least 0.3 up): 1.75x the mounting height where it
 * is known, as a street lamp's pool, otherwise the fixture's own length (the
 * tunnel fixtures' objects do not stand on the road, so their height is not
 * known). Both producers build a look-at view matrix with 259A65 (eye, at,
 * up): its axes are columns, the aim z = normalize(at - eye) is column 2, so
 * its height is at +0x18. The pool peaks at 0x24 of the lamp colour: with
 * every uplighter lit, the 1.3x reach and lights adding up, 0x48 pools
 * overlapped into white walls and a white car down the whole tunnel (user,
 * 10-29: "black white black white"); at half, self-play down the Night
 * Driver tunnel showed the car at most 125/255 instead of 232, on average
 * 49 instead of 60, the road still lit. */
#define LAMP_SPILL_PEAK 0x24u
static float lamp_matrix_up(uint32_t matrix)
{
    float up = lamp_address(matrix, 0x40u) ? MEMF(matrix + 0x18u) : 0.0f;
    return up >= 0.3f && up <= 1.001f ? up : 0.0f;
}

static int lamp_is_uplighter(uint32_t source, uint32_t matrix)
{
    float range = MEMF(source + 0x4F);
    return MEM8(source + 0x53) && lamp_matrix_up(matrix) > 0.0f &&
           lamp_lights_ground(MEM32(source + 0x2B)) && range > 0.0f && range <= LAMP_STREET_MAX_RANGE;
}

static void lamp_uplighter_spill(uint32_t matrix, uint32_t authored, uint32_t alpha, float reach,
                                 float fade, const char *kind, float range, float up)
{
    static uint32_t logged[16];
    static unsigned nlogged;
    uint32_t color, key = authored ^ ((uint32_t)(range * 16.0f) << 24);
    unsigned red = (authored >> 16) & 255u, green = (authored >> 8) & 255u, blue = authored & 255u;
    unsigned peak = red > green ? red : green, i;
    uint32_t saved[] = {g_eax, g_ecx, g_edx, g_ebp, g_seh_ebp}, stack_top = g_esp;
    union { float f; uint32_t u; } radius = {reach};
    if (blue > peak) peak = blue;
    if (!peak || !(reach > 0.0f) || reach > 2.0f * LAMP_STREET_MAX_RANGE || !lamp_address(matrix, 0x40u) ||
        !lamp_address(g_esp - 16u, 16u)) return;
    color = lamp_color_gain((alpha & 0xFF000000u) | (authored & 0x00FFFFFFu),
        (float)LAMP_SPILL_PEAK / (float)peak * mm3_local_light_gain(MEM32(0x00407C30u)) * fade *
        s_lamp_spill_scale);
    PUSH32(g_esp, radius.u);
    PUSH32(g_esp, color);
    PUSH32(g_esp, matrix);
    PUSH32(g_esp, 0); /* host-owned call, no guest return address */
    sub_00021E18_original();
    g_eax = saved[0]; g_ecx = saved[1]; g_edx = saved[2]; g_ebp = saved[3]; g_seh_ebp = saved[4];
    g_esp = stack_top;
    for (i = 0; i < nlogged && logged[i] != key; ++i) {}
    if (i == nlogged && nlogged < 16) {
        logged[nlogged++] = key;
        fprintf(stderr, "[MM3_LAMPS] %s uplighter %06X (%.0f m spot aimed %.0f deg up) lights the road under it: "
                "%.1f m pool %06X\n", kind, (unsigned)(authored & 0xFFFFFFu), range,
                asinf(fminf(1.0f, up)) * 57.29578f, reach, (unsigned)(color & 0xFFFFFFu));
    }
}

/* The object-light producer (0xCF424: a light object's position and target,
 * colour halved) submits its spots from 0xCF5ED. */
static void lamp_object_spill(uint32_t matrix, uint32_t color, float range)
{
    float up = lamp_matrix_up(matrix);
    if (!(up > 0.0f) || !isfinite(range) || !(range > 0.0f) || range > LAMP_STREET_MAX_RANGE ||
        !lamp_lights_ground(color & 0x00FFFFFFu)) return;
    lamp_uplighter_spill(matrix, color, color, range * lamp_area_reach(), 1.0f, "object", range, up);
}

void sub_000489A6(void)
{
    if (!mm3_mod_lamp_lighting()) { sub_000489A6_original(); return; }
    uint32_t stack = g_esp;
    uint32_t color = MEM32(stack + 8);
    uint32_t matrix = MEM32(stack + 4), source = g_ebx;
    int world_source = MEM32(stack) == 0x000E2533u;
    int traffic_signal = MEM32(stack) == 0x0016945Au;
    int object_light = MEM32(stack) == 0x000CF5EDu;
    float range = MEMF(stack + 12);
    uint32_t queue = 0, count = 0, record = 0;
    if (world_source && s_lamp_glow_only) { g_esp += 20; return; } /* ret 16 */
    if (world_source) {
        uint32_t tuned = lamp_source_color(g_ebx, color);
        /* An added fixture eases its pool out between 70m and 100m. */
        MEM32(stack + 8) = s_lamp_added_fade < 1.0f ? lamp_color_gain(tuned, s_lamp_added_fade) : tuned;
        /* The record the native push fills: the free slot at the count. */
        queue = MEM32(0x003C5D8Cu);
        if (lamp_address(source, 0x56) && lamp_is_pool_spot(source) && lamp_address(queue, 0x5B20)) {
            count = MEM32(queue + 0x5810);
            if (count < mm3_light_queue_capacity(queue)) record = MEM32(mm3_light_queue_slots(queue) + count * 4u);
        }
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
    if (record && lamp_address(record, 0x54) && MEM32(queue + 0x5810) == count + 1u && MEM32(record + 0x50) == 1u)
        lamp_pool_spot_place(record);
    if (world_source || traffic_signal) MEM32(stack + 8) = color;
    if (world_source && lamp_address(source, 0x56) && lamp_is_uplighter(source, matrix))
        lamp_uplighter_spill(matrix, MEM32(source + 0x2B), color, lamp_area_reach() *
                             (s_lamp_height >= 1.0f ? LAMP_POINT_MOUNT_REACH * s_lamp_height : MEMF(source + 0x4F)),
                             s_lamp_added_fade, "city", MEMF(source + 0x4F), lamp_matrix_up(matrix));
    if (object_light) lamp_object_spill(matrix, color, range);
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
