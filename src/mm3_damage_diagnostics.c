#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "recomp_types.h"

/* Car damage, made frame-rate independent.
 *
 * Each frame the car's update sub_001D7425 -> sub_001D4794(dt) turns the car's
 * contact record into damage in sub_001CDB6C: +0x1A4 holds the frame's largest
 * contact impulse (sub_001DA648 keeps the maximum) and
 *   v = impulse * damage factor * car factor * 1230;
 *   nothing below 800; damage = (min(v, 30000) - 800) / 29200, capped at 1.
 *
 * The impulse is one frame's worth (proportional to the frame time), so at
 * 60 fps each frame carries half of a 30 fps frame's impulse, and the 800
 * threshold discards most of it. In the user's van job (log
 * direct-20261023-071535) hits peaked at a median 3,272 at 30 fps and 1,260 at
 * 60, which is why the job was only doable at 30 fps. Doubling each frame's
 * impulse would overshoot, because damage is applied every frame: twice the
 * frames would then do twice the console's damage. Every car takes damage this
 * way, so the player's car in the ambulance and other "do not wreck" jobs got
 * off just as lightly as the van.
 *
 * So whenever the game runs faster than the console's 30 fps, each car's
 * impulse is summed over 1/30 s of play (game frame time, frame object at
 * 0x3C5C90, seconds at +0x54). The damage step sees that sum once per 1/30 s and
 * no impulse in between, exactly as a 30 fps frame. The record keeps its real
 * value for everything else (crash sounds). At 30 fps or slower nothing
 * changes. MM3_DAMAGE_FRAME_FIX=0 turns this off.
 *
 * The wreck jobs (Paris Career 05/01 Inga's escape and 06/02) also call
 * physicsCarToCarDamageScale(1, 50) (sub_00212D9D: 0x3C0330 and 0x3C0328) and
 * playerSetDamageFactor(target, 0.15); every car-to-car contact ends with
 * sub_00021DCF(scale) multiplying the contact record. While a job has set a
 * scale, each scaled contact is logged as [MM3_DAMAGE]. */
extern void sub_00021DCF_original(void);
extern void sub_001CDB6C_original(void);

#define DAMAGE_LOG_LINES 3000u
#define CONSOLE_FRAME (1.0f / 30.0f)
#define WINDOWS 256u

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

/* One car's impulse gathered towards a 30 fps frame. A window opens at a car's
 * first hit and closes once everything gathered has been handed over, so only
 * cars in a collision hold one. */
static struct { uint32_t record; float impulse, seconds; } s_window[WINDOWS];
static unsigned s_windows_open;

static unsigned window_find(uint32_t record)
{
    unsigned i, seen = 0;
    for (i = 0; i < WINDOWS && seen < s_windows_open; ++i) {
        if (!s_window[i].record) continue;
        if (s_window[i].record == record) return i;
        ++seen;
    }
    return WINDOWS;
}

static unsigned window_open(uint32_t record)
{
    unsigned i;
    for (i = 0; i < WINDOWS && s_window[i].record; ++i) {}
    if (i == WINDOWS) i = record / 4u % WINDOWS;           /* full: reuse a slot */
    else ++s_windows_open;
    s_window[i].record = record;
    s_window[i].impulse = s_window[i].seconds = 0.0f;
    return i;
}

static void window_close(unsigned slot)
{
    s_window[slot].record = 0;
    --s_windows_open;
}

void sub_001CDB6C(void)
{
    uint32_t component = g_edx, body, record;
    float impulse, seconds = frame_seconds(), seen;
    unsigned slot;
    if (!frame_fix_enabled() || !guest_object(component) || !guest_object(body = MEM32(component + 0x3Cu)) ||
        !guest_object(record = MEM32(body + 4u))) {
        sub_001CDB6C_original();
        return;
    }
    impulse = MEMF(record + 0x1A4u);
    slot = window_find(record);
    if (!(seconds > 0.0f) || seconds >= CONSOLE_FRAME || !(impulse >= 0.0f) || impulse > 1.0e12f) {
        /* 30 fps or slower (or not a plain impulse): the game's own frame,
         * plus whatever a faster stretch had gathered and not handed over. */
        if (slot != WINDOWS) {
            if (impulse >= 0.0f && impulse <= 1.0e12f) {
                MEMF(record + 0x1A4u) = impulse + s_window[slot].impulse;
                window_close(slot);
                sub_001CDB6C_original();
                MEMF(record + 0x1A4u) = impulse;
                return;
            }
            window_close(slot);
        }
        sub_001CDB6C_original();
        return;
    }
    if (slot == WINDOWS) {
        if (impulse == 0.0f) {                         /* no hit and nothing gathered */
            sub_001CDB6C_original();
            return;
        }
        slot = window_open(record);
    }
    s_window[slot].impulse += impulse;
    s_window[slot].seconds += seconds;
    seen = 0.0f;
    if (s_window[slot].seconds >= CONSOLE_FRAME * 0.999f) {
        /* A full console frame: hand over its share of the gathered impulse
         * and keep the rest (frame times that do not divide 1/30 s). */
        float share = CONSOLE_FRAME / s_window[slot].seconds;
        if (share > 1.0f) share = 1.0f;
        seen = s_window[slot].impulse * share;
        s_window[slot].impulse -= seen;
        s_window[slot].seconds -= CONSOLE_FRAME;
        if (s_window[slot].seconds < 0.0f) s_window[slot].seconds = 0.0f;
        if (!(s_window[slot].impulse > 1.0e-3f)) window_close(slot);
    }
    MEMF(record + 0x1A4u) = seen;
    sub_001CDB6C_original();
    MEMF(record + 0x1A4u) = impulse;
}
