#ifndef MM3_POLICE_LOG_H
#define MM3_POLICE_LOG_H
#include <stdint.h>

/* [MM3_POLICE] lines (src/mm3_police_log.c): the police pool, each cop sent,
 * its distance while out and each cop called off. Called from the primary
 * player's update with its rigid body; samples once a second, read-only. */
void mm3_police_log_tick(uint32_t player_body);

#endif
