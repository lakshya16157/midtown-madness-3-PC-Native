/* F10's Stop the time limit (src/mm3_stop_timer.c): the scripts' race_timer
 * keeps its time left and does not run out while it is ticked, carries on
 * from there when unticked, and other timers keep running. The fake timer
 * list below follows the title's sub_001F5F24 / sub_001F5DBF / sub_001F5E01. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
static unsigned char memory[0x100000];
static const uint32_t name = 0x20000, race = 0x30000, other = 0x30100;
static uint32_t creating;     /* the object the fake create hands to the set */
static float seconds_given, clock_now;
static int stopped;

int mm3_cheat_timer_stopped(void) { return stopped; }
void sub_001F5F24(void);
void sub_001F5DBF(void);
void sub_001F5E01(void);

/* Set the seconds: time left and start seconds, start clock = now. */
void sub_001F5DBF_original(void)
{
    uint32_t t = g_esi;
    MEM8(t + 4) = 0;
    MEMF(t + 0x14) = MEMF(t + 0xC) = seconds_given;
    MEMF(t + 0x10) = clock_now;
}

void sub_001F5F24_original(void)
{
    g_esi = creating;
    sub_001F5DBF();
}

/* The countdown: time left = start seconds - (clock - start clock); AL 0 on expiry. */
void sub_001F5E01_original(void)
{
    uint32_t t = g_esi;
    float left;
    if (MEM8(t + 4) || MEM8(t + 5)) { g_eax = 1; return; }
    left = MEMF(t + 0xC) - (clock_now - MEMF(t + 0x10));
    if (left <= 0.0f) { MEM8(t + 4) = 1; MEMF(t + 0x14) = 0.0f; g_eax = 0; return; }
    MEMF(t + 0x14) = left;
    g_eax = 1;
}

static void string_at(uint32_t at, const char *text)
{
    uint32_t length = (uint32_t)strlen(text);
    memset(memory + at, 0, 0x1C);
    MEM32(at + 0x14) = length;
    MEM32(at + 0x18) = length < 16 ? 15 : length;
    if (length < 16) memcpy(memory + at + 4, text, length + 1);
    else { MEM32(at + 4) = at + 0x40; memcpy(memory + at + 0x40, text, length + 1); }
}

static void create(const char *text, uint32_t timer, float seconds)
{
    string_at(name, text);
    g_eax = name; creating = timer; seconds_given = seconds;
    sub_001F5F24();
}

/* One frame of `dt` seconds for one timer: AL as the caller sees it. */
static int update(uint32_t timer, float dt)
{
    clock_now += dt;
    g_esi = timer; g_eax = 0xABCDEF00u;
    sub_001F5E01();
    return (int)(g_eax & 0xFF);
}

static int near(float a, float b) { return a - b < 0.001f && b - a < 0.001f; }

int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)memory;
    create("race_timer", race, 10.0f);
    create("reset_passenger_vo", other, 10.0f);
    assert(update(race, 2.0f) == 1 && near(MEMF(race + 0x14), 8.0f));     /* running */
    stopped = 1;
    for (int i = 0; i < 100; ++i) assert(update(race, 0.5f) == 1);        /* 50 s, well past the limit */
    assert(near(MEMF(race + 0x14), 8.0f) && !MEM8(race + 4));
    assert(update(other, 0.0f) == 0 && MEM8(other + 4));                  /* other timers still run out */
    stopped = 0;
    assert(update(race, 3.0f) == 1 && near(MEMF(race + 0x14), 5.0f));     /* carries on from 8 s */
    stopped = 1;                                                          /* stopped in the frame it runs out */
    assert(update(race, 6.0f) == 1 && near(MEMF(race + 0x14), 5.0f) && !MEM8(race + 4));
    stopped = 0;
    assert(update(race, 6.0f) == 0 && MEM8(race + 4));                    /* and runs out once unticked */
    /* another timer created in the old race_timer's memory is not held */
    create("survive_timer_long_name", race, 4.0f);
    stopped = 1;
    assert(update(race, 5.0f) == 0 && MEM8(race + 4));
    /* a new race_timer elsewhere is */
    create("race_timer", other, 30.0f);
    assert(update(other, 40.0f) == 1 && near(MEMF(other + 0x14), 30.0f));
    printf("PASS stop timer: race_timer held and resumed, expiry taken back, other timers run\n");
    return 0;
}
