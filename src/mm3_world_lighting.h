#ifndef MM3_WORLD_LIGHTING_H
#define MM3_WORLD_LIGHTING_H

#include <stdint.h>

/* LightApply multiplies these normalized RGB channels by four. A 64/255
 * floor therefore keeps ordinary world materials at about 1.004 times their
 * unlit texture color, regardless of their encoded diffuse/backlight weight. */
enum { MM3_WORLD_LIGHT_FLOOR = 64 };

/* RGB is returned as a packed native color. Alpha also carries material
 * information in this engine and must never change as part of illumination. */
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
