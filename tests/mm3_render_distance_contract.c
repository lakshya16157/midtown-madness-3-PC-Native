/* The runner supplies the actual recovered PAL setters and XBE constants.
 * Only camera/renderer and final VB allocation are observation boundaries. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef MM3_DISTANCE_SOURCE
#define MM3_DISTANCE_SOURCE "mm3_render_distance.c"
#endif
#include MM3_DISTANCE_SOURCE
#include "mm3_facade_capacity.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_esi, g_ebx, g_edi, g_ebp, g_seh_ebp;
RECOMP_TLS int g_fp_top, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word, g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
static unsigned char memory[0x600000];
static const uint32_t stack = 0x1000, frame = 0x2000, manager = 0x500000, renderer = 0x510000;
static float camera_far, shadow_distance, remap_scale;
static unsigned remap_calls, vb_calls;
static int native_fog;
int nv2a_d3d_distance_fog_ready(void) { return native_fog; }
static uint32_t vb_capacities[4];
static int vb_success = 1;

void nv2a_d3d_set_depth_view_remap(float scale, float knee)
{
    assert(knee == 0.78f);
    ++remap_calls;
    remap_scale = scale;
}

void sub_000F4AC3_original(void)
{
    camera_far = MEMF(g_esp + 0x10);
    g_esp += 20;
}

void sub_0022F298_original(void)
{
    shadow_distance = MEMF(g_esp + 4);
    g_esp += 12;
}

void sub_002256C7_original(void) { g_esp += 4; }

void sub_001633B7_original(void)
{
    ++vb_calls;
    vb_capacities[0] = MEM32(g_esi + 0x1ED0);
    vb_capacities[1] = MEM32(g_esi + 0x1F2C);
    vb_capacities[2] = MEM32(g_esi + 0x1F88);
    vb_capacities[3] = MEM32(g_esi + 0x48);
    g_eax = (uint32_t)vb_success;
    g_esp += 4;
}

#include "distance_original_setters.inc"

static void prepare(uint32_t caller, float first, float second)
{
    g_esp = stack;
    g_ebp = g_seh_ebp = frame;
    g_ecx = manager;
    g_fp_top = g_fp_cmp = g_fp_cc = 0;
    MEM32(stack) = caller;
    MEMF(stack + 4) = first;
    MEMF(stack + 8) = second;
}

static void check_pedestrians(const float base[5], float scale)
{
    unsigned i;
    for (i = 0; i < 5; ++i)
        assert(MEMF(0x39ADC0u + i * 4) == base[i] * scale);
}

static void set_pedestrians(const float base[5], int valid)
{
    unsigned i;
    prepare(0x100, 0, 0);
    g_ecx = 0x3000;
    for (i = 0; i < 5; ++i) MEMF(g_ecx + i * 4) = base[i];
    if (!valid) MEMF(g_ecx + 8) = -1;
    sub_000D9E2E();
    assert(!!LO8(g_eax) == valid && g_esp == stack + 4 && !g_fp_top);
}

static void check_object(uint32_t caller, float input, float wanted)
{
    prepare(caller, input, 0.75f);
    sub_000E2D5B();
    assert(LO8(g_eax) && g_esp == stack + 12 && !g_fp_top);
    assert(MEMF(manager + 0x1AC) == wanted);
    assert(MEMF(manager + 0x1B0) == 0.75f);
    assert(MEMF(manager + 0x1B8) == wanted * 0.75f);
    assert(fabsf(MEMF(manager + 0x1B4) - 4.0f / wanted) < 0.000001f);
}

static void check_city(uint32_t caller, float input, float wanted)
{
    prepare(caller, input, 0);
    MEMF(frame + 8) = input; /* The native clamp writes its local first. */
    sub_000FE71E();
    assert(LO8(g_eax) && g_esp == stack + 8 && !g_fp_top);
    assert(MEMF(manager + 0x34) == wanted);
    assert(MEMF(manager + 0x108) == wanted);
    if (caller == 0x224C4Eu) assert(MEMF(frame + 8) == input);
}

static void check_city_load(float authored, float view, float cell_size)
{
    float cap = cell_size > 0 ? (float)((double)cell_size * MEMF(0x3860E0u)) : 0;
    float wanted = cap > 0 ? fminf(authored * view, cap) : authored * view;
    float baseline = cap > 0 ? fminf(authored, cap) : authored;
    float expected_haze = native_fog ? 1.0f : fmaxf(1.0f, wanted / baseline);
    unsigned calls;
    check_city(0x100ED6u, authored, authored * view);
    prepare(0, 0, 0);
    MEMF(frame + 8) = MEMF(manager + 0x34);
    MEMF(frame - 12) = 1.0f; MEMF(frame - 16) = 2.0f; MEMF(frame - 20) = 3.0f;
    MEMF(renderer + 0x1505C) = cell_size;
    g_eax = cell_size > 0 ? renderer : 0;
    g_ebx = manager;
    contract_native_city_limit();
    assert(g_esp == stack && !g_fp_top);
    assert(MEMF(frame + 8) == wanted);
    assert(MEMF(manager + 0x34) == wanted && MEMF(manager + 0x108) == wanted);
    assert(camera_far == wanted && remap_scale == expected_haze);
    calls = remap_calls;
    /* Cubemap save/install/restore and FOV calls already hold effective
     * distances. They must not multiply, clamp again or move city haze. */
    check_city(0x22CE39u, wanted * 0.5f, wanted * 0.5f);
    check_city(0x22CFE3u, wanted, wanted);
    prepare(0x22CE67u, 0, 0); MEMF(stack + 0x10) = wanted * 0.5f;
    sub_000F4AC3(); assert(camera_far == wanted * 0.5f);
    prepare(0x22D009u, 0, 0); MEMF(stack + 0x10) = wanted;
    sub_000F4AC3(); assert(camera_far == wanted);
    prepare(0x210E1Eu, 0, 0); MEMF(stack + 0x10) = wanted;
    sub_000F4AC3(); assert(camera_far == wanted && remap_calls == calls);
    /* Actual Lua order is projection first, then city visibility. */
    prepare(0x210EB4u, 0, 0); MEMF(stack + 0x10) = authored;
    sub_000F4AC3();
    assert(camera_far == wanted && remap_scale == expected_haze && remap_calls == calls);
    check_city(0x210EDEu, authored, wanted);
}

int main(int argc, char **argv)
{
    static const float retail_peds[5] = {20, 30, 40, 80, 160};
    static const float custom_peds[5] = {30, 45, 60, 120, 240};
    float object, view, facade;
    unsigned i, factor;
    int custom_first;
    assert(argc == 6 || argc == 7);
    native_fog = argc == 7 && !strcmp(argv[6], "native");
    _putenv_s("MM3_NATIVE_FOG", native_fog ? "1" : "0");
    _putenv_s("RECOMP_DRAW_DISTANCE", strcmp(argv[1], "-") ? argv[1] : "");
    _putenv_s("RECOMP_VIEW_DISTANCE", strcmp(argv[2], "-") ? argv[2] : "");
    _putenv_s("MM3_FOG_REMAP", "1");
    object = (float)atof(argv[3]); view = (float)atof(argv[4]);
    facade = fminf(view, 2.0f);
    custom_first = !strcmp(argv[5], "custom-first");
    g_xbox_mem_offset = (ptrdiff_t)memory;
    g_fp_control_word = 0x37F;
    load_original_constants();
    MEM32(manager + 8) = manager + 0x100;
    assert(mm3_draw_distance_scale() == object);
    assert(mm3_view_distance_scale() == view && mm3_facade_distance_scale() == facade);
    assert(remap_calls == 0); /* Requested scale is not the effective far plane. */
    if (custom_first) set_pedestrians(custom_peds, 1);
    else set_pedestrians(custom_peds, 0); /* Failed custom input keeps defaults. */
    check_object(0xE4672u, 100, 100 * object);
    check_pedestrians(custom_first ? custom_peds : retail_peds, object);
    check_object(0xE4672u, 100, 100 * object); /* Repeated construction. */
    check_pedestrians(custom_first ? custom_peds : retail_peds, object);
    check_object(0xE2E94u, 125, 125 * object); /* Explicit title override. */
    check_object(0x211273u, 300 * object, 300 * object);
    check_object(0x2114EDu, 125 * object, 125 * object); /* Restore exactly. */
    set_pedestrians(custom_peds, 1);
    check_pedestrians(custom_peds, object);
    set_pedestrians(custom_peds, 0);
    check_pedestrians(custom_peds, object);
    for (i = 0; i < 3; ++i) {
        static const uint32_t callers[] = {0x1002CEu, 0x100ED6u, 0x210EDEu};
        check_city(callers[i], 700, 700 * view);
    }
    check_city(0x22CE39u, 350 * view, 350 * view);
    check_city(0x22CFE3u, 700 * view, 700 * view);
    prepare(0x210EB4u, 0, 0);
    MEMF(stack + 0x10) = 700;
    sub_000F4AC3();
    assert(camera_far == 700 * view && g_esp == stack + 20);
    prepare(0x210E1Eu, 0, 0); /* FOV update reuses the effective far plane. */
    MEMF(stack + 0x10) = camera_far;
    sub_000F4AC3();
    assert(camera_far == 700 * view);
    assert(remap_calls == 1 && remap_scale == (native_fog ? 1.0f : view));
    /* City types with tighter/looser grids, authored ranges below/above the
     * native cap, uncapped cities, and returning to a different grid. */
    check_city_load(700, view, 800);
    check_city_load(1000, view, 800);
    check_city_load(250, view, 1200);
    check_city_load(700, view, 0);
    check_city_load(700, view, 1600);
    check_city_load(700, view, 800);
    prepare(0x22F9D7u, 100, 0);
    sub_0022F298();
    assert(shadow_distance == 100 * object);
    prepare(0x211220u, shadow_distance, 0);
    sub_0022F298();
    assert(shadow_distance == 100 * object);
    prepare(0x15FA1Du, 80, 280);
    sub_001661D0();
    assert(g_esp == stack + 12 && !g_fp_top);
    assert(MEMF(0x3C04E0u) == 80 * facade && MEMF(0x3C04E4u) == 280 * facade);
    assert(fabsf(MEMF(0x3B8134u) + MEMF(0x3B8138u) * (70 * facade) * (70 * facade) - 1) < 0.00001f);
    assert(fabsf(MEMF(0x3B8134u) + MEMF(0x3B8138u) * (79 * facade) * (79 * facade)) < 0.00001f);
    factor = (unsigned)ceilf(facade * facade);
    g_esi = manager;
    MEM32(manager + 0x1ED0) = 0x3000;
    MEM32(manager + 0x1F2C) = 0x800;
    MEM32(manager + 0x1F88) = 0x200;
    MEM32(manager + 0x48) = 0x600;
    g_esp = stack;
    sub_001633B7();
    assert(vb_capacities[0] == 0x3000 * factor && vb_capacities[1] == 0x800 * factor);
    assert(vb_capacities[2] == 0x200 * factor && vb_capacities[3] == 0x600 * factor);
    assert(MEM32(manager + 0x48) == 0x600 * factor);  /* the detail stream grows with its zone */
    g_esp = stack;
    sub_001633B7();
    assert(MEM32(manager + 0x1ED0) == 0x3000 * factor); /* No double growth. */
    assert(MEM32(manager + 0x48) == 0x600 * factor);
    assert(vb_calls == 2);
    puts("PASS: native city clamp and camera handover, effective haze, independent detail/facade ranges, Lua/FOV/temporary restores and bounded facade pools");
    return 0;
}
