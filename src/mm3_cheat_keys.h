#ifndef MM3_CHEAT_KEYS_H
#define MM3_CHEAT_KEYS_H

/* The free camera and photo mode keys (src/mm3_cheat_keys.c), switched in
 * the launcher's Gameplay mods tab. */

void mm3_cheat_keys_start(void);
/* The race opponents' difficulty, 0..9 (5 = the Xbox): MM3_AI_LEVEL, the
 * launcher's Opponents' difficulty, else 5. */
int mm3_cheat_ai_level(void);
/* Presses of the free camera key (MM3_FREE_CAMERA_KEY, F8 by default) since
 * the last call; none with MM3_FREE_CAMERA=0. */
int mm3_cheat_camera_presses(void);
/* Presses of the pause key (MM3_PAUSE_KEY, F2 by default) since the last call;
 * none with MM3_PHOTO_MODE=0. src/mm3_pause_clock.c turns them into holds. */
int mm3_cheat_pause_presses(void);
/* One more press, from the free camera's controller button. */
void mm3_cheat_pause_press(void);
/* A press of the free camera's hide-HUD button (X): the key thread shows or
 * hides the race HUD as the hide-HUD key (F4) does. */
void mm3_cheat_hud_press(void);
/* src/mm3_free_camera.c: gives the input back when races stop drawing. */
void mm3_free_camera_watchdog(void);

#endif
