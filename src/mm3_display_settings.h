#ifndef MM3_DISPLAY_SETTINGS_H
#define MM3_DISPLAY_SETTINGS_H

#include "mm3_display_config.h"

/* Load saved preferences and register the display's F10 settings panel before
 * either the framebuffer window or renderer starts. */
void mm3_display_settings_init(void);

/* Shared by the native menu and the F10 panel. Commit saves before publishing
 * live renderer/window changes; status also describes settings needing restart. */
void mm3_display_settings_get(MM3DisplaySettings *settings);
int mm3_display_settings_commit(const MM3DisplaySettings *settings,
                               char *status, size_t capacity);

/* Settings the launcher fixes for this run, as it greys them out there: the
 * aspect with the widescreen mod off, both distances with the Original look
 * or the distance mod off, the minimap with the Original look or Xbox
 * gameplay, the night brightness with Improved world lighting off. NULL when
 * the setting is free, else why it is fixed. A commit keeps their live
 * values and saves the player's own stored choice. */
typedef enum { MM3_FIXED_ASPECT, MM3_FIXED_DISTANCE, MM3_FIXED_MINIMAP, MM3_FIXED_NIGHT_LIGHT } MM3FixedSetting;
const char *mm3_display_setting_fixed(MM3FixedSetting setting);
/* Copy the fixed settings from `from` into `to`. */
void mm3_display_settings_keep_fixed(MM3DisplaySettings *to, const MM3DisplaySettings *from);

#endif
