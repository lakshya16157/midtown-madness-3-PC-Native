#ifndef MM3_LAMP_GLOW_H
#define MM3_LAMP_GLOW_H

#include <stdint.h>

/* Move a lamp glow's world position toward the eye, keeping its screen
 * position, so it clears its own lens or housing. Returns 0 when unchanged. */
int mm3_glow_lift(const float eye[3], float position[3]);

/* Glows the native flare manager holds a frame (MM3_GLOW_CAPACITY, default
 * 1024; the Xbox held 256). */
uint32_t mm3_glow_capacity(void);

/* Non-zero while the player's own car draws its lamps, for the glow report. */
void mm3_glow_driver_scope(int driver);

#endif
