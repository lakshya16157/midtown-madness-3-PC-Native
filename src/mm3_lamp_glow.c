#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "mm3_lamp_glow.h"
#include "mm3_view_eye.h"

extern void sub_001A831B_original(void);

/* Every lamp glow (car lenses, street lamps, signs) is a 64-byte record that
 * native 1A831B appends to its manager's list (+0x23C, count +0x218); +0x04
 * holds its world position. Flares/Flares.psh shows a glow only where that
 * point passes a depth test against the scene, and the host renderer depth
 * tests the sprite at the same depth. A point that sits on or just inside its
 * own lens or housing therefore blinks out: imported cars' rear lamps are 1cm
 * off a curved lens whose stored positions are only about 1cm exact, ring
 * lamps sit in a recess, and street-lamp bulbs sit inside their heads.
 *
 * Moving the point toward the camera leaves its screen position unchanged and
 * lets it clear those few centimetres. The lift grows with distance to cover
 * depth precision and stays far thinner than any building or hill, which
 * still hide the glow. MM3_GLOW_LIFT=0 restores the retail positions. */
#define GLOW_LIFT_BASE 0.12f
#define GLOW_LIFT_SLOPE 0.003f
#define GLOW_LIFT_MAX 3.0f
/* Beyond this the lift is always under a quarter of the distance. */
#define GLOW_LIFT_NEAREST 1.0f
#define GLOW_ENTRY_SIZE 64u
#define GLOW_LIST_LIMIT 0x10000u

/* How many glows a frame may hold. Native 22C86A creates the flare manager
 * with room for 256 (the Xbox budget); that one number sizes the 64-byte
 * records (taken from the manager's own 1 MB pool) and the three vertex
 * buffers they are drawn from, 8 vertices of 56 bytes a glow, and every
 * append past it is refused. At night the lit lenses of the cars in view pass
 * 256 on their own in traffic, before street lamps, signals and signs, so the
 * last glows of a frame were dropped -- the player's own tail lamps blinked
 * as the count moved across the limit, and lamps further on were missing.
 * MM3_GLOW_CAPACITY=256..4096; 1024 by default (64 KB of records and about
 * 1.4 MB of vertex buffers). The seam in scripts/mm3_glow_capacity_seam.cmake
 * passes it where the retail constant was. */
#define GLOW_RETAIL_CAPACITY 256u
#define GLOW_DEFAULT_CAPACITY 1024u
#define GLOW_MIN_CAPACITY 256u
#define GLOW_MAX_CAPACITY 4096u

static uint32_t s_glow_capacity;

uint32_t mm3_glow_capacity(void)
{
    if (!s_glow_capacity) {
        const char *text = getenv("MM3_GLOW_CAPACITY");
        char *end = NULL;
        unsigned long value = text && *text ? strtoul(text, &end, 10) : 0;
        s_glow_capacity = text && *text && end && !*end &&
                          value >= GLOW_MIN_CAPACITY && value <= GLOW_MAX_CAPACITY
                          ? (uint32_t)value : GLOW_DEFAULT_CAPACITY;
        fprintf(stderr, "[MM3_GLOWS] glow list %u per frame (retail %u; MM3_GLOW_CAPACITY)\n",
                (unsigned)s_glow_capacity, GLOW_RETAIL_CAPACITY);
        fflush(stderr);
    }
    return s_glow_capacity;
}

int mm3_glow_lift(const float eye[3], float position[3])
{
    float toward[3], distance, lift;
    unsigned i;
    for (i = 0; i != 3; ++i) toward[i] = eye[i] - position[i];
    distance = sqrtf(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
    if (!isfinite(distance) || distance < GLOW_LIFT_NEAREST) return 0;
    lift = fminf(GLOW_LIFT_BASE + GLOW_LIFT_SLOPE * distance, GLOW_LIFT_MAX);
    for (i = 0; i != 3; ++i) position[i] += toward[i] / distance * lift;
    return 1;
}

static int glow_address(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && size <= 0x08000000u && address <= 0x08000000u - size;
}

/* Set by the car-lamp draw (mm3_car_lighting.c) while it draws a lamp of the
 * player's own car, so a refused glow can be attributed to it. */
static RECOMP_TLS int s_glow_driver;

void mm3_glow_driver_scope(int driver)
{
    s_glow_driver = driver;
}

/* Glows a frame, from the appends themselves: the list count only falls when
 * native 1A762A clears it for a new frame. With renderer profiling on (Steam
 * launches), one line a second gives the mean and peak per frame against the
 * list's size, and the appends it refused. */
static RECOMP_TLS struct {
    uint32_t manager, count, peak, frames, refused, driver_refused;
    double total;
    clock_t start;
} s_glow_stats;
static int s_glow_report_on = -1;

static void glow_report(uint32_t capacity)
{
    clock_t now;
    if (s_glow_report_on < 0) {
        const char *profile = getenv("RECOMP_D3D_PROFILE");
        s_glow_report_on = profile && *profile && *profile != '0';
    }
    if (!s_glow_report_on) return;
    now = clock();
    if (!s_glow_stats.start) { s_glow_stats.start = now; return; }
    if (now - s_glow_stats.start < CLOCKS_PER_SEC || !s_glow_stats.frames) return;
    fprintf(stderr, "[MM3_GLOWS] per frame mean %.0f peak %u of %u, refused %u (the player's car %u) per %.2f s\n",
            s_glow_stats.total / s_glow_stats.frames, (unsigned)s_glow_stats.peak, (unsigned)capacity,
            (unsigned)s_glow_stats.refused, (unsigned)s_glow_stats.driver_refused,
            (double)(now - s_glow_stats.start) / CLOCKS_PER_SEC);
    fflush(stderr);
    s_glow_stats.start = now;
    s_glow_stats.peak = s_glow_stats.frames = s_glow_stats.refused = s_glow_stats.driver_refused = 0;
    s_glow_stats.total = 0;
}

static void glow_note(uint32_t manager, uint32_t before, int appended)
{
    uint32_t capacity = MEM32(manager + 0x214), after = MEM32(manager + 0x218);
    if (manager != s_glow_stats.manager || before < s_glow_stats.count) {
        /* The previous frame ended with s_glow_stats.count glows. */
        if (manager == s_glow_stats.manager) {
            s_glow_stats.total += s_glow_stats.count;
            if (s_glow_stats.count > s_glow_stats.peak) s_glow_stats.peak = s_glow_stats.count;
            ++s_glow_stats.frames;
        }
        s_glow_stats.manager = manager;
    }
    if (!appended && before >= capacity) {
        ++s_glow_stats.refused;
        if (s_glow_driver) ++s_glow_stats.driver_refused;
    }
    s_glow_stats.count = after;
    glow_report(capacity);
}

/* stdcall (manager, flare type, record, optional output); returns AL=1 when
 * the record was appended. Native lists, capacity and output are unchanged. */
void sub_001A831B(void)
{
    uint32_t manager = MEM32(g_esp + 4), count = 0, entry;
    int known = glow_address(manager, 0x240u);
    int track = known && mm3_mod_glow_lift();
    float eye[3], position[3];
    unsigned i;
    if (known) count = MEM32(manager + 0x218);
    sub_001A831B_original();
    if (known) glow_note(manager, count, (g_eax & 0xFFu) != 0);
    if (!track || !(g_eax & 0xFFu) || count >= GLOW_LIST_LIMIT ||
        MEM32(manager + 0x218) != count + 1) return;
    entry = MEM32(manager + 0x23C);
    if (!glow_address(entry, (count + 1) * GLOW_ENTRY_SIZE)) return;
    entry += count * GLOW_ENTRY_SIZE + 4;
    for (i = 0; i != 3; ++i) {
        position[i] = MEMF(entry + i * 4);
        if (!isfinite(position[i])) return;
    }
    if (!mm3_view_eye(eye) || !mm3_glow_lift(eye, position)) return;
    for (i = 0; i != 3; ++i) MEMF(entry + i * 4) = position[i];
}
