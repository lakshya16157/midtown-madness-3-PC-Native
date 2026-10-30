/* Car damage counted as on the Xbox (src/mm3_damage_diagnostics.c): the
 * physics runs in 1/120 s steps and each step's damage step reads the car's
 * largest impulse found since its frame began. Faster than 30 fps game frames
 * are grouped into four-step console frames: what the first game frame finds
 * is applied in the console frame's later steps (three times, as on the
 * Xbox), what a later game frame finds is left for the next console frame;
 * the record keeps its real value. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static unsigned char memory[0x410000];   /* up to the game settings at 0x407C18 */
static float seen_scale, seen_impulse[64], seen_hit_factor[64], seen_factor;
static unsigned damage_calls;
static const uint32_t frame = 0x30000, component = 0x21000, body = 0x22000, record = 0x20000,
                      other = 0x23000, other_body = 0x24000, other_record = 0x25000, stack = 0xFF00,
                      manager = 0x31000, slot = 0x32000, player = 0x33000, vehicle = 0x34000;

void sub_00021DCF_original(void) { seen_scale = MEMF(g_esp + 4); g_esp += 8; }
void sub_001CDB6C_original(void)
{
    uint32_t rec = MEM32(MEM32(g_edx + 0x3C) + 4);
    seen_impulse[damage_calls % 64] = MEMF(rec + 0x1A4);
    seen_hit_factor[damage_calls++ % 64] = MEMF(g_edx + 0x24);  /* playerSetDamageFactor's */
    seen_factor = MEMF(g_edx + 0x8718);              /* the car's damage factor */
}
void sub_00021DCF(void);
void sub_001CDB6C(void);

/* One physics step's damage step for the car with part `comp`: what it saw. */
static float step(uint32_t comp, float impulse)
{
    uint32_t rec = MEM32(MEM32(comp + 0x3C) + 4);
    unsigned before = damage_calls;
    float factor = MEMF(comp + 0x24);
    MEMF(rec + 0x1A4) = impulse;
    g_edx = comp;
    sub_001CDB6C();
    assert(damage_calls == before + 1);
    assert(MEMF(rec + 0x1A4) == impulse);           /* the record keeps its real value */
    assert(MEMF(comp + 0x24) == factor);            /* and the car its factor */
    return seen_impulse[before % 64];
}

/* The playerSetDamageFactor factor the last damage step used. */
static float last_factor(void)
{
    return seen_hit_factor[(damage_calls - 1) % 64];
}

/* The end of a game frame: the frame timer adds the frame's ticks to its
 * total (frame object +0x28). */
static void end_frame(float fps)
{
    MEMF(frame + 0x54) = 1.0f / fps;
    MEM32(frame + 0x28) += (uint32_t)(733333333.0f / fps);
}

int main(int argc, char **argv)
{
    int off = argc > 1 && !strcmp(argv[1], "off");
    const char *percent = argc > 2 && !strcmp(argv[1], "percent") ? argv[2] : NULL;
    const char *own = argc > 2 && !strcmp(argv[1], "own") ? argv[2] : NULL;
    int i;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    if (off) _putenv_s("MM3_DAMAGE_FRAME_FIX", "0");
    MEM32(component + 0x3C) = body; MEM32(body + 4) = record;
    MEM32(other + 0x3C) = other_body; MEM32(other_body + 4) = other_record;
    MEMF(component + 0x8718) = MEMF(other + 0x8718) = 1.25f;
    if (percent) {
        /* Launcher car damage: the damage step sees the car's factor scaled,
         * and the car keeps its own factor afterwards. */
        float scale = (float)atoi(percent) / 100.0f;
        _putenv_s("MM3_DAMAGE_PERCENT", percent);
        MEM32(0x3C5C90) = frame;
        end_frame(60);
        step(component, 900);
        assert(seen_factor == 1.25f * scale && MEMF(component + 0x8718) == 1.25f);
        printf("PASS car damage %s%%: each crash's damage scaled for every car, its own factor kept\n", percent);
        return 0;
    }
    if (own) {
        /* Your car's damage: the primary player's car (player manager
         * 0x3C5CDC +0x54 -> +0xD8 player -> +0x19C vehicle -> +8 car) gets
         * the factor once more, on top of Car damage; other cars do not. */
        float scale = (float)atoi(own) / 100.0f;
        _putenv_s("MM3_DAMAGE_PERCENT", "200");
        _putenv_s("MM3_OWN_DAMAGE_PERCENT", own);
        MEM32(0x3C5CDC) = manager; MEM32(manager + 0x54) = slot; MEM32(slot + 0xD8) = player;
        MEM32(player + 0x19C) = vehicle; MEM32(vehicle) = 0x383768; MEM32(vehicle + 8) = component;
        MEM32(0x3C5C90) = frame;
        end_frame(60);
        step(component, 900);
        assert(seen_factor == 1.25f * 2.0f * scale && MEMF(component + 0x8718) == 1.25f);
        step(other, 900);
        assert(seen_factor == 1.25f * 2.0f && MEMF(other + 0x8718) == 1.25f);
        MEM32(vehicle) = 0x383000;                    /* not a vehicle interface: no player car */
        step(component, 900);
        assert(seen_factor == 1.25f * 2.0f);
        printf("PASS your car's damage %s%%: only the player's car, on top of car damage 200%%\n", own);
        return 0;
    }
    MEM32(0x3C5C90) = frame;
    MEMF(0x3C0328) = 50.0f; MEMF(0x3C0330) = 1.0f;          /* a wreck job */
    /* The contact scaler passes the job's scale through unchanged. */
    g_esp = stack; g_ecx = record; MEM32(g_esp) = 0x1F14E5; MEMF(g_esp + 4) = 50.0f;
    sub_00021DCF(); assert(seen_scale == 50.0f && g_esp == stack + 8);
    end_frame(30);                                          /* a console frame starts */
    if (off) {
        assert(step(component, 900) == 900 && step(component, 900) == 900);
        end_frame(60);
        assert(step(component, 0) == 0 && step(component, 0) == 0);
        puts("PASS damage frame fix off: MM3_DAMAGE_FRAME_FIX=0 keeps the game's own frames");
        return 0;
    }
    /* 60 fps: two steps a game frame, two game frames a console frame. The
     * main impact, found in the first game frame, is applied in steps 2-4:
     * three times, as on the Xbox. */
    assert(step(component, 0) == 0 && step(component, 900) == 900);
    end_frame(60);
    assert(step(component, 0) == 900 && step(component, 0) == 900);
    end_frame(60);
    /* The next console frame starts from nothing. */
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    /* Found in the second game frame: left for the next console frame, where
     * the Xbox would have found it, and applied three times there. */
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 700) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 700);
    end_frame(60);
    assert(step(component, 0) == 700 && step(component, 0) == 700);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    /* A larger find takes over: the first frame's 300, then the 800 the
     * second frame found, in the next console frame. */
    assert(step(component, 0) == 0 && step(component, 300) == 300);
    end_frame(60);
    assert(step(component, 0) == 300 && step(component, 800) == 300);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 200) == 800);
    end_frame(60);
    assert(step(component, 0) == 800 && step(component, 0) == 800);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    /* A contact found every frame (pushing against a wall) is applied three
     * times a console frame, as on the Xbox, not twice as often. */
    for (i = 0; i < 3; ++i) {
        assert(step(component, 0) == 0 && step(component, 500) == 500);
        end_frame(60);
        assert(step(component, 0) == 500 && step(component, 500) == 500);
        end_frame(60);
    }
    assert(step(component, 0) == 0 && step(component, 0) == 500);
    end_frame(60);
    assert(step(component, 0) == 500 && step(component, 0) == 500);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    /* A hit already in the record at a later game frame's first step came
     * before that frame's steps (another car's update): the Xbox finds it
     * before its next frame's first step and applies it four times. */
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 600) == 0 && step(component, 600) == 0);
    end_frame(60);
    assert(step(component, 0) == 600 && step(component, 0) == 600);
    end_frame(60);
    assert(step(component, 0) == 600 && step(component, 0) == 600);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    /* One there at a console frame's first step: four times as well. */
    assert(step(component, 650) == 650 && step(component, 650) == 650);
    end_frame(60);
    assert(step(component, 0) == 650 && step(component, 0) == 650);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    /* Cars are kept apart, each with its own steps. */
    assert(step(other, 0) == 0 && step(component, 0) == 0);
    assert(step(other, 400) == 400 && step(component, 0) == 0);
    end_frame(60);
    assert(step(other, 0) == 400 && step(component, 0) == 0);
    assert(step(other, 0) == 400 && step(component, 200) == 0);
    end_frame(60);
    assert(step(other, 0) == 0 && step(component, 0) == 0);
    assert(step(other, 0) == 0 && step(component, 0) == 200);
    end_frame(60);
    assert(step(other, 0) == 0 && step(component, 0) == 200);
    assert(step(other, 0) == 0 && step(component, 0) == 200);
    end_frame(60);
    /* Washington 05/01: the job's script drops the hit car's factor to 0.0001
     * between game frames, once it hears of the contact. A hit keeps the
     * factor it was found with: three times at 0.16, as on the Xbox, not once
     * at 0.16 and twice at 0.0001. */
    MEMF(component + 0x24) = 0.16f;
    assert(step(component, 0) == 0 && step(component, 900) == 900 && last_factor() == 0.16f);
    end_frame(60);
    MEMF(component + 0x24) = 0.0001f;                      /* the script reacts */
    assert(step(component, 0) == 900 && last_factor() == 0.16f);
    assert(step(component, 0) == 900 && last_factor() == 0.16f);
    end_frame(60);
    MEMF(component + 0x24) = 0.16f;                        /* 0.2 s later */
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    /* Found in a console frame's second game frame: applied in the next one
     * at the factor it was found with, though the script dropped it since. */
    assert(step(component, 0) == 0 && step(component, 700) == 0);
    end_frame(60);
    MEMF(component + 0x24) = 0.0001f;
    assert(step(component, 0) == 0 && step(component, 0) == 700 && last_factor() == 0.16f);
    end_frame(60);
    assert(step(component, 0) == 700 && last_factor() == 0.16f);
    assert(step(component, 0) == 700 && last_factor() == 0.16f);
    end_frame(60);
    /* A hit found while the factor is down counts at that factor. */
    assert(step(component, 0) == 0 && step(component, 500) == 500 && last_factor() == 0.0001f);
    end_frame(60);
    assert(step(component, 0) == 500 && last_factor() == 0.0001f);
    assert(step(component, 0) == 500 && last_factor() == 0.0001f);
    end_frame(60);
    MEMF(component + 0x24) = 0.0f;
    /* 30 fps: four steps a frame, every frame its own console frame, exactly
     * the game's own behaviour. */
    assert(step(component, 0) == 0 && step(component, 800) == 800);
    assert(step(component, 800) == 800 && step(component, 800) == 800);
    end_frame(30);
    for (i = 0; i < 4; ++i) assert(step(component, 0) == 0);
    end_frame(30);
    /* Slower (20 fps, six steps a frame): still the game's own frames. */
    for (i = 0; i < 6; ++i) assert(step(component, 600) == 600);
    end_frame(20);
    for (i = 0; i < 6; ++i) assert(step(component, 0) == 0);
    end_frame(20);
    /* 45 fps (three steps a frame): frames stand alone. */
    assert(step(component, 0) == 0 && step(component, 0) == 0 && step(component, 600) == 600);
    end_frame(45);
    for (i = 0; i < 3; ++i) assert(step(component, 0) == 0);
    end_frame(45);
    /* Ordinary races and jobs without a damage scale (the ambulance jobs) too. */
    MEMF(0x3C0328) = 1.0f; MEMF(0x3C0330) = 1.0f;
    assert(step(component, 0) == 0 && step(component, 800) == 800);
    end_frame(60);
    assert(step(component, 0) == 800 && step(component, 0) == 800);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(step(component, 0) == 0 && step(component, 0) == 0);
    end_frame(60);
    assert(seen_factor == 1.25f);                   /* no launcher scale: the car's own factor */
    /* Many cars at once, each in its own console frame. */
    for (i = 0; i < 300; ++i) {
        uint32_t comp = 0x100000 + (uint32_t)i * 0x100, rec = 0x200000 + (uint32_t)i * 0x400;
        MEM32(comp + 0x3C) = comp + 0x40; MEM32(comp + 0x40 + 4) = rec;
        assert(step(comp, 0.0f) == 0.0f && step(comp, 400.0f + (float)i) == 400.0f + (float)i);
    }
    end_frame(60);
    for (i = 0; i < 300; ++i)
        assert(step(0x100000 + (uint32_t)i * 0x100, 0.0f) == 400.0f + (float)i &&
               step(0x100000 + (uint32_t)i * 0x100, 0.0f) == 400.0f + (float)i);
    end_frame(60);
    for (i = 0; i < 300; ++i)
        assert(step(0x100000 + (uint32_t)i * 0x100, 0.0f) == 0.0f &&
               step(0x100000 + (uint32_t)i * 0x100, 0.0f) == 0.0f);
    puts("PASS damage frame fix: at 60 fps the main impact found in a console frame's first game frame is "
         "applied three times as on the Xbox, a later frame's find moves to the next console frame, a "
         "hit found before a frame's steps four times wherever it falls, a "
         "steady contact is applied three times a console frame, a larger find takes over, cars apart "
         "(300 at once), a hit keeps the damage factor it was found with when the job's script drops it, "
         "frames told apart by the game's frame clock, record restored; 30 fps and slower keep the game's "
         "own frames, 45 fps frames stand alone; ordinary races and ambulance jobs alike");
    return 0;
}
