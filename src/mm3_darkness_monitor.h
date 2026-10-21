#ifndef MM3_DARKNESS_MONITOR_H
#define MM3_DARKNESS_MONITOR_H
#include <stdint.h>

/* Once a frame, from the light-queue consumer: the scene manager's own light
 * level, reported when it jumps (see mm3_darkness_monitor.c). */
void mm3_darkness_frame(uint32_t manager);

#endif
