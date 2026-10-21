#include <math.h>
#include <stdint.h>
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

/* stdcall (manager, flare type, record, optional output); returns AL=1 when
 * the record was appended. Native lists, capacity and output are unchanged. */
void sub_001A831B(void)
{
    uint32_t manager = MEM32(g_esp + 4), count = 0, entry;
    int track = mm3_mod_glow_lift() && glow_address(manager, 0x240u);
    float eye[3], position[3];
    unsigned i;
    if (track) count = MEM32(manager + 0x218);
    sub_001A831B_original();
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
