/* Wreck-job hits count as at the console's 30 fps (src/mm3_damage_diagnostics.c):
 * the car-to-car damage scale follows the game's averaged frame time. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static unsigned char memory[0x400000];
static float seen;
static const uint32_t frame = 0x30000, record = 0x20000, stack = 0xFF00;

void sub_00021DCF_original(void) { seen = MEMF(g_esp + 4); g_esp += 8; }
void sub_00021DCF(void);

static float hit(uint32_t back, float scale)
{
    g_esp = stack; g_ecx = record;
    MEM32(g_esp) = back; MEMF(g_esp + 4) = scale;
    sub_00021DCF();
    assert(g_esp == stack + 8);
    return seen;
}

static int near(float a, float b) { return fabsf(a - b) <= 1e-4f * fabsf(b) + 1e-6f; }

int main(int argc, char **argv)
{
    int off = argc > 1 && !strcmp(argv[1], "off");
    g_xbox_mem_offset = (ptrdiff_t)memory;
    if (off) _putenv_s("MM3_DAMAGE_FRAME_FIX", "0");
    MEM32(0x3C5C90) = frame;
    MEMF(0x3C0328) = 50.0f; MEMF(0x3C0330) = 1.0f;          /* a wreck job */
    MEMF(frame + 0x54) = 1.0f / 60.0f;
    if (off) {
        assert(hit(0x1F14E5, 50.0f) == 50.0f && hit(0x1F1508, 1.0f) == 1.0f);
        puts("PASS damage frame fix off: MM3_DAMAGE_FRAME_FIX=0 keeps the game's own scale");
        return 0;
    }
    assert(near(hit(0x1F14E5, 50.0f), 100.0f));               /* 60 fps: hits count double */
    assert(near(hit(0x1F1508, 1.0f), 2.0f));                  /* both cars' records */
    MEMF(frame + 0x54) = 1.0f / 30.0f;
    assert(hit(0x1F14E5, 50.0f) == 50.0f);                    /* the console's rate: unchanged */
    MEMF(frame + 0x54) = 1.0f / 45.0f;
    assert(near(hit(0x1F14E5, 50.0f), 75.0f));
    MEMF(frame + 0x54) = 1.0f / 20.0f;                        /* slower than 30: never less */
    assert(hit(0x1F14E5, 50.0f) == 50.0f);
    MEMF(frame + 0x54) = 0.5f; assert(hit(0x1F14E5, 50.0f) == 50.0f);       /* implausible times */
    MEMF(frame + 0x54) = 1.0f / 1000.0f; assert(hit(0x1F14E5, 50.0f) == 50.0f);
    MEMF(frame + 0x54) = 1.0f / 60.0f;
    assert(hit(0x123456, 50.0f) == 50.0f);                    /* another caller of the scaler */
    MEM32(0x3C5C90) = 0; assert(hit(0x1F14E5, 50.0f) == 50.0f);              /* no frame object yet */
    MEM32(0x3C5C90) = frame;
    MEMF(0x3C0328) = 1.0f; MEMF(0x3C0330) = 1.0f;            /* an ordinary race */
    assert(hit(0x1F14E5, 1.0f) == 1.0f);
    MEMF(0x3C0330) = 2.0f;                                    /* either scale marks a job */
    assert(near(hit(0x1F1508, 2.0f), 4.0f));
    puts("PASS damage frame fix: wreck-job hits scale by (1/30 s)/frame time for both cars, "
         "never below the game's own, ordinary races, other callers and implausible times untouched");
    return 0;
}
