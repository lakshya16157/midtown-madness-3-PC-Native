#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "recomp_types.h"
#include "mm3_darkness_monitor.h"

/* Black or "tunnel-lit" frames while driving (G-06): the scene's light level
 * is the game's own decision. sub_002305D1 averages the alpha of the 40x30
 * probe target it drew three frames earlier (four targets at manager+0x5984)
 * into a running level per view (manager+0x5AD0, moving 15% a frame), and
 * sub_00230DEE turns that into the light factor (manager+0x5B08) which the
 * scene, the headlights and the HUD read through sub_0003713E: clamp((level -
 * 0.05) * 2.5), inverted while 0x4087B0 is set, and 0.5 when the player object
 * (0x3C55C8) is missing. The light-queue consumer runs once a frame between
 * the level update and the factor update, so this sees this frame's level and
 * the last frame's factor. Any one-frame jump of either is reported with the
 * inputs that can cause one, so a [BLACK_FRAME] in the renderer log can be
 * matched to the game's decision. A minute summary keeps the normal range. */
#define DARK_LEVEL 0x5AD0u
#define DARK_FACTOR 0x5B08u
#define DARK_VIEW 0x5B18u
#define DARK_PROBE_INDEX 0x5ACCu
#define DARK_PLAYER 0x003C55C8u
#define DARK_INVERT 0x004087B0u
#define DARK_SKIP 0x003B3A40u
#define DARK_MAX_LINES 40

typedef struct {
    int valid;
    float level, factor;
    uint32_t player, probe;
    uint8_t invert;
} DarkSample;

static DarkSample s_last[2];
static unsigned s_lines, s_jumps, s_frames;
static float s_factor_min = 1e9f, s_factor_max = -1e9f, s_level_min = 1e9f, s_level_max = -1e9f;
static clock_t s_window;

static int dark_jump(const DarkSample *a, const DarkSample *b)
{
    if (!isfinite(b->level) || !isfinite(b->factor)) return isfinite(a->level) && isfinite(a->factor);
    if (fabsf(b->level - a->level) > 0.1f) return 1;
    if (fabsf(b->factor - a->factor) > 0.2f) return 1;
    if (!a->player != !b->player) return 1;
    return a->invert != b->invert;
}

void mm3_darkness_frame(uint32_t manager)
{
    DarkSample now;
    uint32_t view;
    clock_t t;
    if (manager < 0x10000u || manager > 0x08000000u - 0x5B20u) return;
    view = MEM32(manager + DARK_VIEW);
    if (view > 1) return;
    now.valid = 1;
    now.level = MEMF(manager + DARK_LEVEL + view * 4u);
    now.factor = MEMF(manager + DARK_FACTOR + view * 4u);
    now.player = MEM32(DARK_PLAYER);
    now.probe = MEM32(manager + DARK_PROBE_INDEX);
    now.invert = MEM8(DARK_INVERT);
    if (s_last[view].valid && dark_jump(&s_last[view], &now)) {
        ++s_jumps;
        if (s_lines < DARK_MAX_LINES) {
            ++s_lines;
            fprintf(stderr, "[MM3_DARKNESS] view %u jumped: light level %.3f -> %.3f, factor %.2f -> %.2f, "
                    "player %08X -> %08X, invert %u -> %u, probe %u -> %u, brightness probe %s\n",
                    view, s_last[view].level, now.level, s_last[view].factor, now.factor,
                    s_last[view].player, now.player, s_last[view].invert, now.invert,
                    s_last[view].probe, now.probe, MEM32(DARK_SKIP) > 1 ? "skipped" : "read");
            fflush(stderr);
        }
    }
    s_last[view] = now;
    if (isfinite(now.factor)) {
        if (now.factor < s_factor_min) s_factor_min = now.factor;
        if (now.factor > s_factor_max) s_factor_max = now.factor;
    }
    if (isfinite(now.level)) {
        if (now.level < s_level_min) s_level_min = now.level;
        if (now.level > s_level_max) s_level_max = now.level;
    }
    ++s_frames;
    t = clock();
    if (!s_window) { s_window = t; return; }
    if (t - s_window < 60 * CLOCKS_PER_SEC) return;
    fprintf(stderr, "[MM3_DARKNESS] last %.0f s: light level %.3f..%.3f, factor %.2f..%.2f, %u one-frame jumps in %u frames\n",
            (double)(t - s_window) / CLOCKS_PER_SEC, s_level_min, s_level_max, s_factor_min, s_factor_max,
            s_jumps, s_frames);
    s_window = t;
    s_jumps = s_frames = 0;
    s_factor_min = s_level_min = 1e9f;
    s_factor_max = s_level_max = -1e9f;
}
