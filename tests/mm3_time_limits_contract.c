/* Launcher time limits (src/mm3_time_limits.c): the scripts' race_timer gets
 * its seconds scaled when it is created and for each time bonus; other timers,
 * penalties and other callers of the time-left query are left alone. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
static unsigned char memory[0x100000];
static float created, asked;
static const uint32_t name = 0x20000, stack = 0x1FF00, frame = 0x1FF28;

void sub_001F5F24_original(void) { created = MEMF(g_esp + 4); }
void sub_001F5A5A_original(void) { asked = MEMF(g_ebp - 4); }
void sub_001F5F24(void);
void sub_001F5A5A(void);

/* An MSVC std::string: short names inline at +4, long ones behind a pointer. */
static void string_at(uint32_t at, const char *text)
{
    uint32_t length = (uint32_t)strlen(text);
    memset(memory + at, 0, 0x1C);
    MEM32(at + 0x14) = length;
    MEM32(at + 0x18) = length < 16 ? 15 : length;
    if (length < 16) memcpy(memory + at + 4, text, length + 1);
    else { MEM32(at + 4) = at + 0x40; memcpy(memory + at + 0x40, text, length + 1); }
}

/* timerCreate(text, seconds): what the timer list was given. */
static float create(const char *text, float seconds)
{
    string_at(name, text);
    g_eax = name; g_esp = stack; MEMF(stack + 4) = seconds;
    sub_001F5F24();
    return created;
}

/* timerChangeDelta(text, seconds) asking for the time left: the seconds the
 * handler will add; `back` is where the query returns to. */
static float change(const char *text, float seconds, uint32_t back)
{
    string_at(frame - 0x20, text);
    g_esp = stack; MEM32(stack) = back; g_ebp = frame; g_eax = frame - 0x20;
    MEMF(frame - 4) = seconds;
    sub_001F5A5A();
    return asked;
}

int main(int argc, char **argv)
{
    int percent = argc > 1 ? atoi(argv[1]) : 100;
    float scale = percent >= 10 && percent <= 1000 ? (float)percent / 100.0f : 1.0f; /* else as on the Xbox */
    g_xbox_mem_offset = (ptrdiff_t)memory;
    if (argc > 1) _putenv_s("MM3_TIME_PERCENT", argv[1]);
    assert(create("race_timer", 150.0f) == 150.0f * scale);           /* a job's time limit */
    assert(create("reset_passenger_vo", 3.0f) == 3.0f);              /* other timers: as they are */
    assert(create("survive_timer_long_name", 60.0f) == 60.0f);
    assert(create("race_timer", 0.0f) == 0.0f);
    assert(change("race_timer", 20.0f, 0x00201515) == 20.0f * scale); /* a delivery's bonus */
    assert(change("race_timer", -1.0f, 0x00201515) == -1.0f);         /* a penalty */
    assert(change("time_mistake", 5.0f, 0x00201515) == 5.0f);
    assert(change("race_timer", 20.0f, 0x00123456) == 20.0f);         /* another caller */
    printf("PASS time limits %s: race_timer and its bonuses x%.2f, penalties, other timers and callers unchanged\n",
           argc > 1 ? argv[1] : "unset", scale);
    return 0;
}
