#ifndef MM3_CAR_LIGHTING_H
#define MM3_CAR_LIGHTING_H

#include <stdint.h>

/* Native time-of-day enum (2501CD): morning, noon, evening, night.
 * Front lenses and road-light handles share one activation rule, including
 * source-only collection when the native body draw is outside the view. */
static inline int mm3_car_lighting_dark_period(uint32_t time_of_day)
{
    return time_of_day == 2u || time_of_day == 3u;
}

static inline uint32_t mm3_car_lighting_effect_mask(uint32_t mask, uint32_t time_of_day)
{
    return mm3_car_lighting_dark_period(time_of_day) ? mask : mask & ~0x101u;
}

/* Source-only collection never submits lenses, geometry or native effects. */
void mm3_car_lighting_emit(uint32_t node, uint32_t mask, const float source[12]);
void mm3_car_lighting_model(uint32_t graph, uint32_t selector);

#endif
