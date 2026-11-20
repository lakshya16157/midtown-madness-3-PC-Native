/* Unlock all races and jobs: with MM3_UNLOCK_ALL_RACES=1 the profile's race counts become the
 * installed counts, the job menu is opened around its original, and the profile callback gets
 * the real counts. With it off nothing changes. The race list is mocked.
 * Run as `contract off` or `contract on` (the setting is read once per process). */
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "../tools/xboxrecomp/src/kernel/guest_state.c"
#include "../src/mm3_race_unlocks.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp, g_fs_base;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3;
RECOMP_TLS RecompXmm g_xmm4, g_xmm5, g_xmm6, g_xmm7;
RECOMP_TLS RecompMmx g_mm0, g_mm1, g_mm2, g_mm3;
RECOMP_TLS RecompMmx g_mm4, g_mm5, g_mm6, g_mm7;

static unsigned char memory[0x400000];
int xbox_GuestMapped(uint32_t va, uint32_t bytes)
{
    return va >= 0x10000u && (uint64_t)va + bytes <= sizeof memory;
}

enum { MENU = 0x200000, PROFILE = 0x210000, STACK_TOP = 0x300000 };
static unsigned original_calls;
static uint32_t seen[2];

/* The race list: sub_001E12E3 (RET 4) makes a handle for the mode, sub_001E0F69 (city in EAX, RET 8)
 * counts its races: 7 Undercover jobs (mode 2), 25 Checkpoint races (mode 3). */
void sub_001E12E3(void) { g_eax = 0x1000u + MEM32(g_esp + 4); g_esp += 8; }
void sub_001E0F69(void)
{
    uint32_t mode = MEM32(g_esp + 4) - 0x1000u;
    assert(g_eax <= 1u && (mode == 2u || mode == 3u));
    g_eax = mode == 2u ? 7u : 25u;
    g_esp += 12;
}
void sub_001E5E9D_original(void) { ++original_calls; g_eax = 2; g_esp += 12; }   /* the profile's own count (RET 8) */
void sub_0012530C_original(void) { ++original_calls; }
void sub_001218F7_original(void)
{
    ++original_calls;
    seen[0] = MEM32(MENU + 0x1B0);
    seen[1] = MEM32(MENU + 0x1B4);
}

static uint32_t profile_count(uint32_t city, uint32_t type)
{
    g_esp = STACK_TOP;
    PUSH32(g_esp, type); PUSH32(g_esp, city); PUSH32(g_esp, 0xDEAD);
    sub_001E5E9D();
    return g_eax;
}

int main(int argc, char **argv)
{
    int on = argc > 1 && !strcmp(argv[1], "on");
    MM3RaceUnlockMenu saved;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    _putenv(on ? "MM3_UNLOCK_ALL_RACES=1" : "MM3_UNLOCK_ALL_RACES=0");

    /* The profile's counts (city, type 2 jobs / 3 races): installed counts only for the two modes, cities 0-1. */
    assert(profile_count(1, 3) == (on ? 25u : 2u));
    assert(profile_count(0, 2) == (on ? 7u : 2u));
    assert(profile_count(1, 1) == 2u && profile_count(5, 3) == 2u);   /* other modes and cities: the original's */
    assert(g_esp == STACK_TOP);                                        /* the original RET 8 popped what was pushed */

    /* The job menu object (ESI of its per-frame state) and the profile callback. */
    memset(memory + MENU, 0, 0x200);
    g_eax = MENU; sub_0012530C();
    assert(MEM32(MENU + 0x1B0) == (on ? 8u : 0u) && MEM32(MENU + 0x1B4) == (on ? 8u : 0u));

    MEM32(0x003C5CB4u) = PROFILE;                       /* the profile */
    MEM32(0x003B8580u) = 1;                             /* the shown city */
    MEM32(PROFILE + 0x140 + 4) = 3; MEM32(PROFILE + 0x148 + 4) = 4;   /* real unlocked jobs / missions */
    memset(memory + MENU, 0, 0x200);
    g_edi = MENU; sub_001218F7();
    assert(seen[0] == (on ? 3u : 0u) && seen[1] == (on ? 4u : 0u));    /* the callback saw the real counts */
    assert(MEM32(MENU + 0x1B0) == (on ? 8u : 0u));                      /* and the menu shows everything open */

    memset(memory + MENU, 0, 0x200);
    MEM32(MENU + 0x154) = 2; MEM32(MENU + 0x158) = 1; MEM32(0x003B8578u) = 1;
    mm3_race_unlock_menu_begin(&saved, MENU);
    assert(saved.on == on);
    g_eax = 1;                                          /* the city the original returns */
    mm3_race_unlock_menu_end(&saved, MENU);
    assert(MEM32(MENU + 0x148) == (on ? 7u : 0u) && MEM32(MENU + 0x150) == (on ? 25u : 0u));
    assert(MEM32(MENU + 0x15C) == (on ? 7u : 0u) && MEM32(MENU + 0x160) == (on ? 4u : 0u));
    assert(MEM32(MENU + 0x154) == (on ? 2u : 2u) && MEM32(MENU + 0x158) == 1u);   /* the selection is kept */
    if (on) {
        MEM32(MENU + 0x154) = 6; MEM32(MENU + 0x158) = 3;   /* the last job has three missions */
        mm3_race_unlock_menu_begin(&saved, MENU);
        g_eax = 1; mm3_race_unlock_menu_end(&saved, MENU);
        assert(MEM32(MENU + 0x154) == 6u && MEM32(MENU + 0x158) == 0u && MEM32(MENU + 0x160) == 3u);
        MEM32(MENU + 0x154) = 4; MEM32(MENU + 0x158) = 2; MEM32(0x003B8578u) = 0;   /* another city was shown */
        mm3_race_unlock_menu_begin(&saved, MENU);
        g_eax = 1; mm3_race_unlock_menu_end(&saved, MENU);
        assert(MEM32(MENU + 0x154) == 0u && MEM32(MENU + 0x158) == 0u);
        MEM32(MENU + 0x148) = 0;
        mm3_race_unlock_menu_begin(&saved, MENU);
        g_eax = 7; mm3_race_unlock_menu_end(&saved, MENU);                          /* not a city: untouched */
        assert(MEM32(MENU + 0x148) == 0u);
    }
    printf("PASS: unlock all races %s: profile counts, open menu, real counts for the profile callback, selection kept or reset\n",
           on ? "on" : "off");
    return 0;
}
