#ifndef MM3_DISPLAY_CONFIG_H
#define MM3_DISPLAY_CONFIG_H

#include <stddef.h>

typedef struct MM3DisplaySettings {
    int window_width;
    int window_height;
    int fullscreen;
    int fps_limit;
    int vsync;
    int show_fps;
    int minimap_improved; /* 0 = retail minimap; 1 = transparent perspective roads. */
    float render_scale; /* 0 = automatic window-based size; otherwise 0.5..8. */
    char aspect[32]; /* auto, a familiar ratio, or a normalized numeric ratio. */
    int msaa;
    int aniso;
    float draw_distance; /* Object detail/LOD distance; retains the legacy key. */
    float view_distance; /* City/building visibility; legacy configurations inherit draw_distance. */
} MM3DisplaySettings;

void mm3_display_settings_defaults(MM3DisplaySettings *settings, const char *profile);

/* Paths are UTF-8. MM3_DISPLAY_CONFIG overrides the per-user configuration.
 * Resolving the path does not create a directory or file. */
int mm3_display_settings_path(char *path, size_t capacity,
                              char *error, size_t error_capacity);

/* Missing files succeed. Invalid/unknown individual fields are ignored, and
 * fields absent from the file retain their incoming values. A legacy file
 * without view_distance inherits its draw_distance for compatibility. */
int mm3_display_settings_load(MM3DisplaySettings *settings,
                              char *error, size_t error_capacity);

/* Validate the complete snapshot before creating an exclusive temporary file,
 * then atomically replace the previous configuration after flushing it. */
int mm3_display_settings_save(const MM3DisplaySettings *settings,
                              char *error, size_t error_capacity);

/* For startup only: environment access is not synchronized with runtime
 * renderer threads. Returns the failed environment key, or NULL. */
const char *mm3_display_settings_apply_environment(const MM3DisplaySettings *settings,
                                                   int overwrite);

/* Profile defaults < saved fields < valid explicit environment settings.
 * Output is initialized even on an I/O failure. Invalid environment settings
 * are normalized to the saved/default value and reported in error as a warning;
 * they do not prevent startup. Call before renderer/guest initialization. */
int mm3_display_settings_startup(MM3DisplaySettings *settings,
                                 char *error, size_t error_capacity);

/* Read the live HUD preference safely from either the title or render thread. */
int mm3_minimap_improved(void);

#endif
