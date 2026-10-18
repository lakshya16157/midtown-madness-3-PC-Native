/* Ordered native fog, based on MattSG 73dfa55..b1b1d30. Failed submissions
 * retain the retail fog and horizon instead of silently dropping either. */
#include <math.h>
#include <stdio.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "nv2a_host_commands.h"

extern void sub_000F8DCE_original(void);
extern void sub_000F8750_original(void);
extern int nv2a_d3d_distance_fog_ready(void);
static RECOMP_TLS uint32_t s_native_fog_camera;
static RECOMP_TLS int s_horizon_reported;

void mm3_native_fog_frame_begin(void)
{
    s_native_fog_camera = 0;
}

static int fog_range(uint32_t address, uint32_t bytes)
{
    return (address >= 0x10000u && address < 0x08000000u && bytes <= 0x08000000u - address) ||
           (address >= 0x80000000u && address < 0x84000000u && bytes <= 0x84000000u - address);
}

void sub_000F8DCE(void)
{
    uint32_t camera, device, cursor, limit;
    float depth_scale, far_clip, project_x, project_y, inverse_depth, start, end, plane[3];
    s_native_fog_camera = 0;
    if (!mm3_mod_native_fog() || !nv2a_d3d_distance_fog_ready() ||
        (g_ecx & 3u) || !fog_range(g_ecx, 16u) || !fog_range(g_esp, 16u)) goto fallback;
    camera = MEM32(g_ecx + 12u);
    device = MEM32(0x00351F48u);
    if ((camera & 3u) || (device & 3u) ||
        !fog_range(camera, 0x1FB4u) || !fog_range(device, 8u)) goto fallback;
    cursor = MEM32(device); limit = MEM32(device + 4u);
    if ((cursor & 3u) || (limit & 3u) || limit < cursor ||
        limit - cursor < NV2A_HOST_FOG_WORDS * 4u ||
        !fog_range(cursor, limit - cursor)) goto fallback;
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

    /* Reserve and validate the complete packet before touching its cursor or
     * guest registers. Its no-op encoding preserves pushbuffer ordering. */
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
    g_eax = (g_eax & 0xFFFFFF00u) | 1u;
    g_esp += 16u;
    return;
fallback:
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
