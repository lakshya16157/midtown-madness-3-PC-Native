#ifndef MM3_STRESS_DEBUG_H
#define MM3_STRESS_DEBUG_H
#include <stddef.h>
#include <stdint.h>
#define MM3_STRESS_REPLY_CAPACITY 8192
/* Snapshot police.actors contains copied native rigid-body positions. Nearby
 * requires a nonzero execution mode, <=150m horizontal and <=50m vertical.
 * Active pools cannot shrink; same-total requests relocate their existing IDs. */
void mm3_stress_tick(uint32_t player, uint32_t body);
/* Actual empty factory guard, diagnostics only after the opt-in sampler starts.
 * Integer-only saturating counter: no guest state or host FP/errors changed. */
void mm3_stress_audio_pool_empty(void);
/* stress native_apu [0|1] queries or switches the native register access path.
 * The reply reports the effective backend mode; switching does not reset audio. */
int mm3_stress_command(const char *command, char *reply, size_t size);
#endif
