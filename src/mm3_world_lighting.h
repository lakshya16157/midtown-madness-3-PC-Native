#ifndef MM3_WORLD_LIGHTING_H
#define MM3_WORLD_LIGHTING_H

#include <stdint.h>

/* Optional accessibility/readability boost, disabled by default. LightApply
 * multiplies these RGB channels by four, so 48/255 makes unlit surfaces about
 * 75% as bright as their texture. Normal lighting preserves daylight and uses
 * low night world fill so local lights provide most illumination. */
enum { MM3_WORLD_LIGHT_FLOOR = 48 };

/* Enable eased, bounded local-light accumulation and configure how strongly
 * the world-lighting composite applies its
 * local-light buffer (MM3_LOCAL_LIGHT_GAIN, 1..8, default 1.3; 1 is retail).
 * Call once at startup, after stderr is set up. */
void mm3_world_lighting_apply_gain(void);

/* RGB is returned as a packed native color. Alpha also carries material
 * information in this engine and must never change as part of illumination. */
static inline uint32_t mm3_world_light_scale(uint32_t color, unsigned percent)
{
    uint32_t result = color & 0xFF000000u;
    unsigned shift;
    if (percent > 100u) percent = 100u;
    for (shift = 0; shift < 24; shift += 8) {
        unsigned channel = (color >> shift) & 255u;
        result |= ((channel * percent + 50u) / 100u) << shift;
    }
    return result;
}

static inline uint32_t mm3_world_ambient_lift(uint32_t color, unsigned lift)
{
    uint32_t result = color & 0xFF000000u;
    unsigned shift;
    if (lift > 16u) lift = 16u;
    for (shift = 0; shift < 24; shift += 8) {
        unsigned channel = ((color >> shift) & 255u) + lift;
        result |= (channel > 255u ? 255u : channel) << shift;
    }
    return result;
}

static inline uint32_t mm3_world_light_floor(uint32_t color, unsigned floor)
{
    uint32_t result = color & 0xFF000000u;
    unsigned shift;
    if (floor > MM3_WORLD_LIGHT_FLOOR) floor = MM3_WORLD_LIGHT_FLOOR;
    for (shift = 0; shift < 24; shift += 8) {
        unsigned channel = (color >> shift) & 255u;
        result |= (channel < floor ? floor : channel) << shift;
    }
    return result;
}

#endif
