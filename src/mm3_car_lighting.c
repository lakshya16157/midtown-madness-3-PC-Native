#include <math.h>
#include <stdint.h>
#include <string.h>
#include "recomp_types.h"

extern void sub_000CF2DA_original(void);

/* Native local-light records contain a light-to-world matrix, three volume
 * dimensions, packed color and a point(0)/cone(1) selector. Keep the native
 * point-before-cone ordering and leave 64 slots available for road beams. */
static void car_light_queue(const float matrix[16], float width, float height,
                            float length, uint32_t color, unsigned cone)
{
    uint32_t queue = MEM32(0x003C5D8Cu), count, record, at;
    if (!queue || !(color & 0x00FFFFFFu)) return;
    count = MEM32(queue + 0x5810);
    if (count >= (cone ? 256u : 192u)) return;
    record = MEM32(queue + 0x5410 + count * 4);
    if (!record) return;
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
}

static int car_light_normalize(float v[3])
{
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
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

/* The source's third row is vehicle forward. The native LookAt/inverse
 * pipeline produces these same row axes. Build a fresh orthonormal frame
 * after adding the low-beam downward pitch, retaining bank and road grade. */
static int car_light_matrix(const float source[12], float pitch, float out[16])
{
    float forward[3], up[3], right[3];
    unsigned i;
    for (i = 0; i != 12; ++i) if (!isfinite(source[i])) return 0;
    for (i = 0; i != 3; ++i) {
        forward[i] = source[6 + i];
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

static uint32_t car_light_color(uint32_t color, float brightness, float strength)
{
    uint32_t result = color & 0xFF000000u;
    unsigned shift;
    if (!isfinite(brightness) || brightness <= 0) return 0;
    if (brightness > 1) brightness = 1;
    for (shift = 0; shift < 24; shift += 8) {
        unsigned channel = (unsigned)(((color >> shift) & 255u) * brightness * strength + .5f);
        result |= (channel > 255u ? 255u : channel) << shift;
    }
    return result;
}

static void car_light_point(const float source[12], float radius, uint32_t color)
{
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    unsigned i;
    for (i = 0; i != 3; ++i) {
        if (!isfinite(source[9 + i])) return;
        matrix[12 + i] = source[9 + i];
    }
    /* Native point submission stores diameter, not radius. */
    car_light_queue(matrix, radius * 2, radius * 2, radius * 2, color, 0);
}

void sub_000CF2DA(void)
{
    uint32_t node = g_ecx, stack = g_esp, kind = MEM32(node + 0x34);
    uint32_t mask = MEM32(stack + 12), world = MEM32(0x003C55A0u);
    uint32_t color = MEM32(node + 0x64), transform = MEM32(stack + 4);
    float source[12], matrix[16], brightness;
    int enabled = world && !MEM8(world + 0x2A8) && MEM32(world + 0x24C);
    memcpy(source, (const void *)XBOX_PTR(transform), sizeof source);

    /* Active lamp nodes now supply their own road beams, including ambient
     * traffic models which have no HeadLightEnv node. Keep the original
     * flare call and activation mask; remove only the duplicate shared beam.
     * All 245 installed models with a shared beam also have active front
     * lamps, except the two deliberately disabled Formula 1 variants. */
    if (kind == 8) MEM32(stack + 12) = mask & ~0x100u;
    sub_000CF2DA_original();
    if (kind == 8) MEM32(stack + 12) = mask;
    if (!enabled) return;

    if (kind == 8) {
        /* The Formula 1 assets retain a zero-color road-light handle. Give
         * those cars driving illumination at night without creating lenses
         * on their bodywork or changing saved effect data. */
        if (color || MEM32(0x00407C30u) != 3 || !(mask & 0x101u)) return;
        if (car_light_matrix(source, .055f, matrix))
            car_light_queue(matrix, 17.7f, 3.2f, 38.0f, 0xFFA6A69Eu, 1);
        car_light_point(source, 2.5f, 0xFF353531u);
        return;
    }
    if (kind > 9 || kind == 7 || !(mask & (1u << kind))) return;
    brightness = MEMF(node + 0x54);
    if (!isfinite(brightness) || brightness <= 0) return;
    if (kind == 5 || kind == 6) {
        uint32_t owner = MEM32(node + 4);
        /* Native signal lamps omit their flare during this blink phase.
         * Reuse that same clock/owner gate for their local amber spill. */
        if (MEM8(owner + 0xED) && MEMF(world + 0x1C0) * .5f > MEMF(owner + 0x12C)) return;
    }
    if (kind == 0) {
        /* Retain the broad horizontal coverage and aim the vertical beam
         * at the road, with enough reach for driving at night. */
        if (car_light_matrix(source, .055f, matrix))
            car_light_queue(matrix, 17.7f, 3.2f, 38.0f,
                            car_light_color(color, brightness, .42f), 1);
        car_light_point(source, 2.5f, car_light_color(color, brightness, .16f));
    } else if (kind == 1) {
        car_light_point(source, 1.25f, car_light_color(color, brightness, .12f));
    } else if (kind == 2) {
        car_light_point(source, 1.8f, car_light_color(color, brightness, .26f));
    } else if (kind == 3) {
        car_light_point(source, 3.0f, car_light_color(color, brightness, .28f));
    } else if (kind == 4 || kind == 9) {
        car_light_point(source, 2.5f, car_light_color(color, brightness, .22f));
    } else {
        car_light_point(source, 1.25f, car_light_color(color, brightness, .14f));
    }
}
