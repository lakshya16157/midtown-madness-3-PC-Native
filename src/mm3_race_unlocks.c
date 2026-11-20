/* MM3_UNLOCK_ALL_RACES (launcher: Unlock all races and jobs): every installed Checkpoint race
 * and Undercover job is open in the menus, as the title's own unlockAllRaces does. The save is
 * not changed and no race is marked won: the menu's counts are raised on screen only, and the
 * profile callback (sub_001218F7) is fed the real counts so the cheat's values never reach the
 * save. Based on MattSG's unlock hooks (src/mods/unlocks.c in mm3-recomp), reworked for this
 * port: the job menu's per-frame hook (sub_001238B7) is owned by mm3_circuit_races.c, which calls
 * mm3_race_unlock_menu_begin/_end around the original, so both can change the same menu.
 *
 * Menu object (sub_001238B7's ESI): +0x148 / +0x150 race counts for modes 2 and 3
 * (Undercover jobs, Checkpoint races) of the shown city, +0x154 / +0x158 selected job and
 * mission, +0x15C / +0x160 job and mission limits, +0x1B0 / +0x1B4 unlocked job and mission
 * counts (8 = the all-jobs sentinel unlockAllRaces uses). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "xbox_memory_layout.h"
#include "guest_state.h"
#include "mm3_race_unlocks.h"

extern void sub_001E5E9D_original(void);
extern void sub_0012530C_original(void);
extern void sub_001218F7_original(void);
extern void sub_001E12E3(void);
extern void sub_001E0F69(void);

#define FRONTEND_CITY_SHOWN_A 0x003B8580u
#define FRONTEND_CITY_SHOWN_B 0x003B8578u
#define PROFILE_SINGLETON 0x003C5CB4u
#define MENU_BYTES 0x1B8u
#define MENU_CITY_RACE 0x110u   /* + 4 * city: the race the Checkpoint list remembers for each city */
#define MENU_RACE 0x14Cu        /* the Checkpoint race shown (the cursor) */
#define MENU_RACES 0x150u       /* the Checkpoint races listed */

static int race_unlock_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *setting = getenv("MM3_UNLOCK_ALL_RACES");
        on = setting && !strcmp(setting, "1");
        if (on) {
            fprintf(stderr, "[MM3_RACES] all installed races and jobs open (save unchanged)\n");
            fflush(stderr);
        }
    }
    return on;
}

int mm3_race_unlock_enabled(void)
{
    return race_unlock_on();
}

/* The installed race count for (city, type), including installed DLC, from the race list. */
static uint32_t installed_count(uint32_t city, uint32_t type)
{
    uint32_t stack = g_esp;
    if (city <= 1 && (type == 2 || type == 3) && stack >= 0x10200u && xbox_GuestMapped(stack - 0x110u, 0x110u)) {
        xbox_guest_state state;
        uint32_t count;
        xbox_guest_state_save(&state);
        g_esp = stack - 0x100u;
        PUSH32(g_esp, type); PUSH32(g_esp, 0); sub_001E12E3();
        PUSH32(g_esp, g_eax); g_eax = city; PUSH32(g_esp, 0); sub_001E0F69();
        count = g_eax;
        xbox_guest_state_restore(&state, state.eax);
        return count;
    }
    return 0;
}

/* The profile's unlocked-race count for a city and type (sub_001E5E9D): the installed count. */
void sub_001E5E9D(void)
{
    uint32_t city = MEM32(g_esp + 4), type = MEM32(g_esp + 8);
    sub_001E5E9D_original();
    if (race_unlock_on() && city <= 1 && (type == 2 || type == 3))
        g_eax = installed_count(city, type);
}

static void open_jobs(uint32_t menu)
{
    if (xbox_GuestMapped(menu, MENU_BYTES)) {
        MEM32(menu + 0x1B0) = 8;
        MEM32(menu + 0x1B4) = 8;
    }
}

void sub_0012530C(void)
{
    uint32_t menu = g_eax;
    sub_0012530C_original();
    if (race_unlock_on()) open_jobs(menu);
}

void sub_001218F7(void)
{
    uint32_t menu = g_edi, profile = MEM32(PROFILE_SINGLETON), city = MEM32(FRONTEND_CITY_SHOWN_A);
    int unlock = race_unlock_on() && profile && xbox_GuestMapped(profile, 0x150) && xbox_GuestMapped(menu, MENU_BYTES);
    /* This menu callback copies its displayed counts back to the profile: give it the real ones. */
    if (unlock && city <= 1) {
        MEM32(menu + 0x1B0) = MEM32(profile + 0x140 + city * 4);
        MEM32(menu + 0x1B4) = MEM32(profile + 0x148 + city * 4);
    }
    sub_001218F7_original();
    if (unlock) open_jobs(menu);
}

/* The job menu's per-frame state (sub_001238B7, ESI = menu): called by the owner of that hook
 * right before and right after the original. begin remembers the selection; end (EAX = the
 * shown city, straight from the original) opens every installed job and race. */
void mm3_race_unlock_menu_begin(MM3RaceUnlockMenu *saved, uint32_t menu)
{
    memset(saved, 0, sizeof *saved);
    saved->on = race_unlock_on() && xbox_GuestMapped(menu, MENU_BYTES);
    if (!saved->on) return;
    saved->job = MEM32(menu + 0x154);
    saved->mission = MEM32(menu + 0x158);
    saved->city = MEM32(FRONTEND_CITY_SHOWN_B);
    saved->cursor = MEM32(menu + MENU_RACE);
}

void mm3_race_unlock_menu_end(const MM3RaceUnlockMenu *saved, uint32_t menu)
{
    uint32_t city = g_eax, job = saved->job, mission = saved->mission;
    if (!saved->on || city > 1) return;
    uint32_t races = installed_count(city, 3);
    MEM32(menu + 0x148) = installed_count(city, 2);
    MEM32(menu + MENU_RACES) = races;
    /* Each frame the original copies the player's pick (+0x14C, set by the menu when it moves) into the
     * city's remembered race and clamps that to the races the profile has unlocked, before the count above
     * is raised: pressing right moved the pick past that count and the next frame took it back. Where the
     * player put it (same city as the last frame; a new city's pick is the original's) stays, below the
     * installed count. */
    if (saved->city == city) {
        uint32_t race = saved->cursor;
        if (races && race >= races) race = races - 1u;
        MEM32(menu + MENU_CITY_RACE + 4u * city) = race;
        MEM32(menu + MENU_RACE) = race;
    }
    if (saved->city != city || job > 6) job = mission = 0;
    if (mission >= (job == 6 ? 3u : 4u)) mission = 0;
    MEM32(menu + 0x154) = job;
    MEM32(menu + 0x158) = mission;
    MEM32(menu + 0x15C) = 7;
    MEM32(menu + 0x160) = job == 6 ? 3 : 4;
}
