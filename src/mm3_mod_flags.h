#ifndef MM3_MOD_FLAGS_H
#define MM3_MOD_FLAGS_H
#include <stdlib.h>
#include <string.h>
/* The launcher publishes settings before guest threads start. Each thread
 * resolves each used feature once; no environment lookup or lock in hot paths.
 * Include after recomp_types.h, which supplies RECOMP_TLS. */
#define MM3_MOD_FLAG(function, environment, fallback) \
    static inline int function(void) { \
        static RECOMP_TLS int cached = -1; \
        if (cached < 0) { \
            const char *v = getenv(environment); \
            cached = v && !strcmp(v, "0") ? 0 : v && !strcmp(v, "1") ? 1 : fallback; \
        } \
        return cached; \
    }
MM3_MOD_FLAG(mm3_mod_car_lighting, "MM3_MOD_CAR_LIGHTING", 1)
MM3_MOD_FLAG(mm3_mod_lamp_lighting, "MM3_MOD_LAMP_LIGHTING", 1)
MM3_MOD_FLAG(mm3_mod_world_lighting, "MM3_MOD_WORLD_LIGHTING", 1)
MM3_MOD_FLAG(mm3_mod_dev_tools, "MM3_MOD_DEV_TOOLS", 1)
MM3_MOD_FLAG(mm3_mod_native_fog, "MM3_NATIVE_FOG", 1)
MM3_MOD_FLAG(mm3_mod_glow_lift, "MM3_GLOW_LIFT", 1)
MM3_MOD_FLAG(mm3_mod_lamp_glows, "MM3_ALL_STREET_LAMPS", 1)
MM3_MOD_FLAG(mm3_mod_north_marker, "MM3_HUD_NORTH_MARKER", 0)
MM3_MOD_FLAG(mm3_mod_race_arrow, "MM3_HUD_RACE_ARROW", 0)
MM3_MOD_FLAG(mm3_mod_route_navigation, "MM3_ROUTE_NAVIGATION", 1)
#undef MM3_MOD_FLAG
#endif
