#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "recomp_types.h"

/* Car damage, counted as on the Xbox at any frame rate.
 *
 * The cars' physics runs in 1/120 s steps, several a frame, and each step
 * starts with the damage step sub_001CDB6C (sub_001D7425 -> sub_001D4794),
 * before that step's forces and contacts:
 * +0x1A4 of the car's contact record holds the largest contact impulse since
 * the frame began (sub_001DA648 keeps the maximum; the record is cleared once
 * a frame) and
 *   v = impulse * part factor * car factor * 1230;
 *   nothing below 800; damage = (min(v, 30000) - 800) / 29200, capped at 1.
 * So a hit is applied again in every later step of its frame: in the Xbox's
 * 30 fps frames of four steps up to four times (2.5 on average), in 60 fps
 * frames of two steps at most twice (1.5). The impulse itself does not depend
 * on the frame rate: on 2026-10-26 the same 30 m/s crash into a wall measured
 * 77,000 at 30 fps and 79,000-85,000 at 60, with 1/120 s steps at both rates.
 * That is why the wreck jobs (Olle in Paris 06/02, Inga's van in 05/01) were
 * harder at 60 fps, and every car got off lighter.
 *
 * The main impact of a crash is found once a game frame, after that frame's
 * first step, so the remaining steps apply it: on the Xbox's four-step frames
 * three times (every measured wall crash at 30 fps: x3), at 60 fps once.
 * Faster than 30 fps, game frames are grouped into console frames of four
 * steps, as the Xbox's: what the console frame's first game frame found is
 * applied in each of its later steps (three times, as on the Xbox), and what
 * a later game frame finds is left for the next console frame, where the Xbox
 * would have found it, instead of being applied in between. Game frames are
 * told apart by the game's own frame clock (the total ticks its timer adds up
 * once a frame, frame object +0x28) and each car's steps are counted within
 * them. Measured the same day with identical slow crashes into a wall: the
 * main impact was applied three times at 30 fps (80% or 300% part damage in
 * total) and once at 60 (28% or 105%).
 * Some hits are already in the record at a game frame's first step: they
 * were found between frames, before the steps (another car's update meeting
 * this one, as in the wreck jobs), and the Xbox applies them in all four
 * steps. At 60 fps that only happened when such a hit came before a console
 * frame's first game frame; one before a later game frame was left for the
 * next console frame, which applied it from its second step, three times.
 * It now counts from that console frame's first step, four times, as on the
 * Xbox. In the user's Paris van job (2026-10-26) 16 of 61 hits were of this
 * kind at a console frame's start; about as many more came between its game
 * frames and were applied once too few.
 * Each car's per-frame update (sub_002203E5 -> sub_0021BE03) runs fixed
 * 1/120 s steps and carries the rest of the frame's time to the next; above
 * eight steps a frame they are stretched.
 * The record keeps its real value for everything else (crash sounds). At
 * 30 fps or slower nothing changes. MM3_DAMAGE_FRAME_FIX=0 turns this off.
 *
 * The wreck jobs (Paris Career 05/01 Inga's escape and 06/02) also call
 * physicsCarToCarDamageScale(1, 50) (sub_00212D9D: 0x3C0330 and 0x3C0328) and
 * playerSetDamageFactor(target, 0.15); every car-to-car contact ends with
 * sub_00021DCF(scale) multiplying the contact record. While a job has set a
 * scale, each scaled contact is logged as [MM3_DAMAGE]. */
extern void sub_00021DCF_original(void);
extern void sub_001CDB6C_original(void);

#define DAMAGE_LOG_LINES 600u
#define HIT_LOG_LINES 400u
#define CONSOLE_STEPS 4         /* 1/120 s physics steps in an Xbox frame */
#define CARS 1024u               /* a power of two: open-addressed table */

static int damage_job(void)
{
    return MEMF(0x003C0328u) != 1.0f || MEMF(0x003C0330u) != 1.0f;
}

static int frame_fix_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *v = getenv("MM3_DAMAGE_FRAME_FIX");
        enabled = !(v && !strcmp(v, "0"));
    }
    return enabled;
}

/* The game's averaged frame time in seconds, or 0 when unknown or implausible. */
static float frame_seconds(void)
{
    uint32_t frame = MEM32(0x003C5C90u);
    float seconds;
    if (frame < 0x10000u || frame >= 0x84000000u) return 0.0f;
    seconds = MEMF(frame + 0x54u);
    return seconds > 1.0f / 1000.0f && seconds < 0.5f ? seconds : 0.0f;
}

static int guest_object(uint32_t address)
{
    return address >= 0x10000u && address < 0x84000000u && !(address & 3u);
}

void sub_00021DCF(void)
{
    static unsigned lines;
    static clock_t first;
    uint32_t record = g_ecx, back = MEM32(g_esp);
    if (lines < DAMAGE_LOG_LINES && damage_job() && (back == 0x001F14E5u || back == 0x001F1508u) &&
        guest_object(record)) {
        clock_t now = clock();
        if (!lines++) first = now;
        fprintf(stderr, "[MM3_DAMAGE] %.3f car %08X scale %.2f impulse %.3f %.3f\n",
                (double)(now - first) / CLOCKS_PER_SEC, record, MEMF(g_esp + 4u),
                MEMF(record + 0x1A4u), MEMF(record + 0x1A8u));
    }
    sub_00021DCF_original();
}

/* The game's frame: the low word of the total ticks its frame timer (frame
 * object +0x18) adds up once a frame; 0 when there is no frame object. */
static uint32_t game_frame(void)
{
    uint32_t frame = MEM32(0x003C5C90u);
    return frame >= 0x10000u && frame < 0x84000000u ? MEM32(frame + 0x28u) | 1u : 0u;
}

/* Each car's console frame: the game frame of its last step (frame); the
 * physics steps of the console frame (steps) and of the game frame
 * (frame_steps); which game frame of the console frame this is (part, 0 the
 * first); the largest impulse of the current game frame (now); what the
 * console frame applies after its first game frame (found); what its later
 * game frames found (later), and what the previous console frame's later
 * frames left for this one (left); of those, what was already there at
 * their first step (later_first, left_first). Indexed by the record's
 * address. */
static struct {
    uint32_t record, frame;
    int steps, frame_steps, part;
    float now, found, later, left, later_first, left_first;
} s_car[CARS];

static unsigned car_slot(uint32_t record)
{
    unsigned i = (record >> 4) * 2654435761u >> 22 & (CARS - 1u), n;
    for (n = 0; n < CARS; ++n, i = (i + 1u) & (CARS - 1u))
        if (s_car[i].record == record || !s_car[i].record) break;
    if (n == CARS) i = (record >> 4) & (CARS - 1u);       /* full: reuse a slot */
    if (s_car[i].record != record) {
        memset(&s_car[i], 0, sizeof s_car[i]);
        s_car[i].record = record;
    }
    return i;
}

/* A car's first step of a new game frame. The console frame ends once it has
 * had four steps, or when another frame like the last would overshoot four by
 * more than stopping here falls short (three-step frames at 45 fps stand
 * alone, and every frame at 30 fps or slower is its own console frame). */
static void car_new_frame(unsigned slot, uint32_t frame)
{
    int n = s_car[slot].frame_steps;
    s_car[slot].frame = frame;
    if (!n) return;                                 /* a new car: its first frame */
    if (s_car[slot].part == 0) s_car[slot].found = fmaxf(s_car[slot].left, s_car[slot].now);
    else s_car[slot].later = fmaxf(s_car[slot].later, s_car[slot].now);
    if (s_car[slot].steps >= CONSOLE_STEPS ||
        s_car[slot].steps + n - CONSOLE_STEPS > CONSOLE_STEPS - s_car[slot].steps) {
        s_car[slot].left = s_car[slot].later;
        s_car[slot].left_first = s_car[slot].later_first;
        s_car[slot].later = s_car[slot].found = s_car[slot].later_first = 0.0f;
        s_car[slot].steps = 0;
        s_car[slot].part = 0;
    } else {
        ++s_car[slot].part;
    }
    s_car[slot].frame = frame;
    s_car[slot].frame_steps = 0;
    s_car[slot].now = 0.0f;
}

/* The damage step, as the game runs it: v = impulse * the part's factor *
 * the car's factor (playerSetDamageFactor) * 1230, nothing below 800, and
 * (min(v, 30000) - 800) / 29200 of the part's damage, at most all of it.
 * During a wreck job each hit that counts is logged with the damage it does,
 * so a job that seems impossible shows what the hits were worth. */
static void damage_step(uint32_t component, uint32_t record)
{
    static unsigned lines;
    static clock_t first;
    float impulse = MEMF(record + 0x1A4u), v;
    if (lines < HIT_LOG_LINES && damage_job()) {
        v = impulse * MEMF(component + 0x24u) * MEMF(record + 0x14u) * 1230.0f;
        if (v > 800.0f && v < 1.0e12f) {
            clock_t now = clock();
            if (!lines++) first = now;
            fprintf(stderr, "[MM3_DAMAGE] hit %.3f car %08X impulse %.1f -> %.0f, part damage +%.1f%%\n",
                    (double)(now - first) / CLOCKS_PER_SEC, record, impulse, v,
                    100.0f * ((v < 30000.0f ? v : 30000.0f) - 800.0f) / 29200.0f);
        }
    }
    sub_001CDB6C_original();
}

void sub_001CDB6C(void)
{
    uint32_t component = g_edx, body, record, frame;
    float impulse, seen;
    unsigned slot;
    if (!guest_object(component) || !guest_object(body = MEM32(component + 0x3Cu)) ||
        !guest_object(record = MEM32(body + 4u))) {
        sub_001CDB6C_original();
        return;
    }
    impulse = MEMF(record + 0x1A4u);
    if (!frame_fix_enabled() || !(impulse >= 0.0f) || impulse > 1.0e12f) {
        damage_step(component, record);
        return;
    }
    frame = game_frame();
    if (!frame) {
        damage_step(component, record);
        return;
    }
    slot = car_slot(record);
    if (s_car[slot].frame != frame) car_new_frame(slot, frame);
    ++s_car[slot].steps;
    ++s_car[slot].frame_steps;
    s_car[slot].now = fmaxf(s_car[slot].now, impulse);
    /* A later game frame's hit already there at its first step was found
     * before that frame's steps: the Xbox would find it before the next
     * console frame's first step. */
    if (s_car[slot].part != 0 && s_car[slot].frame_steps == 1)
        s_car[slot].later_first = fmaxf(s_car[slot].later_first, impulse);
    /* The console frame's first step sees what the game shows and what the
     * last console frame left from before a frame's steps; the rest of its
     * first game frame everything the last console frame left; its later game
     * frames what the first one found. */
    if (s_car[slot].part == 0)
        seen = fmaxf(impulse, s_car[slot].frame_steps == 1 ? s_car[slot].left_first : s_car[slot].left);
    else
        seen = s_car[slot].found;
    MEMF(record + 0x1A4u) = seen;
    damage_step(component, record);
    MEMF(record + 0x1A4u) = impulse;
}
