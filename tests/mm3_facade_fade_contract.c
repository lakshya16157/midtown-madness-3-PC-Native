/* Exercise the production facade wrapper against the setter's guest ABI.
 * The oracle samples the Xbox fade at proportional camera distances, so it
 * detects a moved threshold whose blend width was accidentally left at 9 m. */
#ifdef _WIN32
#define SET_ENV(name, value) _putenv_s((name), (value))
#else
#define _POSIX_C_SOURCE 200809L /* setenv under -std=c11 */
#define SET_ENV(name, value) setenv((name), (value), 1)
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mods/draw_distance.c"
#include "env_range.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_edi, g_ebp, g_seh_ebp;
static unsigned char memory[0x400000], expected[0x400000];
static unsigned setter_calls;
static float original_bias, original_slope;
static const uint32_t stack = 0x1000;
/* The fog hooks reject guest pointers below 0x10000, so their fixtures live above it. */
static const uint32_t fog_stack = 0x20000;
static int fog_ready = 1;
int nv2a_d3d_distance_fog_ready(void) { return fog_ready; }
int mm3_exhaust_enabled(void) { return 0; }
int mm3_exhaust_packet(uint32_t view, uint32_t words[18]) { (void)view; (void)words; return 0; }
void nv2a_d3d_set_depth_view_remap(float scale, float knee)
{
    assert(scale >= 1.0f && scale <= 4.0f && knee == 0.78f);
}
void sub_000E2D5B_original(void) {}
void sub_000D9E2E_original(void) {}
void sub_000FE71E_original(void) {}
void sub_0022F298_original(void) {}
void sub_000F4AC3_original(void) {}
void sub_002256C7_original(void) {}
void sub_001A6CE3_original(void) {}
void sub_00248518_original(void) {}
void sub_00344770(void) { g_esp += 12; }   /* the pushbuffer rollover: ret 8 pops the three words the hook pushes (return slot + 2 args); the ring-full case below reaches it */
static int lod_enabled = 1;
int mm3_mod_on(int mod) { assert(mod == MM3_MOD_DRAW_DISTANCE); return lod_enabled; }
static unsigned native_fog_calls, native_horizon_calls;
void sub_000F8750_original(void)
{
    ++native_horizon_calls;
    g_eax = 0x12340001; g_esp += 8;
}
void sub_000F8DCE_original(void)
{
    ++native_fog_calls;
    g_eax = 0x12340001; g_esp += 16;
}

void sub_001661D0_original(void)
{
    double near_distance = MEMF(g_esp + 4), far_distance = MEMF(g_esp + 8);
    double start, end, slope;
    ++setter_calls;
    if (near_distance < 0.0) near_distance = 0.0;
    if (far_distance < near_distance + 50.0) far_distance = near_distance + 50.0;
    MEMF(0x003C04E0u) = (float)near_distance;
    MEMF(0x003C04E4u) = (float)far_distance;
    start = near_distance - 10.0;
    end = near_distance - 1.0;
    slope = 1.0 / (start * start - end * end);
    original_bias = MEMF(0x003B8134u) = (float)(-end * end * slope);
    original_slope = MEMF(0x003B8138u) = (float)slope;
    /* Other fade constants and guest register results are owned by retail. */
    MEM32(0x003B8160u) = 0x1234ABCD;
    g_eax = 0x11223344;
    g_ecx = 0x55667788;
    g_esp += 12;
    memcpy(expected, memory, sizeof memory);
}

static void call_setter(uint32_t caller, float near_distance, float far_distance)
{
    g_esp = stack;
    MEM32(stack) = caller;
    MEMF(stack + 4) = near_distance;
    MEMF(stack + 8) = far_distance;
    sub_001661D0();
    assert(g_esp == stack + 12 && g_eax == 0x11223344 && g_ecx == 0x55667788);
    memcpy(expected + 0x003B8134u, memory + 0x003B8134u, 8);
    assert(memcmp(memory, expected, sizeof memory) == 0);
}

static void check_fade(float scale, float retail_near, float retail_far)
{
    unsigned i;
    double bias = MEMF(0x003B8134u), slope = MEMF(0x003B8138u);
    double start = retail_near - 10.0, end = retail_near - 1.0;
    assert(MEMF(0x003C04E0u) == retail_near * scale);
    assert(MEMF(0x003C04E4u) == retail_far * scale);
    /* Normal retail endpoints are 70 m opaque and 79 m transparent. */
    for (i = 0; i <= 1000; ++i) {
        double distance = start + 9.0 * i / 1000.0;
        double wanted = (end * end - distance * distance) / (end * end - start * start);
        double effective_distance = distance * scale;
        assert(fabs(bias + slope * effective_distance * effective_distance - wanted) < 0.00001);
    }
}

int main(int argc, char **argv)
{
    float scale, saved_near, saved_far;
    uint32_t saved_fade[2];
    /* One setting scales draw and view distance together; a second one sets the view distance. */
    assert(argc == 2 || argc == 3);
    scale = (float)atof(argc == 3 ? argv[2] : argv[1]);
    assert(scale == 1.0f || scale == 2.0f || scale == 4.0f);
    SET_ENV("RECOMP_DRAW_DISTANCE", argv[1]);
    SET_ENV("RECOMP_VIEW_DISTANCE", argc == 3 ? argv[2] : "");
    if (scale > 2.0f) scale = 2.0f;
    memset(memory, 0xA5, sizeof memory);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    call_setter(0x0015FA1Du, 80.0f, 280.0f); /* construction */
    check_fade(scale, 80.0f, 280.0f);
    call_setter(0x0020FABAu, 80.0f, 280.0f); /* explicit Lua setter */
    check_fade(scale, 80.0f, 280.0f);
    saved_near = MEMF(0x003C04E0u);
    saved_far = MEMF(0x003C04E4u);
    memcpy(saved_fade, memory + 0x003B8134u, sizeof saved_fade);
    call_setter(0x00211220u, saved_near * 3.0f, saved_far * 3.0f);
    check_fade(scale, 240.0f, 840.0f);
    call_setter(0x0021153Du, saved_near, saved_far);
    check_fade(scale, 80.0f, 280.0f);
    assert(memcmp(saved_fade, memory + 0x003B8134u, sizeof saved_fade) == 0);
    call_setter(0x0020FABAu, 10.0f, 280.0f); /* no positive fade start */
    assert(MEMF(0x003B8134u) == original_bias && MEMF(0x003B8138u) == original_slope);
    call_setter(0x0020FABAu, -5.0f, 280.0f); /* preserve native validation */
    assert(MEMF(0x003C04E0u) == 0.0f);
    assert(MEMF(0x003B8134u) == original_bias && MEMF(0x003B8138u) == original_slope);
    assert(setter_calls == 6);
    /* The facade pools are sized at native VB creation (sub_001633B7,
     * mm3_facade_capacity.c, tests/mm3_render_distance_contract.c), not by a
     * constructor hook. The fog pass shares this file: native when the renderer
     * has it, the title's own fog and horizon otherwise. */
    for (int on = 0; on <= 1; ++on) {
        fog_ready = on; g_esp = fog_stack; g_eax = 0x12340000;
        g_ecx = 0x13000; MEM32(g_ecx+0xC) = 0x14000;
        MEMF(0x14000+0x1ED8) = 16777215.f/759.9f; MEMF(0x14000+0x1FB0) = 760;
        MEMF(0x14000+0x1EE4) = MEMF(0x14000+0x1ED8);
        MEMF(0x14000+0x1EF8) = MEMF(0x14000+0x1ED8);
        MEMF(0x14000+0x1F34) = 0; MEMF(0x14000+0x1F38) = 1; MEMF(0x14000+0x1F3C) = 0;
        MEM32(fog_stack+4) = 0xA5A5A5A5;
        MEM32(0x351F48) = 0x15000; MEM32(0x15000) = 0x16000; MEM32(0x15004) = 0x16100;
        sub_000F8DCE();
        assert(g_eax == 0x12340001 && g_esp == fog_stack+16);
    }
    /* The horizon strip is replaced only for the camera whose fog was accepted. */
    MEM32(0x13000 + 0x28) = 0x14000;
    for (int on = 0; on <= 1; ++on) {
        fog_ready = on; g_eax = 0x12340000; g_esp = fog_stack;
        memcpy(expected,memory,sizeof memory);
        sub_000F8750();
        assert(g_eax == 0x12340001 && g_esp == fog_stack+8);
        assert(memcmp(expected,memory,sizeof memory) == 0);
    }
    assert(native_horizon_calls == 1);
    assert(native_fog_calls == 1);
    assert(MEM32(0x15000) == 0x16000+NV2A_HOST_FOG_WORDS*4);
    assert(MEM32(0x16000) == NV2A_HOST_FOG_HEADER && MEM32(0x16004) == NV2A_HOST_FOG_TAG);
    assert(MEMF(0x16008) == 1.f/(16777215.f/759.9f));
    assert(MEMF(0x1600C) == 760 && MEMF(0x16010) == 300 && MEMF(0x16014) == 760*.9f);
    assert(MEM32(0x16018) == 0xA5A5A5A5 && MEM32(0x1601C) == (NV2A_HOST_FOG_SKY_MASK | NV2A_HOST_FOG_SKY_FILL | NV2A_HOST_FOG_SKY_FADE));
    assert(MEMF(0x16020) == 0 && MEMF(0x16024) == 1 && MEMF(0x16028) == 0 && MEMF(0x1602C) == 1.f/.16f);
    /* Tilt and asymmetric projection move/rotate the fade with the camera. */
    MEMF(0x14000+0x1EE4) = 2*MEMF(0x14000+0x1ED8);
    MEMF(0x14000+0x1EF8) = 4*MEMF(0x14000+0x1ED8);
    MEMF(0x14000+0x1F34) = .3f; MEMF(0x14000+0x1F38) = .8f; MEMF(0x14000+0x1F3C) = -.2f;
    fog_ready = 1; MEM32(0x15000) = 0x16000; g_esp = fog_stack; sub_000F8DCE();
    assert(fabsf(MEMF(0x16020)-.15f) < 1e-6f && fabsf(MEMF(0x16024)-.2f) < 1e-6f && MEMF(0x16028) == -.2f);
    /* Never overwrite the native ring's end when no packet fits: the hook asks the
     * title for space (the mock changes nothing), then keeps the retail fog. */
    MEM32(0x352E00) = 0x1000; MEM8(0x15008) = 0; /* no push-buffer recording */
    MEM32(0x15000) = 0x16100; g_esp = fog_stack; sub_000F8DCE();
    assert(MEM32(0x15000) == 0x16100 && g_esp == fog_stack+16);
    assert(s_fog_made_space == 1 && s_fog_fallbacks == 1);
    printf("facade fade contract passed (scale %.0f)\n", scale);
    return 0;
}
