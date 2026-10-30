#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

/* Adaptive gameplay view. MM3 has no 16:9 mode: every 3D camera builds its
 * projection through 0x25968E, D3DXMatrixPerspectiveFovLH(out, fovy, aspect,
 * zn, zf), with aspect read from the 4:3 constant at 0x385F20. Replacing that
 * argument with the display aspect preserves the vertical view on wider
 * displays and the horizontal view on taller ones. Culling uses that same
 * frustum, so nothing pops in at the new screen edges. The renderer sizes
 * display surfaces to that aspect and keeps 2D overlays at their own proportions
 * (nv2a_d3d11.c). Other aspects (square environment maps) are left alone.
 * The 0x385F20 constant itself is also read as a 4/3 scale by unrelated code
 * (0x220B1E), which is why the argument is patched rather than the data. */
extern void sub_0025968E_original(void);
extern float nv2a_d3d_display_aspect(void);
extern void nv2a_d3d_set_edge_hud(int on);
extern void nv2a_d3d_set_hud_start(float x0, float y0, float x1, float y1);
extern void nv2a_d3d_set_centered_ui_query(int (*query)(void));
extern void nv2a_d3d_set_map_texture_query(int (*query)(uint32_t texture_addr));
extern void nv2a_d3d_set_minimap_style_query(int (*query)(void));
extern void nv2a_d3d_set_minimap_motion_query(float (*query)(float, float));
extern void nv2a_d3d_set_minimap_expansion_query(float (*query)(float, float));
extern int mm3_minimap_improved(void);
extern float mm3_minimap_motion(float, float);
extern float mm3_minimap_expansion(float, float);
static float camera_fov_degrees(void);

/* Identities only, used on the title thread. Never follow a cached pointer:
 * refresh only the live camera supplied to its native per-frame view update.
 * A menu projection using the same allocation retires its race identity. */
enum { DISPLAY_CAMERA_COUNT = 16 };
static uint32_t s_display_cameras[DISPLAY_CAMERA_COUNT];
static unsigned s_display_camera_next;

static int display_camera_slot(uint32_t camera)
{
    unsigned i;
    for (i = 0; i < DISPLAY_CAMERA_COUNT; ++i)
        if (s_display_cameras[i] == camera && camera) return (int)i;
    return -1;
}

static void remember_display_camera(uint32_t camera)
{
    unsigned i;
    if (!camera || display_camera_slot(camera) >= 0) return;
    for (i = 0; i < DISPLAY_CAMERA_COUNT; ++i)
        if (!s_display_cameras[i]) { s_display_cameras[i] = camera; return; }
    /* Native split screen has at most four current display cameras. Reuse
     * the oldest identity instead of keeping dead level allocations forever;
     * cached identities are never dereferenced, including when evicted. */
    s_display_cameras[s_display_camera_next++ % DISPLAY_CAMERA_COUNT] = camera;
}

extern void sub_000F4AC3(void);
extern void sub_000F4BB1_original(void);

void sub_000F4BB1(void)
{
    /* Camera view update, called before native visibility and GPU matrices
     * are published. Rebuild via the retail setup so matrix, frustum planes,
     * near/far state and device projection all agree after a live resize. */
    uint32_t camera = g_edi;
    if (display_camera_slot(camera) >= 0) {
        float old_aspect = MEMF(camera + 0x1FA8);
        float aspect = nv2a_d3d_display_aspect();
        float fovy = MEMF(camera + 0x1FA4);
        if (isfinite(old_aspect) && old_aspect > 0.0f && isfinite(fovy) &&
            fovy > 0.0f && fovy < 3.14159265f && fabsf(aspect - old_aspect) > 1e-4f) {
            uint32_t saved[] = {g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi,
                                g_edi, g_ebp, g_seh_ebp};
            double fp[8];
            int top = g_fp_top, cmp = g_fp_cmp, df = g_df;
            uint16_t cc = g_fp_cc, control = g_fp_control_word;
            uint32_t fs = g_fs_base;
            RecompXmm simd[8] = {g_xmm0, g_xmm1, g_xmm2, g_xmm3,
                                  g_xmm4, g_xmm5, g_xmm6, g_xmm7};
            float old_basis = fminf(old_aspect, 4.0f / 3.0f);
            float new_basis = fminf(aspect, 4.0f / 3.0f);
            /* The explicit FOV override retains its existing absolute
             * horizontal-degree meaning; the stock view is Hor+/Vert+. */
            if (camera_fov_degrees()) { old_basis = old_aspect; new_basis = aspect; }
            fovy = 2.0f * atanf(tanf(fovy * 0.5f) * old_basis / new_basis);
            memcpy(fp, g_fp_stack, sizeof fp);
            PUSH32(g_esp, MEM32(camera + 0x1FB0));
            PUSH32(g_esp, MEM32(camera + 0x1FAC));
            PUSH32(g_esp, 0); MEMF(g_esp) = aspect;
            PUSH32(g_esp, 0); MEMF(g_esp) = fovy;
            PUSH32(g_esp, 0); /* host-owned call, no guest return address */
            g_esi = camera;
            g_df = 0;
            sub_000F4AC3();
            g_eax = saved[0]; g_ecx = saved[1]; g_edx = saved[2];
            g_esp = saved[3]; g_ebx = saved[4]; g_esi = saved[5];
            g_edi = saved[6]; g_ebp = saved[7]; g_seh_ebp = saved[8];
            memcpy(g_fp_stack, fp, sizeof fp);
            g_fp_top = top; g_fp_cmp = cmp; g_df = df;
            g_fp_cc = cc; g_fp_control_word = control;
            g_fs_base = fs;
            g_xmm0 = simd[0]; g_xmm1 = simd[1]; g_xmm2 = simd[2]; g_xmm3 = simd[3];
            g_xmm4 = simd[4]; g_xmm5 = simd[5]; g_xmm6 = simd[6]; g_xmm7 = simd[7];
        }
    }
    sub_000F4BB1_original();
}

/* MmxGameMapNode owns separate map/icon textures, not the text/HUD atlas.
 * Cache only their Data addresses while the title owns the resources. The
 * pushbuffer runs asynchronously: it must never follow a saved guest node or
 * descriptor pointer, nor wait for the title thread to release a lock. */
enum { MAP_OWNER_COUNT = 16, MAP_TEXTURE_COUNT = 10 };
static struct {
    uint32_t owner; /* Title thread only; an identity, never dereferenced. */
    volatile uint32_t sequence;
    volatile uint32_t textures[MAP_TEXTURE_COUNT];
} s_map_owners[MAP_OWNER_COUNT];

static uint32_t map_atomic_read(volatile uint32_t *word)
{
    return RECOMP_ATOMIC_ADD32(word, 0);
}

static void map_publish(unsigned slot, uint32_t owner, const uint32_t *textures)
{
    unsigned i;
    RECOMP_ATOMIC_ADD32(&s_map_owners[slot].sequence, 1); /* odd: unpublished */
    for (i = 0; i < MAP_TEXTURE_COUNT; ++i)
        RECOMP_ATOMIC_CAS32(&s_map_owners[slot].textures[i],
                           map_atomic_read(&s_map_owners[slot].textures[i]),
                           textures ? textures[i] : 0);
    s_map_owners[slot].owner = owner;
    RECOMP_ATOMIC_ADD32(&s_map_owners[slot].sequence, 1); /* even: complete */
}

static void map_forget(uint32_t owner)
{
    unsigned i;
    for (i = 0; i < MAP_OWNER_COUNT; ++i)
        if (s_map_owners[i].owner == owner && owner)
            map_publish(i, 0, NULL);
}

static int map_texture_role(uint32_t address)
{
    unsigned i, j;
    /* NV2A dma_resolve maps physical contiguous Data to its 0x80000000
     * mirror. Compare the same allocation in either representation. */
    address &= 0x7FFFFFFFu;
    if (!address) return 0;
    for (i = 0; i < MAP_OWNER_COUNT; ++i) {
        uint32_t before = map_atomic_read(&s_map_owners[i].sequence);
        int role = 0;
        if (!before || (before & 1) || !map_atomic_read(&s_map_owners[i].textures[0]))
            continue;
        for (j = 0; j < MAP_TEXTURE_COUNT; ++j)
            if (map_atomic_read(&s_map_owners[i].textures[j]) == address) {
                role = j ? 2 : 1;
                break;
            }
        /* A racing load/reconfiguration is unclassified for this draw. No
         * retry loop, torn generation, or stale guest pointer is possible. */
        if (role && before == map_atomic_read(&s_map_owners[i].sequence))
            return role;
    }
    return 0;
}

extern void sub_0011C742_original(void);
void sub_0011C742(void)
{
    static const unsigned offsets[MAP_TEXTURE_COUNT] = {
        0x08, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30
    };
    uint32_t owner = g_ecx, textures[MAP_TEXTURE_COUNT] = {0};
    unsigned i;
    map_forget(owner);
    sub_0011C742_original();
    if (!LO8(g_eax) || !MEM32(owner + 0x64)) return;
    /* Native 11C742 has just loaded Maps/<city>+MapMask, Checkpoint, You,
     * TagMagIcon, Shield, MapCircle, MapNorth and three arrow resources.
     * 116509/1164F7 establish resource+24 -> descriptor; 11D4AB establishes
     * descriptor+14 -> D3DTexture. Its +4 is the allocation's Data address. */
    for (i = 0; i < MAP_TEXTURE_COUNT; ++i) {
        uint32_t resource = MEM32(owner + offsets[i]);
        uint32_t descriptor = resource ? MEM32(resource + 0x24) : 0;
        uint32_t texture = descriptor ? MEM32(descriptor + 0x14) : 0;
        textures[i] = texture ? MEM32(texture + 4) & 0x7FFFFFFFu : 0;
    }
    if (!textures[0]) return;
    for (i = 0; i < MAP_OWNER_COUNT; ++i)
        if (!s_map_owners[i].owner) {
            map_publish(i, owner, textures);
            return;
        }
    /* More than 16 simultaneous map nodes: retain existing owners rather
     * than guessing which guest allocation is safe to replace. */
}

extern void sub_0011CAAC_original(void);
void sub_0011CAAC(void)
{
    map_forget(g_ecx); /* Reconfiguration may replace this node's resources. */
    sub_0011CAAC_original();
}

extern void sub_0007CEC3_original(void);
void sub_0007CEC3(void)
{
    /* Shared deleting destructor used by the map vtable at 37D790. Covers
     * direct deletion; 11CAAC separately covers configuration replacement. */
    map_forget(g_ecx);
    sub_0007CEC3_original();
}

static float camera_fov_degrees(void)
{
    static int loaded;
    static float degrees;
    if (!loaded) {
        const char *env = getenv("MM3_FOV_DEGREES");
        if (env && *env) {
            char *end;
            float value = strtof(env, &end);
            if (end == env || *end || !isfinite(value) || (value != 0.0f && value < 60.0f) || value > 110.0f)
                fprintf(stderr, "[MM3_CAMERA] invalid MM3_FOV_DEGREES; expected 60..110, using stock FOV\n");
            else {
                degrees = value;
                fprintf(stderr, "[MM3_CAMERA] horizontal field of view %.3g degrees\n", degrees);
            }
        }
        loaded = 1;
    }
    return degrees;
}

/* The near chase sits 0.25 m farther back with the Improved look; the
 * Original look (MM3_IMPROVED=0) keeps the Xbox camera. */
static int chase_adjusted(void)
{
    static int adjusted = -1;
    if (adjusted < 0) {
        const char *look = getenv("MM3_IMPROVED");
        adjusted = !(look && !strcmp(look, "0"));
    }
    return adjusted;
}

extern void sub_001EEC53_original(void);
void sub_001EEC53(void)
{
    /* Native camera mode 5 (001EF926's dispatch table at 001EFAFE) calls
     * 001EF87E, which supplies the near chase's two rearward eye vectors.
     * Mode 7 (far chase) instead returns to 001EF7DC. Adjust only this
     * call's Z arguments, before the original speed interpolation, damping
     * and collision solver; no vehicle parameters or other views change. */
    if (MEM32(g_esp) == 0x001EF922u && chase_adjusted()) {
        float start_z = MEMF(g_esp + 0x14), end_z = MEMF(g_esp + 0x20);
        if (isfinite(start_z) && isfinite(end_z) && start_z < 0.0f && end_z < 0.0f) {
            MEMF(g_esp + 0x14) = start_z - 0.25f;
            MEMF(g_esp + 0x20) = end_z - 0.25f;
        }
    }
    sub_001EEC53_original();
}

static int pause_ui_active(void)
{
    /* PAL pause singleton (001E25C1): open 001E2CD5 sets +2 and resume
     * 001E2BDA clears it. Its controls use several textures/draws, all in
     * the same 4:3 layout; they must not get individual race-HUD anchors.
     * The pushbuffer thread reads the current state without guest calls. */
    return *(const volatile uint8_t *)XBOX_PTR(0x00429DB2u) != 0;
}

void mm3_widescreen_init(void)
{
    /* Register after the XBE is mapped, before the renderer thread starts. */
    nv2a_d3d_set_centered_ui_query(pause_ui_active);
    nv2a_d3d_set_map_texture_query(map_texture_role);
    nv2a_d3d_set_minimap_style_query(mm3_minimap_improved);
    nv2a_d3d_set_minimap_motion_query(mm3_minimap_motion);
    nv2a_d3d_set_minimap_expansion_query(mm3_minimap_expansion);
}

void sub_0025968E(void)
{
    float aspect = MEMF(g_esp + 0xC), wide = nv2a_d3d_display_aspect();
    int camera_setup = MEM32(g_esp) == 0x000F4AF5u;
    int race = camera_setup && MEM32(g_ebp + 4) == 0x00210E1Eu;
    int menu = camera_setup && MEM32(g_ebp + 4) == 0x000E51EEu;
    int patch = !menu && fabsf(aspect - 4.0f / 3.0f) < 1e-4f && fabsf(wide - aspect) > 1e-4f;
    /* renderSetFov derives fovy from the aspect the camera last stored. A
     * shape left over from an earlier window (resized, or still sizing while
     * the race loaded) is neither 4:3 nor the current one: without this the
     * whole race kept the narrowed Vert- view, 25% zoomed in, with the
     * checkpoint arrow pushed off the top of the screen. The constructor's
     * square placeholder (1.0) is not a display shape and stays alone. */
    int stale = race && isfinite(aspect) && aspect >= 0.5f && aspect <= 4.0f &&
        fabsf(aspect - 1.0f) >= 1e-4f && fabsf(aspect - 4.0f / 3.0f) >= 1e-4f &&
        fabsf(aspect - wide) >= 1e-4f;
    if (menu) {
        memset(s_display_cameras, 0, sizeof s_display_cameras);
        s_display_camera_next = 0;
        /* Turntables retain their authored projection. The renderer places
         * their 3D view and all menu artwork in the same fitted 4:3 area. */
        MEMF(g_esp + 0xC) = MEMF(g_ebp + 0xC) = 4.0f / 3.0f;
    }
    else if (camera_setup && MEM32(g_ebp + 4) == 0x000F4D45u) {
        /* The native constructor establishes a square placeholder before a
         * camera is configured. A reused allocation is a new identity. */
        int slot = display_camera_slot(g_esi);
        if (slot >= 0) s_display_cameras[slot] = 0;
    }
    if (patch)
        MEMF(g_esp + 0xC) = wide;
    /* Called from the camera's projection setup (0xF4AC3, returning to
     * 0xF4AF5), which afterwards stores its own fov/aspect arguments at
     * +0x1FA4/+0x1FA8 of the camera. The city's visibility culling reads them
     * back, so widen that argument too or blocks beyond the 4:3 frustum
     * vanish at the sides of the wider view. Its frame is published in g_ebp. */
    if (patch && MEM32(g_esp) == 0x000F4AF5u &&
        fabsf(MEMF(g_ebp + 0xC) - 4.0f / 3.0f) < 1e-4f)
        MEMF(g_ebp + 0xC) = wide;
    if (stale)
        MEMF(g_esp + 0xC) = MEMF(g_ebp + 0xC) = wide;
    /* The race camera (0x210D6F, calling the setup with return 0x210E1E)
     * derives fovy from a horizontal field of view and that stored aspect:
     * fovy = 2 atan(tan(hfov / 2) / aspect). With the stored aspect widened
     * that narrows fovy (Vert-, a zoom). Undo it so the vertical view stays
     * the 4:3 one, for the matrix and for the copy stored for culling.
     * It is also how the renderer knows a race has started, so its HUD can
     * move out to the screen edges (nv2a_d3d11.c); the race camera builds its
     * projection once per race, car select's (0xE51EE) every frame. The live
     * view-update hook above rebuilds race projections after shape changes. */
    if (race) {
        /* The race HUD starts with the minimap's base quad; 2D drawn ahead of
         * it is the name tags over cars, positioned through this camera. */
        nv2a_d3d_set_hud_start(50.0f, 320.0f, 178.0f, 448.0f);
        nv2a_d3d_set_edge_hud(1);
        if (fabsf(aspect - 4.0f / 3.0f) < 1e-4f || fabsf(aspect - wide) < 1e-4f || stale)
            remember_display_camera(g_esi);
    }
    else if (MEM32(g_esp) == 0x000F4AF5u && MEM32(g_ebp + 4) == 0x000E51EEu)
        nv2a_d3d_set_edge_hud(0);              /* car select's turntable */
    if (race) {
        float degrees = camera_fov_degrees();
        if (degrees) {
            /* Absolute horizontal degrees at the actual render aspect.
             * The matrix and the camera's visibility frustum must agree.
             * This explicit renderSetFov call excludes menu turntables,
             * mirrors, cubemaps and temporary projection save/restores. */
            float fovy = 2.0f * atanf(tanf(degrees * (3.14159265358979323846f / 360.0f)) /
                                     MEMF(g_esp + 0xC));
            MEMF(g_esp + 8) = fovy;
            MEMF(g_ebp + 8) = fovy;
        } else if (fabsf(aspect - 4.0f / 3.0f) < 1e-4f || fabsf(aspect - wide) < 1e-4f || stale) {
            /* Undo the native horizontal-to-vertical conversion against the
             * stored aspect it used. Wider windows keep vertical coverage;
             * taller windows keep horizontal coverage and reveal more sky. */
            float fovy = 2.0f * atanf(tanf(MEMF(g_esp + 8) * 0.5f) * aspect /
                                     fminf(wide, 4.0f / 3.0f));
            MEMF(g_esp + 8) = fovy;
            MEMF(g_ebp + 8) = fovy;
        }
    }
    sub_0025968E_original();
    /* Native D3DX consumes all five arguments (ret 20). Nothing remains
     * to restore at the advanced stack pointer. The camera setup copies
     * its separate frame arguments into +1FA4/+1FA8 for visibility. */
}
