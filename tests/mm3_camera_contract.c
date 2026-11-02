/* Native caller/matrix integration contract for the production camera hooks.
 * The common chase solver is a capture boundary: its interpolation/collision
 * logic remains retail. Everything feeding it is the real generated code. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;
RECOMP_TLS uint32_t g_fs_base;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
static unsigned char memory[0x440000];
static const uint32_t stack = 0x80000, camera = 0x10000, params = 0x14000;
static uint32_t captured[14];
static unsigned solver_calls, hud_calls, edge_calls;
static int edge_value;
static float display_aspect;
static unsigned view_updates;

float nv2a_d3d_display_aspect(void) { return display_aspect; }
void mm3_hud_set_edge(int on) { ++edge_calls; edge_value = on; }
void mm3_hud_set_start(float a, float b, float c, float d) {
    assert(a == 50 && b == 320 && c == 178 && d == 448); ++hud_calls;
}
/* The race HUD hooks: the widescreen source calls them through whichever name its owner uses. */
void nv2a_d3d_set_edge_hud(int on) { mm3_hud_set_edge(on); }
void nv2a_d3d_set_hud_start(float a, float b, float c, float d) { mm3_hud_set_start(a, b, c, d); }
void nv2a_d3d_set_centered_ui_query(int (*query)(void)) { (void)query; }
void nv2a_d3d_set_map_texture_query(int (*query)(uint32_t)) { (void)query; }
int mm3_minimap_improved(void) { return 1; }
void nv2a_d3d_set_minimap_style_query(int (*query)(void)) { (void)query; }
float mm3_minimap_motion(float cx, float cy) { (void)cx; (void)cy; return 0; }
void nv2a_d3d_set_minimap_motion_query(float (*query)(float, float)) { (void)query; }
float mm3_minimap_expansion(float cx, float cy) { (void)cx; (void)cy; return 0; }
void nv2a_d3d_set_minimap_expansion_query(float (*query)(float, float)) { (void)query; }
void nv2a_d3d_set_split_screen_query(int (*query)(void)) { (void)query; }
void sub_0011C742_original(void) { g_esp += 8; }
void sub_0011CAAC_original(void) { g_esp += 12; }
void sub_0007CEC3_original(void) { g_esp += 8; }
void sub_000F4BB1_original(void) { ++view_updates; g_esp += 4; }
void sub_001EEC53_original(void) {
    ++solver_calls;
    memcpy(captured, (const void *)XBOX_PTR(g_esp + 4), sizeof captured);
    g_esp += 60; /* Native RET 56. */
}
#include "mods.h"
int mm3_mod_on(int mod) {
    const char *setting = getenv(mod == MM3_MOD_WIDESCREEN ? "MM3_MOD_WIDESCREEN" : "MM3_MOD_FOV");
    return !setting || strcmp(setting, "0") != 0;
}
#define sub_000F4AC3 bridge_projection_setup
#include "production_widescreen.c"
#undef sub_000F4AC3
#include "camera_math.c"
#include "slot_publish.c"

#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
#include "native_camera.inc"

void bridge_projection_setup(void) {
    RecompXmm zero = {0};
    native_projection_setup();
    /* Helpers reached by an injected guest call may clobber caller-saved
     * SIMD registers and TLS context; the surrounding frame must retain all. */
    g_fs_base=0; g_xmm0=zero; g_xmm1=zero; g_xmm2=zero; g_xmm3=zero;
    g_xmm4=zero; g_xmm5=zero; g_xmm6=zero; g_xmm7=zero;
}

static void close_float(float got, float expected) {
    if (!isfinite(got) || fabsf(got - expected) > 0.00003f) {
        fprintf(stderr, "camera mismatch: got %.9g expected %.9g\n", got, expected);
        abort();
    }
}
static float number(uint32_t word) { float f; memcpy(&f, &word, 4); return f; }

static void reset_call(void) {
    g_esp = stack; g_ebp = 0x70000;
    g_ecx = camera; g_esi = camera; g_ebx = 0x13579; g_edi = 0x24680;
    g_fp_top = 0; g_fp_control_word = 0x37F; g_df = 0;
    memset(g_fp_stack, 0, sizeof g_fp_stack);
    MEM32(stack) = 0xDEADBEEF; MEMF(stack + 4) = 1.0f / 60.0f;
    MEM32(stack + 8) = 0x12345678;
}

static int xbox_camera;

static void chase_contract(void) {
    unsigned i, j, repeat;
    uint32_t retail[14];
    unsigned char before[0x40];
    /* The Original look (MM3_IMPROVED=0) keeps the Xbox camera. */
    const char *look = getenv("MM3_IMPROVED");
    float shift;
    xbox_camera = look && !strcmp(look, "0");
    shift = xbox_camera ? 0.0f : 0.25f;
    /* Varied per-car eye/goal parameters, plus the native null-parameter
     * defaults. Values also distinguish every coordinate and near/far pair. */
    for (i = 0; i < 6; ++i) {
        for (j = 0; j < 0x40; j += 4) MEMF(params + j) = (float)j * 0.07f + (float)i;
        MEMF(params + 0x20) = 3.68f + (float)i * 0.17f;
        MEMF(params + 0x24) = 3.85f + (float)i * 0.23f;
        MEMF(params + 0x34) = 6.98f + (float)i * 0.29f;
        MEMF(params + 0x38) = 7.81f + (float)i * 0.31f;
        MEM32(camera + 0x80) = i ? params : 0;
        memcpy(before, (const void *)XBOX_PTR(params), sizeof before);
        reset_call(); retail_near(); memcpy(retail, captured, sizeof retail);
        assert(g_esp == stack + 8 && g_fp_top == 0);
        for (repeat = 0; repeat < 100; ++repeat) {
            unsigned calls = solver_calls;
            reset_call(); sub_001EF87E();
            assert(solver_calls == calls + 1 && g_esp == stack + 8 && g_fp_top == 0);
            assert(g_ebx == 0x13579 && g_edi == 0x24680 && MEM32(stack + 8) == 0x12345678);
            for (j = 0; j < 14; ++j) {
                if (j == 4 || j == 7) close_float(number(captured[j]), number(retail[j]) - shift);
                else assert(captured[j] == retail[j]);
            }
            assert(!memcmp(before, (const void *)XBOX_PTR(params), sizeof before));
        }
        reset_call(); retail_far(); memcpy(retail, captured, sizeof retail);
        reset_call(); sub_001EF738();
        assert(g_esp == stack + 8 && g_fp_top == 0 && !memcmp(retail, captured, sizeof retail));
        assert(!memcmp(before, (const void *)XBOX_PTR(params), sizeof before));
    }
    /* Unknown caller, forward-facing and nonfinite distances are untouched. */
    for (i = 0; i < 4; ++i) {
        reset_call();
        MEM32(stack) = i ? 0x001EF922 : 0x11111111;
        for (j = 0; j < 14; ++j) MEMF(stack + 4 + j * 4) = (float)j + 1.0f;
        MEMF(stack + 0x14) = i == 2 ? NAN : -4.0f;
        MEMF(stack + 0x20) = i == 1 ? 5.0f : (i == 3 ? INFINITY : -5.0f);
        memcpy(retail, (const void *)XBOX_PTR(stack + 4), sizeof retail);
        sub_001EEC53();
        assert(g_esp == stack + 60 && !memcmp(retail, captured, sizeof retail));
    }
}

static float vertical(float horizontal, float aspect) {
    return 2.0f * atanf(tanf(horizontal * (3.14159265358979323846f / 360.0f)) / aspect);
}
/* MM3_FOV as a tangent scale, read the way the camera hook reads it. */
static float fov_percentage(void) {
    const char *setting = mm3_mod_on(MM3_MOD_FOV) ? getenv("MM3_FOV") : NULL;
    float percentage = setting ? (float)atof(setting) / 100.0f : 1.0f;
    if (!isfinite(percentage) || percentage < 1.0f) percentage = 1.0f;
    return percentage > 1.5f ? 1.5f : percentage;
}
static void projection_contract(float override) {
    static const float aspects[] = {0.75f, 1.0f, 4.0f / 3.0f, 1.6f, 16.0f / 9.0f, 21.0f / 9.0f};
    static const uint32_t callers[] = {0x00210E1E, 0x000E51EE, 0x001EC9C2, 0x001ECA3A, 0x12345678};
    unsigned a, pass, k;
    int widescreen = mm3_mod_on(MM3_MOD_WIDESCREEN);
    float percentage = fov_percentage();
    for (a = 0; a < sizeof aspects / sizeof aspects[0]; ++a) {
        display_aspect = aspects[a];
        /* Pass 3: the camera still holds a shape from an earlier window
         * (resized, or sizing while the race loaded) when renderSetFov runs. */
        float stale = fabsf(display_aspect - 1.6f) < 1e-4f ? 16.0f / 9.0f : 1.6f;
        for (pass = 0; pass < 4; ++pass) {
            float input_aspect = pass == 0 ? 4.0f / 3.0f : pass == 1 ? display_aspect : pass == 2 ? 1.0f : stale;
            for (k = 0; k < sizeof callers / sizeof callers[0]; ++k) {
                float input_fovy = vertical(65.0f, input_aspect);
                int race = k == 0;
                float final_aspect = !widescreen ? input_aspect : k == 1 ? 4.0f / 3.0f : pass == 2 ? 1.0f :
                    pass == 3 && !race ? stale : display_aspect;
                float expected_fovy = input_fovy;
                if (race && override) expected_fovy = vertical(override, final_aspect);
                else if (race && widescreen && pass != 2) expected_fovy = vertical(65.0f, fminf(display_aspect, 4.0f / 3.0f));
                if (race && !override) expected_fovy = 2.0f * atanf(tanf(expected_fovy * 0.5f) * percentage);
                reset_call(); hud_calls = edge_calls = 0;
                MEM32(stack) = callers[k];
                MEMF(stack + 4) = input_fovy; MEMF(stack + 8) = input_aspect;
                MEMF(stack + 12) = 0.2f; MEMF(stack + 16) = 1000.0f;
                MEM32(stack + 20) = 0x76543210;
                native_projection_setup();
                assert(g_esp == stack + 20 && g_fp_top == 0 && MEM32(stack + 20) == 0x76543210);
                assert(g_ebx == 0x13579 && g_edi == 0x24680);
                close_float(MEMF(camera + 0x1FA4), expected_fovy);
                close_float(MEMF(camera + 0x1FA8), final_aspect);
                close_float(MEMF(camera + 0x1EE4), 1.0f / tanf(expected_fovy * 0.5f) / final_aspect);
                close_float(MEMF(camera + 0x1EF8), 1.0f / tanf(expected_fovy * 0.5f));
                /* The visibility cone and the actual native matrix have the
                 * same screen-edge slope, not just the same requested FOV. */
                close_float(tanf(MEMF(camera + 0x1FA4) * 0.5f) * MEMF(camera + 0x1FA8),
                            1.0f / MEMF(camera + 0x1EE4));
                /* The race HUD latch stays on with the widescreen mod off: the renderer's
                 * map and name-tag placement need it (at 4:3 it only anchors the HUD). */
                assert(hud_calls == (unsigned)race);
                assert(edge_calls == (unsigned)(race || k == 1));
                /* A race camera on a display shape is kept current through
                 * later window changes (the menu caller retires it again). */
                if (race && widescreen) assert((display_camera_slot(camera) >= 0) ==
                                               (pass != 2 || fabsf(display_aspect - 1.0f) < 1e-4f));
                if (edge_calls) assert(edge_value == race);
            }
        }
        /* A direct non-camera projection must not interpret an arbitrary
         * caller frame, change FOV, or write after native RET 20 consumes args. */
        for (pass = 0; pass < 2; ++pass) {
            float input_aspect = pass ? 4.0f / 3.0f : 1.0f;
            float final_aspect = pass && widescreen ? display_aspect : input_aspect;
            reset_call(); g_ebp = 0xFFFFFFF0; hud_calls = edge_calls = 0;
            MEM32(stack) = 0x11111111; MEM32(stack + 4) = camera;
            MEMF(stack + 8) = 0.7f; MEMF(stack + 12) = input_aspect;
            MEMF(stack + 16) = 0.2f; MEMF(stack + 20) = 1000.0f;
            for (k = 24; k < 48; k += 4) MEM32(stack + k) = 0xA55AA55A;
            sub_0025968E();
            assert(g_esp == stack + 24 && g_fp_top == 0 && !hud_calls && !edge_calls);
            close_float(MEMF(camera), 1.0f / tanf(0.35f) / final_aspect);
            for (k = 24; k < 48; k += 4) assert(MEM32(stack + k) == 0xA55AA55A);
        }
    }
}

static void live_resize_contract(float override) {
    static const float shapes[] = {16.0f / 9.0f, 1.0f, 21.0f / 9.0f, 0.75f, 4.0f / 3.0f};
    unsigned repeat, i;
    float percentage = override ? 1.0f : fov_percentage(); /* explicit degrees never stack with it */
    display_aspect = 4.0f / 3.0f;
    reset_call();
    MEM32(stack) = 0x00210E1E;
    MEMF(stack + 4) = vertical(65, display_aspect); MEMF(stack + 8) = display_aspect;
    MEMF(stack + 12) = MEMF(camera + 0x1FAC) = .2f;
    MEMF(stack + 16) = MEMF(camera + 0x1FB0) = 1000;
    native_projection_setup();
    assert(display_camera_slot(camera) >= 0);
    for (repeat = 0; repeat < 40; ++repeat) for (i = 0; i < sizeof shapes / sizeof shapes[0]; ++i) {
        uint32_t saved[9]; double fp[8]; unsigned j, updates = view_updates;
        RecompXmm simd[8], actual[8];
        float expected;
        display_aspect = shapes[i];
        expected = vertical(override ? override : 65, override ? display_aspect : fminf(display_aspect, 4.0f / 3.0f));
        expected = 2.0f * atanf(tanf(expected * 0.5f) * percentage);
        reset_call(); g_edi = camera; g_seh_ebp = 0x23456;
        g_fp_top = 3; g_fp_cmp = 1; g_fp_cc = 0x100; g_df = 1;
        memset(simd,0xA5,sizeof simd); g_fs_base=0x12345678;
        g_xmm0=simd[0]; g_xmm1=simd[1]; g_xmm2=simd[2]; g_xmm3=simd[3];
        g_xmm4=simd[4]; g_xmm5=simd[5]; g_xmm6=simd[6]; g_xmm7=simd[7];
        for (j = 0; j < 8; ++j) fp[j] = g_fp_stack[j] = (double)j + .25;
        saved[0]=g_eax; saved[1]=g_ecx; saved[2]=g_edx; saved[3]=g_esp;
        saved[4]=g_ebx; saved[5]=g_esi; saved[6]=g_edi; saved[7]=g_ebp; saved[8]=g_seh_ebp;
        sub_000F4BB1();
        assert(view_updates == updates + 1 && g_esp == saved[3] + 4);
        assert(g_eax==saved[0] && g_ecx==saved[1] && g_edx==saved[2] && g_ebx==saved[4]);
        assert(g_esi==saved[5] && g_edi==saved[6] && g_ebp==saved[7] && g_seh_ebp==saved[8]);
        assert(g_fp_top==3 && g_fp_cmp==1 && g_fp_cc==0x100 && g_df==1 && g_fp_control_word==0x37F);
        assert(!memcmp(fp,g_fp_stack,sizeof fp));
        actual[0]=g_xmm0; actual[1]=g_xmm1; actual[2]=g_xmm2; actual[3]=g_xmm3;
        actual[4]=g_xmm4; actual[5]=g_xmm5; actual[6]=g_xmm6; actual[7]=g_xmm7;
        assert(g_fs_base==0x12345678 && !memcmp(simd,actual,sizeof simd));
        close_float(MEMF(camera + 0x1FA4), expected);
        close_float(MEMF(camera + 0x1FA8), display_aspect);
        close_float(MEMF(camera + 0x1EE4), 1 / tanf(expected * .5f) / display_aspect);
        close_float(MEMF(camera + 0x1EF8), 1 / tanf(expected * .5f));
        close_float(MEMF(camera + 0x1FAC), .2f); close_float(MEMF(camera + 0x1FB0), 1000);
    }
    /* The same allocation becomes a menu turntable. Further resizes must
     * leave its authored projection alone and retire its old race identity. */
    reset_call(); MEM32(stack)=0x000E51EE;
    MEMF(stack+4)=.7f; MEMF(stack+8)=4.0f/3.0f; MEMF(stack+12)=.2f; MEMF(stack+16)=1000;
    native_projection_setup(); assert(display_camera_slot(camera)<0);
    display_aspect=21.0f/9.0f; reset_call(); g_edi=camera; sub_000F4BB1();
    close_float(MEMF(camera+0x1FA4),.7f); close_float(MEMF(camera+0x1FA8),4.0f/3.0f);
    for (i=0;i<DISPLAY_CAMERA_COUNT*3;++i) {
        uint32_t identity=0x15000+i*0x2400;
        remember_display_camera(identity); assert(display_camera_slot(identity)>=0);
    }
}

static void project(uint32_t caller, float fovy, float aspect) {
    reset_call(); hud_calls = edge_calls = 0;
    MEM32(stack) = caller;
    MEMF(stack + 4) = fovy; MEMF(stack + 8) = aspect;
    MEMF(stack + 12) = 0.2f; MEMF(stack + 16) = 1000.0f;
    MEM32(stack + 20) = 0x76543210;
    native_projection_setup();
    assert(g_esp == stack + 20 && g_fp_top == 0 && MEM32(stack + 20) == 0x76543210);
    close_float(tanf(MEMF(camera + 0x1FA4) * 0.5f) * MEMF(camera + 0x1FA8), 1.0f / MEMF(camera + 0x1EE4));
}
static void hook_update(void) {
    unsigned updates = view_updates;
    reset_call(); g_edi = camera; sub_000F4BB1();
    assert(view_updates == updates + 1 && g_esp == stack + 4);
}

/* Two-player split screen (0x407C20 == 1): each half view's native projection
 * passes twice the camera's stored aspect (001EC8E6). It is 2 * display wide on
 * a wide display and must reach the matrix as it is; forcing the display aspect
 * there stretched both views 2x horizontally (the 2.2 report). */
static void split_screen_contract(float override) {
    static const float shapes[] = {4.0f / 3.0f, 1.6f, 16.0f / 9.0f, 21.0f / 9.0f};
    int widescreen = mm3_mod_on(MM3_MOD_WIDESCREEN);
    float percentage = override ? 1.0f : fov_percentage();
    unsigned i;
    for (i = 0; i < sizeof shapes / sizeof shapes[0]; ++i) {
        float w = shapes[i], other = shapes[(i + 1) % (sizeof shapes / sizeof shapes[0])];
        float half_fovy = vertical(65.0f, 8.0f / 3.0f), expected;
        display_aspect = w;
        MEM32(0x00407C20u) = 1;
        /* A half view of a camera still holding 4:3 (8:3) becomes 2 * wide. */
        memset(s_display_cameras, 0, sizeof s_display_cameras);
        project(0x001ECA3Au, half_fovy, 8.0f / 3.0f);
        close_float(MEMF(camera + 0x1FA8), widescreen ? 2.0f * w : 8.0f / 3.0f);
        close_float(MEMF(camera + 0x1FA4), half_fovy);
        /* A half view of an already widened camera is right as it is, for the
         * camera update and for a race projection that finds it (the stale-shape
         * rule must not turn 2 * wide into the single view's wide). */
        project(0x001EC9C2u, half_fovy, 2.0f * w);
        close_float(MEMF(camera + 0x1FA8), 2.0f * w);
        close_float(MEMF(camera + 0x1FA4), half_fovy);
        memset(s_display_cameras, 0, sizeof s_display_cameras);
        project(0x00210E1Eu, vertical(65.0f, 2.0f * w), 2.0f * w);
        close_float(MEMF(camera + 0x1FA8), 2.0f * w);
        expected = override ? vertical(override, 2.0f * w) : vertical(65.0f, 2.0f * w);
        if (!override) expected = 2.0f * atanf(tanf(expected * 0.5f) * percentage);
        close_float(MEMF(camera + 0x1FA4), expected);
        /* The same 8:3 outside split screen is a stale display shape. */
        MEM32(0x00407C20u) = 0;
        memset(s_display_cameras, 0, sizeof s_display_cameras);
        project(0x001ECA3Au, half_fovy, 8.0f / 3.0f);
        close_float(MEMF(camera + 0x1FA8), 8.0f / 3.0f);
        project(0x00210E1Eu, half_fovy, 8.0f / 3.0f);
        close_float(MEMF(camera + 0x1FA8), widescreen ? w : 8.0f / 3.0f);
        assert((display_camera_slot(camera) >= 0) == (widescreen != 0));
        /* The live view update runs inside the doubled window: it keeps the
         * half view, then follows a display change for both states. */
        MEM32(0x00407C20u) = 1;
        memset(s_display_cameras, 0, sizeof s_display_cameras);
        display_aspect = w;
        project(0x00210E1Eu, vertical(65.0f, 4.0f / 3.0f), 4.0f / 3.0f);
        if (widescreen) assert(display_camera_slot(camera) >= 0);
        {
            float single_fovy = MEMF(camera + 0x1FA4);
            project(0x001ECA3Au, 0.3f, 2.0f * MEMF(camera + 0x1FA8));
            close_float(MEMF(camera + 0x1FA8), 2.0f * (widescreen ? w : 4.0f / 3.0f));
            hook_update();
            close_float(MEMF(camera + 0x1FA4), 0.3f);
            close_float(MEMF(camera + 0x1FA8), 2.0f * (widescreen ? w : 4.0f / 3.0f));
            /* The native restore of the stored single view. */
            project(0x001ECB2Au, single_fovy, widescreen ? w : 4.0f / 3.0f);
            hook_update();
            close_float(MEMF(camera + 0x1FA8), widescreen ? w : 4.0f / 3.0f);
            if (widescreen && fabsf(w - other) > 1e-4f && w < 1.5f * other && other < 1.5f * w) {
                /* Window change: a single stored shape follows it ... */
                display_aspect = other; hook_update();
                close_float(MEMF(camera + 0x1FA8), other);
                /* ... and so does a doubled one (2 * the old shape). */
                project(0x001ECA3Au, 0.3f, 2.0f * w);
                hook_update();
                close_float(MEMF(camera + 0x1FA8), 2.0f * other);
            }
        }
        MEM32(0x00407C20u) = 0;
    }
}

int main(int argc, char **argv) {
    float override;
    assert(argc == 2);
    override = strtof(argv[1], NULL);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    native_constants();
    chase_contract(); projection_contract(override);
    /* Live resizing follows the display shape, which MM3_MOD_WIDESCREEN=0 turns off. */
    if (mm3_mod_on(MM3_MOD_WIDESCREEN)) live_resize_contract(override);
    split_screen_contract(override);
    printf("PASS camera: near-only %s, native matrix/culling, FOV %.3g, six aspects, 200 live resizes, split-screen half views, menu retirement and ABI guards\n",
           xbox_camera ? "Xbox distance" : "+0.25m", override);
    return 0;
}
