#ifndef MM3_SPEEDOMETER_H
#define MM3_SPEEDOMETER_H

#include <stdint.h>

/* Called by the native HUD after its speed getter. The renderer remains in
 * g_esi; the helper restores every guest register before returning. */
int mm3_speedometer_draw(uint32_t node, uint32_t frame, double speed_kmh);

/* Retail cars keep their numeric-only HUD layout too. Only an identified
 * added car uses the existing hidden numeric readout. Guest state is untouched. */
int mm3_speedometer_hide_readout(void);

#endif
