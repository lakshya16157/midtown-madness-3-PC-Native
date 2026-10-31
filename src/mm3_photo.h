#ifndef MM3_PHOTO_H
#define MM3_PHOTO_H

#include <stdint.h>
#include <wchar.h>

/* Photo mode: F2 holds the race still (src/mm3_pause_clock.c) and a picture
 * key saves what the window shows as a PNG (src/mm3_photo.c). Both are
 * switched on the launcher's Gameplay mods tab, MM3_PHOTO_MODE. */

/* Any thread: save the next image the window presents under the pictures
 * folder, MM3_PHOTO_DIR if set. Pictures requested closer than half a
 * second apart are one. */
void mm3_photo_take(void);
/* Writes the BGRA image (rows pitch bytes apart, alpha ignored) as a PNG,
 * returning nonzero on success. Windows Imaging Component; initialises COM
 * for the calling thread when it has not been. */
int mm3_photo_save_png(const wchar_t *path, const void *bgra, uint32_t width, uint32_t height, uint32_t pitch);

/* src/mm3_pause_clock.c: the game's clock as the game sees it, given the
 * real guest counter. While held it runs at a hundred-thousandth of the speed. */
uint64_t mm3_pause_clock(uint64_t real);
/* Nonzero while F2 holds the race. */
int mm3_pause_frozen(void);

/* src/mm3_free_camera.c: a race view was drawn in the last 1.5 seconds. */
int mm3_race_view_recent(void);
/* src/mm3_cheat_keys.c: nonzero while the hide-HUD key (F4) hides the race HUD. */
int mm3_hud_hidden(void);

#endif
