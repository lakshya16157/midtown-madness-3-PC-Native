#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_cheat_keys.h"
#include "mm3_photo.h"

/* F2 holds the race still without a menu, so a picture can be taken of it.
 *
 * The game keeps its frame time from one timer object (sub_00170316 reads the
 * counter through sub_0016FC89, a bare rdtsc scaled to 733 MHz, and turns the
 * difference into seconds; sub_001E6C87 averages those into the frame time
 * every system steps with). Holding that counter holds the cars, traffic,
 * police, particles, camera and race clock together, and the draw carries on,
 * so the free camera (src/mm3_free_camera.c) flies around a still scene. The
 * game's own pause stays what it is: the Start button, with its menu.
 *
 * The counter is not stopped but slowed to a hundred-thousandth: a frame
 * time of exactly zero would divide by zero in anything that divides by it.
 * A car's physics runs whole steps of 1/120 s from the time it has gathered
 * (sub_0021A75E: none, and nothing divided, below one step), so at this rate
 * a car takes its next step after about fourteen minutes. The averaged frame
 * time takes a fraction of a second to run down and up again, so the world
 * slows into the hold and out of it. The game never sees the time spent
 * held: the clock continues from where it stopped.
 *
 * Only the game thread reads the counter this way, and only it toggles; the
 * key thread counts F2 presses (src/mm3_cheat_keys.c). Held away from a race
 * view, while the game's own pause is open (its menu needs a running clock),
 * or with System Link (the other players' clocks would run on) it lets go. */
extern void sub_0016FC89_original(void);

#define RATE 100000ull          /* the held clock runs at 1/RATE */
#define PAUSE_ACTIVE 0x00429DB2u /* the game's own pause singleton +2 (open 001E2CD5, resume 001E2BDA) */

static uint64_t s_cut;           /* real ticks the game's clock has dropped */
static uint64_t s_hold_real, s_hold_game;
static volatile LONG s_frozen;

int mm3_pause_frozen(void) { return InterlockedCompareExchange(&s_frozen, 0, 0) != 0; }

static int allowed(void)
{
    static int network = -1;
    if (network < 0) {
        const char *v = getenv("RECOMP_NET");
        network = v && *v && strcmp(v, "0") != 0;
        if (network) {
            fprintf(stderr, "[MM3_PHOTO] System Link is on: the pause key is off (the other players' clocks would go on)\n");
            fflush(stderr);
        }
    }
    return !network;
}

static uint64_t game_now(uint64_t real)
{
    if (!s_frozen) return real >= s_cut ? real - s_cut : 0;
    return s_hold_game + (real - s_hold_real) / RATE;
}

static void let_go(uint64_t real, const char *why)
{
    if (!s_frozen) return;
    s_cut = real - game_now(real);
    InterlockedExchange(&s_frozen, 0);
    fprintf(stderr, "[MM3_PHOTO] race running again (%s)\n", why);
    fflush(stderr);
}

uint64_t mm3_pause_clock(uint64_t real)
{
    int presses = mm3_cheat_pause_presses();
    int race = mm3_race_view_recent(), paused = MEM8(PAUSE_ACTIVE) != 0;
    if (!race) presses = 0;               /* a press made in a menu is not for the next race */
    if (s_frozen && (!race || paused)) let_go(real, !race ? "no race view" : "the game's pause opened");
    if ((presses & 1) && allowed()) {
        if (s_frozen) {
            let_go(real, "F2");
        } else if (!paused) {
            s_hold_real = real;
            s_hold_game = game_now(real);
            InterlockedExchange(&s_frozen, 1);
            fprintf(stderr, "[MM3_PHOTO] race held\n");
            fflush(stderr);
        }
    }
    return game_now(real);
}

/* The engine timer's counter, as the game reads it. */
void sub_0016FC89(void)
{
    uint64_t real, game;
    sub_0016FC89_original();
    real = ((uint64_t)g_edx << 32) | g_eax;
    game = mm3_pause_clock(real);
    g_eax = (uint32_t)game;
    g_edx = (uint32_t)(game >> 32);
}
