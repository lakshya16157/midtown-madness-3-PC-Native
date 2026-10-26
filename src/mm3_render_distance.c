#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_render_distance.h"
#include "mm3_mod_flags.h"

extern void nv2a_d3d_set_depth_view_remap(float scale, float knee);
extern int nv2a_d3d_distance_fog_ready(void);

/* Sky fog ramps (Skies/<sky>_fog.cdds) index w-buffer depth relative to the
 * far plane and were authored for the 700 m city cap. The renderer anchors
 * their haze while fading extended city geometry into the sky. Only the
 * effective city/camera multiplier belongs in that remap: changing object
 * LODs or requesting a range outside the resident cells must not move the
 * haze. The ramp's common sky-mask knee remains 0.78. */
#define FOG_KNEE MM3_FOG_RAMP_KNEE
/* PAL MM3: increase the title's own visibility/LOD distances before its
 * native D3D11 translation sees geometry. Keep the original fade fractions,
 * frustum construction, streaming and temporary render-pass save/restore.
 * Object/LOD and city/camera ranges are separate. Both at 1 provide the Xbox
 * baseline; an unset view setting inherits the old combined draw setting. */
static float env_scale(const char *name, float fallback)
{
    const char *env = getenv(name);
    char *end = NULL;
    float value = env && *env ? strtof(env, &end) : fallback;
    if (env && *env && (end == env || *end || !isfinite(value) || value < 1.0f || value > 4.0f)) {
        fprintf(stderr, "[MM3_DISTANCE] invalid %s; expected 1..4, using %.3g\n", name, fallback);
        value = fallback;
    }
    return value;
}

static float distance_scale(void)
{
    static float scale;
    if (!scale) {
        scale = env_scale("RECOMP_DRAW_DISTANCE", 2.0f);
        fprintf(stderr, "[MM3_DISTANCE] object/LOD distance multiplier %.3g\n", scale);
    }
    return scale;
}

float mm3_view_distance_scale(void)
{
    static float scale;
    if (!scale) {
        scale = env_scale("RECOMP_VIEW_DISTANCE", distance_scale());
        fprintf(stderr, "[MM3_DISTANCE] object_lod=%.3g city_view=%.3g facade_zones=%.3g facade_zone_cap=2\n",
                distance_scale(), scale, scale > 2.0f ? 2.0f : scale);
    }
    return scale;
}

float mm3_facade_distance_scale(void)
{
    float scale = mm3_view_distance_scale();
    return scale > 2.0f ? 2.0f : scale;
}

float mm3_draw_distance_scale(void)
{
    return distance_scale();
}

static float s_city_stream_cap;
static float s_city_authored_distance;
static float s_haze_scale;

float mm3_city_haze_scale(void)
{
    return s_haze_scale >= 1.0f ? s_haze_scale : 1.0f;
}

/* Called once at the native loader's facade-renderer lookup, before its
 * original clamp. Streaming and both facade draw entries visit only the
 * camera cell +/-1 (nine cells, nine inline visibility records). The native
 * 0.95*cell-size cap keeps the far plane inside that resident neighborhood.
 * Scaling this cap exposed missing whole rows whenever the camera crossed a
 * cell boundary. Record it even if the current range does not need clamping,
 * and clear it when loading a city without that renderer. */
void mm3_city_streaming_limit(uint32_t renderer)
{
    float cell_size = renderer ? MEMF(renderer + 0x1505Cu) : 0.0f;
    s_city_stream_cap = isfinite(cell_size) && cell_size > 0.0f
        ? (float)((double)cell_size * MEMF(0x003860E0u)) : 0.0f;
    fprintf(stderr, "[MM3_DISTANCE] city resident renderer=0x%08X cell_size=%.3g cap=%.3g\n",
            renderer, cell_size, s_city_stream_cap);
}

static float bounded_city_distance(float distance)
{
    return s_city_stream_cap > 0.0f && distance > s_city_stream_cap
        ? s_city_stream_cap : distance;
}

static void update_city_haze(float effective, float authored)
{
    float baseline = bounded_city_distance(authored);
    if (isfinite(effective) && effective > 0.0f && isfinite(baseline) && baseline > 0.0f) {
        float scale = fmaxf(1.0f, effective / baseline);
        if (scale != s_haze_scale) {
            /* MM3_FOG_REMAP=0 retains the native depth ramp for A/B tests. */
            if (!getenv("MM3_FOG_REMAP") || strcmp(getenv("MM3_FOG_REMAP"), "0"))
                nv2a_d3d_set_depth_view_remap(mm3_mod_native_fog() && nv2a_d3d_distance_fog_ready() ? 1.0f : scale, FOG_KNEE);
            s_haze_scale = scale;
            fprintf(stderr, "[MM3_DISTANCE] city authored=%.3g effective=%.3g resident_cap=%.3g haze_scale=%.3g\n",
                    authored, effective, s_city_stream_cap, scale);
        }
    }
}

static int s_pedestrian_distances_initialized;

static void pedestrian_defaults(void)
{
    if (!s_pedestrian_distances_initialized) {
        float scale = distance_scale();
        unsigned i;
        /* XBE defaults are {20,30,40,80,160}. The title's supersampled
         * passes save these effective values and restore them unchanged. */
        for (i = 0; i < 5; ++i) MEMF(0x0039ADC0u + 4 * i) *= scale;
        s_pedestrian_distances_initialized = 1;
    }
}

extern void sub_000E2D5B_original(void);
void sub_000E2D5B(void)
{
    uint32_t caller = MEM32(g_esp);
    /* Object-manager construction and the explicit objMSetLodDistance
     * callback receive original distances. 0x211273/0x2114ED instead
     * install and restore already effective values for temporary passes. */
    if (caller == 0x000E4672u || caller == 0x000E2E94u) {
        pedestrian_defaults();
        MEMF(g_esp + 4) *= distance_scale();
    }
    sub_000E2D5B_original();
}

extern void sub_000D9E2E_original(void);
void sub_000D9E2E(void)
{
    sub_000D9E2E_original();
    /* This setter validates all five positive, increasing distances and
     * copies them to the global array. Do not alter failed requests. */
    if (LO8(g_eax)) {
        unsigned i;
        for (i = 0; i < 5; ++i) MEMF(0x0039ADC0u + 4 * i) *= distance_scale();
        /* A custom pedestrian setter can precede object-manager creation.
         * Those globals are already effective and must not be scaled again. */
        s_pedestrian_distances_initialized = 1;
    }
}

extern void sub_000FE71E_original(void);
void sub_000FE71E(void)
{
    uint32_t caller = MEM32(g_esp);
    /* City construction, DSS loading and renderSetViewDistance. Exclude
     * cubemap passes (0x22CE39/0x22CFE3): their values come from the
     * effective camera distance and their restore must stay exact. */
    if (caller == 0x001002CEu || caller == 0x00100ED6u || caller == 0x00210EDEu) {
        float authored = MEMF(g_esp + 4);
        MEMF(g_esp + 4) *= mm3_view_distance_scale();
        if (caller == 0x00210EDEu)
            MEMF(g_esp + 4) = bounded_city_distance(MEMF(g_esp + 4));
        if (isfinite(authored) && authored > 0.0f)
            s_city_authored_distance = authored;
    }
    /* 0x224C4E already supplies the native resident-cell cap and writes the
     * same value into the loader local used by camera projection. Keep both
     * untouched. Temporary cubemap settings likewise remain effective. */
    sub_000FE71E_original();
}

extern void sub_001661D0_original(void);
extern void sub_0022F298_original(void);
void sub_0022F298(void)
{
    uint32_t caller = MEM32(g_esp);
    /* Blob shadows have a separate clip distance and reciprocal fade.
     * Scale initialization and explicit Lua settings, leaving temporary
     * supersampled-pass install/restore calls unchanged. */
    if (caller == 0x0022F9D7u || caller == 0x0022F338u)
        MEMF(g_esp + 4) *= distance_scale();
    sub_0022F298_original();
}

void sub_001661D0(void)
{
    uint32_t caller = MEM32(g_esp);
    /* Facade construction uses {80,280}; renderFacadeSetZones supplies
     * explicit Lua overrides. Let the original setter recompute its fade
     * coefficients from the larger distances. The supersampled-pass
     * save/restore calls at 0x211220/0x21153D already use effective values. */
    if (caller == 0x0015FA1Du || caller == 0x0020FABAu) {
        MEMF(g_esp + 4) *= mm3_facade_distance_scale();
        MEMF(g_esp + 8) *= mm3_facade_distance_scale();
    }
    sub_001661D0_original();
    {
        float scale = mm3_facade_distance_scale();
        float near_distance = MEMF(0x003C04E0u);
        /* The native facade-detail fade uses squared camera distance and
         * fixed offsets of 10 m / 1 m below the near zone. Scaling only the
         * zone left its blend just 9 m wide: 150..159 m at the default 2x,
         * instead of the corresponding 140..158 m. Scale those offsets too.
         * Rebuild from the validated effective zone after every setter call,
         * including temporary-pass install/restore; never scale saved values
         * again. Leave unusual small zones and the Xbox baseline untouched. */
        if (scale > 1.0f && isfinite(near_distance) && near_distance > 10.0f * scale) {
            double fade_start = (double)near_distance - 10.0 * scale;
            double fade_end = (double)near_distance - scale;
            double slope = 1.0 / (fade_start * fade_start - fade_end * fade_end);
            MEMF(0x003B8134u) = (float)(-fade_end * fade_end * slope);
            MEMF(0x003B8138u) = (float)slope;
        }
    }
}

extern void sub_000F4AC3_original(void);
void sub_000F4AC3(void)
{
    uint32_t caller = MEM32(g_esp);
    /* Explicit view-distance settings supply an original distance. FOV
     * changes (0x210E1E) reuse the stored far plane. City loading
     * (0x224C73) supplies the already bounded effective city distance. */
    if (caller == 0x00210EB4u) {
        float authored = MEMF(g_esp + 0x10);
        MEMF(g_esp + 0x10) = bounded_city_distance(authored * mm3_view_distance_scale());
        update_city_haze(MEMF(g_esp + 0x10), authored);
    } else if (caller == 0x00224C73u) {
        update_city_haze(MEMF(g_esp + 0x10), s_city_authored_distance);
    }
    sub_000F4AC3_original();
}

extern void sub_002256C7_original(void);
static unsigned particle_budget(void)
{
    static unsigned budget;
    if (!budget) {
        const char *env = getenv("MM3_PARTICLE_MAX_OVERDRAW");
        char *end = NULL;
        long value = env ? strtol(env, &end, 10) : 24;
        if (env && (end == env || *end || value < 1 || value > 255)) {
            fprintf(stderr, "[MM3_DISTANCE] invalid MM3_PARTICLE_MAX_OVERDRAW; expected 1..255, using 24\n");
            value = 24;
        }
        budget = (unsigned)value;
    }
    return budget;
}

void sub_002256C7(void)
{
    uint32_t self = g_ecx, ps;
    sub_002256C7_original();
    /* Keep a bounded effects budget at the extended view distance. Raising
     * the Xbox budget of 12 to its maximum of 255 allowed twenty-one times
     * as much overlapping smoke/spray during collisions and burnouts.
     * Explicit title/Lua settings remain authoritative. */
    ps = MEM32(self + 4);
    if (ps && MEM32(ps + 0x524) == 12u && distance_scale() > 1.0f) {
        unsigned budget = particle_budget();
        MEM32(ps + 0x524) = budget;
        fprintf(stderr, "[MM3_DISTANCE] particle max overdraw 12 -> %u\n", budget);
    }
}
