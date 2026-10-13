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
static unsigned char memory[0x440000];
static const uint32_t stack = 0x80000, node = 0x10000, second_node = 0x10200;
static const uint32_t player = 0x20000, players = 0x21000, property = 0x22000;
static const uint32_t layout = 0x23000, markers = 0x24000;
typedef struct { uint32_t texture; float xy[4][2], uv[4][2], mask_uv[4][2], rgba[4][4]; } Draw;
static Draw draws[16];
static uint32_t emitted[7];
static unsigned draw_count;
static int player_present = 1;
static int zoom_markers;
static const char *check = "speed";

#include "production_map_zoom.c"

static void push_float(double value) {
    g_fp_top = (g_fp_top + 7) & 7;
    g_fp_stack[g_fp_top] = value;
}
void retail_marker(void);
void zoom_marker(void);
static void fake_icall(uint32_t address) {
    if (address == 0x100) { push_float(0); g_esp += 8; return; }
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
void sub_001F0399(void) {
    g_eax = (uint32_t)(int32_t)g_fp_stack[g_fp_top];
    g_fp_top = (g_fp_top + 1) & 7; g_esp += 4;
}
void sub_001F33A2(void) { g_eax = 0; g_esp += 4; }
void sub_001F3979(void) { assert(!"unreachable race input branch"); }
void sub_0004EE0D(void) { g_eax = 1; g_esp += 4; }
void sub_002414D8(void) { assert(!"unreachable alternate marker color branch"); }
void sub_00012D29(void) { assert(!"unreachable wrapped animation branch"); }
void sub_000629BE(void) { g_eax = 0; g_esp += 4; }
void sub_0006AEB6(void) { assert(!"unreachable map movement branch"); }
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
    draws[draw_count].texture = texture;
    for (i = 0; i < 4; ++i) {
        unsigned component;
        draws[draw_count].xy[i][0] = MEMF(vertices + 0xC + i * 0x24);
        draws[draw_count].xy[i][1] = MEMF(vertices + 0x10 + i * 0x24);
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
    MEMF(identity + 0x50) = scale;
    mm3_map_zoom_apply(identity, record);
}
static float settle(uint32_t identity, int speed, float baseline) {
    unsigned i;
    MEM32(player + 4) = (uint32_t)speed;
    mm3_map_zoom_reset(identity);
    fresh_scale(identity, player, baseline);
    for (i = 0; i < 600; ++i) mm3_map_zoom_update(identity, 1.0f / 60.0f);
    fresh_scale(identity, player, baseline);
    return baseline / MEMF(identity + 0x50);
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
    const float spans[] = {1, 1, 1, 1.0390625f, 1.125f, 1.2109375f, 1.25f, 1.25f, 1.25f, 1.25f};
    unsigned i;
    for (i = 0; i < sizeof speeds / sizeof speeds[0]; ++i)
        near(settle(node, speeds[i], 2), spans[i]);
    mm3_map_zoom_reset(node);
    MEM32(player + 4) = 120; fresh_scale(node, player, 2);
    near(MEMF(node + 0x50), 2); /* Draw announces the target, not a time step. */
    for (i = 0; i < 30; ++i) fresh_scale(node, player, 2);
    near(MEMF(node + 0x50), 2);
    mm3_map_zoom_update(node, 0.1f); fresh_scale(node, player, 2);
    near(2 / MEMF(node + 0x50), 1 + .25f * (1 - expf(-.1f / .6f)));
    mm3_map_zoom_reset(node); fresh_scale(node, player, 2);
    mm3_map_zoom_update(node, 10); fresh_scale(node, player, 2);
    near(2 / MEMF(node + 0x50), 1 + .25f * (1 - expf(-.1f / .6f)));
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
    mm3_map_zoom_reset(node); fresh_scale(node, player, 2); near(MEMF(node + 0x50), 2);
    fresh_scale(node, 0, 2); near(MEMF(node + 0x50), 2);
    mm3_map_zoom_reset(0); mm3_map_zoom_update(0, .1f); mm3_map_zoom_apply(0, player);
    for (i = 0; i < 3; ++i) {
        const float invalid[] = {0, -1, INFINITY};
        fresh_scale(node, player, invalid[i]);
        assert(MEMF(node + 0x50) == invalid[i]);
    }
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
        float span = 1.25f, cx = large ? 320.0f : 114.0f, cy = large ? 240.0f : 384.0f;
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
        near(MEMF(node + 0x50), native_scale);
        assert(draw_count + 1 == original_count);
        roads = texture_draw(original, original_count, 1); roads_zoom = texture_draw(draws, draw_count, 1);
        road_samples(roads, roads_zoom, cx, cy, 1);
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
                        centre + (retail->xy[vertex][axis] - centre) * .75f :
                        retail->xy[vertex][axis]);
            }
        }
        for (i = 0; i < 600; ++i) mm3_map_zoom_update(node, 1.0f / 60.0f);
        draw_count = 0; abi_start(node); zoom_draw(); abi_end(24);
        assert(draw_count + 1 == original_count); near(MEMF(node + 0x50), native_scale / span);
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
                near(checkpoint_zoom->xy[i][axis] - b, (checkpoint->xy[i][axis] - a) * .75f / span);
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
    fresh_scale(node, player, 1); near(MEMF(node + 0x50), 1);
    /* The actual native update argument is +0x10 before its frame prologue. */
    MEM32(player + 4) = 120; fresh_scale(node, player, 1);
    MEMF(node + 0x9C) = MEMF(node + 0x94); MEMF(node + 0xA0) = MEMF(node + 0x98);
    MEMF(node + 0xAC) = MEMF(node + 0xB0) = 0;
    abi_start(node); MEMF(stack + 16) = .1f; zoom_update(); abi_end(20);
    fresh_scale(node, player, 1);
    near(1 / MEMF(node + 0x50), 1 + .25f * (1 - expf(-.1f / .6f)));
}
static void native_scale_progression(void) {
    float stopped;
    unsigned i;
    /* Checkpoint proximity changes the stock scale independently of speed.
     * Preserve this layout's first valid framing and let speed zoom it out. */
    mm3_map_zoom_reset(node);
    MEM32(player + 4) = 0; MEMF(player + 0x34) = .728f;
    fresh_scale(node, player, .728f); stopped = MEMF(node + 0x50);
    near(stopped, .728f);
    MEM32(player + 4) = 120; MEMF(player + 0x34) = 1.4f;
    fresh_scale(node, player, 1.4f); near(MEMF(node + 0x50), stopped);
    for (i = 0; i < 600; ++i) mm3_map_zoom_update(node, 1.0f / 60.0f);
    fresh_scale(node, player, 1.4f); near(MEMF(node + 0x50), stopped / 1.25f);
    assert(MEMF(node + 0x50) < stopped); near(MEMF(player + 0x34), 1.4f);
    /* At fixed speed, repeated native checkpoint updates cannot zoom back in. */
    for (i = 0; i < 4; ++i) {
        const float stock[] = {.5f, .728f, 1, 1.4f};
        MEMF(player + 0x34) = stock[i]; fresh_scale(node, player, stock[i]);
        near(MEMF(node + 0x50), stopped / 1.25f); near(MEMF(player + 0x34), stock[i]);
    }
    /* A reused node captures its new layout, while a null player or invalid
     * stock scale cannot establish a stale default baseline. */
    mm3_map_zoom_reset(node); MEM32(player + 4) = 0;
    fresh_scale(node, 0, 1); near(MEMF(node + 0x50), 1);
    fresh_scale(node, player, NAN); assert(isnan(MEMF(node + 0x50)));
    fresh_scale(node, player, -1); near(MEMF(node + 0x50), -1);
    fresh_scale(node, player, .9f); near(MEMF(node + 0x50), .9f);
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
int main(void) {
    g_xbox_mem_offset = (ptrdiff_t)memory; native_constants();
    MEMF(1 + 0x50) = 1; MEMF(1 + 0x54) = 1;
    MEMF(1 + 0x60) = 0; MEMF(1 + 0x64) = 0;
    MEM32(node) = 0x26000; MEM32(0x26000 + 0xC4) = 0x0011CAE8;
    MEM32(node + 8) = 1; MEM32(node + 0x20) = 2; MEM32(node + 0x24) = 3; MEM32(node + 0x10) = 4;
    MEM32(node + 0x8C) = property; MEM32(property) = property + 0x100;
    MEM32(property + 0x100 + 0x5C) = 0x100;
    MEM32(players + 0x14) = player; MEM32(0x407F78 + 4) = markers;
    speed_contract(); square_geometry_contract(); native_contract(); native_scale_progression(); square_marker_contract();
    puts("PASS square map/zoom: city samples match native turns and markers, colors/opacity intact, circle border omitted, small/big/split layouts, speed smoothing and guest ABI");
    return 0;
}
