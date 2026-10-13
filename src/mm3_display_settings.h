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

#endif
