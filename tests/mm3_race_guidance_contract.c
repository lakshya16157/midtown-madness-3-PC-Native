/* Real native checkpoint quad creation, projection, rotation and guest ABI;
 * only player property lookup and GPU submission are capture boundaries. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "recomp_types.h"
#include "nv2a_minimap_projection.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;
static unsigned char memory[0x800000];
static const uint32_t stack = 0x80000, node = 0x10000, property = 0x22000;
static const uint32_t layout = 0x23000, markers = 0x24000, position = 0x28000;
static const uint32_t origin = 0x28100, event = 0x60000, event_vtable = 0x61000;
static const uint32_t event_list = 0x62000, event_head = 0x62100, list_nodes = 0x62200;
static const uint32_t empty_list = 0x62800, empty_head = 0x62810;
static const uint32_t players = 0x62900, player_thing = 0x62A00;
static const uint32_t renderer_vtable = 0x68000, viewport = 0x68100;
static const uint32_t packed_vertices = 0x500000, packed_batches = 0x790000, test_quad = 0x6C000;
static const uint32_t objects[4] = {0x30018, 0x30118, 0x30218, 0x30318};
static const float points[4][2] = {{-22, -8}, {-10, 14}, {9, 10}, {23, -5}};
typedef struct { uint32_t texture; float xy[4][2], color[4]; } Draw;
static unsigned route_segment_draw_count(void);
static unsigned road_join_covers(float x, float y);
static const Draw *route_segment_draw(unsigned index);
static Draw draws[65536];
static unsigned draw_count, active_player, marker_count, world_draw_count;
static unsigned hud_players, native_marker_count;
static unsigned native_submission;
static unsigned native_submission_dropped, native_submission_gray, native_submission_blue;
static unsigned native_submission_markers, native_pool_allocations, native_pool_frees;
static uint32_t current_target;
static uint32_t world_color;
static const char *check = "guidance";
static int improved, cruise_origin_valid;
static float cruise_origin[3];
static int player_heading_valid;
static float player_heading;
int mm3_minimap_improved(void) { return improved; }
int mm3_minimap_heading(uint32_t owner, float *angle) {
    assert(owner == node);
    if (!player_heading_valid) return 0;
    *angle = player_heading; return 1;
}
int mm3_minimap_origin(uint32_t owner, float *copy) {
    assert(owner == node);
    if (!cruise_origin_valid) return 0;
    memcpy(copy, cruise_origin, sizeof cruise_origin); return 1;
}

#include "production_race_guidance.c"
void sub_00201BCA(void);
void sub_0020BF9C(void);
void sub_0001BA6A(void);
void native_submit(void);
void sub_0011964B(void);
void sub_0034BEB0(void) { assert(!"unexpected unrelated GPU dirty state"); }
void sub_00344770(void) { assert(!"unexpected GPU command buffer rollover"); }

static void fake_icall(uint32_t address) {
    if (address == 0x100) {
        g_fp_top = (g_fp_top + 7) & 7; g_fp_stack[g_fp_top] = active_player;
        g_esp += 8; return;
    }
    if (address == 0x0002DC7E) {
        memset((void *)XBOX_PTR(g_ecx), 0, 0x24); g_esp += 4; return;
    }
    if (address == 0x101) { g_eax = event; g_esp += 4; return; }
    if (address == 0x102) { g_eax = current_target; g_esp += 4; return; }
    if (address == 0x103) { g_eax = 0; g_esp += 4; return; }
    if (address == 0x104) { g_eax = event_list; g_esp += 4; return; }
    if (address == 0x105) { g_eax = empty_list; g_esp += 4; return; }
    if (address == 0x106) { sub_0001BA6A(); return; }
    if (address == 0x107) { sub_0020BF9C(); return; }
    if (address == 0x108) { g_eax = viewport; g_esp += 4; return; }
    if (address == 0x0011964B) { sub_0011964B(); return; }
    fprintf(stderr, "unexpected guidance indirect call %08X\n", address); abort();
}
void sub_001F0399(void) {
    g_eax = (uint32_t)(int32_t)g_fp_stack[g_fp_top];
    g_fp_top = (g_fp_top + 1) & 7; g_esp += 4;
}
void sub_00078DF9(void) { g_eax = g_eax == players ? hud_players : 0; g_esp += 4; }
void sub_0011FE40(void) {
    uint32_t saved_ebp = g_ebp, saved_seh = g_seh_ebp;
    uint32_t player = MEM32(g_esp + 8), player_object = MEM32(g_esp + 12);
    /* Property lookup boundary: execute the actual native checkpoint-list
     * publisher with the arguments 11FE40 passes at 11FF03. */
    PUSH32(g_esp, player); PUSH32(g_esp, player_object);
    PUSH32(g_esp, event); PUSH32(g_esp, 0x407D20); PUSH32(g_esp, 0x0011FF12);
    sub_00201BCA();
    g_ebp = saved_ebp; g_seh_ebp = saved_seh; g_esp += 16;
}
void sub_0004EE0D(void) { g_eax = marker_count; g_esp += 4; }
void sub_002414D8(void) { assert(!"unexpected non-checkpoint color"); }
void sub_00012D29(void) {
    g_fp_top = (g_fp_top + 7) & 7; g_fp_stack[g_fp_top] = floorf(MEMF(g_esp + 4));
    g_esp += 8;
}
/* World geometry/resource boundaries; native draw runs through the exact
 * packed-color branch and reuses +0x40 as a float before that branch. */
void sub_00013A63(void) {
    float x = MEMF(g_eax), y = MEMF(g_eax + 4), z = MEMF(g_eax + 8);
    g_fp_top = (g_fp_top + 7) & 7; g_fp_stack[g_fp_top] = sqrt(x*x + y*y + z*z);
    g_esp += 4;
}
void sub_0020C1C9(void) { g_eax = 1; g_esp += 8; }
void sub_00012BFA(void) { memset((void *)XBOX_PTR(g_ecx), 0, 48); g_esp += 4; }
void sub_0001944A(void) { g_esp += 4; }
void sub_00019B8D(void) {
    uint32_t result = MEM32(g_esp + 4);
    memset((void *)XBOX_PTR(result), 0, 48);
    MEMF(result) = MEMF(result + 16) = MEMF(result + 32) = 1;
    g_eax = result; g_esp += 8;
}
void sub_00164019(void) { g_esp += 20; }
void sub_0025373E(void) { g_fp_top = (g_fp_top + 1) & 7; g_esp += 4; }
void sub_00019C06(void) { g_esp += 4; }
void sub_0001BB30(void) { world_color = MEM32(g_esp + 72); ++world_draw_count; g_esp += 100; }
void sub_00013A34(void) { g_esp += 4; }
void sub_0001BADD(void) { g_esp += 72; }
/* The publisher's unrelated HUD collections are empty in this fixture. */
void sub_0001C9C9(void) { g_esp += 28; }
void sub_0001C30C(void) { ++native_marker_count; g_esp += 8; }
void sub_0001C357(void) { ++native_marker_count; g_esp += 12; }
void sub_001F77CE(void) { g_eax = 0; g_esp += 4; }
void sub_001F7449(void) { g_esp += 8; }
void sub_001FB158(void) { g_esp += 32; }
void sub_001C032D(void) { g_eax = 0; g_esp += 4; }
void sub_001C0107(void) { g_eax = 0; g_esp += 4; }
void sub_00012B8A(void) { g_esp += 20; }
void sub_000127A9(void) { assert(!"unexpected auxiliary HUD collection"); }
void sub_00015C02(void) { assert(!"unexpected auxiliary HUD collection"); }
void sub_000160BC(void) { assert(!"unexpected auxiliary HUD collection"); }
void sub_0001B8AD(void) { assert(!"unexpected auxiliary HUD collection"); }
void sub_00095338(void) { assert(!"unexpected auxiliary HUD collection"); }
void sub_00180E92(void) { assert(!"unexpected auxiliary HUD collection"); }
void sub_00013A84(void) { assert(!"unexpected auxiliary HUD collection"); }
void sub_00170EC0(void) {
    assert(MEM32(g_esp + 4) == packed_vertices); ++native_pool_frees; g_esp += 4;
}
void sub_00170EE2(void) {
    assert(g_eax == 160000 * 16); ++native_pool_allocations;
    g_eax = packed_vertices; g_esp += 4;
}
void sub_001C1585(void) {
    uint32_t texture = MEM32(g_esp + 4), vertices = MEM32(g_esp + 8);
    unsigned i;
    assert(draw_count < sizeof draws / sizeof draws[0]);
    assert(MEM32(vertices + 4) == 7 && MEM32(vertices + 8) == 8);
    draws[draw_count].texture = texture;
    for (i = 0; i < 4; ++i) {
        draws[draw_count].xy[i][0] = MEMF(vertices + 0xC + i * 0x24);
        draws[draw_count].xy[i][1] = MEMF(vertices + 0x10 + i * 0x24);
        draws[draw_count].color[i] = MEMF(vertices + 0x20 + i * 4);
    }
    ++draw_count; g_esp += 12;
    if (native_submission) {
        unsigned used = MEM32(0x3C568C);
        g_esp -= 12;
        native_submit();
        if (MEM32(0x3C568C) == used) ++native_submission_dropped;
        else if (texture) ++native_submission_markers;
        else if (draws[draw_count - 1].color[0] == .62f) ++native_submission_gray;
        else ++native_submission_blue;
    }
}
#undef RECOMP_ICALL_SAFE_AT
#define RECOMP_ICALL_SAFE_AT(target, saved, site) fake_icall(target)
#undef RECOMP_ICALL_SAFE
#define RECOMP_ICALL_SAFE(target, saved) fake_icall(target)
#undef RECOMP_ITAIL
#define RECOMP_ITAIL(target) fake_icall(target)
#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
#include "native_race_guidance.inc"

static void near(float got, float expected) {
    if (!isfinite(got) || fabsf(got - expected) > .001f) {
        fprintf(stderr, "%s: got %.9g expected %.9g\n", check, got, expected); abort();
    }
}
static void publish(unsigned player, unsigned current) {
    unsigned i;
    mm3_race_guidance_begin(player);
    for (i = 0; i < 4; ++i) {
        MEMF(position) = points[i][0]; MEMF(position + 8) = points[i][1];
        mm3_race_guidance_checkpoint(player, objects[i], position, origin, current < 4 ? objects[current] : 0);
    }
    mm3_race_guidance_end(player);
}
static void fixture_roads(void) {
    unsigned i;
    road_graph_clear(); assert(road_graph_allocate(4));
    for (i = 0; i < 4; ++i) {
        s_roads.nodes[i].position.x = points[i][0];
        s_roads.nodes[i].position.z = points[i][1];
        if (i) assert(road_graph_edge(i - 1, i));
    }
}
static void abi_start(unsigned outside) {
    g_eax = 0xABABABAB; g_ecx = node; g_edx = 0x12345678;
    g_esp = stack; g_ebp = 0x71000; g_seh_ebp = g_ebp;
    g_ebx = 0x11223344; g_esi = 0x22334455; g_edi = 0x33445566;
    g_fp_top = 0; g_fp_control_word = 0x37F; g_df = 0;
    memset(g_fp_stack, 0, sizeof g_fp_stack);
    MEM32(stack) = 0xDEADBEEF;
    MEM32(stack + 4) = 0x25000; MEM32(stack + 8) = layout;
    MEM32(stack + 12) = 0x26262626; MEM32(stack + 16) = outside;
    MEM32(stack + 20) = 0xA5A5A5A5;
    draw_count = 0;
}
static void abi_end(void) {
    assert(g_esp == stack + 20 && g_fp_top == 0);
    assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
    assert(MEM32(stack + 20) == 0xA5A5A5A5);
}
static float centre(const Draw *draw, unsigned axis) {
    return .25f * (draw->xy[0][axis] + draw->xy[1][axis] + draw->xy[2][axis] + draw->xy[3][axis]);
}
static float edge(const Draw *draw) {
    return hypotf(draw->xy[1][0] - draw->xy[0][0], draw->xy[1][1] - draw->xy[0][1]);
}
static void native_contract(void) {
    unsigned large, turn, split, pulse, i, axis, vertex;
    const unsigned order[4] = {2, 0, 1, 3}; /* Native vector puts current first. */
    for (split = 0; split < 2; ++split) for (large = 0; large < 2; ++large)
    for (turn = 0; turn < 8; ++turn) for (pulse = 0; pulse < 4; ++pulse) {
        Draw retail[4], guided[4];
        active_player = split; marker_count = 4; publish(active_player, 4);
        MEM32(0x407F78 + active_player * 0x10 + 4) = markers;
        MEMF(node + 0x94) = large ? 320.0f : 114.0f;
        MEMF(node + 0x98) = large ? 240.0f : split ? 180.0f : 384.0f;
        MEMF(node + 0x50) = large ? 1.2f : .8f;
        MEMF(node + 0x4C) = turn * .3926990817f;
        MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1000;
        MEMF(node + 0xB4) = large ? 3.0f : 1.0f;
        MEMF(node + 0xAC) = pulse * .25f;
        MEMF(layout + 8) = large ? 256.0f : 128.0f;
        MEMF(layout + 12) = MEMF(layout + 8);
        for (i = 0; i < 4; ++i) {
            MEMF(markers + i * 24) = points[order[i]][0];
            MEMF(markers + i * 24 + 8) = points[order[i]][1];
            MEM32(markers + i * 24 + 12) = order[i] == 2 ? 0x80 : order[i] == 1 ? 0x40 : 0;
        }
        abi_start(0); retail_marker(); abi_end();
        assert(draw_count == 4);
        for (i = 0; i < 4; ++i) retail[order[3 - i]] = draws[i];
        abi_start(0); sub_0011CAE8(); abi_end();
        assert(draw_count == 7);
        for (i = 0; i < 4; ++i) guided[order[3 - i]] = draws[3 + i];
        for (i = 0; i < 4; ++i) {
            assert(guided[i].texture == retail[i].texture);
            for (axis = 0; axis < 4; ++axis) near(guided[i].color[axis], retail[i].color[axis]);
            near(edge(&guided[i]), edge(&retail[i]));
            for (vertex = 0; vertex < 4; ++vertex) for (axis = 0; axis < 2; ++axis)
                near(guided[i].xy[vertex][axis], retail[i].xy[vertex][axis]);
        }
        /* Course endpoints coincide with real native icons across small,
         * expanded, split-screen and rotated maps, independent of vector order. */
        for (i = 0; i < 3; ++i) {
            assert(draws[i].texture == 0);
            near(draws[i].color[3], .8f);
            for (axis = 0; axis < 2; ++axis) {
                check = "course start matches native checkpoint";
                near(.5f * (draws[i].xy[0][axis] + draws[i].xy[3][axis]), centre(&guided[i], axis));
                check = "course end matches native checkpoint";
                near(.5f * (draws[i].xy[1][axis] + draws[i].xy[2][axis]), centre(&guided[i + 1], axis));
            }
        }
        abi_start(1); sub_0011CAE8(); abi_end(); assert(draw_count == 0);
    }
}
static void clipping_and_state_contract(void) {
    unsigned i;
    /* Both points may be outside the map while their segment crosses it. */
    road_graph_clear(); assert(road_graph_allocate(2));
    s_roads.nodes[0].position.x = -200; s_roads.nodes[1].position.x = 200;
    assert(road_graph_edge(0, 1));
    mm3_race_guidance_begin(0);
    for (i = 0; i < 2; ++i) {
        MEMF(position) = i ? 200.0f : -200.0f; MEMF(position + 8) = 0;
        mm3_race_guidance_checkpoint(0, objects[i], position, origin, 0);
    }
    mm3_race_guidance_end(0); active_player = 0; marker_count = 0;
    MEMF(node + 0x4C) = 0; MEMF(node + 0x50) = 1;
    MEMF(layout + 8) = 128; MEMF(node + 0xB4) = 1;
    MEMF(layout + 12) = 128;
    abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 1);
    near(.5f * (draws[0].xy[0][0] + draws[0].xy[3][0]), MEMF(node + 0x94) + 56);
    near(.5f * (draws[0].xy[1][0] + draws[0].xy[2][0]), MEMF(node + 0x94) - 56);
    /* Exercise the production layout-height lookup through the actual map
     * caller: turn that same street vertically in a shorter split panel. */
    MEMF(layout + 12) = 96; MEMF(node + 0x4C) = 1.5707963268f;
    abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 1);
    near(.5f * (draws[0].xy[0][1] + draws[0].xy[3][1]), MEMF(node + 0x98) - 40);
    near(.5f * (draws[0].xy[1][1] + draws[0].xy[2][1]), MEMF(node + 0x98) + 40);
    MEMF(layout + 12) = 128; MEMF(node + 0x4C) = 0;
    fixture_roads();
    /* No current target must not make an otherwise valid course disappear. */
    mm3_race_guidance_begin(0);
    for (i = 0; i < 4; ++i) {
        MEMF(position) = points[i][0]; MEMF(position + 8) = points[i][1];
        mm3_race_guidance_checkpoint(0, objects[i], position, origin, 0);
    }
    mm3_race_guidance_end(0);
    abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 3);
    for (i = 0; i < 3; ++i) near(draws[i].color[3], .8f);
    /* The real HUD boundary clears players skipped in Cruise/no-player HUDs. */
    publish(0, 2); publish(1, 0);
    abi_start(0); sub_00120091();
    assert(g_esp == stack + 8 && g_fp_top == 0);
    assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
    for (active_player = 0; active_player < 2; ++active_player) {
        abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 0);
    }
    active_player = 0;
    mm3_race_guidance_begin(0);
    MEMF(position) = NAN;
    mm3_race_guidance_checkpoint(0, objects[0], position, origin, objects[0]);
    mm3_race_guidance_end(0);
    abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 0);
}
static void course_finish_and_revisit_contract(void) {
    unsigned i, axis;
    active_player = 0; marker_count = 0;
    MEMF(node + 0x4C) = 0; MEMF(node + 0x50) = 1;
    MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
    MEMF(layout + 8) = 256; MEMF(node + 0xB4) = 1;
    MEMF(layout + 12) = 256;
    MEM32(0x407C1C) = 2; publish(0, 4);
    abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 3);
    MEM32(0x407C1C) = 3; publish(0, 4);
    /* Retail Checkpoint scripts finish after total_targets, just like Blitz.
     * Event mode must not synthesize a last-to-first return leg. */
    abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 3);
    assert(s_paths[0].lengths[3] == 0);
    MEM32(0x407C1C) = 2; mm3_race_guidance_begin(0);
    /* Two checkpoint identities can share a crossing without losing either
     * adjacent course segment or depending on the native icon's order. */
    for (i = 0; i < 4; ++i) {
        MEMF(position) = points[i == 2 ? 0 : i][0];
        MEMF(position + 8) = points[i == 2 ? 0 : i][1];
        mm3_race_guidance_checkpoint(0, objects[i], position, origin, 0);
    }
    mm3_race_guidance_end(0);
    abi_start(0); sub_0011CAE8(); abi_end(); assert(route_segment_draw_count() == 5);
    for (axis = 0; axis < 2; ++axis)
        near(.5f * (draws[0].xy[0][axis] + draws[0].xy[3][axis]),
             .5f * (route_segment_draw(2)->xy[0][axis] + route_segment_draw(2)->xy[3][axis]));
}
static void native_publication_lifecycle_contract(void) {
    unsigned i, pass, axis;
    MEM32(0x407C1C) = 2; MEM32(0x3C569C) = players;
    MEM32(players + 4) = players + 0x40;
    MEM32(players + 0x40) = player_thing;
    MEM32(0x3C5CDC) = players + 0x80;
    MEM32(players + 0x80 + 0x54) = player_thing;
    MEM8(player_thing + 8) = 1;
    MEM32(event) = event_vtable;
    MEM32(event_vtable + 0x28) = 0x101;
    MEM32(event_vtable + 0x30) = 0x102;
    MEM32(event_vtable + 0x34) = 0x103;
    MEM32(event_vtable + 0x9C) = 0x103;
    MEM32(event_vtable + 0xE8) = 0x104;
    MEM32(event_vtable + 0xEC) = MEM32(event_vtable + 0xF0) = 0x105;
    MEM32(event_list + 4) = event_head;
    MEM32(event_head) = list_nodes;
    MEM32(empty_list + 4) = empty_head; MEM32(empty_head) = empty_head;
    MEM32(0x407D20 + 0x168) = 0;
    for (i = 0; i < 4; ++i) {
        MEM32(list_nodes + i * 16) = i == 3 ? event_head : list_nodes + (i + 1) * 16;
        MEM32(list_nodes + i * 16 + 8) = objects[i] - 0x18;
        MEM32(objects[i]) = event_vtable + 0x200;
        MEMF(objects[i] + 0x48) = points[i][0];
        MEMF(objects[i] + 0x50) = points[i][1];
    }
    MEM32(event_vtable + 0x200 + 0x48) = 0x106;
    MEM32(event_vtable + 0x200 + 0x24) = 0x107;
    MEMF(node + 0x4C) = 0; MEMF(node + 0x50) = 1;
    MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
    MEMF(layout + 8) = 256; MEMF(node + 0xB4) = 1;
    MEMF(layout + 12) = 256;
    hud_players = 1; active_player = 0; marker_count = 0;
    for (pass = 0; pass < 5; ++pass) {
        float player_x = pass * 3.0f, player_z = pass * -2.0f;
        MEMF(player_thing + 0x34) = player_x; MEMF(player_thing + 0x3C) = player_z;
        current_target = pass == 4 ? 0 : objects[pass];
        for (i = 0; i < 4; ++i) MEM8(objects[i] + 0x70) = i >= pass;
        native_marker_count = 0;
        abi_start(0); sub_00120091();
        assert(g_esp == stack + 8 && g_fp_top == 0);
        assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
        assert(native_marker_count == 4 - pass);
        assert(s_race[0].current == (pass == 4 ? 4 : pass));
        /* Check the complete course independently of the live reroute below. */
        s_race[0].current = 4;
        abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 3);
        for (i = 0; i < 3; ++i) for (axis = 0; axis < 2; ++axis) {
            float base = MEMF(node + 0x94 + 4 * axis);
            float player_position = axis ? player_z : player_x;
            float sign = axis ? 1.0f : -1.0f;
            near(.5f * (draws[i].xy[0][axis] + draws[i].xy[3][axis]),
                 base + sign * (points[i][axis] - player_position));
            near(.5f * (draws[i].xy[1][axis] + draws[i].xy[2][axis]),
                 base + sign * (points[i + 1][axis] - player_position));
        }
    }
    /* Removed split-screen players clear even when the remaining player
     * republishes; a Cruise/no-player update then clears the final course. */
    publish(1, 0);
    abi_start(0); sub_00120091();
    active_player = 1; abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 0);
    hud_players = 0; abi_start(0); sub_00120091();
    active_player = 0; abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 0);
    /* Actual 20DA6F removes the crossed node from the pending list. Exercise
     * both native race modes through 4 -> 3 -> 2 -> 1 -> 0, not just icon flags. */
    for (i = 2; i <= 3; ++i) {
        MEM32(0x407C1C) = i; hud_players = 1;
        for (pass = 0; pass <= 4; ++pass) {
            unsigned route_draws = 0, draw;
            MEM32(event_head) = pass == 4 ? event_head : list_nodes + pass * 16;
            current_target = pass == 4 ? 0 : objects[pass];
            /* A live car on the previous gate's road must still see the last
             * approach after the publisher has only one checkpoint to copy. */
            MEMF(player_thing + 0x34) = points[pass ? pass - 1 : 1][0];
            MEMF(player_thing + 0x3C) = points[pass ? pass - 1 : 1][1];
            native_marker_count = 0;
            abi_start(0); sub_00120091();
            assert(g_esp == stack + 8 && g_fp_top == 0);
            assert(s_race[0].count == 4 - pass);
            assert(s_race[0].valid == (pass < 4));
            assert(s_race[0].current == 0);
            abi_start(0); sub_0011CAE8(); abi_end();
            for (draw = 0; draw < draw_count; ++draw)
                if (draws[draw].color[3] == .98f) ++route_draws;
            assert(route_draws == (pass < 4 ? 1u : 0u));
            if (pass == 3) {
                assert(s_paths[0].count == 1 && s_paths[0].course_count == 0);
                assert(s_paths[0].active_target == objects[3]);
                near(s_paths[0].active[s_paths[0].active_count - 1].x, points[3][0]);
                near(s_paths[0].active[s_paths[0].active_count - 1].z, points[3][1]);
            }
            if (pass == 4) assert(draw_count == 0);
        }
    }
    hud_players = 0; MEM32(0x407C1C) = 2;
    MEM32(0x3C569C) = 0;
}
static void native_submission_begin(void) {
    MEM32(0x25000) = renderer_vtable;
    MEM32(renderer_vtable + 0x88) = 0x0011964B;
    MEM32(renderer_vtable + 0xC0) = 0x108;
    MEMF(viewport) = -320; MEMF(viewport + 4) = -240;
    MEMF(viewport + 8) = 640; MEMF(viewport + 12) = 480;
    MEM32(0x3C561C) = 0x65000; /* Already-created native default texture. */
    MEM32(0x3C5688) = packed_vertices; MEM32(0x3C568C) = 0;
    MEM32(0x3C77CC) = packed_batches; MEM32(0x3C5630) = 0;
    MEM32(0x3A0EC0) = 256;
    MEMF(0x3B8588) = 1; MEMF(0x3B8584) = 1;
    MEMF(0x4085FC) = MEMF(0x4085F8) = 0;
    memset((void *)XBOX_PTR(packed_vertices), 0, 0x1000);
    memset((void *)XBOX_PTR(packed_batches), 0, 0x100);
}
static unsigned submit_test_quad(unsigned tagged, unsigned clipped, unsigned crossing) {
    unsigned i, previous = MEM32(0x3C568C);
    memset((void *)XBOX_PTR(test_quad), 0, 0xA0);
    MEM32(test_quad) = clipped ? viewport : 0;
    MEM32(test_quad + 4) = 7; MEM32(test_quad + 8) = 8;
    for (i = 0; i < 4; ++i) {
        uint32_t vertex = test_quad + 0xC + i * 0x24;
        MEMF(vertex) = (i == 0 || i == 3) ? (crossing ? -400.0f : -30.0f) : 30.0f;
        MEMF(vertex + 4) = i < 2 ? -20.0f : 20.0f;
        MEMF(vertex + 0xC) = tagged ? (float)ROUTE_TAG_U : 0;
        MEMF(vertex + 0x10) = tagged ? (float)ROUTE_TAG_V : 0;
        MEMF(vertex + 0x14) = 1; MEMF(vertex + 0x18) = .72f;
        MEMF(vertex + 0x1C) = .15f; MEMF(vertex + 0x20) = .8f;
    }
    abi_start(0);
    PUSH32(g_esp, test_quad); PUSH32(g_esp, 0); PUSH32(g_esp, 0x0011CB45);
    g_eax = 0; g_edx = 0x25000;
    native_submit();
    assert(g_esp == stack && g_fp_top == 0);
    assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
    if (MEM32(0x3C568C) <= previous) {
        fprintf(stderr, "native quad emitted no vertices: tagged=%u clipped=%u crossing=%u, batches=%u\n",
                tagged, clipped, crossing, MEM32(0x3C5630));
        abort();
    }
    for (i = previous; i < MEM32(0x3C568C); ++i) {
        uint32_t vertex = packed_vertices + i * 16;
        assert((int16_t)MEM16(vertex + 8) == (tagged ? ROUTE_TAG_U : 0));
        assert((int16_t)MEM16(vertex + 0xA) == (tagged ? ROUTE_TAG_V : 0));
        assert(MEM16(vertex + 0xC) == 0 && MEM16(vertex + 0xE) == 0);
    }
    return MEM32(0x3C568C) - previous;
}
static void native_submission_contract(void) {
    unsigned clipped, crossing;
    for (clipped = 0; clipped < 2; ++clipped) for (crossing = 0; crossing < 2; ++crossing) {
        unsigned ordinary, tagged;
        unsigned char stock[256];
        native_submission_begin();
        ordinary = submit_test_quad(0, clipped, crossing);
        assert(ordinary * 16 <= sizeof stock);
        memcpy(stock, (void *)XBOX_PTR(packed_vertices), ordinary * 16);
        tagged = submit_test_quad(1, clipped, crossing);
        assert(tagged == ordinary);
        /* Actual native 195F7 batches ordinary and route vertices together. */
        assert(MEM32(0x3C5630) == 1);
        assert(MEM32(packed_batches + 8) == ordinary + tagged);
        for (unsigned i = 0; i < tagged; ++i) {
            uint32_t vertex = packed_vertices + (ordinary + i) * 16;
            assert(!memcmp(stock + i * 16, (void *)XBOX_PTR(vertex), 8));
            assert(!memcmp(stock + i * 16 + 12, (void *)XBOX_PTR(vertex + 12), 4));
        }
        /* A later ordinary quad must not inherit the route's identity. */
        submit_test_quad(0, clipped, crossing);
    }
    native_submission_begin();
    abi_start(0); native_submission = 1;
    route_segment(0x25000, 0, 80, 20, 100, 30, .8f, 1.25f);
    native_submission = 0;
    assert(g_esp == stack && g_fp_top == 0);
    assert(MEM32(0x3C568C) == 4);
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t vertex = packed_vertices + i * 16;
        assert((int16_t)MEM16(vertex + 8) == ROUTE_TAG_U);
        assert((int16_t)MEM16(vertex + 0xA) == ROUTE_TAG_V);
    }
}
static void native_gpu_format_contract(void) {
    const uint32_t declaration = 0x6D000, shader = 0x6D100;
    const uint32_t device = 0x6D400, commands = 0x6E000;
    unsigned i;
    native_ui_declaration_data();
    memset((void *)XBOX_PTR(shader), 0, 0x200);
    for (i = 0; i < 16; ++i) MEM32(shader + 0x1C + i * 16) = 2;
    abi_start(0); g_edi = shader;
    PUSH32(g_esp, 1); PUSH32(g_esp, declaration); PUSH32(g_esp, 0x00349341);
    sub_00349470();
    assert(g_esp == stack && g_fp_top == 0);
    assert(MEM32(shader + 0x1C) == 0x21); /* v0: normalized SHORT2 position */
    assert(MEM32(shader + 0x2C) == 0x40); /* v1: packed diffuse */
    assert(MEM32(shader + 0x3C) == 0x41); /* v2: normalized SHORT4 UV0/UV1 */
    assert(MEM32(shader + 0x38) == 8);
    memset((void *)XBOX_PTR(device), 0, 0x900);
    MEM32(0x351F50) = 0x20; MEM32(device) = commands;
    MEM32(device + 4) = commands + 0x1000; MEM32(device + 0x37C) = shader;
    abi_start(0); g_esi = device;
    PUSH32(g_esp, 0x00349DC5); sub_0034C0E0();
    assert(g_esp == stack && g_fp_top == 0);
    assert(MEM32(commands) == 0x401760); /* Native PB: all 16 attribute formats */
    for (i = 0; i < 16; ++i)
        assert(MEM32(commands + 4 + i * 4) == (i == 0 ? 0x21u : i == 1 ? 0x40u : i == 2 ? 0x41u : 2u));
    /* INLINE_ARRAY derives 4+4+8=16 byte stride from these exact formats.
     * Thus native packed UV0 is v2.xy at byte8, with UV1 in v2.zw byte12;
     * the renderer must consume normalized SHORT4 v2, never assumed v9. */
    assert(MEM32(device + 0x778) == 4 && MEM32(device + 0x804) == 1);
}
static void native_world_contract(void) {
    unsigned i, highlighted;
    publish(0, 2); mm3_race_guidance_begin(1);
    for (i = 0; i < 4; ++i) for (highlighted = 0; highlighted < 2; ++highlighted) {
        uint32_t object = objects[i] - 0x18;
        MEM8(object + 0x70) = 1; MEM32(object + 0x54) = 0x40000;
        MEM32(0x40000 + 0x15368) = 0x58000;
        MEMF(object + 0x7C) = 10; MEMF(object + 0x88) = 0;
        abi_start(0); g_ecx = object; MEM32(stack + 4) = highlighted ? 2 : 0;
        world_draw_count = 0; sub_0020C9F9();
        assert(g_esp == stack + 8 && g_fp_top == 0);
        assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
        assert(world_draw_count == 1);
        assert(world_color == (highlighted ? 0x80FFAA88u : 0x80000000u));
    }
}
static void road_navigation_contract(void) {
    const RoadPoint positions[8] = {{-60,0,-40}, {-60,0,40}, {60,0,40}, {60,0,-40},
                                   {0,0,-80}, {0,0,80}, {-60,12,-40}, {-60,12,40}};
    RoadPoint route[16], start = {-60,0,-30}, finish = {60,0,-30};
    RoadSnap snap;
    unsigned i, count;
    road_graph_clear(); assert(road_graph_allocate(8));
    for (i = 0; i < 8; ++i) s_roads.nodes[i].position = positions[i];
    for (i = 0; i < 3; ++i) assert(road_graph_edge(i, i+1));
    assert(road_graph_edge(4,5)); assert(road_graph_edge(6,7));
    count = road_path(start, finish, route, 16);
    assert(count == 4);
    near(route[0].x, -60); near(route[1].z, 40);
    near(route[2].z, 40); near(route[3].x, 60);
    /* A crossing in screen space cannot connect unrelated roads. */
    assert(road_path(positions[4], finish, route, 16) == 0);
    assert(road_snap((RoadPoint){-60,12,0}, &snap)); near(snap.position.y, 12);
    assert(!road_snap((RoadPoint){1000,0,1000}, &snap));
    /* Capacity reserves both snapped endpoints and every intervening bend. */
    assert(road_path(start, finish, route, 3) == 0);
    assert(road_path(start, (RoadPoint){-50,0,40}, route, 3) == 3);
    assert(road_path(start, (RoadPoint){-60,0,10}, route, 2) == 2);
    MEM32(0x407C1C) = 2; active_player = marker_count = 0;
    MEMF(node + 0x4C) = 0; MEMF(node + 0x50) = 1;
    MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
    MEMF(layout + 8) = 512; MEMF(node + 0xB4) = 1;
    MEMF(layout + 12) = 512;
    MEMF(origin) = -60; MEMF(origin + 4) = 0; MEMF(origin + 8) = -20;
    mm3_race_guidance_begin(0);
    for (i = 0; i < 2; ++i) {
        MEMF(position) = i ? finish.x : start.x; MEMF(position + 4) = 0;
        MEMF(position + 8) = i ? finish.z : start.z;
        mm3_race_guidance_checkpoint(0, objects[i], position, origin, objects[1]);
    }
    mm3_race_guidance_end(0);
    abi_start(0); sub_0011CAE8(); abi_end(); assert(route_segment_draw_count() == 6);
    /* Pending course plus brighter live approach: neither replaces the other. */
    for (i = 0; i < draw_count && draws[i].color[3] != .98f; ++i) near(draws[i].color[3], .8f);
    assert(i > 0 && i < draw_count);
    near(.5f*(draws[i].xy[0][0]+draws[i].xy[3][0]), MEMF(node+0x94));
    near(.5f*(draws[i].xy[0][1]+draws[i].xy[3][1]), MEMF(node+0x98));
    /* Moving onto the destination street refreshes live guidance while the
     * checkpoint-to-checkpoint course remains connected. */
    s_race[0].origin = (RoadPoint){60,0,-10};
    abi_start(0); sub_0011CAE8(); abi_end(); assert(route_segment_draw_count() == 4);
    assert(s_paths[0].active_count == 2);
    for (i = 0; i < draw_count && draws[i].color[3] != .98f; ++i) near(draws[i].color[3], .8f);
    assert(i + 1 == draw_count);
    near(.5f*(draws[i].xy[1][1]+draws[i].xy[2][1]), MEMF(node+0x98)-20);
    mm3_race_guidance_roads(0, 0);
    abi_start(0); sub_0011CAE8(); abi_end(); assert(draw_count == 0);
    assert(!s_roads.nodes && !s_roads.edge_count);
    MEMF(origin) = MEMF(origin + 4) = MEMF(origin + 8) = 0;
}

static void draw_test_polyline(const RoadPoint *polyline, unsigned count, float radius) {
    abi_start(0);
    route_polyline(0x25000, 0, polyline, count, (RoadPoint){0}, 1, 1, 0, radius, radius, 0, 0, .8f);
    assert(g_esp == stack && g_fp_top == 0);
    assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
}
static unsigned route_segment_draw_count(void) {
    unsigned i, count = 0;
    for (i = 0; i < draw_count; ++i)
        if (draws[i].xy[0][0] != draws[i].xy[3][0] || draws[i].xy[0][1] != draws[i].xy[3][1]) ++count;
    return count;
}
static const Draw *route_segment_draw(unsigned index) {
    unsigned i;
    for (i = 0; i < draw_count; ++i) {
        if (draws[i].xy[0][0] == draws[i].xy[3][0] && draws[i].xy[0][1] == draws[i].xy[3][1]) continue;
        if (!index--) return draws + i;
    }
    assert(!"missing route segment"); return NULL;
}
static float distance_to_draw(float x, float y, const Draw *draw) {
    float ax = .5f*(draw->xy[0][0] + draw->xy[3][0]);
    float ay = .5f*(draw->xy[0][1] + draw->xy[3][1]);
    float bx = .5f*(draw->xy[1][0] + draw->xy[2][0]);
    float by = .5f*(draw->xy[1][1] + draw->xy[2][1]);
    float dx = bx - ax, dy = by - ay;
    float t = ((x-ax)*dx + (y-ay)*dy) / (dx*dx + dy*dy);
    if (t < 0) t = 0; if (t > 1) t = 1;
    return hypotf(x - ax - t*dx, y - ay - t*dy);
}
static void route_draw_simplification_contract(void) {
    static RoadPoint dense[COURSE_POINTS];
    const RoadPoint turns[5] = {{-60,0,-10}, {-20,0,10}, {-20,0,-10}, {20,0,10}, {60,0,-10}};
    const RoadPoint reverse[3] = {{-20,0,0}, {60,0,0}, {10,0,0}};
    const RoadPoint loop[4] = {{0,0,0}, {60,0,0}, {60,0,60}, {0,0,0}};
    const RoadPoint long_chord[2] = {{-100000,0,0}, {100000,0,0}};
    unsigned i, j, curved_draws;
    /* A 758-point straight street needs one native submission. Clipping
     * still finds square crossings with both retained ends outside. */
    for (i = 0; i < 758; ++i) dense[i] = (RoadPoint){-200 + 400.0f*i/757,0,0};
    draw_test_polyline(dense, 758, 56); assert(draw_count == 1);
    near(.5f*(draws[0].xy[0][0]+draws[0].xy[3][0]), 56);
    near(.5f*(draws[0].xy[1][0]+draws[0].xy[2][0]), -56);
    /* Coalesced chords can be much longer than one road sample. Square
     * intersections must remain inside the inset at both ends. */
    draw_test_polyline(long_chord, 2, 56); assert(draw_count == 1);
    near(.5f*(draws[0].xy[0][0]+draws[0].xy[3][0]), 56);
    near(.5f*(draws[0].xy[1][0]+draws[0].xy[2][0]), -56);
    /* Dense curves coalesce, while the independently captured quad
     * centreline remains within a quarter pixel of every original point. */
    for (i = 0; i < 201; ++i) {
        float x = (float)i - 100;
        dense[i] = (RoadPoint){x,0,.006f*x*x - 40};
    }
    draw_test_polyline(dense, 201, 1000); curved_draws = route_segment_draw_count();
    assert(curved_draws >= 8 && curved_draws <= 40 && draw_count <= curved_draws * 5);
    for (i = 0; i < 201; ++i) {
        float nearest = FLT_MAX;
        for (j = 0; j < curved_draws; ++j) {
            float distance = distance_to_draw(-dense[i].x, dense[i].z, route_segment_draw(j));
            if (distance < nearest) nearest = distance;
        }
        assert(nearest <= .2501f);
    }
    for (i = 0; i < 4; ++i) for (j = 0; j < 16; ++j) {
        float t = (float)j/16;
        dense[i*16+j] = (RoadPoint){turns[i].x + t*(turns[i+1].x-turns[i].x),0,
                                   turns[i].z + t*(turns[i+1].z-turns[i].z)};
    }
    dense[64] = turns[4];
    draw_test_polyline(dense, 65, 1000); assert(route_segment_draw_count() == 4);
    for (i = 0; i < 4; ++i) {
        const Draw *draw = route_segment_draw(i);
        near(.5f*(draw->xy[1][0]+draw->xy[2][0]), -turns[i+1].x);
        near(.5f*(draw->xy[1][1]+draw->xy[2][1]), turns[i+1].z);
    }
    /* Distance to a finite segment preserves a street reversal; a closed
     * shape with coincident endpoints must retain its whole triangle. */
    draw_test_polyline(reverse, 3, 1000); assert(draw_count == 2);
    near(.5f*(draws[0].xy[1][0]+draws[0].xy[2][0]), -60);
    draw_test_polyline(loop, 4, 1000); assert(route_segment_draw_count() == 3);
    for (i = 0; i < COURSE_POINTS; ++i) dense[i] = (RoadPoint){-100 + 200.0f*i/(COURSE_POINTS-1),0,0};
    draw_test_polyline(dense, COURSE_POINTS, 1000); assert(draw_count == 1);
    /* Full-capacity alternating turns stress bounded scratch and the work
     * fallback without asking the native capture boundary for 16K quads. */
    for (i = 0; i < COURSE_POINTS; ++i)
        s_route_draw.points[i] = (RoutePoint){.01f*i, (float)(i & 1)};
    route_simplify(COURSE_POINTS);
    for (i = 0; i < COURSE_POINTS; ++i) assert(s_route_draw.keep[i]);
    printf("PASS route draw simplification: 758 straight samples -> 1 segment; 201 curved samples -> %u segments, <=0.25px error; sharp turns/reversal/closed loop/square clipping/maximum-capacity zigzag\n", curved_draws);
}

static void square_route_clipping_contract(void) {
    const RoadPoint corner_edge[2] = {{-80,0,50}, {80,0,50}};
    const RoadPoint beyond_edge[2] = {{-80,0,57}, {80,0,57}};
    const RoadPoint corner_crossing[2] = {{-200,0,-200}, {200,0,200}};
    const RoadPoint tangent[2] = {{-100,0,12}, {-12,0,100}};
    const RoadPoint street[2] = {{-200,0,0}, {200,0,0}};
    const RoadPoint vertical[2] = {{-50,0,-80}, {-50,0,80}};
    float diagonal = sqrtf(.5f);
    draw_test_polyline(corner_edge, 2, 56); assert(draw_count == 1);
    near(.5f*(draws[0].xy[0][0]+draws[0].xy[3][0]), 56);
    near(.5f*(draws[0].xy[1][0]+draws[0].xy[2][0]), -56);
    near(.5f*(draws[0].xy[0][1]+draws[0].xy[3][1]), 50);
    near(.5f*(draws[0].xy[1][1]+draws[0].xy[2][1]), 50);
    draw_test_polyline(beyond_edge, 2, 56); assert(draw_count == 0);
    draw_test_polyline(corner_crossing, 2, 56); assert(draw_count == 1);
    near(.5f*(draws[0].xy[0][0]+draws[0].xy[3][0]), 56);
    near(.5f*(draws[0].xy[0][1]+draws[0].xy[3][1]), -56);
    near(.5f*(draws[0].xy[1][0]+draws[0].xy[2][0]), -56);
    near(.5f*(draws[0].xy[1][1]+draws[0].xy[2][1]), 56);
    draw_test_polyline(tangent, 2, 56); assert(draw_count == 0);
    /* Clip after projection, including diagonal headings. The source street
     * is unchanged; the visible diagonal reaches both square corners. */
    abi_start(0);
    route_polyline(0x25000, 0, street, 2, (RoadPoint){0}, 1,
                   diagonal, diagonal, 56, 56, 128, 96, .8f);
    assert(draw_count == 1);
    near(.5f*(draws[0].xy[0][0]+draws[0].xy[3][0]), 184);
    near(.5f*(draws[0].xy[0][1]+draws[0].xy[3][1]), 40);
    near(.5f*(draws[0].xy[1][0]+draws[0].xy[2][0]), 72);
    near(.5f*(draws[0].xy[1][1]+draws[0].xy[2][1]), 152);
    assert(g_esp == stack && g_fp_top == 0);
    assert(g_ebx == 0x11223344 && g_esi == 0x22334455 && g_edi == 0x33445566);
    /* A split/expanded map can be rectangular. Preserve independent native
     * width/height insets rather than letting a tall route escape its panel. */
    abi_start(0);
    route_polyline(0x25000, 0, vertical, 2, (RoadPoint){0}, 1,
                   1, 0, 56, 40, 128, 96, .8f);
    assert(draw_count == 1);
    near(.5f*(draws[0].xy[0][0]+draws[0].xy[3][0]), 178);
    near(.5f*(draws[0].xy[0][1]+draws[0].xy[3][1]), 56);
    near(.5f*(draws[0].xy[1][0]+draws[0].xy[2][0]), 178);
    near(.5f*(draws[0].xy[1][1]+draws[0].xy[2][1]), 136);
    puts("PASS square route: corner coverage, outside crossings, outside rejection, tangent rejection, rotated/rectangular axes, native inset/placement and ABI");
}

static unsigned road_line_cache_complete(void) {
    unsigned i, segments = 0, points = 0;
    RoadPoint *cached_points;
    RoadLine *cached_lines;
    assert(s_roads.edge_count && !(s_roads.edge_count & 1));
    assert(road_lines_build());
    assert(s_road_lines.points && s_road_lines.lines && s_road_lines.count);
    assert(s_road_lines.generation == s_roads.generation && s_road_lines.edges == s_roads.edge_count);
    assert(s_road_lines.count <= s_roads.edge_count / 2);
    for (i = 0; i < s_road_lines.count; ++i) {
        const RoadLine *line = s_road_lines.lines + i;
        unsigned j;
        float min_x = FLT_MAX, max_x = -FLT_MAX, min_z = FLT_MAX, max_z = -FLT_MAX;
        assert(line->first == points && line->count >= 2 && line->count <= COURSE_POINTS);
        assert(line->first + line->count <= s_roads.edge_count + 1);
        for (j = 0; j < line->count; ++j) {
            RoadPoint p = s_road_lines.points[line->first + j];
            assert(road_point_valid(p));
            if (p.x < min_x) min_x = p.x; if (p.x > max_x) max_x = p.x;
            if (p.z < min_z) min_z = p.z; if (p.z > max_z) max_z = p.z;
        }
        near(line->min_x, min_x); near(line->max_x, max_x);
        near(line->min_z, min_z); near(line->max_z, max_z);
        segments += line->count - 1; points += line->count;
    }
    /* Every undirected street edge contributes exactly one drawable segment;
     * shared chain endpoints account for the additional cached points. */
    assert(segments == s_roads.edge_count / 2);
    assert(points == segments + s_road_lines.count && points <= s_roads.edge_count);
    cached_points = s_road_lines.points; cached_lines = s_road_lines.lines;
    assert(road_lines_build());
    assert(s_road_lines.points == cached_points && s_road_lines.lines == cached_lines);
    return segments;
}

static void road_line_fixture_edges_once(void) {
    unsigned char seen[16] = {0};
    unsigned i, j, edge;
    assert(s_roads.edge_count / 2 <= sizeof seen);
    for (i = 0; i < s_road_lines.count; ++i) {
        const RoadLine *line = s_road_lines.lines + i;
        for (j = 1; j < line->count; ++j) {
            RoadPoint a = s_road_lines.points[line->first + j - 1];
            RoadPoint b = s_road_lines.points[line->first + j];
            unsigned matches = 0;
            for (edge = 0; edge < s_roads.edge_count; edge += 2) {
                RoadPoint p = s_roads.nodes[s_roads.edges[edge ^ 1].to].position;
                RoadPoint q = s_roads.nodes[s_roads.edges[edge].to].position;
                if ((road_distance(a,p) == 0 && road_distance(b,q) == 0) ||
                    (road_distance(a,q) == 0 && road_distance(b,p) == 0)) {
                    assert(!seen[edge / 2]); seen[edge / 2] = 1; ++matches;
                }
            }
            assert(matches == 1);
        }
    }
    for (edge = 0; edge < s_roads.edge_count / 2; ++edge) assert(seen[edge]);
}

static void road_line_cache_contract(void) {
    static const RoadPoint branch_cycle[10] = {
        {0,0,0}, {-10,0,0}, {-20,0,0}, {10,0,0}, {20,0,0}, {0,0,10},
        {40,0,0}, {50,0,0}, {45,0,10}, {1000,0,1000}
    };
    static const unsigned edges[8][2] = {{0,1},{1,2},{0,3},{3,4},{0,5},{6,7},{7,8},{8,6}};
    unsigned i, closed = 0;
    road_graph_clear(); assert(road_graph_allocate(10));
    for (i = 0; i < 10; ++i) s_roads.nodes[i].position = branch_cycle[i];
    for (i = 0; i < 8; ++i) assert(road_graph_edge(edges[i][0],edges[i][1]));
    assert(road_line_cache_complete() == 8 && s_road_lines.count == 4);
    road_line_fixture_edges_once();
    /* Three branch arms and one disconnected degree-two cycle. The cycle
     * retains its closing edge; the isolated node invents no street. */
    for (i = 0; i < s_road_lines.count; ++i) {
        const RoadLine *line = s_road_lines.lines + i;
        RoadPoint a = s_road_lines.points[line->first];
        RoadPoint b = s_road_lines.points[line->first + line->count - 1];
        if (road_distance(a,b) == 0) { assert(line->count == 4); ++closed; }
        else assert(road_distance(a,branch_cycle[0]) == 0 || road_distance(b,branch_cycle[0]) == 0);
    }
    assert(closed == 1);
    /* An edge added within one generation retires the earlier decomposition.
     * It closes an arm into a junction loop while preserving every old edge. */
    assert(road_graph_edge(5,2));
    assert(s_road_lines.edges != s_roads.edge_count);
    assert(road_line_cache_complete() == 9 && s_road_lines.count == 3);
    road_line_fixture_edges_once();
    closed = 0;
    for (i = 0; i < s_road_lines.count; ++i) {
        const RoadLine *line = s_road_lines.lines + i;
        if (road_distance(s_road_lines.points[line->first],
                          s_road_lines.points[line->first + line->count - 1]) == 0) ++closed;
    }
    assert(closed == 2);
    road_graph_clear();
    assert(!s_road_lines.points && !s_road_lines.lines && !s_road_lines.count);

    /* A street longer than the draw scratch capacity splits into bounded
     * chains without dropping or duplicating the join's street segment. */
    assert(road_graph_allocate(COURSE_POINTS + 5));
    for (i = 0; i < COURSE_POINTS + 5; ++i) {
        s_roads.nodes[i].position.x = (float)i;
        if (i) assert(road_graph_edge(i-1,i));
    }
    assert(road_line_cache_complete() == COURSE_POINTS + 4 && s_road_lines.count == 2);
    assert(s_road_lines.lines[0].count == COURSE_POINTS);
    {
        unsigned char seen[COURSE_POINTS + 4] = {0};
        unsigned j;
        for (i = 0; i < s_road_lines.count; ++i) {
            const RoadLine *line = s_road_lines.lines + i;
            for (j = 1; j < line->count; ++j) {
                float a = s_road_lines.points[line->first + j - 1].x;
                float b = s_road_lines.points[line->first + j].x;
                unsigned segment = (unsigned)fminf(a,b);
                assert(fabsf(a-b) == 1 && segment < sizeof seen && !seen[segment]);
                seen[segment] = 1;
            }
        }
        for (i = 0; i < sizeof seen; ++i) assert(seen[i]);
    }
    road_graph_clear();
    puts("PASS road-line cache: branch arms, disconnected degree-two cycle, isolated node, exact edge coverage, cache reuse/invalidation, teardown and oversized-chain split");
}

#include "mm3_map_areas_contract.inc"

static unsigned retail_traffic_contract(uint32_t city) {
    unsigned i, j, total = 0, samples = 0;
    for (i = 0; i < MEM32(city + 0xAC); ++i) {
        uint32_t area = MEM32(city + 0x4C) + i * 0xE8;
        uint32_t geometry;
        for (geometry = MEM32(area + 0x78); geometry < MEM32(area + 0x7C); geometry += 7) {
            unsigned count;
            if ((MEM8(geometry + 6) & 15) != 1) continue;
            count = MEM16(geometry) == 2 ? 6 : MEM16(geometry); total += count;
            for (j = 0; j < count; ++j) {
                RoadPoint expected = road_traffic_point(area, geometry, j);
                abi_start(0); g_eax = position + 0x200; g_ecx = area + 0xD4; g_edx = geometry;
                MEMF(stack + 4) = MEMF(area + 0xE0); MEM32(stack + 8) = j;
                sub_0013532D();
                assert(g_esp == stack + 12 && g_fp_top == 0);
                assert(fabsf(MEMF(position + 0x200) - expected.x) < .005f);
                assert(fabsf(MEMF(position + 0x204) - expected.y) < .005f);
                assert(fabsf(MEMF(position + 0x208) - expected.z) < .005f);
                ++samples;
            }
        }
    }
    printf("PASS retail traffic paths: native sample oracle=%u; type-zero island/footpaths omitted\n", samples);
    return total;
}

static void retail_roundabout_contract(uint32_t city) {
    uint32_t area = MEM32(city + 0x4C) + 19 * 0xE8;
    const unsigned streets[2] = {153, 145};
    RoadPoint center = road_control(area + 0x34, 0), ends[2], route[ACTIVE_POINTS];
    unsigned i, count, drawn = 0;
    float length = 0;
    for (i = 0; i < 2; ++i) {
        uint32_t road = MEM32(city + 0x40) + streets[i] * 0xDC, spline = MEM32(road + 0x78);
        ends[i] = road_control(MEM32(spline + 4), (MEM32(spline + 8) - MEM32(spline + 4)) / 12 - 2);
    }
    count = road_path(ends[0], ends[1], route, ACTIVE_POINTS);
    assert(count >= 8);
    for (i = 0; i < count; ++i) {
        assert(hypotf(route[i].x - center.x, route[i].z - center.z) > 48);
        if (i) length += road_distance(route[i - 1], route[i]);
    }
    assert(length > 150 && length < 350);
    /* Inspect submitted gray geometry too: cache decomposition and screen
     * simplification must retain the loop while leaving its island clear. */
    improved = 1; s_road_layer = 1;
    for (i = 0; i < s_road_lines.count; ++i) {
        RoadLine *line = s_road_lines.lines + i;
        unsigned j;
        if (line->max_x < center.x - 110 || line->min_x > center.x + 110 ||
            line->max_z < center.z - 110 || line->min_z > center.z + 110) continue;
        abi_start(0);
        route_polyline(1, 0, s_road_lines.points + line->first, line->count,
                       center, 1, 1, 0, 110, 110, 0, 0, .82f);
        for (j = 0; j < draw_count; ++j) {
            float ax = (draws[j].xy[0][0] + draws[j].xy[3][0]) * .5f;
            float ay = (draws[j].xy[0][1] + draws[j].xy[3][1]) * .5f;
            float bx = (draws[j].xy[1][0] + draws[j].xy[2][0]) * .5f;
            float by = (draws[j].xy[1][1] + draws[j].xy[2][1]) * .5f;
            float dx = bx - ax, dy = by - ay, t = -(ax*dx + ay*dy) / (dx*dx + dy*dy);
            if (t < 0) t = 0; if (t > 1) t = 1;
            assert(hypotf(ax + t*dx, ay + t*dy) > 48);
            near(draws[j].color[0], .62f); ++drawn;
        }
    }
    s_road_layer = 0;
    assert(drawn > 24);
    printf("PASS retail Paris roundabout: opposite entrances route %.1fm around clear island; %u gray segments retain ring\n", length, drawn);
}

static void retail_route_cache_benchmark(const char *filename, const unsigned char *visited) {
    const RoadPoint heading = {0, 0, 0};
    static RoadPoint expected[COURSE_POINTS];
    unsigned i, candidate, expected_count;
    clock_t begin, initialized, advanced;
    memset(&s_race[0], 0, sizeof s_race[0]);
    memset(&s_paths[0], 0, sizeof s_paths[0]);
    for (i = 0; i < 15; ++i) {
        candidate = (i * s_roads.node_count / 15) % s_roads.node_count;
        while (!visited[candidate] || !s_roads.nodes[candidate].routable)
            candidate = (candidate + 1) % s_roads.node_count;
        s_race[0].checkpoints[i].object = 1000 + i;
        s_race[0].checkpoints[i].position = s_roads.nodes[candidate].position;
    }
    s_race[0].count = 15; s_race[0].current_object = 1000;
    s_race[0].origin = s_race[0].checkpoints[0].position;
    begin = clock(); race_paths_update(0, heading); initialized = clock();
    assert(s_paths[0].course_rebuilt == 14 && !s_paths[0].course_reused);
    expected_count = s_paths[0].course_count - s_paths[0].lengths[0];
    memcpy(expected, s_paths[0].course + s_paths[0].lengths[0], expected_count * sizeof(RoadPoint));
    memmove(s_race[0].checkpoints, s_race[0].checkpoints + 1, 14 * sizeof(RaceCheckpoint));
    s_race[0].count = 14; s_race[0].current_object = 1001;
    race_paths_update(0, heading); advanced = clock();
    assert(!s_paths[0].course_rebuilt && s_paths[0].course_reused == 13);
    assert(s_paths[0].course_count == expected_count);
    assert(!memcmp(expected, s_paths[0].course, expected_count * sizeof(RoadPoint)));
    printf("PERF retail route cache: %s initial_15_ms=%.3f advance_14_ms=%.3f course_searches=0 reused_legs=13\n", filename,
           1000.0 * (initialized - begin) / CLOCKS_PER_SEC,
           1000.0 * (advanced - initialized) / CLOCKS_PER_SEC);
}

static void retail_hud_capacity_contract(const char *filename) {
    RoadPoint low = s_roads.nodes[0].position, high = low, center;
    unsigned checkpoints[8], count = 0, i, grown, baseline_draws = 0;
    float extent;
    check = "dense retail Ctrl overview preserves gray/blue/checkpoints/later HUD";
    for (i = 0; i < s_roads.node_count; ++i) {
        RoadPoint p = s_roads.nodes[i].position;
        low.x = fminf(low.x,p.x); low.z = fminf(low.z,p.z);
        high.x = fmaxf(high.x,p.x); high.z = fmaxf(high.z,p.z);
        if (count < 8 && s_roads.nodes[i].routable && i % 173 == 0) checkpoints[count++] = i;
    }
    assert(count == 8);
    center = (RoadPoint){(low.x + high.x)*.5f,0,(low.z + high.z)*.5f};
    extent = fmaxf(high.x - low.x,high.z - low.z)*.5f;
    assert(extent > 100);
    MEMF(origin) = center.x; MEMF(origin + 4) = center.y; MEMF(origin + 8) = center.z;
    mm3_race_guidance_begin(0);
    for (i = 0; i < 8; ++i) {
        RoadPoint p = s_roads.nodes[checkpoints[i]].position;
        uint32_t marker = markers + i*24;
        MEMF(position) = p.x; MEMF(position + 4) = p.y; MEMF(position + 8) = p.z;
        mm3_race_guidance_checkpoint(0,0x33000 + i*256,position,origin,0);
        MEMF(marker) = p.x - center.x; MEMF(marker + 8) = p.z - center.z;
        MEM32(marker + 12) = i ? 0 : 0x80;
    }
    mm3_race_guidance_end(0);
    MEM32(0x407F78 + 4) = markers; marker_count = 8; active_player = 0; improved = 1;
    MEMF(node + 0x50) = 100 / extent; MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
    MEMF(node + 0x4C) = MEMF(node + 0x94) = MEMF(node + 0x98) = MEMF(node + 0xAC) = 0;
    MEMF(node + 0xB4) = MEMF(node + 0xBC) = 3;
    MEMF(layout + 8) = MEMF(layout + 12) = 384;
    MEMF(4 + 0x50) = MEMF(4 + 0x54) = 1; /* Native Checkpoint texture UVs. */
    memset((void *)XBOX_PTR(test_quad),0,0xA0);
    MEM32(test_quad + 4) = 7; MEM32(test_quad + 8) = 8;
    for (i = 0; i < 4; ++i) {
        uint32_t vertex = test_quad + 0xC + i*0x24;
        MEMF(vertex) = i == 0 || i == 3 ? -30.0f : 30.0f;
        MEMF(vertex + 4) = i < 2 ? -20.0f : 20.0f;
        MEMF(vertex + 0x14) = MEMF(vertex + 0x18) = MEMF(vertex + 0x1C) = MEMF(vertex + 0x20) = 1;
    }
    for (grown = 0; grown < 2; ++grown) {
        unsigned used, hud_vertices, vertices;
        native_submission_begin(); MEM32(0x3A0EC0) = 5000;
        MEM8(0x3C5694) = (uint8_t)grown;
        native_pool_allocations = native_pool_frees = 0;
        abi_start(0); PUSH32(g_esp,0x0011E7E0); sub_0011DCBF();
        assert(g_esp == stack && g_fp_top == 0);
        assert(native_pool_allocations == grown && native_pool_frees == grown);
        vertices = grown ? 160000 : 5000;
        assert(MEM32(0x3A0EC0) == vertices && !MEM8(0x3C5694));
        MEM32(packed_vertices + vertices*16) = 0xC01DFACE;
        native_submission_dropped = native_submission_gray = native_submission_blue = native_submission_markers = 0;
        native_submission = 1;
        abi_start(0); MEM32(stack + 12) = 0; sub_0011CAE8(); abi_end();
        native_submission = 0; used = MEM32(0x3C568C);
        if (!grown) baseline_draws = draw_count;
        else assert(draw_count == baseline_draws);
        /* A later untextured HUD quad uses the same real native pool and gate.
         * The old 5000-vertex pool loses it after roads exhaust that pool. */
        abi_start(0);
        PUSH32(g_esp,test_quad); PUSH32(g_esp,0); PUSH32(g_esp,0x0011CB45);
        g_eax = 0; g_edx = 0x25000; native_submit();
        assert(g_esp == stack && g_fp_top == 0); hud_vertices = MEM32(0x3C568C) - used;
        assert(MEM32(packed_vertices + vertices*16) == 0xC01DFACE);
        if (!grown) {
            assert(native_submission_dropped && !native_submission_markers && !hud_vertices);
        } else {
            assert(!native_submission_dropped && native_submission_gray > 1250);
            assert(native_submission_blue && native_submission_markers == 8 && hud_vertices == 4);
            assert(used > 5000 && used + 18 < vertices);
            printf("PASS retail HUD pool: %s gray_quads=%u blue_quads=%u native_checkpoints=%u used=%u/%u, later HUD retained; original 5000-vertex drop reproduced\n",
                   filename,native_submission_gray,native_submission_blue,native_submission_markers,used,vertices);
        }
    }
    marker_count = 0; MEMF(origin) = MEMF(origin + 4) = MEMF(origin + 8) = 0;
    mm3_race_guidance_frame_begin();
}

#include "mm3_big_one_contract.inc"

static void retail_road_contract(const char *filename) {
    FILE *file = fopen(filename, "rb");
    uint32_t header[7], city, roads, junctions, i;
    unsigned samples = 0, reachable = 0, ordinary_reachable = 0, pending = 0, traffic_nodes;
    unsigned char *visited;
    RoadPoint route[ACTIVE_POINTS];
    assert(file && fread(header, sizeof header, 1, file) == 1);
    assert(header[0] < sizeof memory && header[2] <= sizeof memory - header[0]);
    assert(fread(memory + header[0], header[2], 1, file) == 1); fclose(file);
    city = header[1]; roads = header[3]; junctions = header[4];
    traffic_nodes = retail_traffic_contract(city);
    mm3_race_guidance_roads(city, 1);
    retail_map_areas_contract(city);
    assert(s_roads.node_count == header[5] + traffic_nodes && s_roads.edge_count > s_roads.node_count);
    {
        char *audit_directory = NULL;
        size_t audit_length = 0;
        assert(!_dupenv_s(&audit_directory, &audit_length, "MM3_ROAD_AUDIT"));
        if (audit_directory) {
            char path[1024];
            FILE *audit;
            snprintf(path, sizeof path, "%s/%s.graph.bin", audit_directory, traffic_nodes ? "Paris" : "Washington");
            assert(!fopen_s(&audit, path, "wb") && audit);
            fwrite(&s_roads.node_count, sizeof s_roads.node_count, 1, audit);
            fwrite(&s_roads.edge_count, sizeof s_roads.edge_count, 1, audit);
            fwrite(s_roads.nodes, sizeof *s_roads.nodes, s_roads.node_count, audit);
            fwrite(s_roads.edges, sizeof *s_roads.edges, s_roads.edge_count, audit);
            fclose(audit);
            snprintf(path, sizeof path, "%s/%s.map-areas.bin", audit_directory, traffic_nodes ? "Paris" : "Washington");
            assert(!fopen_s(&audit, path, "wb") && audit);
            fwrite(&s_map_areas.count, sizeof s_map_areas.count, 1, audit);
            fwrite(&s_map_areas.point_count, sizeof s_map_areas.point_count, 1, audit);
            if (s_map_areas.point_count)
                fwrite(s_map_areas.points, sizeof *s_map_areas.points, s_map_areas.point_count, audit);
            if (s_map_areas.count)
                fwrite(s_map_areas.lines, sizeof *s_map_areas.lines, s_map_areas.count, audit);
            fclose(audit);
        }
        free(audit_directory);
    }
    assert(road_line_cache_complete() == s_roads.edge_count / 2);
    visited = (unsigned char *)calloc(s_roads.node_count, 1); assert(visited);
    visited[junctions] = 1; s_roads.heap[pending++] = junctions;
    while (pending) {
        unsigned n = s_roads.heap[--pending], edge; ++reachable;
        if (n < header[5]) ++ordinary_reachable;
        for (edge = s_roads.nodes[n].first; edge != NO_NODE; edge = s_roads.edges[edge].next) {
            unsigned next = s_roads.edges[edge].to;
            if (!visited[next]) { visited[next] = 1; s_roads.heap[pending++] = next; }
        }
    }
    /* Both cities have a complete core street network. Washington's two
     * isolated streets remain isolated instead of inventing cross-city joins. */
    assert(ordinary_reachable == header[6]);
    for (i = 0; i < s_roads.node_count; ++i) {
        if (i < header[5]) assert(s_roads.nodes[i].routable);
        else assert(s_roads.nodes[i].routable == (unsigned)(visited[i] != 0));
    }
    printf("PASS retail traffic routing: %u/%u native samples have a verified street entrance; other traffic paths render without stealing route snaps\n",
           reachable - ordinary_reachable, traffic_nodes);
    for (i = 0; i < roads; i += 7) {
        uint32_t object = MEM32(city + 0x40) + i * 0xDC, spline = MEM32(object + 0x78);
        RoadCurve curve = {0};
        unsigned sample;
        curve.controls = MEM32(spline + 4);
        curve.control_count = (MEM32(spline + 8) - curve.controls) / 12;
        curve.bias = MEMF(spline + 0x10); curve.tension = MEMF(spline + 0x14);
        curve.continuity = MEMF(spline + 0x18);
        for (sample = 0; sample <= 6; ++sample) {
            const float times[7] = {0, .13f, .375f, .51f, .8f, .9991f, 1};
            RoadPoint expected = road_curve_point(&curve, times[sample]);
            abi_start(0); g_ebx = spline;
            MEM32(stack + 4) = position + 0x200; MEM32(stack + 8) = position + 0x300;
            MEMF(stack + 12) = times[sample] * (curve.control_count - 3);
            sub_000C191C();
            assert(g_esp == stack + 16 && g_fp_top == 0);
            /* The native x87 evaluator rounds intermediates to float; the
             * host curve must agree within 5mm even at large world positions. */
            assert(fabsf(MEMF(position + 0x200) - expected.x) < .005f);
            assert(fabsf(MEMF(position + 0x204) - expected.y) < .005f);
            assert(fabsf(MEMF(position + 0x208) - expected.z) < .005f);
            ++samples;
        }
    }
    for (i = junctions; i < s_roads.node_count; i += 311) {
        unsigned finish = (i + s_roads.node_count / 2) % s_roads.node_count;
        if (visited[i] && visited[finish]) {
            unsigned count = road_path(s_roads.nodes[i].position, s_roads.nodes[finish].position, route, ACTIVE_POINTS);
            assert(count >= 2);
            assert(road_distance(route[0], s_roads.nodes[i].position) < .01f);
            assert(road_distance(route[count-1], s_roads.nodes[finish].position) < .01f);
        }
    }
    printf("PASS retail street graph: %s roads=%u junctions=%u reachable=%u/%u native_spline_samples=%u cache_lines=%u cache_segments=%u\n",
           filename, roads, junctions, reachable, s_roads.node_count, samples,
           s_road_lines.count, s_roads.edge_count / 2);
    if (traffic_nodes) retail_roundabout_contract(city);
    if (!traffic_nodes) { retail_big_one_contract(); retail_big_one_free_order_contract(); retail_big_one_live_draw_contract(); }
    retail_route_cache_benchmark(filename, visited);
    retail_hud_capacity_contract(filename);
    retail_map_areas_capacity_contract(city);
    free(visited); mm3_race_guidance_roads(city, 0);
    assert(!s_roads.node_count && !s_roads.edge_count);
    assert(!s_road_lines.points && !s_road_lines.lines && !s_road_lines.count);
    assert(!s_map_areas.points && !s_map_areas.lines && !s_map_areas.count && !s_map_areas.point_count);
}

#include "mm3_race_routing_contract.inc"
#include "mm3_race_route_cache_contract.inc"
#include "mm3_roundabout_contract.inc"
#include "mm3_road_width_contract.inc"
#include "mm3_route_join_contract.inc"

static void live_direction_route_contract(void) {
    const RoadPoint loop[] = {{-50,0,0}, {50,0,0}, {50,0,80}, {-50,0,80}, {-50,0,-40}};
    routing_fixture(loop, 5);
    routing_street(0,1,100); routing_street(1,2,200); routing_street(2,3,300);
    routing_street(3,0,400); routing_street(0,4,500);
    MEMF(origin) = MEMF(origin + 4) = MEMF(origin + 8) = 0;
    MEMF(position) = -50; MEMF(position + 4) = 0; MEMF(position + 8) = -20;
    mm3_race_guidance_begin(0);
    mm3_race_guidance_checkpoint(0, objects[0], position, origin, objects[0]);
    mm3_race_guidance_end(0);
    MEMF(node + 0x50) = 1; MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
    MEMF(node + 0xB4) = 1; MEMF(layout + 8) = MEMF(layout + 12) = 512;
    /* Expanded/north-up map rotation differs from the real car heading. */
    MEMF(node + 0x4C) = 0; player_heading_valid = 1; player_heading = -1.5707963268f;
    abi_start(0); mm3_race_map_route(node, 0x407F78, 1, 0, layout);
    assert(s_paths[0].active_count == 6); near(s_paths[0].active[1].x, 50);
    /* Turning while stationary invalidates the cached forward route. */
    player_heading = 1.5707963268f;
    abi_start(0); mm3_race_map_route(node, 0x407F78, 1, 0, layout);
    assert(s_paths[0].active_count == 3); near(s_paths[0].active[1].x, -50);
    /* The same current gate can be approached from opposite directions
     * after recovery. Its outgoing cached leg must share the new approach. */
    MEMF(origin) = -20; player_heading = -1.5707963268f;
    mm3_race_guidance_begin(0);
    MEMF(position) = MEMF(position + 8) = 0;
    mm3_race_guidance_checkpoint(0, objects[0], position, origin, objects[0]);
    MEMF(position) = -50; MEMF(position + 8) = -20;
    mm3_race_guidance_checkpoint(0, objects[1], position, origin, objects[0]);
    mm3_race_guidance_end(0);
    abi_start(0); mm3_race_map_route(node, 0x407F78, 1, 0, layout);
    assert(s_paths[0].active_count == 2 && s_paths[0].lengths[0] == 6);
    near(s_paths[0].course[s_paths[0].starts[0] + 1].x, 50);
    s_race[0].origin.x = 20; player_heading = 1.5707963268f;
    abi_start(0); mm3_race_map_route(node, 0x407F78, 1, 0, layout);
    assert(s_paths[0].active_count == 2 && s_paths[0].lengths[0] == 3);
    near(s_paths[0].course[s_paths[0].starts[0] + 1].x, -50);
    player_heading_valid = 0; road_graph_clear(); mm3_race_guidance_frame_begin();
    MEMF(origin) = 0;
}

int main(int argc, char **argv) {
    unsigned i;
    g_xbox_mem_offset = (ptrdiff_t)memory; native_constants();
    MEM32(0x3BA52C) = 0x80FFAA88;
    MEM32(0x407C1C) = 2;
    for (i = 0; i < 4; ++i) MEM32(objects[i] - 0x18 + 0x54) = 0x40000;
    MEM32(node + 0x10) = 4;
    MEM32(node + 0x8C) = property; MEM32(property) = property + 0x100;
    MEM32(property + 0x100 + 0x5C) = 0x100;
    fixture_roads();
    native_contract(); clipping_and_state_contract(); course_finish_and_revisit_contract();
    native_publication_lifecycle_contract(); native_world_contract(); native_submission_contract();
    native_gpu_format_contract();
    road_navigation_contract();
    roundabout_traffic_contract();
    road_width_contract();
    race_direction_routing_contract();
    route_cache_contract();
    live_direction_route_contract();
    route_draw_simplification_contract();
    route_join_contract();
    square_route_clipping_contract();
    /* Roads also draw in Cruise; blue guidance overlays gray street chains
     * in races. Stock keeps its former route color, and teardown drops data. */
    fixture_roads(); improved = 1; cruise_origin_valid = 1;
    memset(cruise_origin, 0, sizeof cruise_origin);
    MEMF(node + 0x50) = 1; MEMF(node + 0x54) = 0; MEMF(node + 0x58) = 1024;
    MEMF(node + 0x4C) = 0; MEMF(node + 0x94) = 114; MEMF(node + 0x98) = 384;
    MEMF(node + 0xB4) = 1; MEMF(layout + 8) = 128; MEMF(layout + 12) = 128;
    mm3_race_guidance_frame_begin(); abi_start(0);
    mm3_race_map_route(node, 0x407F78, 1, 0, layout);
    assert(draw_count > 0);
    for (i = 0; i < draw_count; ++i) {
        near(draws[i].color[0], .62f); near(draws[i].color[1], .65f); near(draws[i].color[2], .69f);
    }
    assert(s_road_lines.count == 1 && s_road_lines.lines[0].count == 4);
    publish(0, 1); abi_start(0); mm3_race_map_route(node, 0x407F78, 1, 0, layout);
    {
        unsigned gray = 0, blue = 0;
        for (i = 0; i < draw_count; ++i) {
            if (draws[i].color[0] == .62f) ++gray;
            else { near(draws[i].color[0], .12f); near(draws[i].color[1], .58f); near(draws[i].color[2], 1); ++blue; }
        }
        assert(gray && blue);
    }
    mm3_race_guidance_frame_begin(); cruise_origin_valid = 0; abi_start(0);
    mm3_race_map_route(node, 0x407F78, 1, 0, layout); assert(!draw_count);
    improved = 0; road_graph_clear(); assert(!s_road_lines.points && !s_road_lines.lines);
    road_line_cache_contract();
    for (i = 1; i < (unsigned)argc; ++i) retail_road_contract(argv[i]);
    puts("PASS race guidance: native small/expanded/split/rotated maps, checkpoint styles/pulses, persistent course/HUD lifecycle, connected street detours, overpass snapping, live reroutes, city teardown, quad clipping/packing/batch identity, shader/PB format and guest ABI");
    return 0;
}
