#ifndef MM3_RACE_UNLOCKS_H
#define MM3_RACE_UNLOCKS_H

#include <stdint.h>

/* Unlock all races and jobs (src/mm3_race_unlocks.c). */
typedef struct MM3RaceUnlockMenu {
    int on;
    uint32_t job, mission, city;
} MM3RaceUnlockMenu;

int mm3_race_unlock_enabled(void);
/* Around the original sub_001238B7 (ESI = the menu): begin before it, end right after it (EAX = the city). */
void mm3_race_unlock_menu_begin(MM3RaceUnlockMenu *saved, uint32_t menu);
void mm3_race_unlock_menu_end(const MM3RaceUnlockMenu *saved, uint32_t menu);

#endif
