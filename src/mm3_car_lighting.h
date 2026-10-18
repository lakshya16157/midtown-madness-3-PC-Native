#ifndef MM3_CAR_LIGHTING_H
#define MM3_CAR_LIGHTING_H

#include <stdint.h>

/* Source-only collection never submits lenses, geometry or native effects. */
void mm3_car_lighting_emit(uint32_t node, uint32_t mask, const float source[12]);
void mm3_car_lighting_model(uint32_t graph, uint32_t selector);

#endif
