#include <stdint.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"

/* HUD markers this port hides unless the launcher's Mods tab shows them:
 * the minimap's compass pins (the MapNorth letter, collapsed in mm3_map_zoom.c,
 * and the E/S/W pins of the MapCircle ring, cleared in mm3_map_pins.c) and the
 * top-screen checkpoint arrow (skipped in sub_001EC352 by
 * scripts/mm3_hud_cleanup_seam.cmake). */
int mm3_hud_north_marker(void) { return mm3_mod_north_marker(); }
int mm3_hud_race_arrow(void) { return mm3_mod_race_arrow(); }
