#include "platform/xbox_winnt.h"
#include "platform/win32_compat.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mods.h"
#include "env_range.h"
#include "mm3_mod_flags.h"
#include "mm3_render_distance.h"
#include "mm3_exhaust.h"
#include "nv2a_host_commands.h"

/* The title's draw distances and its fog pass. Every guest hook here is the
 * single definition (the generated function is renamed <name>_original by
 * CMakeLists.txt MM3_DISTANCE_FUNCTIONS / MM3_DEFINITION_ONLY_FUNCTIONS).
 * The D3D11 renderer owns the native fog pass itself; the SDL3 GPU renderer
 * draws the same ordered packet from render_commands.c/render_fog.c, and has
 * neither the depth-view remap nor the sky fog ramp (XBOXRECOMP_SDL3). */
#ifndef XBOXRECOMP_SDL3
extern void nv2a_d3d_set_depth_view_remap(float scale, float knee);
extern int nv2a_d3d_distance_fog_ready(void);
#define MM3_SKY_RAMP 1
#else
#define MM3_SKY_RAMP 0
#endif

#ifndef MM3_NATIVE_FOG_CLOCK
#define MM3_NATIVE_FOG_CLOCK() GetTickCount64()
#endif

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
 * baseline; an unset view setting inherits the old combined draw setting.
 * Two knobs, both 1 = Xbox (MM3_MOD_DRAW_DISTANCE off gives 1 and 1):
 *   RECOMP_DRAW_DISTANCE (default 2): object/pedestrian/traffic LODs, blob
 *     shadows, street props, the flare queue and the particle overdraw cap.
 *   RECOMP_VIEW_DISTANCE (default: the draw distance): city view distance and
 *     far plane. A smooth per-pixel depth haze replaces the distance-fog
 *     texture pass. */
static float env_scale(const char *name, float fallback)
{
    return recomp_env_float_in_range("MM3_DISTANCE", name, fallback, 1.0f, 4.0f);
}

float mm3_draw_distance_scale(void)
{
    static float scale;
    if (!scale) {
        scale = mm3_mod_on(MM3_MOD_DRAW_DISTANCE) ? env_scale("RECOMP_DRAW_DISTANCE", 2.0f) : 1.0f;
        fprintf(stderr, "[MM3_DISTANCE] object/LOD distance multiplier %.3g\n", scale);
    }
    return scale;
}

float mm3_view_distance_scale(void)
{
    static float scale;
    if (!scale) {
        scale = mm3_mod_on(MM3_MOD_DRAW_DISTANCE) ?
            env_scale("RECOMP_VIEW_DISTANCE", mm3_draw_distance_scale()) : 1.0f;
        fprintf(stderr, "[MM3_DISTANCE] object_lod=%.3g city_view=%.3g facade_zones=%.3g facade_zone_cap=2\n",
                mm3_draw_distance_scale(), scale, scale > 2.0f ? 2.0f : scale);
    }
    return scale;
}

/* Facade zones {80,280}: buildings nearer than the first get the full facade
 * path, nearer than the second the textured mid path, the rest the flat far
 * path. Both scale with the view distance, capped at 2: the mid and near
 * paths fill the facade manager's per-frame vertex pools (sized by
 * mm3_facade_capacity.c), and once a pool is full the allocator
 * (sub_00162F75) returns 0 and the building is drawn with only its dark
 * cloud-shadow pass -- the slabs seen at Montmartre (X 1298.7 Z 1167.7 facing
 * 225 degrees) when the zones were scaled alone. */
float mm3_facade_distance_scale(void)
{
    float scale = mm3_view_distance_scale();
    return scale > 2.0f ? 2.0f : scale;
}

static float s_city_stream_cap;
static float s_city_authored_distance;
static float s_haze_scale;

float mm3_city_haze_scale(void)
{
    return s_haze_scale >= 1.0f ? s_haze_scale : 1.0f;
}

/* Native fog drives the title's fog pass from the renderer; the SDL3 GPU
 * renderer always has its own pass. */
static int native_fog_on(void)
{
#ifdef XBOXRECOMP_SDL3
    return mm3_mod_native_fog();
#else
    return mm3_mod_native_fog() && nv2a_d3d_distance_fog_ready();
#endif
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
#ifndef XBOXRECOMP_SDL3
            /* MM3_FOG_REMAP=0 retains the native depth ramp for A/B tests. */
            if (!getenv("MM3_FOG_REMAP") || strcmp(getenv("MM3_FOG_REMAP"), "0"))
                nv2a_d3d_set_depth_view_remap(native_fog_on() ? 1.0f : scale, FOG_KNEE);
#endif
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
        float scale = mm3_draw_distance_scale();
        unsigned i;
        /* XBE defaults are {20,30,40,80,160}. The title's supersampled
         * passes save these effective values and restore them unchanged. */
        for (i = 0; i < 5; ++i) MEMF(0x0039ADC0u + 4 * i) *= scale;
        s_pedestrian_distances_initialized = 1;
    }
}

static int s_prop_distances_initialized;

/* Street props (cones, bollards, barrels, benches, signs: the city's breakable
 * objects) keep their 3D model to 125 m along the view, cross-fade to a flat
 * billboard until 175 m and are billboards beyond (sub_0016946B; 0x16A476
 * splits the sorted billboards at 175 m and 0x168DE5/0x168EC2/0x169171 derive
 * the fade from both). They are XBE constants no setter or Lua call reaches,
 * so props stayed at the Xbox range while everything else drew farther and
 * thin ones, the highway's white posts, faded out early. Scale them once with
 * the other object distances, keeping their 125:175 ratio. */
static void prop_distance_defaults(void)
{
    if (!s_prop_distances_initialized) {
        float scale = mm3_draw_distance_scale();
        if (MEMF(0x003A8C90u) == 125.0f && MEMF(0x003A8C94u) == 175.0f) {
            MEMF(0x003A8C90u) *= scale;
            MEMF(0x003A8C94u) *= scale;
        }
        fprintf(stderr, "[MM3_DISTANCE] street props: 3D model to %.4g m, billboards from %.4g m\n",
                MEMF(0x003A8C90u), MEMF(0x003A8C94u));
        s_prop_distances_initialized = 1;
    }
}

/* Street props also have a size cull that no distance setting reaches
 * (sub_001A67BA with flag 1, called by sub_0016AEDB at 0x16AFD1): with
 * engine.occlusion's clip distance K (+0xE0, 85 from its constructor), a prop is
 * dropped once 0.62 * depth / R > K, R being the half diagonal of its bounding
 * box, i.e. beyond K * R / 0.62 = 137 R. Thin posts and cones (R 0.4-1.2 m) vanish
 * at 55-165 m however far the 3D model/billboard ranges above are set; trees
 * and lamp posts (R 3-40 m) never meet it. The same K feeds the sphere
 * variant sub_001A65D1, but every caller passes flag 0 there. No shipped
 * script calls occlusionSetClipDistance (0x2287BA), so the constructor's
 * value is the only one. Scale it once, with the other object distances. */
extern void sub_001A6CE3_original(void);
void sub_001A6CE3(void)
{
    uint32_t self = g_edi;
    sub_001A6CE3_original();
    if (self && MEMF(self + 0xE0u) == 85.0f) {
        MEMF(self + 0xE0u) *= mm3_draw_distance_scale();
        fprintf(stderr, "[MM3_DISTANCE] small street props kept to %.4g x their radius\n",
                MEMF(self + 0xE0u) / 0.62f);
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
        prop_distance_defaults();
        MEMF(g_esp + 4) *= mm3_draw_distance_scale();
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
        for (i = 0; i < 5; ++i) MEMF(0x0039ADC0u + 4 * i) *= mm3_draw_distance_scale();
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
        MEMF(g_esp + 4) *= mm3_draw_distance_scale();
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

/* Extended visibility can exhaust the native 256-entry flare queue before
 * vehicle lamps are submitted. Scale its constructor budget so the original
 * allocator grows both the records and every buffered vertex allocation. */
extern void sub_00248518_original(void);
void sub_00248518(void)
{
    if (MEM32(g_esp) == 0x0022C89Eu && g_eax == 256u &&
        mm3_mod_on(MM3_MOD_DRAW_DISTANCE)) {
        g_eax = (uint32_t)(256.f * fmaxf(mm3_draw_distance_scale(), mm3_view_distance_scale()));
        fprintf(stderr, "[MM3_DISTANCE] flare capacity %u\n", g_eax);
    }
    sub_00248518_original();
}

extern void sub_002256C7_original(void);
static unsigned particle_budget(void)
{
    static unsigned budget;
    if (!budget)
        budget = (unsigned)recomp_env_long_in_range("MM3_DISTANCE", "MM3_PARTICLE_MAX_OVERDRAW", 24, 1, 255);
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
    if (ps && MEM32(ps + 0x524) == 12u && mm3_draw_distance_scale() > 1.0f) {
        unsigned budget = particle_budget();
        MEM32(ps + 0x524) = budget;
        fprintf(stderr, "[MM3_DISTANCE] particle max overdraw 12 -> %u\n", budget);
    }
}

/* ---- Ordered native fog, based on MattSG 73dfa55..b1b1d30. Failed
 * submissions retain the retail fog and horizon instead of silently dropping
 * either. The camera supplies the W-buffer scale and sky colour, never a fog
 * texture. ---- */
extern void sub_000F8DCE_original(void);
extern void sub_000F8750_original(void);
extern void sub_00344770(void);
static RECOMP_TLS uint32_t s_native_fog_camera;
static RECOMP_TLS int s_horizon_reported;
static unsigned s_fog_packets, s_fog_made_space, s_fog_fallbacks;
static uint64_t s_fog_window;
/* Camera cuts: fog passes since the frame began, the last frame's camera
 * rotation (rows of 3) and whether there is one. */
static RECOMP_TLS unsigned s_cut_pass;
static RECOMP_TLS int s_cut_have;
static RECOMP_TLS float s_cut_rows[9];
static unsigned s_cuts, s_cut_holds;
static volatile long s_cut_override = -1;

void mm3_native_fog_frame_begin(void)
{
    s_native_fog_camera = 0;
    /* A frame without a fog pass leaves nothing to compare with. */
    if (!s_cut_pass) s_cut_have = 0;
    s_cut_pass = 0;
}

static int fog_range(uint32_t address, uint32_t bytes)
{
    return (address >= 0x10000u && address < 0x08000000u && bytes <= 0x08000000u - address) ||
           (address >= 0x80000000u && address < 0x84000000u && bytes <= 0x84000000u - address);
}

static int fog_room(uint32_t cursor, uint32_t limit, uint32_t words)
{
    return !(cursor & 3u) && !(limit & 3u) && limit >= cursor &&
           limit - cursor >= words * 4u && fog_range(cursor, limit - cursor);
}

/* The sky's fog ramp, as the retail pass binds it: the fog object's texture
 * handle (+0x28) holds the D3D texture at +0x14 (Common, Data, Lock, Format),
 * a 64x32 A8R8G8B8 Skies/<sky>_fog.cdds in contiguous memory. */
static int fog_ramp(uint32_t object, uint32_t *texels, uint32_t *format)
{
    uint32_t handle = MEM32(object + 0x28u), texture, data, color, bytes;
    if ((handle & 3u) || !fog_range(handle, 0x18u)) return 0;
    texture = MEM32(handle + 0x14u);
    if ((texture & 3u) || !fog_range(texture, 0x10u) || (MEM32(texture) & 0x00070000u) != 0x00040000u) return 0;
    data = MEM32(texture + 4u);
    *format = MEM32(texture + 0xCu);
    color = (*format >> 8) & 0xFFu;
    if ((color != 0x06u && color != 0x07u) || ((*format >> 4) & 15u) != 2u ||
        ((*format >> 20) & 15u) > 10u || ((*format >> 24) & 15u) > 10u || data >= 0x04000000u) return 0;
    bytes = 4u << (((*format >> 20) & 15u) + ((*format >> 24) & 15u));
    *texels = 0x80000000u + data;
    return fog_range(*texels, bytes);
}

/* The title's command writers (StartPush: 0x3442A0, 0x33FF05 and 90 more)
 * write at PUT (device+0) while it is below the threshold at device+4, and
 * otherwise call MakeSpace (0x344770, stdcall minimum and requested bytes,
 * from 0x352E00). It waits for the GPU or wraps the ring and returns the new
 * PUT. Do the same when the packet does not fit before the threshold. Falling
 * back instead drew that one frame with the retail fog ramp and horizon
 * strip: distant blocks lost most of their haze and the horizon brightened,
 * the flicker in the distance seen at midday. */
static int fog_make_space(uint32_t device)
{
    uint32_t size = MEM32(0x00352E00u);
    uint32_t saved[] = {g_eax, g_ecx, g_edx, g_esp, g_ebp, g_seh_ebp};
    /* Push-buffer recording (device+8 bit 2) restarts its own buffer. */
    if ((MEM8(device + 8u) & 4u) || size < 2u * (NV2A_HOST_FOG_WORDS + NV2A_HOST_RAMP_WORDS) * 4u ||
        size > 0x01000000u || !fog_range(g_esp - 12u, 12u)) return 0;
    PUSH32(g_esp, size);
    PUSH32(g_esp, size >> 1);
    PUSH32(g_esp, 0); /* host-owned call, no guest return address */
    sub_00344770();
    g_eax = saved[0]; g_ecx = saved[1]; g_edx = saved[2];
    g_esp = saved[3]; g_ebp = saved[4]; g_seh_ebp = saved[5];
    ++s_fog_made_space;
    return 1;
}

/* Room for `words` at PUT, asking the title for space when they do not fit
 * before its threshold: the cursor, or 0. */
static uint32_t host_room(uint32_t device, uint32_t words)
{
    uint32_t cursor = MEM32(device), limit = MEM32(device + 4u);
    if (fog_room(cursor, limit, words)) return cursor;
    if ((cursor & 3u) || !fog_range(cursor, 4u) || !fog_make_space(device)) return 0;
    cursor = MEM32(device); limit = MEM32(device + 4u);
    return fog_room(cursor, limit, words) ? cursor : 0;
}

static void fog_report(void)
{
    uint64_t now = MM3_NATIVE_FOG_CLOCK();
    if (!s_fog_window) { s_fog_window = now; return; }
    if (now - s_fog_window < 30000u) return;  /* races can be under a minute */
    fprintf(stderr, "[MM3_FOG] last %.0f s: %u native fog passes, %u after asking the title for "
            "push-buffer space, %u retail fog fallbacks\n",
            (double)(now - s_fog_window) / 1000.0, s_fog_packets, s_fog_made_space, s_fog_fallbacks);
    s_fog_window = now;
    s_fog_packets = s_fog_made_space = s_fog_fallbacks = 0;
}

/* The exhaust smoke (mm3_exhaust.c) names this camera's smoke ahead
 * of its fog pass, with or without native fog. */
static void exhaust_send(void)
{
    uint32_t words[18], camera, device, cursor, i;
    if (!mm3_exhaust_enabled() || (g_ecx & 3u) || !fog_range(g_ecx, 16u)) return;
    camera = MEM32(g_ecx + 12u);
    device = MEM32(0x00351F48u);
    if ((camera & 3u) || (device & 3u) || !fog_range(device, 12u) || !mm3_exhaust_packet(camera, words))
        return;
    if (!(cursor = host_room(device, NV2A_HOST_TRIS_WORDS))) return;
    MEM32(cursor) = NV2A_HOST_TRIS_HEADER;
    MEM32(cursor + 4u) = NV2A_HOST_TRIS_TAG;
    for (i = 0; i < 18u; ++i) MEM32(cursor + 8u + i * 4u) = words[i];
    MEM32(device) = cursor + NV2A_HOST_TRIS_WORDS * 4u;
}

/* Look left/right/back is a camera cut, and MM3 draws the first frame of the
 * new view with lights and objects still chosen for the old one (light
 * buffer about half as bright, a few hundred extra objects). With the car
 * lighting's fields that frame flashes in tunnels; the next is right. The
 * first fog pass of each frame compares its camera's rotation with the last
 * frame's, and past 45 degrees (no camera turns that fast) asks the renderer
 * to keep the frame off the screen (NV2A_HOST_HOLD_TAG): the old view stays
 * up one refresh longer. Part of the car lighting; MM3_CAMERA_CUT_HOLD=0
 * turns it off and the dev pipe's `cuthold` overrides both (cuts are still
 * counted, and logged with MM3_CAMERA_CUT_LOG=1). */
#define CUT_COSINE 0.70710678f

void mm3_native_fog_cut_hold(long on)
{
    s_cut_override = on;
}

static int cut_hold_on(void)
{
    static RECOMP_TLS int setting = -1;
    long override = s_cut_override;
    if (override >= 0) return override != 0;
    if (setting < 0) {
        const char *value = getenv("MM3_CAMERA_CUT_HOLD");
        setting = mm3_mod_car_lighting() && !(value && !strcmp(value, "0"));
    }
    return setting;
}

/* The camera's world-to-camera rotation, when it is one. */
static int cut_rows(uint32_t camera, float rows[9])
{
    unsigned i, j, k;
    for (i = 0; i < 9u; ++i) {
        rows[i] = MEMF(camera + 0x1F24u + (i / 3u) * 16u + (i % 3u) * 4u);
        if (!isfinite(rows[i])) return 0;
    }
    for (i = 0; i < 3u; ++i)
        for (j = 0; j < 3u; ++j) {
            float dot = 0.0f;
            for (k = 0; k < 3u; ++k) dot += rows[i * 3u + k] * rows[j * 3u + k];
            if (fabsf(dot - (i == j ? 1.0f : 0.0f)) > 0.01f) return 0;
        }
    return 1;
}

/* MM3_CAMERA_CUT_LOG=1 names every cut, held or not, with its QPC time (the
 * clock of [ODD_FRAME]). */
static int cut_log(void)
{
    static int on = -1;
    if (on < 0) {
        const char *value = getenv("MM3_CAMERA_CUT_LOG");
        on = value && !strcmp(value, "1");
    }
    return on;
}

static double cut_qpc_ms(void)
{
    LARGE_INTEGER now, frequency;
    return QueryPerformanceCounter(&now) && QueryPerformanceFrequency(&frequency) && frequency.QuadPart ?
        (double)now.QuadPart * 1000.0 / (double)frequency.QuadPart : 0.0;
}

static void cut_send(void)
{
    uint32_t camera, device, cursor = 0, i;
    float rows[9], trace = 0.0f, cosine;
    int hold;
    if ((g_ecx & 3u) || !fog_range(g_ecx, 16u)) { s_cut_have = 0; return; }
    camera = MEM32(g_ecx + 12u);
    if ((camera & 3u) || !fog_range(camera, 0x1F60u) || !cut_rows(camera, rows)) { s_cut_have = 0; return; }
    for (i = 0; i < 9u; ++i) trace += rows[i] * s_cut_rows[i];
    memcpy(s_cut_rows, rows, sizeof rows);
    /* trace(old^T new) = 1 + 2 cos(angle between the two) */
    cosine = (trace - 1.0f) * 0.5f;
    if (!s_cut_have) { s_cut_have = 1; return; }
    if (cosine >= CUT_COSINE) return;
    ++s_cuts;
    device = MEM32(0x00351F48u);
    /* Never into a recorded push buffer (device+8 bit 2): it is replayed. */
    hold = cut_hold_on() && !(device & 3u) && fog_range(device, 12u) && !(MEM8(device + 8u) & 4u) &&
           (cursor = host_room(device, NV2A_HOST_HOLD_WORDS)) != 0;
    if (hold) {
        MEM32(cursor) = NV2A_HOST_HOLD_HEADER;
        MEM32(cursor + 4u) = NV2A_HOST_HOLD_TAG;
        MEM32(device) = cursor + NV2A_HOST_HOLD_WORDS * 4u;
        ++s_cut_holds;
    }
    if (cut_log() || (hold && (s_cut_holds <= 3u || s_cut_holds % 100u == 0)))
        fprintf(stderr, "[MM3_CUT] camera cut %u (%.0f degrees, qpc_ms=%.1f): %s\n", s_cuts,
                acosf(fmaxf(cosine, -1.0f)) * 57.29578f, cut_qpc_ms(),
                hold ? "its first frame is held back" : "shown");
}

void sub_000F8DCE(void)
{
    uint32_t camera, device, cursor, texels = 0, format = 0, words;
    float depth_scale, row, far_clip, project_x, project_y, inverse_depth, start, end, plane[3];
    s_native_fog_camera = 0;
    if (!s_cut_pass++) cut_send();
    exhaust_send();
    if (!native_fog_on()) {
        sub_000F8DCE_original();
        return;
    }
    fog_report();
    if ((g_ecx & 3u) || !fog_range(g_ecx, 16u) || !fog_range(g_esp, 16u)) goto fallback;
    camera = MEM32(g_ecx + 12u);
    device = MEM32(0x00351F48u);
    if ((camera & 3u) || (device & 3u) ||
        !fog_range(camera, 0x1FB4u) || !fog_range(device, 12u)) goto fallback;
    depth_scale = MEMF(camera + 0x1ED8u);
    far_clip = MEMF(camera + 0x1FB0u);
    project_x = MEMF(camera + 0x1EE4u);
    project_y = MEMF(camera + 0x1EF8u);
    if (!isfinite(depth_scale) || depth_scale <= 0 || !isfinite(far_clip) || far_clip <= 0 ||
        !isfinite(project_x) || project_x == 0 || !isfinite(project_y) || project_y == 0) goto fallback;
    inverse_depth = 1.0f / depth_scale;
    plane[0] = MEMF(camera + 0x1F34u) * depth_scale / project_x;
    plane[1] = MEMF(camera + 0x1F38u) * depth_scale / project_y;
    plane[2] = MEMF(camera + 0x1F3Cu);
    start = fminf(300.0f, far_clip * 0.4f);
    end = far_clip * 0.9f;
    if (!isfinite(inverse_depth) || inverse_depth <= 0 ||
        !isfinite(inverse_depth * 16777216.0f) || end <= start || !isfinite(1.0f / (end - start)) ||
        !isfinite(plane[0]) || !isfinite(plane[1]) || !isfinite(plane[2]) ||
        fabsf(plane[0]) > 1.0e6f || fabsf(plane[1]) > 1.0e6f || fabsf(plane[2]) > 1.0e6f) goto fallback;

    /* The ramp's row is the pass's own texture coordinate (second argument,
     * stored as each vertex's V); its texels go with the packet. */
    row = MEMF(g_esp + 8u);
    if (!MM3_SKY_RAMP || !fog_ramp(g_ecx, &texels, &format) || !isfinite(row)) texels = 0;
    words = NV2A_HOST_FOG_WORDS + (texels ? NV2A_HOST_RAMP_WORDS : 0u);

    /* Reserve and validate the complete packet before touching its cursor or
     * guest registers. Its no-op encoding preserves pushbuffer ordering. */
    if (!(cursor = host_room(device, words))) goto fallback;
    if (texels) {
        MEM32(cursor) = NV2A_HOST_RAMP_HEADER;
        MEM32(cursor + 4u) = NV2A_HOST_RAMP_TAG;
        MEM32(cursor + 8u) = texels;
        MEM32(cursor + 12u) = format;
        MEMF(cursor + 16u) = row;
        MEMF(cursor + 20u) = mm3_city_haze_scale();
        MEMF(cursor + 24u) = MM3_FOG_RAMP_KNEE;
        cursor += NV2A_HOST_RAMP_WORDS * 4u;
    }
    MEM32(cursor) = NV2A_HOST_FOG_HEADER;
    MEM32(cursor + 4u) = NV2A_HOST_FOG_TAG;
    MEMF(cursor + 8u) = inverse_depth;
    MEMF(cursor + 12u) = far_clip;
    MEMF(cursor + 16u) = start;
    MEMF(cursor + 20u) = end;
    MEM32(cursor + 24u) = MEM32(g_esp + 4u);
    MEM32(cursor + 28u) = NV2A_HOST_FOG_SKY_MASK | NV2A_HOST_FOG_SKY_FILL | NV2A_HOST_FOG_SKY_FADE;
    /* World-up dotted with a camera ray: pitch/roll and projection follow
     * the native cloud dome. Fade its lower edge over about nine degrees. */
    MEMF(cursor + 32u) = plane[0]; MEMF(cursor + 36u) = plane[1]; MEMF(cursor + 40u) = plane[2];
    MEMF(cursor + 44u) = 6.25f;
    MEM32(device) = cursor + NV2A_HOST_FOG_WORDS * 4u;
    s_native_fog_camera = camera;
    ++s_fog_packets;
    g_eax = (g_eax & 0xFFFFFF00u) | 1u;
    g_esp += 16u;
    return;
fallback:
    ++s_fog_fallbacks;
    sub_000F8DCE_original();
}

/* HorizonMap.dma is a separate low-resolution city panorama, not depth fog.
 * Keep Atmosphere.dma (cloud sky) and the host depth fog; omit this strip. */
void sub_000F8750(void)
{
    /* The horizon object stores its camera at +0x28. Remove only the strip
     * belonging to a camera whose native fog packet was accepted. Cloud sky
     * composition remains on the game's own path. */
    if (!native_fog_on() ||
        (g_ecx & 3u) || !fog_range(g_ecx, 0x2Cu) || !s_native_fog_camera ||
        MEM32(g_ecx + 0x28u) != s_native_fog_camera) {
        sub_000F8750_original();
        return;
    }
    s_native_fog_camera = 0;
    if (!s_horizon_reported) {
        s_horizon_reported = 1;
        fprintf(stderr, "[MM3_FOG] horizon strip replaced for camera=%08X\n", MEM32(g_ecx + 0x28u));
    }
    g_eax = (g_eax & 0xFFFFFF00u) | 1u;
    g_esp += 8u;
}
