#ifndef MM3_ZONE_DIAGNOSTICS_H
#define MM3_ZONE_DIAGNOSTICS_H
#include <stdint.h>
/* Read-only: a player's enabled mission zones just before its native zone
 * check (sub_0020F1AB at 0x0020F256). points = the car's seven current
 * reference points (three floats each). */
void mm3_zone_status(uint32_t player, uint32_t points);
#endif
