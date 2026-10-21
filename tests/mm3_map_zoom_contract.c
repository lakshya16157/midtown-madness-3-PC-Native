/* The stock map rebuilds its scalar before every draw. Execute the real native
 * road and checkpoint geometry with only resource/input/GPU boundaries faked. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;
static unsigned char memory[0x800000];
static const uint32_t stack = 0x80000, node = 0x10000, second_node = 0x10200;
static const uint32_t player = 0x20000, players = 0x21000, property = 0x22000;
static const uint32_t layout = 0x23000, markers = 0x24000;
typedef struct { uint32_t texture; float xy[4][2], uv[4][2], mask_uv[4][2], rgba[4][4]; } Draw;
static Draw draws[16];
static uint32_t emitted[7];
static unsigned draw_count;
static float native_window_x, native_window_y;
static float renderer_half_width, renderer_half_height;
static int player_present = 1;
static int race_input_present, ctrl_pressed;
static unsigned marker_count = 1;
static int zoom_markers;
static const char *check = "speed";
static int improved = 1, north_marker;
static unsigned pool_allocations, pool_frees, pool_bytes;
static uint32_t pool_live;
int mm3_minimap_improved(void) { return improved; }
int mm3_hud_north_marker(void) { return north_marker; }

void sub_00170EE2(void) {
    const uint32_t allocation = 0x450000;
    assert(!pool_live && g_eax && g_eax <= 0x310000);
    pool_live = allocation; pool_bytes = g_eax; ++pool_allocations;
    memset((void *)XBOX_PTR(allocation), 0xCD, pool_bytes);
    MEM32(allocation - 4) = MEM32(allocation + pool_bytes) = 0xCAFEBABEu;
    g_eax = allocation; g_esp += 4;
}
void sub_00170EC0(void) {
    assert(pool_live && MEM32(g_esp + 4) == pool_live);
    assert(MEM32(pool_live - 4) == 0xCAFEBABEu);
    assert(MEM32(pool_live + pool_bytes) == 0xCAFEBABEu);
    pool_live = 0; ++pool_frees; g_esp += 4;
}

#include "production_map_zoom.c"

static void push_float(double value) {
    g_fp_top = (g_fp_top + 7) & 7;
    g_fp_stack[g_fp_top] = value;
}
void retail_marker(void);
void zoom_marker(void);
void city_window(void);
static void fake_icall(uint32_t address) {
    if (address == 0x100) { push_float(0); g_esp += 8; return; }
    if (address == 0x200) { g_eax = 0; g_esp += 4; return; }
    if (address == 0x204) { push_float(0); g_esp += 4; return; }
    if (address == 0x208) { g_eax = 0x2D000; g_esp += 8; return; }
    if (address == 0x0011CAE8) {
        if (zoom_markers) zoom_marker(); else retail_marker();
        return;
    }
    if (address == 0x0002DC7E) {
        memset((void *)XBOX_PTR(g_ecx), 0, 0x24); g_esp += 4; return;
    }
    fprintf(stderr, "unexpected map indirect call %08X\n", address);
    abort();
}
void sub_0011E76F(void) { g_eax = 0x25000; g_esp += 4; }
void sub_0011E647(void) { g_eax = player_present ? players : 0; g_esp += 4; }
void sub_001C885C(void) { push_float(0); g_esp += 8; }
void sub_001F33A2(void) { g_eax = race_input_present ? 0x28000 : 0; g_esp += 4; }
void sub_001F3979(void) { g_eax = ctrl_pressed && g_eax == 0x28; g_esp += 4; }
void sub_0004EE0D(void) { g_eax = marker_count; g_esp += 4; }
void sub_002414D8(void) {
    /* Player palette lookup is a resource boundary shared by both draws. */
    MEMF(g_eax) = MEMF(g_eax + 4) = MEMF(g_eax + 8) = .5f;
    MEMF(g_eax + 12) = .7f; g_esp += 4;
}
void sub_00012D29(void) { assert(!"unreachable wrapped animation branch"); }
void sub_001E4A45(void) { g_eax = 0xFFFFFFE8u; g_esp += 4; }
void sub_00069807(void) {
    memcpy(emitted, (void *)XBOX_PTR(g_esp + 4), sizeof emitted);
    g_esp += 32; /* Native ret 28: x, y, ARGB, city U/V, mask U/V. */
}
void sub_001C1585(void) {
    uint32_t texture = MEM32(g_esp + 4), vertices = MEM32(g_esp + 8);
    const uint32_t args = 0x35000;
    int square = zoom_markers && mm3_map_square_begin(texture, vertices);
    unsigned i;
    assert(draw_count < sizeof draws / sizeof draws[0]);
    if (texture == 1) {
        uint32_t mask_window = MEM32(vertices);
        native_window_x = MEMF(mask_window) + MEMF(mask_window + 8) * .5f + renderer_half_width;
        native_window_y = MEMF(mask_window + 4) + MEMF(mask_window + 12) * .5f + renderer_half_height;
    }
    if (square) {
        uint32_t saved_eax = g_eax, saved_edx = g_edx, saved_ebx = g_ebx, saved_ebp = g_ebp;
        uint32_t dimensions = 0x39100;
        g_eax = dimensions - 8; g_ebx = texture; g_ebp = 0x39000;
        MEMF(dimensions) = renderer_half_width * 2;
        MEMF(dimensions + 4) = renderer_half_height * 2;
        PUSH32(g_esp, 0xDEADBEEF);
        city_window();
        g_eax = saved_eax; g_edx = saved_edx; g_ebx = saved_ebx; g_ebp = saved_ebp;
    }
    draws[draw_count].texture = texture;
    for (i = 0; i < 4; ++i) {
        unsigned component;
        draws[draw_count].xy[i][0] = MEMF(vertices + 0xC + i * 0x24) + renderer_half_width;
        draws[draw_count].xy[i][1] = MEMF(vertices + 0x10 + i * 0x24) + renderer_half_height;
        for (component = 0; component < 4; ++component)
            draws[draw_count].rgba[i][component] = MEMF(vertices + 0x20 + i * 0x24 + component * 4);
        /* Native 116535 derives primary UVs from the resource, never source
         * quad UV fields. Emulate its prepared 69807 arguments at this GPU
         * boundary, preserving the independently supplied mask coordinates. */
        MEMF(args) = draws[draw_count].xy[i][0]; MEMF(args + 4) = draws[draw_count].xy[i][1];
        MEM32(args + 8) = 0xA1B2C3D4;
        MEMF(args + 12) = (i == 1 || i == 2) ? 1.0f : 0.0f;
        MEMF(args + 16) = i >= 2 ? 1.0f : 0.0f;
        MEMF(args + 20) = .125f + .125f * (float)i;
        MEMF(args + 24) = .75f - .125f * (float)i;
        if (square) mm3_map_square_city_uv(texture, args);
        assert(MEMF(args) == draws[draw_count].xy[i][0] && MEMF(args + 4) == draws[draw_count].xy[i][1]);
        assert(MEM32(args + 8) == 0xA1B2C3D4);
        assert(MEMF(args + 20) == .125f + .125f * (float)i);
        assert(MEMF(args + 24) == .75f - .125f * (float)i);
        draws[draw_count].uv[i][0] = MEMF(args + 12);
        draws[draw_count].uv[i][1] = MEMF(args + 16);
        draws[draw_count].mask_uv[i][0] = MEMF(args + 20);
        draws[draw_count].mask_uv[i][1] = MEMF(args + 24);
    }
    ++draw_count; g_esp += 12;
}
#undef RECOMP_ICALL_SAFE_AT
#define RECOMP_ICALL_SAFE_AT(target, saved, site) fake_icall(target)
#undef RECOMP_ICALL_SAFE
#define RECOMP_ICALL_SAFE(target, saved) fake_icall(target)
#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
#include "native_map_zoom.inc"

static void near(float got, float expected) {
    if (!isfinite(got) || fabsf(got - expected) > 0.00015f) {
        fprintf(stderr, "map zoom mismatch (%s): got %.9g expected %.9g\n", check, got, expected);
        abort();
    }
}
static void fresh_scale(uint32_t identity, uint32_t record, float scale) {
    const uint32_t window = 0x23300;
    MEMF(identity + 0x50) = scale;
    mm3_map_zoom_apply(identity, record);
    /* Isolated scalar tests use a zero-offset renderer; live native drawing
     * below executes its actual producer viewport conversion instead. */
    MEMF(window) = MEMF(identity + 0x94) - 64;
    MEMF(window + 4) = MEMF(identity + 0x98) - 48;
    MEMF(window + 8) = 128; MEMF(window + 12) = 96;
    mm3_map_zoom_window(identity, window);
    mm3_map_square_publish_window(0, 0);
}
static float settle(uint32_t identity, int speed, float baseline) {
    unsigned i;
    MEM32(player + 4) = (uint32_t)speed;
    mm3_map_zoom_reset(identity);
    fresh_scale(identity, player, baseline);
    for (i = 0; i < 600; ++i) mm3_map_zoom_update(identity, 1.0f / 60.0f);
    fresh_scale(identity, player, baseline);
    return baseline * 1.6f * 1.10f /
        (NV2A_MINIMAP_SCALE + (1 - NV2A_MINIMAP_SCALE) * map_expansion(identity)) / MEMF(identity + 0x50);
}
static void abi_start(uint32_t identity) {
    g_eax = 0xABABABAB; g_ecx = identity; g_edx = 0x12345678;
    g_esp = stack; g_ebp = 0x71000; g_seh_ebp = g_ebp;
    g_ebx = 0x11223344; g_esi = 0x22334455; g_edi = 0x33445566;
    g_fp_top = 0; g_fp_control_word = 0x37F; g_df = 0;
    memset(g_fp_stack, 0, sizeof g_fp_stack);
    MEM32(stack) = 0xDEADBEEF;
    MEM32(stack + 4) = 0x25000; MEM32(stack + 8) = layout;
    MEM32(stack + 12) = 0x26262626; MEM32(stack + 16) = layout;
    MEM32(stack + 20) = 0x27272727; MEM32(stack + 24) = 0xA5A5A5A5;
}
static void abi_end(unsigned consumed) {
    assert(g_esp == stack + consumed && g_fp_top == 0);
    assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
    assert(MEM32(stack + 24) == 0xA5A5A5A5);
}
static void speed_contract(void) {
    const int speeds[] = {0, 19, 20, 45, 70, 95, 120, 121, 400, -120};
    const float spans[] = {1, 1, 1, 1.09375f, 1.3f, 1.50625f, 1.6f, 1.6f, 1.6f, 1.6f};
    unsigned i;
    for (i = 0; i < sizeof speeds / sizeof speeds[0]; ++i)
        near(settle(node, speeds[i], 2), spans[i]);
    mm3_map_zoom_reset(node);
    MEM32(player + 4) = 120; fresh_scale(node, player, 2);
    near(MEMF(node + 0x50), 3.2f * 1.10f / NV2A_MINIMAP_SCALE); /* Drawing is not a time step. */
    for (i = 0; i < 30; ++i) fresh_scale(node, player, 2);
    near(MEMF(node + 0x50), 3.2f * 1.10f / NV2A_MINIMAP_SCALE);
    mm3_map_zoom_update(node, 0.1f); fresh_scale(node, player, 2);
    near(3.2f * 1.10f / NV2A_MINIMAP_SCALE / MEMF(node + 0x50), 1 + .6f * (1 - expf(-.1f / .6f)));
    mm3_map_zoom_reset(node); fresh_scale(node, player, 2);
    mm3_map_zoom_update(node, 10); fresh_scale(node, player, 2);
    near(3.2f * 1.10f / NV2A_MINIMAP_SCALE / MEMF(node + 0x50), 1 + .6f * (1 - expf(-.1f / .6f)));
    for (i = 0; i < 4; ++i) {
        const float invalid[] = {0, -.1f, NAN, INFINITY};
        float before = MEMF(node + 0x50);
        mm3_map_zoom_update(node, invalid[i]); fresh_scale(node, player, 2);
        near(MEMF(node + 0x50), before);
    }
    /* Equal simulation time behaves the same at different update rates. */
    mm3_map_zoom_reset(node); mm3_map_zoom_reset(second_node);
    fresh_scale(node, player, 2); fresh_scale(second_node, player, 2);
    for (i = 0; i < 60; ++i) mm3_map_zoom_update(node, 1.0f / 60.0f);
    for (i = 0; i < 30; ++i) mm3_map_zoom_update(second_node, 1.0f / 30.0f);
    fresh_scale(node, player, 2); fresh_scale(second_node, player, 2);
    near(MEMF(node + 0x50), MEMF(second_node + 0x50));
    MEM32(player + 4) = 0; fresh_scale(node, player, 2);
    for (i = 0; i < 60; ++i) {
        mm3_map_zoom_update(node, 1.0f / 60.0f);
        mm3_map_zoom_update(second_node, 1.0f / 60.0f);
    }
    fresh_scale(node, player, 2);
    MEM32(player + 4) = 120; fresh_scale(second_node, player, 2);
    assert(MEMF(node + 0x50) > MEMF(second_node + 0x50));
    mm3_map_zoom_reset(node); fresh_scale(node, player, 2); near(MEMF(node + 0x50), 3.2f * 1.10f / NV2A_MINIMAP_SCALE);
    fresh_scale(node, 0, 2); near(MEMF(node + 0x50), 2);
    mm3_map_zoom_reset(0); mm3_map_zoom_update(0, .1f); mm3_map_zoom_apply(0, player);
    for (i = 0; i < 3; ++i) {
        const float invalid[] = {0, -1, INFINITY};
        fresh_scale(node, player, invalid[i]);
        assert(MEMF(node + 0x50) == invalid[i]);
    }
}
static void motion_heading_contract(void) {
    unsigned i;
    float motion, heading;
    check = "published motion/heading";
    MEMF(node + 0x94) = 114; MEMF(node + 0x98) = 384;
    MEMF(second_node + 0x94) = 500; MEMF(second_node + 0x98) = 120;
    MEMF(player) = 1.2f; MEM32(player + 4) = 120;
    mm3_map_zoom_reset(node); mm3_map_zoom_reset(second_node);
    fresh_scale(node, player, 2);
    near(mm3_minimap_motion(114, 384), 0);
    assert(mm3_minimap_heading(node, &heading)); near(heading, 1.2f);
    /* An expanded map uses a fixed compass heading, but routing must retain
     * the car's world heading copied from its native player HUD record. */
    MEMF(node + 0x4C) = 0;
    assert(mm3_minimap_heading(node, &heading)); near(heading, 1.2f);
    for (i = 0; i < 60; ++i) mm3_map_zoom_update(node, 1.0f / 60.0f);
    fresh_scale(node, player, 2);
    motion = mm3_minimap_motion(114, 384);
    near(motion, floorf((1 - expf(-1.0f / .6f)) * 127 + .5f) / 127);
    assert(motion > .8f && motion < .9f);
    MEMF(layout) = 128; MEMF(layout + 4) = 48;
    MEMF(layout + 8) = 384; MEMF(layout + 12) = 384;
    mm3_map_zoom_window(node, layout);
    mm3_map_square_publish_window(0, 0);
    near(mm3_minimap_motion(320, 240), motion);
    near(mm3_minimap_motion(114, 384), 0);
    fresh_scale(node, player, 2); /* Restore the direct-node window for these checks. */
    /* Packed native city positions lose less than a quarter pixel; the
     * asynchronous query still identifies that same authored map window. */
    near(mm3_minimap_motion(114.2f, 383.8f), motion);
    MEM32(player + 4) = 0; fresh_scale(second_node, player, 2);
    near(mm3_minimap_motion(500, 120), 0);
    near(mm3_minimap_motion(114, 384), motion);
    for (i = 0; i < 30; ++i) fresh_scale(node, player, 2);
    near(mm3_minimap_motion(114, 384), motion); /* Drawing cannot advance time. */
    near(mm3_minimap_motion(300, 200), 0); near(mm3_minimap_motion(NAN, 384), 0);
    mm3_map_zoom_apply(node, 0); near(mm3_minimap_motion(114, 384), 0);
    assert(!mm3_minimap_heading(node, &heading));
    MEM32(player + 4) = 120; settle(node, 120, 2);
    near(mm3_minimap_motion(114, 384), 1);
    /* Reallocated scene maps at the same screen centre cannot inherit the
     * previous identity's speed. Distinct split-screen centres stay intact. */
    MEMF(second_node + 0x94) = 114; MEMF(second_node + 0x98) = 384;
    MEM32(player + 4) = 0; fresh_scale(second_node, player, 2);
    near(mm3_minimap_motion(114, 384), 0);
    mm3_map_zoom_reset(second_node);
    assert(!mm3_minimap_heading(second_node, &heading));
    MEMF(player) = NAN; fresh_scale(node, player, 2);
    assert(!mm3_minimap_heading(node, &heading));
    MEMF(player) = -.7f; improved = 0; fresh_scale(node, player, 2);
    assert(mm3_minimap_heading(node, &heading)); near(heading, -.7f);
    near(mm3_minimap_motion(114, 384), 0);
    assert(!mm3_minimap_heading(0, &heading)); assert(!mm3_minimap_heading(node, NULL));
    improved = 1; mm3_map_zoom_reset(node); mm3_map_zoom_reset(second_node);
}
static void expansion_contract(void) {
    const float spans[] = {1, 2, 3, 1};
    const float expected[] = {0, .5f, 1, 0};
    const float invalid_maximum[] = {1, 0, -1, NAN, INFINITY};
    unsigned i;
    check = "published native map expansion";
    mm3_map_zoom_reset(node); mm3_map_zoom_reset(second_node);
    MEMF(node + 0x94) = 114; MEMF(node + 0x98) = 384;
    MEMF(node + 0xBC) = 3; MEM32(player + 4) = 0;
    /* Ctrl reuses the same node. Camera expansion must follow the native
     * transition in both directions, independently of speed or HUD size. */
    for (i = 0; i < sizeof spans / sizeof spans[0]; ++i) {
        MEMF(node + 0xB4) = spans[i]; fresh_scale(node, player, 2);
        near(mm3_minimap_expansion(114, 384), expected[i]);
        near(mm3_minimap_expansion(114.2f, 383.8f), expected[i]);
        near(mm3_minimap_motion(114, 384), 0);
        {
            const uint32_t packed = 0x39200;
            memset((void *)XBOX_PTR(packed), 0xA5, 16);
            mm3_map_square_tag_packed(packed);
            assert(MEM32(packed) == 0xA5A5A5A5u && MEM32(packed + 4) == 0xA5A5A5A5u);
            assert(MEM16(packed + 8) == 23213u);
            assert(MEM16(packed + 0xA) == (unsigned)floorf(expected[i] * 255 + .5f));
            assert(MEM32(packed + 0xC) == 0xA5A5A5A5u); /* Secondary mask UVs survive. */
        }
    }
    MEMF(node + 0xB4) = 3; fresh_scale(node, player, 2);
    {
        const uint32_t packed = 0x39200;
        uint32_t captured[4];
        mm3_map_square_tag_packed(packed);
        memcpy(captured, (void *)XBOX_PTR(packed), sizeof captured);
        MEMF(node + 0xB4) = 1; fresh_scale(node, player, 2);
        assert(!memcmp(captured, (void *)XBOX_PTR(packed), sizeof captured));
        assert(MEM16(packed + 0xA) == 255u); /* Old queued overview keeps its own state. */
        MEMF(node + 0xB4) = 3; fresh_scale(node, player, 2);
    }
    MEMF(layout) = 128; MEMF(layout + 4) = 48;
    MEMF(layout + 8) = 384; MEMF(layout + 12) = 384;
    mm3_map_zoom_window(node, layout);
    mm3_map_square_publish_window(0, 0);
    near(mm3_minimap_expansion(320, 240), 1);
    near(mm3_minimap_expansion(114, 384), 0);
    MEMF(node + 0xB4) = 2; mm3_map_zoom_window(node, layout);
    mm3_map_square_publish_window(0, 0);
    near(mm3_minimap_expansion(320, 240), .5f);
    /* Split-screen expansion ends at 2, with an independent screen center. */
    MEMF(second_node + 0x94) = 500; MEMF(second_node + 0x98) = 120;
    MEMF(second_node + 0xBC) = 2; MEMF(second_node + 0xB4) = 1.5f;
    fresh_scale(second_node, player, 2);
    near(mm3_minimap_expansion(500, 120), .5f);
    near(mm3_minimap_expansion(320, 240), .5f);
    MEMF(second_node + 0xB4) = 2; fresh_scale(second_node, player, 2);
    near(mm3_minimap_expansion(500, 120), 1);
    MEMF(second_node + 0xB4) = 3; fresh_scale(second_node, player, 2);
    near(mm3_minimap_expansion(500, 120), 1);
    MEMF(second_node + 0xB4) = 0; fresh_scale(second_node, player, 2);
    near(mm3_minimap_expansion(500, 120), 0);
    MEMF(second_node + 0xB4) = 2;
    for (i = 0; i < sizeof invalid_maximum / sizeof invalid_maximum[0]; ++i) {
        MEMF(second_node + 0xBC) = invalid_maximum[i];
        fresh_scale(second_node, player, 2);
        near(mm3_minimap_expansion(500, 120), 0);
    }
    MEMF(second_node + 0xBC) = 2; MEMF(second_node + 0xB4) = NAN;
    fresh_scale(second_node, player, 2); near(mm3_minimap_expansion(500, 120), 0);
    MEMF(node + 0xB4) = 1;
    MEMF(layout) = 50; MEMF(layout + 4) = 320;
    MEMF(layout + 8) = 128; MEMF(layout + 12) = 128;
    mm3_map_zoom_window(node, layout);
    mm3_map_square_publish_window(0, 0);
    near(mm3_minimap_expansion(114, 384), 0);
    near(mm3_minimap_expansion(320, 240), 0);
    near(mm3_minimap_expansion(NAN, 384), 0);
    near(mm3_minimap_expansion(114, INFINITY), 0);
    MEMF(node + 0xB4) = 3; fresh_scale(node, player, 2);
    near(mm3_minimap_expansion(114, 384), 1);
    MEMF(layout + 8) = 0; mm3_map_zoom_window(node, layout);
    mm3_map_square_publish_window(0, 0);
    near(mm3_minimap_expansion(114, 384), 0);
    fresh_scale(node, player, 2); mm3_map_zoom_apply(node, 0);
    near(mm3_minimap_expansion(114, 384), 0);
    fresh_scale(node, player, 2); mm3_map_zoom_reset(node);
    near(mm3_minimap_expansion(114, 384), 0);
    MEMF(layout + 8) = MEMF(layout + 12) = 128;
    for (i = 0; i < 3; ++i) {
        const float invalid_offset[] = {NAN, INFINITY, -1};
        fresh_scale(node, player, 2);
        mm3_map_zoom_window(node, layout);
        mm3_map_square_publish_window(invalid_offset[i], 240);
        near(mm3_minimap_expansion(114, 384), 0);
        fresh_scale(node, player, 2);
        mm3_map_zoom_window(node, layout);
        mm3_map_square_publish_window(320, invalid_offset[i]);
        near(mm3_minimap_expansion(114, 384), 0);
    }
    mm3_map_zoom_reset(second_node);
}
static Draw *texture_draw(Draw *list, unsigned count, unsigned texture) {
    unsigned i;
    for (i = 0; i < count; ++i) if (list[i].texture == texture) return &list[i];
    fprintf(stderr, "missing native texture %u (draws=%u)\n", texture, count); abort();
}
/* Sample the affine city mapping in screen pixels. This compares road
 * registration against the executable native rotation, including its sign. */
static float sample_uv(const Draw *draw, float x, float y, unsigned axis) {
    double ax = draw->xy[1][0] - draw->xy[0][0];
    double ay = draw->xy[1][1] - draw->xy[0][1];
    double bx = draw->xy[3][0] - draw->xy[0][0];
    double by = draw->xy[3][1] - draw->xy[0][1];
    double dx = x - draw->xy[0][0], dy = y - draw->xy[0][1];
    double determinant = ax * by - ay * bx;
    double a, b;
    assert(fabs(determinant) > .001);
    a = (dx * by - dy * bx) / determinant;
    b = (ax * dy - ay * dx) / determinant;
    return (float)(draw->uv[0][axis] + a * (draw->uv[1][axis] - draw->uv[0][axis]) +
                   b * (draw->uv[3][axis] - draw->uv[0][axis]));
}
static void native_city_vertex(uint32_t texture, uint32_t args, float x, float y, float u, float v) {
    const uint32_t frame = 0x36000, vertex = 0x37000, texcoords = 0x38100;
    unsigned component;
    abi_start(node); g_ebp = frame; g_ebx = texture; g_edi = vertex;
    g_esp = stack - 16;
    MEM32(frame + 0x50) = texcoords; MEM32(frame + 0x54) = 0x38000;
    MEMF(frame + 0x48) = 0; MEMF(frame + 0x4C) = 0;
    MEMF(texcoords) = u; MEMF(texcoords + 4) = v;
    MEMF(vertex) = x; MEMF(vertex + 4) = y;
    for (component = 0; component < 4; ++component) MEMF(vertex + 0x14 + component * 4) = 1;
    MEMF(g_esp + 12) = .68f; push_float(255); push_float(.37f);
    city_first_vertex();
    assert(g_esp == stack && g_fp_top == 0);
    memcpy((void *)XBOX_PTR(args), emitted, sizeof emitted);
    assert(MEMF(args) == x && MEMF(args + 4) == y && MEM32(args + 8) == 0xFFFFFFFFu);
    near(MEMF(args + 20), .37f); near(MEMF(args + 24), .68f);
}
static void road_samples(const Draw *original, const Draw *square, float cx, float cy, float span) {
    static const float offsets[][2] = {{0, 0}, {17, 9}, {-24, 12}, {8, -22}, {-31, -26}};
    unsigned i, axis;
    check = "square city sampling versus native rotation";
    for (i = 0; i < sizeof offsets / sizeof offsets[0]; ++i)
        for (axis = 0; axis < 2; ++axis)
            near(sample_uv(square, cx + offsets[i][0], cy + offsets[i][1], axis),
                 sample_uv(original, cx + span * offsets[i][0], cy + span * offsets[i][1], axis));
    check = "axis-aligned square window";
    near(square->xy[0][1], square->xy[1][1]);
    near(square->xy[1][0], square->xy[2][0]);
    near(square->xy[2][1], square->xy[3][1]);
    near(square->xy[3][0], square->xy[0][0]);
    check = "map opacity and colors";
    for (i = 0; i < 4; ++i) for (axis = 0; axis < 4; ++axis)
        near(square->rgba[i][axis], original->rgba[i][axis]);
}
static void square_geometry_contract(void) {
    static const float angles[] = {0, .3926990817f, 1.5707963268f, -1.5707963268f, 3.1415926536f};
    const uint32_t vertices = 0x30000, pivot = 0x30100, rotated = 0x30200;
    const uint32_t texture = 0x30400, args = 0x30500;
    const float x[] = {50, 230, 230, 50}, y[] = {300, 300, 396, 396};
    unsigned offcenter, angle, i, component;
    for (offcenter = 0; offcenter < 2; ++offcenter) for (angle = 0; angle < sizeof angles / sizeof angles[0]; ++angle) {
        Draw authored = {0}, retail = {0}, square = {0};
        unsigned char before[0xA0];
        MEMF(pivot) = offcenter ? 97.0f : 140.0f;
        MEMF(pivot + 4) = offcenter ? 373.0f : 348.0f;
        MEMF(texture + 0x50) = .6f; MEMF(texture + 0x54) = .4f;
        MEMF(texture + 0x60) = .1f; MEMF(texture + 0x64) = .2f;
        memset((void *)XBOX_PTR(vertices), 0xA5, sizeof before);
        for (i = 0; i < 4; ++i) {
            uint32_t vertex = vertices + 0xC + i * 0x24;
            MEMF(vertex) = x[i]; MEMF(vertex + 4) = y[i]; MEMF(vertex + 8) = 0;
            MEMF(vertex + 0xC) = -91.0f; MEMF(vertex + 0x10) = 47.0f; /* Ignored by native 116535. */
            for (component = 0; component < 4; ++component) MEMF(vertex + 0x14 + component * 4) = .2f + .1f * (float)component;
            authored.xy[i][0] = x[i]; authored.xy[i][1] = y[i];
            authored.uv[i][0] = .1f + ((i == 1 || i == 2) ? .6f : 0);
            authored.uv[i][1] = .2f + (i >= 2 ? .4f : 0);
        }
        memcpy(before, (void *)XBOX_PTR(vertices), sizeof before);
        memcpy((void *)XBOX_PTR(rotated), before, sizeof before);
        abi_start(node); g_eax = rotated; g_ecx = pivot;
        g_esp -= 4; MEMF(g_esp) = angles[angle]; PUSH32(g_esp, 0xDEADBEEF);
        sub_000C6ADC(); abi_end(0);
        mm3_map_square_geometry(vertices, pivot, angles[angle]);
        assert(!memcmp(before, (void *)XBOX_PTR(vertices), sizeof before));
        /* Execute the real owner-entry PUSH before classification: its quad
         * argument moves to esp+8 after the saved frame is pushed. */
        abi_start(node); g_ecx = texture; MEM32(stack + 4) = vertices;
        city_begin(); abi_end(0); assert(g_eax == 1);
        for (i = 0; i < 4; ++i) {
            retail.xy[i][0] = MEMF(rotated + 0xC + i * 0x24);
            retail.xy[i][1] = MEMF(rotated + 0x10 + i * 0x24);
            memcpy(retail.uv[i], authored.uv[i], sizeof retail.uv[i]);
            memcpy(square.xy[i], authored.xy[i], sizeof square.xy[i]);
            native_city_vertex(texture, args, x[i], y[i], (i == 1 || i == 2) ? 1.0f : 0.0f,
                               i >= 2 ? 1.0f : 0.0f);
            square.uv[i][0] = MEMF(args + 12); square.uv[i][1] = MEMF(args + 16);
        }
        assert(!memcmp(before, (void *)XBOX_PTR(vertices), sizeof before));
        road_samples(&retail, &square, MEMF(pivot), MEMF(pivot + 4), 1);
        /* The clipped native branch can emit newly interpolated screen
         * points. It needs the same city registration and independent mask. */
        native_city_vertex(texture, args, 127, 362, (127 - x[0]) / (x[1] - x[0]),
                           (362 - y[0]) / (y[3] - y[0]));
        near(MEMF(args + 12), sample_uv(&retail, 127, 362, 0));
        near(MEMF(args + 16), sample_uv(&retail, 127, 362, 1));
        near(MEMF(args + 20), .37f); near(MEMF(args + 24), .68f);
        assert(!mm3_map_square_begin(texture, vertices)); /* Pending quad is consumed once. */
    }
}
static void native_contract(void) {
    unsigned large, turn, split, i, axis;
    Draw original[16];
    const float angles[] = {0, .3926990817f, 1.5707963268f, -1.5707963268f, 3.1415926536f};
    for (split = 0; split < 2; ++split) for (large = 0; large < 2; ++large) for (turn = 0; turn < sizeof angles / sizeof angles[0]; ++turn) {
        float native_scale = 1.2f * (split ? .75f : 1.0f);
        float frame_scale = 1.6f * 1.10f / (large ? 1.0f : NV2A_MINIMAP_SCALE);
        float icon_scale = large ? 1.0f : 1.0f / NV2A_MINIMAP_SCALE;
        float span = 1.6f;
        float cx = large ? 320.0f : 114.0f, cy = large ? 240.0f : 384.0f;
        unsigned original_count;
        Draw unrotated, *roads, *roads_zoom, *checkpoint, *checkpoint_zoom;
        MEMF(node + 0x94) = cx; MEMF(node + 0x98) = cy;
        MEMF(node + 0xB4) = large ? 3.0f : 1.0f;
        MEMF(node + 0xBC) = 3; MEM8(node + 0x35) = (uint8_t)split;
        MEMF(layout) = cx - 64; MEMF(layout + 4) = cy - 64;
        MEMF(layout + 8) = 128; MEMF(layout + 12) = 96; MEMF(layout + 16) = 0;
        MEMF(player) = angles[turn]; MEM32(player + 4) = 120;
        MEMF(player + 0x34) = 1.2f;
        MEMF(player + 0x10) = 410; MEMF(player + 0x18) = 615;
        MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1000;
        MEMF(node + 0x5C) = 0; MEMF(node + 0x60) = 1000;
        MEMF(markers) = -20; MEMF(markers + 8) = 12; MEM32(markers + 12) = 0;
        MEM8(node + 0x34) = 1;
        player_present = 1; draw_count = 0;
        zoom_markers = 0;
        abi_start(node); retail_draw(); abi_end(24);
        original_count = draw_count; memcpy(original, draws, sizeof original);
        MEMF(player) = 0; draw_count = 0;
        abi_start(node); retail_draw(); abi_end(24);
        unrotated = *texture_draw(draws, draw_count, 1); MEMF(player) = angles[turn];
        mm3_map_zoom_reset(node);
        zoom_markers = 1;
        draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
        near(MEMF(node + 0x50), native_scale * frame_scale);
        assert(draw_count + 1 == original_count);
        roads = texture_draw(original, original_count, 1); roads_zoom = texture_draw(draws, draw_count, 1);
        road_samples(roads, roads_zoom, cx, cy, 1 / frame_scale);
        for (i = 0; i < draw_count; ++i) {
            unsigned vertex;
            Draw *retail;
            assert(draws[i].texture != 2); /* Native MapCircle border is omitted. */
            if (draws[i].texture == 1) continue;
            retail = texture_draw(original, original_count, draws[i].texture);
            for (axis = 0; axis < 2; ++axis) {
                float centre = 0;
                for (vertex = 0; vertex < 4; ++vertex)
                    centre += retail->xy[vertex][axis] * .25f;
                for (vertex = 0; vertex < 4; ++vertex)
                    near(draws[i].xy[vertex][axis], draws[i].texture == 4 ?
                        (axis ? cy : cx) + (centre - (axis ? cy : cx)) * frame_scale + (retail->xy[vertex][axis] - centre) * .75f * icon_scale :
                        centre + (retail->xy[vertex][axis] - centre) * (draws[i].texture == 3 ? 0 : icon_scale));
            }
        }
        /* The launcher's north marker keeps the native letter's quad. */
        north_marker = 1; draw_count = 0;
        abi_start(node); zoom_draw(); abi_end(24);
        assert(draw_count + 1 == original_count);
        {
            const Draw *north = texture_draw(draws, draw_count, 3);
            assert(fabsf(north->xy[0][0] - north->xy[2][0]) > 1 || fabsf(north->xy[0][1] - north->xy[2][1]) > 1);
        }
        north_marker = 0;
        for (i = 0; i < 600; ++i) mm3_map_zoom_update(node, 1.0f / 60.0f);
        draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
        assert(draw_count + 1 == original_count); near(MEMF(node + 0x50), native_scale * frame_scale / span);
        near(mm3_minimap_motion(native_window_x, native_window_y), 1);
        /* Single-precision smoothing asymptotically approaches the cap; use
         * its observed scalar for the much larger native road coordinates. */
        span = native_scale / MEMF(node + 0x50);
        roads = texture_draw(original, original_count, 1); roads_zoom = texture_draw(draws, draw_count, 1);
        checkpoint = texture_draw(original, original_count, 4); checkpoint_zoom = texture_draw(draws, draw_count, 4);
        road_samples(roads, roads_zoom, cx, cy, span);
        for (i = 0; i < 4; ++i) for (axis = 0; axis < 2; ++axis) {
            float centre = axis ? cy : cx;
            check = "native road scale";
            near(roads_zoom->xy[i][axis], centre + (unrotated.xy[i][axis] - centre) / span);
        }
        for (axis = 0; axis < 2; ++axis) {
            float a = 0, b = 0, centre = axis ? cy : cx;
            for (i = 0; i < 4; ++i) { a += checkpoint->xy[i][axis] * .25f; b += checkpoint_zoom->xy[i][axis] * .25f; }
            check = "checkpoint centre";
            near(b, centre + (a - centre) / span);
            check = "checkpoint icon size";
            for (i = 0; i < 4; ++i)
                near(checkpoint_zoom->xy[i][axis] - b, (checkpoint->xy[i][axis] - a) * .75f * icon_scale);
        }
        /* Native scale reconstruction prevents zoom accumulating over draws. */
        draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
        near(MEMF(node + 0x50), native_scale / span);
        player_present = 0; draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
        near(MEMF(node + 0x50), split ? .75f : 1.0f);
    }
    /* The initialization seam resets stale identity state before native stores. */
    settle(node, 120, 1); MEM8(node + 0x35) = 0;
    MEMF(layout) = 50; MEMF(layout + 4) = 320;
    abi_start(node); zoom_reset(); abi_end(16);
    fresh_scale(node, player, 1); near(MEMF(node + 0x50), 1.6f * 1.10f / NV2A_MINIMAP_SCALE);
    /* The actual native update argument is +0x10 before its frame prologue. */
    MEM32(player + 4) = 120; fresh_scale(node, player, 1);
    MEMF(node + 0x9C) = MEMF(node + 0x94); MEMF(node + 0xA0) = MEMF(node + 0x98);
    MEMF(node + 0xAC) = MEMF(node + 0xB0) = 0;
    abi_start(node); MEMF(stack + 16) = .1f; zoom_update(); abi_end(20);
    fresh_scale(node, player, 1);
    near(1.6f * 1.10f / NV2A_MINIMAP_SCALE / MEMF(node + 0x50), 1 + .6f * (1 - expf(-.1f / .6f)));
}
static void native_ctrl_contract(void) {
    unsigned phase, frame;
    const float start_x = -206, start_y = 144;
    check = "real Ctrl input, movement and renderer viewport publication";
    mm3_map_zoom_reset(node);
    renderer_half_width = 320; renderer_half_height = 240;
    race_input_present = 1; player_present = 1; zoom_markers = 1;
    MEM32(0x28000 + 0x10) = MEM32(0x28000 + 0x14) = 0;
    MEM32(0x28000 + 0x2C) = 0x29000;
    MEM8(node + 0x35) = 0; MEM8(node + 0x34) = 1;
    MEMF(node + 0x94) = MEMF(node + 0x9C) = start_x;
    MEMF(node + 0x98) = MEMF(node + 0xA0) = start_y;
    /* Native 11D367 places the overview at local (0, 0). The producer
     * converts that to screen (320, 240), after the map owner has drawn. */
    MEMF(node + 0xA4) = MEMF(node + 0xA8) = 0;
    MEMF(node + 0xB4) = MEMF(node + 0xB8) = 1; MEMF(node + 0xBC) = 3;
    MEMF(node + 0xAC) = MEMF(node + 0xB0) = 0;
    MEMF(layout) = start_x - 64; MEMF(layout + 4) = start_y - 64;
    MEMF(layout + 8) = MEMF(layout + 12) = 128; MEMF(layout + 16) = 0;
    MEMF(player) = .3926990817f; MEM32(player + 4) = 120; MEMF(player + 0x34) = 1.2f;
    MEMF(player + 0x10) = 410; MEMF(player + 0x18) = 615;
    MEMF(node + 0x54) = MEMF(node + 0x5C) = 0;
    MEMF(node + 0x58) = MEMF(node + 0x60) = 1000;
    MEMF(markers) = -20; MEMF(markers + 8) = 12; MEM32(markers + 12) = 0;
    for (phase = 0; phase < 3; ++phase) {
        ctrl_pressed = phase == 1;
        for (frame = 0; frame < 5; ++frame) {
            float expansion;
            Draw *checkpoint;
            draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
            expansion = (MEMF(node + 0xB4) - 1) / 2;
            near(native_window_x, MEMF(node + 0x94) + 320);
            near(native_window_y, MEMF(node + 0x98) + 240);
            near(mm3_minimap_expansion(native_window_x, native_window_y), expansion);
            /* Local snapshots used to miss the renderer's lookup entirely. */
            near(mm3_minimap_expansion(MEMF(node + 0x94), MEMF(node + 0x98)), 0);
            checkpoint = texture_draw(draws, draw_count, 4);
            assert(isfinite(checkpoint->xy[0][0]) && isfinite(checkpoint->xy[0][1]));
            if (phase == 1 && frame == 4) {
                near(native_window_x, 320); near(native_window_y, 240);
                near(expansion, 1);
                assert(checkpoint->xy[0][0] > 128 && checkpoint->xy[0][0] < 512);
                assert(checkpoint->xy[0][1] > 48 && checkpoint->xy[0][1] < 432);
            }
            abi_start(node); MEMF(stack + 16) = .1f; zoom_update(); abi_end(20);
        }
        near(MEMF(node + 0x94), ctrl_pressed ? 0 : start_x);
        near(MEMF(node + 0x98), ctrl_pressed ? 0 : start_y);
        near(MEMF(node + 0xB4), ctrl_pressed ? 3.0f : 1.0f);
    }
    race_input_present = ctrl_pressed = 0;
    renderer_half_width = renderer_half_height = 0;
    mm3_map_zoom_reset(node);
}
static void native_ctrl_checkpoint_contract(void) {
    const unsigned rates[] = {30, 60, 144};
    const unsigned speeds[] = {0, 120};
    const float angles[] = {0, .3926990817f, 1.5707963268f, -1.5707963268f};
    const float durations[] = {.2f, 1, .08f, .08f, 1, 1, 1};
    const int held[] = {0, 1, 0, 1, 0, 1, 0};
    /* Retain inside/edge/newly revealed/far coverage with the closer idle
     * camera; the world-space distances are 1/1.6 of the earlier fixture. */
    const float positions[][2] = {{0, 0}, {70, 0}, {-75, 0}, {0, 75},
        {0, -75}, {62.5f, 43.75f}, {500, -375}, {-562.5f, 437.5f}};
    unsigned rate, split, turn, speed, phase, frame, i, j, checkpoints, newly_visible = 0;
    check = "native Ctrl transition, interrupted release and all checkpoint edges";
    race_input_present = player_present = zoom_markers = 1;
    marker_count = sizeof positions / sizeof positions[0];
    renderer_half_width = 320; renderer_half_height = 240;
    MEM32(0x28000 + 0x10) = MEM32(0x28000 + 0x14) = 0;
    MEM32(0x28000 + 0x2C) = 0x29000;
    for (rate = 0; rate < sizeof rates / sizeof rates[0]; ++rate)
    for (split = 0; split < 2; ++split)
    for (turn = 0; turn < sizeof angles / sizeof angles[0]; ++turn)
    for (speed = 0; speed < sizeof speeds / sizeof speeds[0]; ++speed) {
        float dt = 1.0f / rates[rate], base_width = 128, base_height = 96;
        float split_scale = split ? MEMF(0x385DE4) : 1.0f;
        float start_x = -206 - (split ? MEMF(0x385FB4) : 0);
        float start_y = 144 + (split ? MEMF(0x385FB4) : 0);
        float driving_scale;
        mm3_map_zoom_reset(node);
        MEM8(node + 0x35) = (uint8_t)split; MEM8(node + 0x34) = 1;
        MEMF(node + 0xBC) = split ? 2.0f : 3.0f;
        MEMF(node + 0xB4) = MEMF(node + 0xB8) = 1;
        MEMF(node + 0x94) = MEMF(node + 0x9C) = start_x;
        MEMF(node + 0x98) = MEMF(node + 0xA0) = start_y;
        MEMF(node + 0xA4) = 0; MEMF(node + 0xA8) = split ? 100.0f : 0;
        MEMF(layout) = -206 - base_width * .5f;
        MEMF(layout + 4) = 144 - base_height * .5f;
        MEMF(layout + 8) = base_width; MEMF(layout + 12) = base_height;
        MEMF(layout + 16) = 0;
        MEMF(node + 0x54) = MEMF(node + 0x5C) = 0;
        MEMF(node + 0x58) = MEMF(node + 0x60) = 1000;
        MEMF(player) = angles[turn]; MEM32(player + 4) = speeds[speed];
        MEMF(player + 0x34) = 1.2f;
        MEMF(player + 0x10) = 410; MEMF(player + 0x18) = 615;
        for (i = 0; i < marker_count; ++i) {
            MEMF(markers + i * 24) = positions[i][0];
            MEMF(markers + i * 24 + 8) = positions[i][1];
            MEM32(markers + i * 24 + 12) = (i & 1 ? 3u : 0u) | (i & 2 ? 0x80u : 0u);
        }
        /* Establish the real native draw's speed target, then hold speed
         * steady so Ctrl is the sole cause of any change in world coverage. */
        ctrl_pressed = 0; draw_count = 0;
        abi_start(node); zoom_draw(); abi_end(24);
        for (frame = 0; frame < 600; ++frame) mm3_map_zoom_update(node, 1.0f / 60.0f);
        draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
        driving_scale = MEMF(node + 0x50);
        for (phase = 0; phase < sizeof held / sizeof held[0]; ++phase) {
            ctrl_pressed = held[phase];
            for (frame = 0; frame < (unsigned)ceilf(durations[phase] * rates[rate]); ++frame) {
                float c = cosf(angles[turn]), s = sinf(angles[turn]);
                float scale, rx, ry, cx, cy, expansion;
                unsigned matched[sizeof positions / sizeof positions[0]] = {0};
                /* Pulse timing is orthogonal to Ctrl's interpolation. Exercise
                 * the maximum pulse too, without wrapping the native timer. */
                MEMF(node + 0xAC) = MEMF(node + 0xB0) = 0;
                draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
                scale = MEMF(node + 0x50) * MEMF(0x385EAC) / 1000;
                near(MEMF(node + 0x50) * (NV2A_MINIMAP_SCALE +
                    (1 - NV2A_MINIMAP_SCALE) * map_expansion(node)), driving_scale * NV2A_MINIMAP_SCALE);
                rx = MEMF(node + 0xB4) * (base_width * split_scale * .5f - MEMF(0x385E08));
                ry = MEMF(node + 0xB4) * (base_height * split_scale * .5f - MEMF(0x385E08));
                cx = MEMF(node + 0x94) + renderer_half_width;
                cy = MEMF(node + 0x98) + renderer_half_height;
                expansion = (MEMF(node + 0xB4) - 1) / (MEMF(node + 0xBC) - 1);
                near(mm3_minimap_expansion(cx, cy), expansion);
                checkpoints = 0;
                for (i = 0; i < draw_count; ++i) if (draws[i].texture == 4) {
                    float x = 0, y = 0;
                    unsigned found = 0;
                    ++checkpoints;
                    for (j = 0; j < 4; ++j) {
                        x += draws[i].xy[j][0] * .25f;
                        y += draws[i].xy[j][1] * .25f;
                    }
                    assert(fabsf(x - cx) <= rx + .001f && fabsf(y - cy) <= ry + .001f);
                    for (j = 0; j < marker_count; ++j) {
                        float local_x = -positions[j][0] * scale;
                        float local_y = positions[j][1] * scale;
                        float sx = c * local_x + s * local_y;
                        float sy = c * local_y - s * local_x;
                        float clamp = fmaxf(1, fmaxf(fabsf(sx) / rx, fabsf(sy) / ry));
                        if (!matched[j] && fabsf(x - cx - sx / clamp) < .001f &&
                            fabsf(y - cy - sy / clamp) < .001f) {
                            matched[j] = found = 1;
                            if (!split && j == 1 && !speed && expansion == 1 && clamp == 1) newly_visible = 1;
                            break;
                        }
                    }
                    assert(found); /* Native clamp and final camera rotation agree. */
                }
                assert(checkpoints == marker_count && draw_count == marker_count + 2);
                for (j = 0; j < marker_count; ++j) assert(matched[j]);
                abi_start(node); MEMF(stack + 16) = dt; zoom_update(); abi_end(20);
            }
            if (durations[phase] >= 1) {
                near(MEMF(node + 0xB4), ctrl_pressed ? MEMF(node + 0xBC) : 1);
                near(MEMF(node + 0x94), ctrl_pressed ? 0 : start_x);
                near(MEMF(node + 0x98), ctrl_pressed ? (split ? 100.0f : 0) : start_y);
            }
        }
    }
    assert(newly_visible);
    marker_count = 1; race_input_present = ctrl_pressed = 0;
    renderer_half_width = renderer_half_height = 0;
    MEM8(node + 0x35) = 0; MEMF(node + 0xBC) = 3;
    mm3_map_zoom_reset(node);
    puts("PASS native Ctrl checkpoint continuity: 48 rest/speed/rate/heading/split scenarios, interrupted transitions, all eight near/far checkpoints retained");
}
static void native_scale_progression(void) {
    float stopped;
    unsigned i;
    /* Checkpoint proximity changes the stock scale independently of speed.
     * Preserve this layout's first valid framing and let speed zoom it out. */
    mm3_map_zoom_reset(node);
    MEM32(player + 4) = 0; MEMF(player + 0x34) = .728f;
    fresh_scale(node, player, .728f); stopped = MEMF(node + 0x50);
    near(stopped, .728f * 1.6f * 1.10f / NV2A_MINIMAP_SCALE);
    MEM32(player + 4) = 120; MEMF(player + 0x34) = 1.4f;
    fresh_scale(node, player, 1.4f); near(MEMF(node + 0x50), stopped);
    for (i = 0; i < 600; ++i) mm3_map_zoom_update(node, 1.0f / 60.0f);
    fresh_scale(node, player, 1.4f); near(MEMF(node + 0x50), stopped / 1.6f);
    assert(MEMF(node + 0x50) < stopped); near(MEMF(player + 0x34), 1.4f);
    /* At fixed speed, repeated native checkpoint updates cannot zoom back in. */
    for (i = 0; i < 4; ++i) {
        const float stock[] = {.5f, .728f, 1, 1.4f};
        MEMF(player + 0x34) = stock[i]; fresh_scale(node, player, stock[i]);
        near(MEMF(node + 0x50), stopped / 1.6f); near(MEMF(player + 0x34), stock[i]);
    }
    /* A reused node captures its new layout, while a null player or invalid
     * stock scale cannot establish a stale default baseline. */
    mm3_map_zoom_reset(node); MEM32(player + 4) = 0;
    fresh_scale(node, 0, 1); near(MEMF(node + 0x50), 1);
    fresh_scale(node, player, NAN); assert(isnan(MEMF(node + 0x50)));
    fresh_scale(node, player, -1); near(MEMF(node + 0x50), -1);
    fresh_scale(node, player, .9f); near(MEMF(node + 0x50), .9f * 1.6f * 1.10f / NV2A_MINIMAP_SCALE);
}
static void native_speed_record(float metres_per_second) {
    const uint32_t vehicle = 0x2A000, driver = 0x2B000;
    const uint32_t vehicle_vtable = 0x2C000, driver_vtable = 0x2C100;
    /* Execute 218844's real m/s -> absolute integer km/h producer. Only
     * driver/physics virtual calls are faked, rather than assuming that +4
     * already contains the desired speed or changing displayed speed units. */
    MEM32(vehicle) = vehicle_vtable; MEM32(vehicle + 0x19C) = driver;
    MEM32(driver) = driver_vtable; MEM32(driver + 8) = 0x2B100;
    MEM32(0x2B100 + 0x38) = 0;
    MEM32(driver_vtable + 0x50) = 0x200;
    MEM32(driver_vtable + 0x60) = 0x204;
    MEM32(vehicle_vtable + 0x20) = 0x208;
    MEMF(0x2D000 + 8) = metres_per_second;
    abi_start(vehicle); MEM32(stack + 4) = player;
    sub_00218844(); abi_end(8);
}
static void native_speed_camera_contract(void) {
    unsigned speed, frame, i, axis;
    Draw stopped_car, moving_car, stock_car;
    float stopped_scale = 0;
    const float velocity[] = {0, 40, -40, 0};
    check = "native physics speed -> HUD record -> update -> map drawing";
    mm3_map_zoom_reset(node);
    player_present = 1; race_input_present = ctrl_pressed = 0;
    renderer_half_width = renderer_half_height = 0;
    MEMF(node + 0x94) = MEMF(node + 0x9C) = 114;
    MEMF(node + 0x98) = MEMF(node + 0xA0) = 384;
    MEMF(node + 0xB4) = MEMF(node + 0xB8) = 1; MEMF(node + 0xBC) = 3;
    MEMF(node + 0xAC) = MEMF(node + 0xB0) = 0;
    MEM8(node + 0x35) = 0; MEM8(node + 0x34) = 1;
    MEMF(layout) = 50; MEMF(layout + 4) = 320;
    MEMF(layout + 8) = MEMF(layout + 12) = 128; MEMF(layout + 16) = 0;
    MEMF(player) = 0; MEMF(player + 0x34) = 1.2f;
    MEMF(player + 0x10) = 410; MEMF(player + 0x18) = 615;
    MEMF(node + 0x54) = MEMF(node + 0x5C) = 0;
    MEMF(node + 0x58) = MEMF(node + 0x60) = 1000;
    MEMF(markers) = 0; MEMF(markers + 8) = 0; MEM32(markers + 12) = 1;
    zoom_markers = 0; draw_count = 0; abi_start(node); retail_draw(); abi_end(24);
    stock_car = *texture_draw(draws, draw_count, 5);
    for (speed = 0; speed < sizeof velocity / sizeof velocity[0]; ++speed) {
        native_speed_record(velocity[speed]);
        assert(SMEM32(player + 4) == (velocity[speed] == 0 ? 0 : 144));
        zoom_markers = 1; draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
        for (frame = 0; frame < 360; ++frame) {
            /* Cosmetic pulse clocks are outside this speed-camera contract. */
            MEMF(node + 0xAC) = MEMF(node + 0xB0) = 0;
            abi_start(node); MEMF(stack + 16) = 1.0f / 60.0f; zoom_update(); abi_end(20);
            draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
        }
        moving_car = *texture_draw(draws, draw_count, 5);
        if (!speed) { stopped_scale = MEMF(node + 0x50); stopped_car = moving_car; }
        /* At maximum speed match the former stopped-car world scale.
         * Stopping brings the city 60% closer without enlarging You. */
        near(MEMF(node + 0x50), stopped_scale / (speed == 1 || speed == 2 ? 1.6f : 1.0f));
        near(MEMF(node + 0x50), 1.2f * 1.10f / NV2A_MINIMAP_SCALE *
            (speed == 1 || speed == 2 ? 1.0f : 1.6f));
        for (i = 0; i < 4; ++i) for (axis = 0; axis < 2; ++axis) {
            float centre = axis ? 384.0f : 114.0f;
            near((moving_car.xy[i][axis] - centre) * NV2A_MINIMAP_SCALE,
                stock_car.xy[i][axis] - centre);
            near(moving_car.xy[i][axis], stopped_car.xy[i][axis]);
        }
        assert(draw_count == 3); /* City, compass and current car; fixed frame. */
        near(mm3_minimap_motion(114, 384), speed == 1 || speed == 2 ? 1.0f : 0.0f);
    }
    /* Ctrl expands the native window around the same world camera and keeps
     * the speed span. Icon sizes still follow the renderer's flattening. */
    for (i = 0; i <= 4; ++i) {
        float expansion = (float)i / 4;
        float enlargement = NV2A_MINIMAP_SCALE * (1 - expansion) + expansion;
        MEMF(node + 0xB4) = 1 + 2 * expansion;
        fresh_scale(node, player, 1.2f);
        near(MEMF(node + 0x50) * enlargement, 1.2f * 1.6f * 1.10f);
        for (axis = 0; axis <= 5; ++axis)
            near(mm3_map_zoom_checkpoint_scale(node, axis) * enlargement,
                axis == 0 || axis == 3 ? .75f : 1.0f);
    }
    MEMF(node + 0xB4) = 1;
    mm3_map_zoom_reset(node);
}
static void square_marker_contract(void) {
    const float angles[] = {0, .3926990817f, 1.5707963268f, -1.5707963268f};
    unsigned turn, far, axis, i;
    float cx = 114, cy = 384;
    MEMF(node + 0x94) = cx; MEMF(node + 0x98) = cy;
    MEMF(node + 0xB4) = 1; MEMF(node + 0x50) = 1;
    MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1000;
    MEMF(layout + 8) = 128; MEMF(layout + 12) = 96;
    MEM32(markers + 12) = 0;
    mm3_map_zoom_reset(node);
    for (turn = 0; turn < sizeof angles / sizeof angles[0]; ++turn) for (far = 0; far < 2; ++far) {
        float c = cosf(angles[turn]), s = sinf(angles[turn]);
        float rx = 64 - MEMF(0x385E08), ry = 48 - MEMF(0x385E08);
        float sx = rx * (far ? 2.0f : .85f), sy = ry * (far ? 1.5f : .85f);
        float localx = c * sx - s * sy, localy = s * sx + c * sy;
        float world_to_pixels = MEMF(0x385EAC) / 1000;
        Draw *checkpoint;
        MEMF(node + 0x4C) = angles[turn];
        MEMF(markers) = -localx / world_to_pixels; MEMF(markers + 8) = localy / world_to_pixels;
        check = "square marker distance after camera turn";
        near(mm3_map_square_marker_distance(node, layout, localx, localy), rx * (far ? 2.0f : .85f));
        draw_count = 0; zoom_markers = 0;
        abi_start(node); MEM32(stack + 16) = far; retail_marker(); abi_end(20);
        if (!far) assert(draw_count == 0); /* A useful square corner is beyond the stock circle. */
        draw_count = 0; zoom_markers = 1;
        abi_start(node); MEM32(stack + 16) = far; zoom_marker(); abi_end(20);
        assert(draw_count == 1);
        checkpoint = texture_draw(draws, draw_count, 4);
        for (axis = 0; axis < 2; ++axis) {
            float center = 0, expected = axis ? cy + sy / (far ? 2 : 1) : cx + sx / (far ? 2 : 1);
            for (i = 0; i < 4; ++i) center += checkpoint->xy[i][axis] * .25f;
            check = far ? "native marker clamps to square edge" : "native marker appears in square corner";
            near(center, expected);
        }
    }
}
static void native_ui_pool_owner(uint32_t owner, int construct) {
    abi_start(owner);
    if (construct) {
        g_esi = owner; sub_0011DD26();
        assert(g_esi == owner && g_eax == owner);
        g_esi = 0x22334455;
    } else sub_0011DD00();
    abi_end(4);
}
static void native_ui_pool_contract(void) {
    const uint32_t first = 0x40000, second = 0x40100;
    unsigned i, allocations;
    check = "native shared UI vertex pool allocation and frame reset";
    MEM32(0x3C5688) = MEM32(0x3C568C) = MEM32(0x3C5690) = 0;
    MEM8(0x3C5694) = 0; MEM32(0x3A0EC0) = 5000;
    improved = 0;
    native_ui_pool_owner(first, 1);
    assert(pool_allocations == 1 && !pool_frees && pool_bytes == 5000 * 16);
    assert(MEM32(0x3C5688) == pool_live && MEM32(0x3C5690) == 1);
    MEM32(0x3C568C) = 4998;
    abi_start(0); zoom_pool(); abi_end(4);
    assert(!MEM32(0x3C568C) && !MEM8(0x3C5694));
    assert(MEM32(0x3A0EC0) == 5000 && pool_allocations == 1 && !pool_frees);
    /* This is the real first Improved frame boundary. Native code resets the
     * frame counter, frees the old pool and allocates its own 160,000-vertex
     * mode before any map, marker or later HUD vertex can be packed. */
    improved = 1; MEM32(0x3C568C) = 4998;
    abi_start(0); zoom_pool(); abi_end(4);
    assert(!MEM32(0x3C568C) && !MEM8(0x3C5694));
    assert(MEM32(0x3A0EC0) == 160000 && pool_bytes == 160000 * 16);
    assert(pool_allocations == 2 && pool_frees == 1 && MEM32(0x3C5688) == pool_live);
    assert(MEM32(0x3C5690) == 1);
    for (i = 0; i < 64; ++i) {
        MEM32(0x3C568C) = 10000 + i;
        abi_start(0); zoom_pool(); abi_end(4);
        assert(!MEM32(0x3C568C) && !MEM8(0x3C5694));
        assert(pool_allocations == 2 && pool_frees == 1);
    }
    improved = 0;
    abi_start(0); zoom_pool(); abi_end(4);
    assert(MEM32(0x3A0EC0) == 160000 && pool_allocations == 2 && pool_frees == 1);
    native_ui_pool_owner(second, 1);
    assert(MEM32(0x3C5690) == 2 && pool_allocations == 2);
    native_ui_pool_owner(first, 0);
    assert(MEM32(0x3C5690) == 1 && pool_live && pool_frees == 1);
    native_ui_pool_owner(second, 0);
    assert(!MEM32(0x3C5690) && !MEM32(0x3C5688) && !pool_live && pool_frees == 2);
    /* A scene change uses the retained native capacity for its next owner. */
    native_ui_pool_owner(first, 1);
    assert(pool_bytes == 160000 * 16 && pool_allocations == 3);
    native_ui_pool_owner(first, 0);
    /* A caller's larger custom pool must never be reduced to our threshold. */
    MEM32(0x3A0EC0) = 200000; improved = 1;
    native_ui_pool_owner(first, 1); allocations = pool_allocations;
    abi_start(0); zoom_pool(); abi_end(4);
    assert(MEM32(0x3A0EC0) == 200000 && pool_bytes == 200000 * 16);
    assert(pool_allocations == allocations && !MEM8(0x3C5694));
    native_ui_pool_owner(first, 0);
    /* Native's external grow request remains honored under Stock too. The
     * seam adds a request; it does not override any existing native request. */
    MEM32(0x3A0EC0) = 5000; improved = 0;
    native_ui_pool_owner(first, 1); MEM8(0x3C5694) = 1;
    abi_start(0); zoom_pool(); abi_end(4);
    assert(MEM32(0x3A0EC0) == 160000 && pool_bytes == 160000 * 16);
    assert(!MEM8(0x3C5694)); native_ui_pool_owner(first, 0);
    /* The real native resize also accepts an empty pool; no stale pointer is
     * dereferenced or freed if the request precedes its first owner. */
    MEM32(0x3A0EC0) = 5000; improved = 1; allocations = pool_allocations;
    abi_start(0); zoom_pool(); abi_end(4);
    assert(pool_allocations == allocations + 1 && pool_bytes == 160000 * 16);
    native_ui_pool_owner(first, 1); native_ui_pool_owner(first, 0);
    assert(pool_allocations == pool_frees && !pool_live && !MEM32(0x3C5688));
    puts("PASS native UI pool: first-frame 5000->160000 resize, exact native allocation/free ABI, repeated frames, Stock, larger custom pools and shared-owner scene lifecycle");
}
int main(void) {
    g_xbox_mem_offset = (ptrdiff_t)memory; native_constants();
    MEMF(1 + 0x50) = 1; MEMF(1 + 0x54) = 1;
    MEMF(1 + 0x60) = 0; MEMF(1 + 0x64) = 0;
    MEM32(node) = 0x26000; MEM32(0x26000 + 0xC4) = 0x0011CAE8;
    MEM32(node + 8) = 1; MEM32(node + 0x20) = 2; MEM32(node + 0x24) = 3; MEM32(node + 0x10) = 4;
    MEM32(node + 0x14) = 5; /* Actual native Hud/You; +24 is MapNorth. */
    MEM32(node + 0x8C) = property; MEM32(property) = property + 0x100;
    MEM32(property + 0x100 + 0x5C) = 0x100;
    MEM32(players + 0x14) = player; MEM32(0x407F78 + 4) = markers;
    speed_contract(); motion_heading_contract(); expansion_contract(); square_geometry_contract(); native_ctrl_checkpoint_contract(); native_contract(); native_ctrl_contract(); native_scale_progression(); native_speed_camera_contract(); square_marker_contract();
    /* Live Stock selection restores the real native rotation, border, scale
     * and marker sizes with identical guest calling conventions. */
    improved = 0; player_present = 1; zoom_markers = 0;
    draw_count = 0; abi_start(node); retail_draw(); abi_end(24);
    {
        Draw original[16]; unsigned count = draw_count, i;
        memcpy(original, draws, sizeof original);
        zoom_markers = 1; draw_count = 0;
        abi_start(node); zoom_draw(); abi_end(24);
        assert(draw_count == count);
        for (i = 0; i < count; ++i) {
            if (draws[i].texture == 3) {
                unsigned vertex, axis;
                for (vertex = 1; vertex < 4; ++vertex) for (axis = 0; axis < 2; ++axis)
                    near(draws[i].xy[vertex][axis], draws[i].xy[0][axis]);
            } else assert(!memcmp(&original[i], &draws[i], sizeof(Draw)));
        }
        /* Shown, Stock draws exactly as retail, north letter included. */
        north_marker = 1; draw_count = 0;
        abi_start(node); zoom_draw(); abi_end(24);
        assert(draw_count == count);
        for (i = 0; i < count; ++i) assert(!memcmp(&original[i], &draws[i], sizeof(Draw)));
        north_marker = 0;
        near(mm3_map_zoom_checkpoint_scale(node, 0), 1);
        near(mm3_map_square_marker_distance(node, layout, 20, 30), hypotf(20, 30));
    }
    improved = 1; mm3_map_zoom_reset(node);
    MEMF(player + 0x10) = 410; MEMF(player + 0x14) = 8; MEMF(player + 0x18) = 615;
    fresh_scale(node, player, 1);
    {
        float copied[3]; assert(mm3_minimap_origin(node, copied));
        near(copied[0], 410); near(copied[1], 8); near(copied[2], 615);
        mm3_map_zoom_apply(node, 0); assert(!mm3_minimap_origin(node, copied));
        fresh_scale(node, player, 1); mm3_map_zoom_reset(node);
        assert(!mm3_minimap_origin(node, copied));
    }
    native_ui_pool_contract();
    puts("PASS square map/zoom: native Ctrl input/release, screen-space viewport publication, city/marker turns, colors/opacity, small/big/split layouts, speed smoothing and guest ABI");
    return 0;
}
