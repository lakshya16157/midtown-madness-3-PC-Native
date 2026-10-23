#include <stdio.h>
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
 * Players report the van job is only doable at 30 fps. A per-frame peak
 * impulse is a frame-rate dependent quantity, so log each scaled contact while
 * a job has set a damage scale: one run at 60 fps and one with
 * RECOMP_FPS_LIMIT=30 show how much smaller the hits come out at 60. */
extern void sub_00021DCF_original(void);

#define DAMAGE_LOG_LINES 3000u

void sub_00021DCF(void)
{
    static unsigned lines;
    static clock_t first;
    uint32_t record = g_ecx, back = MEM32(g_esp);
    float scale = MEMF(g_esp + 4u);
    if (lines < DAMAGE_LOG_LINES && (MEMF(0x003C0328u) != 1.0f || MEMF(0x003C0330u) != 1.0f) &&
        (back == 0x001F14E5u || back == 0x001F1508u) && record >= 0x10000u && record < 0x84000000u) {
        clock_t now = clock();
        if (!lines++) first = now;
        fprintf(stderr, "[MM3_DAMAGE] %.3f car %08X scale %.2f impulse %.3f %.3f\n",
                (double)(now - first) / CLOCKS_PER_SEC, record, scale,
                MEMF(record + 0x1A4u), MEMF(record + 0x1A8u));
    }
    sub_00021DCF_original();
}
