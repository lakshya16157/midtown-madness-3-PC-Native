#include <math.h>
#include <stdint.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"

extern void sub_000489A6_original(void);
extern void sub_00021E18_original(void);

/* OTF0 LocalObject effects are packed 0x56-byte records. The same range is
 * consumed by E10A5's bounds builder and E1F49's native light producer. Never
 * change the shared descriptor: masks, breakage, flicker and source placement
 * continue through the game's normal path on every visit. */
float mm3_lamp_source_range(uint32_t source)
{
    float range = MEMF(source + 0x4F);
    float size;
    if (!mm3_mod_lamp_lighting()) return range;
    if (!isfinite(range) || range < 0.0f) return 0.0f;
    if (range > 0.0f) {
        /* Small indicators retain small pools; road lamps gain enough reach
         * below their mounting height. Large architectural spots add at most
         * 6m instead of multiplying their already long 40-50m throw. Keeping
         * these pools local also limits how many fixtures overlap a surface. */
        return range + fminf(range * 0.45f, 6.0f);
    }
    size = MEMF(source + 0x49);
    if (!isfinite(size) || size <= 0.0f || !MEM8(source + 0x4D) ||
        !(MEM32(source + 0x2B) & 0x00FFFFFFu)) return 0.0f;
    /* A visible bulb must illuminate its surroundings even when the retail
     * effect provided only a flare. Derive its reach from the authored flare
     * extent and brightness: tiny bulbs about 1-2m, larger fixtures 5-10m. */
    return fminf(10.0f, (1.0f + 14.0f * fminf(size, 2.0f)) *
                        sqrtf((float)MEM8(source + 0x4D) / 255.0f));
}

float mm3_lamp_source_cone(uint32_t source)
{
    float cone = (float)MEM8(source + 0x53) * (90.0f / 255.0f);
    if (!mm3_mod_lamp_lighting()) return cone;
    /* Native spot volumes use a half-angle in degrees (clamped to 75).
     * Widen the ground footprint while retaining the authored aim/type. */
    return cone > 0.0f ? fminf(cone + 5.0f, 75.0f) : 0.0f;
}

static uint32_t lamp_color_gain(uint32_t color, float gain)
{
    unsigned red = (color >> 16) & 255u, green = (color >> 8) & 255u;
    unsigned blue = color & 255u, peak = red > green ? red : green;
    uint32_t result = color & 0xFF000000u;
    if (blue > peak) peak = blue;
    if (!peak) return color;
    /* Uniform RGB gain preserves warm/cool/signal hues and native color
     * fades. Saturate the gain as a whole, not each channel independently. */
    gain = fminf(gain, 255.0f / (float)peak);
    result |= (uint32_t)((float)red * gain + 0.5f) << 16;
    result |= (uint32_t)((float)green * gain + 0.5f) << 8;
    result |= (uint32_t)((float)blue * gain + 0.5f);
    return result;
}

static uint32_t lamp_source_color(uint32_t source, uint32_t color)
{
    unsigned red = (color >> 16) & 255u, green = (color >> 8) & 255u;
    unsigned blue = color & 255u;
    uint32_t authored = MEM32(source + 0x2B);
    unsigned source_red = (authored >> 16) & 255u;
    unsigned source_green = (authored >> 8) & 255u;
    unsigned source_blue = authored & 255u;
    unsigned peak = source_red > source_green ? source_red : source_green;
    unsigned minimum = source_red < source_green ? source_red : source_green;
    float warmth;
    float gain = MEMF(source + 0x4F) == 0.0f ?
        0.65f * (float)MEM8(source + 0x4D) / 255.0f : 0.85f;
    if (source_blue > peak) peak = source_blue;
    if (source_blue < minimum) minimum = source_blue;
    if (!peak) return color;
    /* Give neutral fixtures an amber incandescent cast. Blend continuously
     * over the nearly-white authored range. Native fades quantize RGB before
     * this call: deriving warmth from that dimmed color made some channels
     * dip as the light brightened. A fixture's tint must stay fixed throughout
     * its fade. Saturated signs and emergency hues retain authored ratios. */
    warmth = fminf(1.0f, fmaxf(0.0f,
        ((float)minimum / (float)peak - 0.75f) * 4.0f));
    return (color & 0xFF000000u) |
        ((uint32_t)((float)red * gain + 0.5f) << 16) |
        ((uint32_t)((float)green * gain * (1.0f - 0.18f * warmth) + 0.5f) << 8) |
        (uint32_t)((float)blue * gain * (1.0f - 0.40f * warmth) + 0.5f);
}

void sub_000489A6(void)
{
    if (!mm3_mod_lamp_lighting()) { sub_000489A6_original(); return; }
    uint32_t stack = g_esp;
    uint32_t color = MEM32(stack + 8);
    int world_source = MEM32(stack) == 0x000E2533u;
    int traffic_signal = MEM32(stack) == 0x0016945Au;
    if (world_source) MEM32(stack + 8) = lamp_source_color(g_ebx, color);
    if (traffic_signal) {
        /* The traffic-light producer selects only the currently lit lens,
         * tint and distance fade before its native 6m/30-degree submission.
         * Preserve that small authored pool and its signal color, at a lower
         * intensity so intersections do not accumulate oversized bright pools. */
        MEM32(stack + 8) = lamp_color_gain(color, 0.8f);
    }
    sub_000489A6_original();
    if (world_source || traffic_signal) MEM32(stack + 8) = color;
}

void sub_00021E18(void)
{
    if (!mm3_mod_lamp_lighting()) { sub_00021E18_original(); return; }
    uint32_t stack = g_esp;
    uint32_t color = MEM32(stack + 8);
    int world_source = MEM32(stack) == 0x000E2508u;
    if (world_source) MEM32(stack + 8) = lamp_source_color(g_ebx, color);
    sub_00021E18_original();
    if (world_source) MEM32(stack + 8) = color;
}
