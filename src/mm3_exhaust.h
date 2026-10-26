#ifndef MM3_EXHAUST_H
#define MM3_EXHAUST_H

#include <stdint.h>

/* Triangle exhaust (launcher Mods, MM3_EXHAUST_TRIANGLES=1): every car puffs
 * small triangles from its exhaust instead of nothing. See mm3_exhaust.c. */
int mm3_exhaust_enabled(void);
/* A car's physics step, after it integrated (sub_001D7441): the car, its
 * rigid body and the step's seconds. */
void mm3_exhaust_step(uint32_t car, uint32_t body, float dt);
/* A lamp node drawn this frame (sub_000CF2DA): its kind and world transform
 * (rows right, up, forward, position). Tail and brake lamps mark a car's
 * back. */
void mm3_exhaust_lamp(uint32_t kind, const float transform[12]);
/* The fog pass's camera (sub_000F8DCE): fills the packet's eighteen words
 * after its tag (sequence, W-to-world scale, view rows, projection) and
 * publishes this frame's triangles. 0 when there is nothing to draw. */
int mm3_exhaust_packet(uint32_t camera, uint32_t words[18]);

#endif
