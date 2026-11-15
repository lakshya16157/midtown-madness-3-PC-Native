#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_cheat_keys.h"

/* "Stop the time limit" (F10 settings window, this session only).
 *
 * Every time limit the scripts set against the player is the timer named
 * race_timer (Blitz races, timed Career jobs). timerCreate("race_timer", ...)
 * reaches the timer list's create, sub_001F5F24, with the name std::string in
 * EAX; it sets the seconds with sub_001F5DBF (ESI = the timer), for a new
 * timer and for one that already exists. That is where the race_timer object
 * is found.
 *
 * Timer object: +4 expired, +5 paused, +0xC seconds at the start, +0x10 clock
 * at the start, +0x14 time left, +0x18 counts up, +0x19 repeats. Each frame
 * sub_001F5E01 (ESI = the timer) sets time left = start seconds - (clock -
 * start clock); at zero it marks the timer expired and returns AL = 0, which
 * fires the script's time-out. While the time limit is stopped the start
 * seconds move by what this frame took, so the time left stays where it was,
 * and an expiry in that frame is taken back. Unticking carries on from the
 * same time left. Other timers (voices, AI checks, survive this long) and
 * the race clock run as before. */
extern void sub_001F5F24_original(void);
extern void sub_001F5DBF_original(void);
extern void sub_001F5E01_original(void);

static int s_creating;      /* 1: a race_timer is being created, 2: another timer */
static uint32_t s_race_timer;
static int s_logged;

static int guest_range(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address < 0x84000000u && size < 0x84000000u - address;
}

/* MSVC std::string: characters inline at +4 below 16 of capacity (+0x18),
 * else a pointer there; length at +0x14. */
static int is_race_timer(uint32_t string)
{
    static const char name[] = "race_timer";
    uint32_t length, text, i;
    if (!guest_range(string, 0x1Cu)) return 0;
    length = MEM32(string + 0x14u);
    if (length != sizeof name - 1u) return 0;
    text = MEM32(string + 0x18u) >= 16u ? MEM32(string + 4u) : string + 4u;
    if (!guest_range(text, length)) return 0;
    for (i = 0; i < length; ++i)
        if (MEM8(text + i) != (uint8_t)name[i]) return 0;
    return 1;
}

void sub_001F5F24(void)
{
    s_creating = is_race_timer(g_eax) ? 1 : 2;
    sub_001F5F24_original();
    s_creating = 0;
}

void sub_001F5DBF(void)
{
    uint32_t timer = g_esi;
    if (s_creating == 1) s_race_timer = timer;
    else if (s_creating == 2 && timer == s_race_timer) s_race_timer = 0;   /* its memory now holds another timer */
    sub_001F5DBF_original();
}

void sub_001F5E01(void)
{
    uint32_t timer = g_esi;
    float before;
    if (!timer || timer != s_race_timer || !mm3_cheat_timer_stopped() || !guest_range(timer, 0x24u) ||
        MEM8(timer + 4u) || MEM8(timer + 5u) || MEM8(timer + 0x18u) || MEM8(timer + 0x19u) ||
        !((before = MEMF(timer + 0x14u)) > 0.0f)) {
        if (!mm3_cheat_timer_stopped()) s_logged = 0;
        sub_001F5E01_original();
        return;
    }
    sub_001F5E01_original();
    MEMF(timer + 0xCu) += before - MEMF(timer + 0x14u);
    MEMF(timer + 0x14u) = before;
    if (MEM8(timer + 4u)) {                     /* ran out this frame: not while stopped */
        MEM8(timer + 4u) = 0;
        g_eax = (g_eax & ~0xFFu) | 1u;
    }
    if (!s_logged) {
        s_logged = 1;
        fprintf(stderr, "[MM3_TIME] race_timer held at %.1f s left\n", before);
        fflush(stderr);
    }
}
