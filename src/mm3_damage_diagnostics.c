#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "recomp_types.h"

/* Car-to-car collision damage in the "wreck the car" jobs (Paris Career 05/01
 * and 06/02, the Washington damage jobs). Their scripts call
 * physicsCarToCarDamageScale(1, 50) (sub_00212D9D: 0x3C0330 and 0x3C0328) and
 * playerSetDamageFactor(target, 0.15). The car-to-car contact handler
 * sub_001F107A ends every contact by scaling each car's contact record with
 * sub_00021DCF(scale): +0x1A4, the largest collision impulse of the frame
 * (sub_001DA648 keeps the maximum), and +0x1A8 are multiplied by it.
 *
 * That impulse is a frame's worth, so it shrinks with the frame time: the
 * user's van job (log direct-20261023-071535) had a median peak of 3,272 at
 * 30 fps and 1,260 at 60, which is why the job was only doable at 30 fps.
 * While a job has set a damage scale, the scale is multiplied by (1/30 s) /
 * the game's averaged frame time (frame object at 0x3C5C90, seconds at +0x54),
 * so hits count as they did on the console's 30 fps. Other races keep the
 * game's own damage. MM3_DAMAGE_FRAME_FIX=0 turns the correction off; each
 * scaled contact is logged as [MM3_DAMAGE]. */
extern void sub_00021DCF_original(void);

#define DAMAGE_LOG_LINES 3000u
#define CONSOLE_FRAME (1.0f / 30.0f)

static float frame_correction(void)
{
    static int enabled = -1;
    uint32_t frame = MEM32(0x003C5C90u);
    float seconds;
    if (enabled < 0) {
        const char *v = getenv("MM3_DAMAGE_FRAME_FIX");
        enabled = !(v && !strcmp(v, "0"));
    }
    if (!enabled || frame < 0x10000u || frame >= 0x84000000u) return 1.0f;
    seconds = MEMF(frame + 0x54u);
    if (!(seconds > 1.0f / 240.0f) || !(seconds < 0.1f)) return 1.0f;
    return seconds < CONSOLE_FRAME ? CONSOLE_FRAME / seconds : 1.0f;
}

void sub_00021DCF(void)
{
    static unsigned lines;
    static clock_t first;
    uint32_t record = g_ecx, back = MEM32(g_esp);
    float scale = MEMF(g_esp + 4u), correction = 1.0f;
    int job = MEMF(0x003C0328u) != 1.0f || MEMF(0x003C0330u) != 1.0f;
    if (job && (back == 0x001F14E5u || back == 0x001F1508u) && record >= 0x10000u && record < 0x84000000u) {
        correction = frame_correction();
        if (lines < DAMAGE_LOG_LINES) {
            clock_t now = clock();
            if (!lines++) first = now;
            fprintf(stderr, "[MM3_DAMAGE] %.3f car %08X scale %.2f x%.2f impulse %.3f %.3f\n",
                    (double)(now - first) / CLOCKS_PER_SEC, record, scale, correction,
                    MEMF(record + 0x1A4u), MEMF(record + 0x1A8u));
        }
        if (correction != 1.0f) MEMF(g_esp + 4u) = scale * correction;
    }
    sub_00021DCF_original();
}
