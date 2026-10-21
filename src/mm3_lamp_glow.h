#ifndef MM3_LAMP_GLOW_H
#define MM3_LAMP_GLOW_H

/* Move a lamp glow's world position toward the eye, keeping its screen
 * position, so it clears its own lens or housing. Returns 0 when unchanged. */
int mm3_glow_lift(const float eye[3], float position[3]);

#endif
