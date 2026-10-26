/* Ordered native fog, based on MattSG 73dfa55..b1b1d30. Failed submissions
 * retain the retail fog and horizon instead of silently dropping either. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "nv2a_host_commands.h"
#include "mm3_render_distance.h"
#include "mm3_exhaust.h"

#ifndef MM3_NATIVE_FOG_CLOCK
#define MM3_NATIVE_FOG_CLOCK() GetTickCount64()
#endif

extern void sub_000F8DCE_original(void);
extern void sub_000F8750_original(void);
extern void sub_00344770(void);
extern int nv2a_d3d_distance_fog_ready(void);
static RECOMP_TLS uint32_t s_native_fog_camera;
static RECOMP_TLS int s_horizon_reported;
static unsigned s_fog_packets, s_fog_made_space, s_fog_fallbacks;
static uint64_t s_fog_window;

void mm3_native_fog_frame_begin(void)
{
    s_native_fog_camera = 0;
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
    uint32_t words[18], camera, device, cursor, limit, i;
    if (!mm3_exhaust_enabled() || (g_ecx & 3u) || !fog_range(g_ecx, 16u)) return;
    camera = MEM32(g_ecx + 12u);
    device = MEM32(0x00351F48u);
    if ((camera & 3u) || (device & 3u) || !fog_range(device, 12u) || !mm3_exhaust_packet(camera, words))
        return;
    cursor = MEM32(device); limit = MEM32(device + 4u);
    if (!fog_room(cursor, limit, NV2A_HOST_TRIS_WORDS)) {
        if ((cursor & 3u) || !fog_range(cursor, 4u) || !fog_make_space(device)) return;
        cursor = MEM32(device); limit = MEM32(device + 4u);
        if (!fog_room(cursor, limit, NV2A_HOST_TRIS_WORDS)) return;
    }
    MEM32(cursor) = NV2A_HOST_TRIS_HEADER;
    MEM32(cursor + 4u) = NV2A_HOST_TRIS_TAG;
    for (i = 0; i < 18u; ++i) MEM32(cursor + 8u + i * 4u) = words[i];
    MEM32(device) = cursor + NV2A_HOST_TRIS_WORDS * 4u;
}

void sub_000F8DCE(void)
{
    uint32_t camera, device, cursor, limit, texels = 0, format = 0, words;
    float depth_scale, row, far_clip, project_x, project_y, inverse_depth, start, end, plane[3];
    s_native_fog_camera = 0;
    exhaust_send();
    if (!mm3_mod_native_fog() || !nv2a_d3d_distance_fog_ready()) {
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
    if (!fog_ramp(g_ecx, &texels, &format) || !isfinite(row)) texels = 0;
    words = NV2A_HOST_FOG_WORDS + (texels ? NV2A_HOST_RAMP_WORDS : 0u);

    /* Reserve and validate the complete packet before touching its cursor or
     * guest registers. Its no-op encoding preserves pushbuffer ordering. */
    cursor = MEM32(device); limit = MEM32(device + 4u);
    if (!fog_room(cursor, limit, words)) {
        if ((cursor & 3u) || !fog_range(cursor, 4u) || !fog_make_space(device)) goto fallback;
        cursor = MEM32(device); limit = MEM32(device + 4u);
        if (!fog_room(cursor, limit, words)) goto fallback;
    }
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

void sub_000F8750(void)
{
    /* The horizon object stores its camera at +0x28. Remove only the strip
     * belonging to a camera whose native fog packet was accepted. Cloud sky
     * composition remains on the game's own path. */
    if (!mm3_mod_native_fog() || !nv2a_d3d_distance_fog_ready() ||
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
