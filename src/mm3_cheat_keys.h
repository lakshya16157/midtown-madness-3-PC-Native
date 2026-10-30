#ifndef MM3_CHEAT_KEYS_H
#define MM3_CHEAT_KEYS_H

/* Keyboard cheats typed into the game window (src/mm3_cheat_keys.c),
 * switched in the launcher's Gameplay mods tab. */

void mm3_cheat_keys_start(void);
/* The race opponents' difficulty, 0..9 (5 = the Xbox): the last
 * howhardcanitbeN typed, else MM3_AI_LEVEL, else 5. */
int mm3_cheat_ai_level(void);
/* Presses of the free camera key (MM3_FREE_CAMERA_KEY, F8 by default) since
 * the last call; none with MM3_FREE_CAMERA=0. */
int mm3_cheat_camera_presses(void);
/* src/mm3_free_camera.c: gives the input back when races stop drawing. */
void mm3_free_camera_watchdog(void);

#endif
