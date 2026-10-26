/* Car damage sees 30 fps frames (src/mm3_damage_diagnostics.c): faster than
 * 30 fps, each car's contact impulse is summed over 1/30 s and handed to the
 * damage step once per 1/30 s, in every job and race; the record keeps its
 * real value. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static unsigned char memory[0x400000];
static float seen_scale, seen_impulse[64];
static unsigned damage_calls;
static const uint32_t frame = 0x30000, component = 0x21000, body = 0x22000, record = 0x20000,
                      other = 0x23000, other_body = 0x24000, other_record = 0x25000, stack = 0xFF00;

void sub_00021DCF_original(void) { seen_scale = MEMF(g_esp + 4); g_esp += 8; }
void sub_001CDB6C_original(void)
{
    uint32_t rec = MEM32(MEM32(g_edx + 0x3C) + 4);
    seen_impulse[damage_calls++ % 64] = MEMF(rec + 0x1A4);
}
void sub_00021DCF(void);
void sub_001CDB6C(void);

static float damage_step(uint32_t comp, float impulse)
{
    uint32_t rec = MEM32(MEM32(comp + 0x3C) + 4);
    unsigned before = damage_calls;
    MEMF(rec + 0x1A4) = impulse;
    g_edx = comp;
    sub_001CDB6C();
    assert(damage_calls == before + 1);
    assert(MEMF(rec + 0x1A4) == impulse);           /* the record keeps its real value */
    return seen_impulse[before % 64];
}

static int near(float a, float b) { return fabsf(a - b) <= 1e-3f * fabsf(b) + 1e-4f; }

int main(int argc, char **argv)
{
    int off = argc > 1 && !strcmp(argv[1], "off");
    float total = 0;
    int i;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    if (off) _putenv_s("MM3_DAMAGE_FRAME_FIX", "0");
    MEM32(component + 0x3C) = body; MEM32(body + 4) = record;
    MEM32(other + 0x3C) = other_body; MEM32(other_body + 4) = other_record;
    MEM32(0x3C5C90) = frame;
    MEMF(0x3C0328) = 50.0f; MEMF(0x3C0330) = 1.0f;          /* a wreck job */
    MEMF(frame + 0x54) = 1.0f / 60.0f;
    /* The contact scaler passes the job's scale through unchanged. */
    g_esp = stack; g_ecx = record; MEM32(g_esp) = 0x1F14E5; MEMF(g_esp + 4) = 50.0f;
    sub_00021DCF(); assert(seen_scale == 50.0f && g_esp == stack + 8);
    if (off) {
        assert(damage_step(component, 900.0f) == 900.0f && damage_step(component, 700.0f) == 700.0f);
        puts("PASS damage frame fix off: MM3_DAMAGE_FRAME_FIX=0 keeps every frame's impulse");
        return 0;
    }
    /* 60 fps: two frames make one console frame. */
    assert(damage_step(component, 900.0f) == 0.0f);
    assert(near(damage_step(component, 700.0f), 1600.0f));
    assert(damage_step(component, 0.0f) == 0.0f);
    assert(damage_step(component, 0.0f) == 0.0f);
    /* Cars are gathered separately. */
    assert(damage_step(other, 300.0f) == 0.0f);
    assert(damage_step(component, 500.0f) == 0.0f);
    assert(near(damage_step(other, 100.0f), 400.0f));
    assert(near(damage_step(component, 500.0f), 1000.0f));
    /* 45 fps: two console frames every three frames, the impulse conserved. */
    MEMF(frame + 0x54) = 1.0f / 45.0f;
    for (i = 0; i < 9; ++i) total += damage_step(component, 300.0f);
    assert(near(total, 2700.0f - 0.0f) || total < 2700.0f);   /* what is handed over never exceeds what came in */
    assert(total > 2000.0f);
    /* 30 fps and slower keep every frame as the game has it. */
    MEMF(frame + 0x54) = 1.0f / 30.0f;
    assert(damage_step(component, 800.0f) >= 800.0f);         /* plus any 45 fps leftover */
    assert(damage_step(component, 800.0f) == 800.0f);
    MEMF(frame + 0x54) = 1.0f / 20.0f;
    assert(damage_step(component, 800.0f) == 800.0f);
    /* A slow frame hands over what a faster stretch had gathered. */
    MEMF(frame + 0x54) = 1.0f / 60.0f;
    assert(damage_step(component, 900.0f) == 0.0f);
    MEMF(frame + 0x54) = 1.0f / 30.0f;
    assert(near(damage_step(component, 100.0f), 1000.0f));
    assert(damage_step(component, 100.0f) == 100.0f);
    /* Ordinary races and jobs without a damage scale (the ambulance jobs) too. */
    MEMF(frame + 0x54) = 1.0f / 60.0f;
    MEMF(0x3C0328) = 1.0f; MEMF(0x3C0330) = 1.0f;
    assert(damage_step(component, 800.0f) == 0.0f);
    assert(near(damage_step(component, 800.0f), 1600.0f));
    /* A car without a hit opens no window. */
    for (i = 0; i < 300; ++i) {
        uint32_t comp = 0x100000 + (uint32_t)i * 0x100, rec = 0x200000 + (uint32_t)i * 0x400;
        MEM32(comp + 0x3C) = comp + 0x40; MEM32(comp + 0x40 + 4) = rec;
        assert(damage_step(comp, 0.0f) == 0.0f);
    }
    /* 200 cars in collisions at once each gather their own. */
    for (i = 0; i < 200; ++i)
        assert(damage_step(0x100000 + (uint32_t)i * 0x100, 400.0f + (float)i) == 0.0f);
    for (i = 0; i < 200; ++i)
        assert(near(damage_step(0x100000 + (uint32_t)i * 0x100, 100.0f), 500.0f + (float)i));
    /* All handed over: every window closed, so 300 cars fit again. */
    for (i = 0; i < 300; ++i)
        assert(damage_step(0x100000 + (uint32_t)i * 0x100, 0.0f) == 0.0f);
    for (i = 0; i < 250; ++i)
        assert(damage_step(0x100000 + (uint32_t)i * 0x100, 50.0f) == 0.0f);
    for (i = 0; i < 250; ++i)
        assert(near(damage_step(0x100000 + (uint32_t)i * 0x100, 50.0f), 100.0f));
    MEM32(0x3C5C90) = 0; MEMF(0x3C0328) = 50.0f;               /* no frame object yet */
    assert(damage_step(component, 800.0f) == 800.0f);
    puts("PASS damage frame fix: faster than 30 fps each car's impulse reaches the damage step once per 1/30 s "
         "(60 and 45 fps) in wreck jobs, ordinary races and ambulance jobs alike, cars apart (250 at once), "
         "record restored; 30 fps and slower unchanged, a slow frame flushes what was gathered");
    return 0;
}
