#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned long long test_clock = 1;
#define MM3_NATIVE_FOG_CLOCK() test_clock
#include "../src/mm3_native_fog.c"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebp, g_seh_ebp;
static unsigned fog_calls, horizon_calls, space_calls;
static int available = 1, space_room;
static unsigned char *arena;
static const uint32_t self = 0x10000, horizon = 0x12000, stack = 0x20000;
static const uint32_t camera = 0x30000, device = 0x40000, packet = 0x50000, space = 0x60000;
int nv2a_d3d_distance_fog_ready(void) { return available; }
/* The exhaust smoke is off here (tests/mm3_exhaust_regression.py). */
int mm3_exhaust_enabled(void) { return 0; }
int mm3_exhaust_packet(uint32_t view, uint32_t words[18]) { (void)view; (void)words; return 0; }
float mm3_city_haze_scale(void) { return 2.0f; }
void sub_000F8DCE_original(void) { ++fog_calls; assert(g_ecx == self && g_esp == stack); g_eax = 0x55660000; g_esp += 16; }
void sub_000F8750_original(void) { ++horizon_calls; assert(g_ecx == horizon && g_esp == stack); g_eax = 0x77880000; g_esp += 8; }
/* Title MakeSpace: stdcall (minimum, requested) below a return slot. With
 * room it moves PUT to fresh space whose threshold leaves the packet room;
 * it clobbers the scratch registers and the frame globals either way. */
void sub_00344770(void)
{
    ++space_calls;
    assert(g_esp == stack - 12 && MEM32(g_esp) == 0);
    assert(MEM32(g_esp + 4) == 0x4000 && MEM32(g_esp + 8) == 0x8000);
    assert(MEM32(stack + 4) == 0xFFAABBCC);
    if (space_room) { MEM32(device) = space; MEM32(device + 4) = space + 0x100; }
    g_eax = MEM32(device); g_ecx = 0xDEAD0001; g_edx = 0xDEAD0002;
    g_ebp = 0xDEAD0003; g_seh_ebp = 0xDEAD0004;
    g_esp += 12;
}
static void commit(uint32_t address, size_t bytes)
{ assert(VirtualAlloc(arena + address, bytes, MEM_COMMIT, PAGE_READWRITE) == arena + address); }
static void setup(void)
{
    memset(arena + 0x10000, 0xA5, 0x60000);
    MEM32(self + 12) = camera; MEM32(horizon + 0x28) = camera;
    MEM32(0x351F48) = device; MEM32(0x352E00) = 0x8000;
    MEM32(device) = packet; MEM32(device + 4) = packet + 48; MEM32(device + 8) = 0;
    MEMF(camera + 0x1ED8) = .25f; MEMF(camera + 0x1FB0) = 1000;
    MEMF(camera + 0x1EE4) = 2; MEMF(camera + 0x1EF8) = 4;
    MEMF(camera + 0x1F34) = 2; MEMF(camera + 0x1F38) = 4; MEMF(camera + 0x1F3C) = .5f;
    MEM32(stack) = 0x12345678; MEM32(stack + 4) = 0xFFAABBCC;
    g_ecx = self; g_esp = stack; g_eax = 0x12345678;
    g_edx = 0x2468ACE0; g_ebp = 0x00F00F00; g_seh_ebp = 0x00E00E00;
    fog_calls = horizon_calls = space_calls = 0; available = 1; space_room = 0;
    s_native_fog_camera = 0;
}
static void check_packet(uint32_t at)
{
    assert(!fog_calls && g_esp == stack + 16 && g_ecx == self && g_eax == 0x12345601);
    assert(g_edx == 0x2468ACE0 && g_ebp == 0x00F00F00 && g_seh_ebp == 0x00E00E00);
    assert(MEM32(device) == at + 48 && MEM32(at - 4) == 0xA5A5A5A5 && MEM32(at + 48) == 0xA5A5A5A5);
    assert(MEM32(at) == 0x402C0100 && MEM32(at + 4) == 0x58424647);
    assert(MEMF(at + 8) == 4 && MEMF(at + 12) == 1000);
    assert(MEMF(at + 16) == 300 && MEMF(at + 20) == 900);
    assert(MEM32(at + 24) == 0xFFAABBCC && MEM32(at + 28) == 7);
    assert(MEMF(at + 32) == .25f && MEMF(at + 36) == .25f);
    assert(MEMF(at + 40) == .5f && MEMF(at + 44) == 6.25f);
    assert(MEM32(stack) == 0x12345678 && MEM32(stack + 4) == 0xFFAABBCC);
    assert(s_native_fog_camera == camera);
}
static void check_fallback(void)
{
    unsigned char before[64];
    memcpy(before, arena + packet, sizeof before);
    sub_000F8DCE();
    assert(fog_calls == 1 && g_eax == 0x55660000 && g_esp == stack + 16 && g_ecx == self);
    assert(!memcmp(before, arena + packet, sizeof before));
    assert(!s_native_fog_camera);
    g_ecx = horizon; g_esp = stack; sub_000F8750();
    assert(horizon_calls == 1 && g_esp == stack + 8);
}
/* The camera turned `yaw` radians about world up. */
static void turn_camera(float yaw)
{
    float m[9];
    unsigned i;
    m[0] = cosf(yaw); m[1] = 0; m[2] = -sinf(yaw);
    m[3] = 0; m[4] = 1; m[5] = 0;
    m[6] = sinf(yaw); m[7] = 0; m[8] = cosf(yaw);
    for (i = 0; i < 9; ++i) MEMF(camera + 0x1F24 + (i / 3) * 16 + (i % 3) * 4) = m[i];
}
/* A frame's first fog pass, the camera at `yaw`. */
static void cut_frame(float yaw)
{
    mm3_native_fog_frame_begin();
    setup(); MEM32(device + 4) = packet + 0x100;
    turn_camera(yaw);
    sub_000F8DCE();
    assert(g_esp == stack + 16 && g_ecx == self);
}
/* Camera cuts put a hold packet ahead of the frame's fog pass, native or
 * retail (whose own commands then follow at PUT). */
static void cut_cases(int native)
{
    const uint32_t after = native ? 0x402C0100u : 0xA5A5A5A5u;
    const float quarter = 1.5707964f;
    s_cuts = s_cut_holds = 0;
    /* Nothing to compare with yet, then a fast but continuous turn. */
    cut_frame(0); assert(MEM32(packet) == after);
    cut_frame(.7f); assert(MEM32(packet) == after);
    /* Look left: the hold packet first, the title's PUT moved past it. */
    cut_frame(.7f + quarter);
    assert(MEM32(packet) == 0x40040100 && MEM32(packet + 4) == 0x58444C48 && MEM32(packet + 8) == after);
    assert(MEM32(device) == packet + (native ? 56u : 8u) && s_cut_holds == 1 && fog_calls == (unsigned)!native);
    /* Only a frame's first pass compares, and a frame without one forgets. */
    setup(); MEM32(device + 4) = packet + 0x100; turn_camera(0);
    sub_000F8DCE(); assert(MEM32(packet) == after);
    mm3_native_fog_frame_begin();
    cut_frame(0); assert(MEM32(packet) == after);
    cut_frame(3.1415927f); assert(MEM32(packet) == 0x40040100 && s_cut_holds == 2 && s_cuts == 2);
    /* No room before the threshold: ask the title for space, as the fog does. */
    mm3_native_fog_frame_begin(); setup(); space_room = 1;
    MEM32(device + 4) = packet + 4; turn_camera(0); sub_000F8DCE();
    assert(space_calls == 1 && MEM32(space) == 0x40040100 && MEM32(packet) == 0xA5A5A5A5 && s_cut_holds == 3);
    assert(g_esp == stack + 16 && g_ecx == self && fog_calls == (unsigned)!native);
    /* Turned off (the dev pipe; MM3_CAMERA_CUT_HOLD=0 or no car lighting
     * likewise): cuts are still counted, none is held. */
    mm3_native_fog_cut_hold(0);
    cut_frame(quarter); assert(MEM32(packet) == after && s_cut_holds == 3 && s_cuts == 4);
    mm3_native_fog_cut_hold(-1);
    cut_frame(0); assert(MEM32(packet) == 0x40040100 && s_cut_holds == 4 && s_cuts == 5);
    /* Never into a push buffer the title is recording (device+8 bit 2): it
     * is replayed later. */
    mm3_native_fog_frame_begin(); setup(); MEM32(device + 4) = packet + 0x100; MEM32(device + 8) = 4;
    turn_camera(quarter); sub_000F8DCE();
    assert(MEM32(packet) == after && s_cut_holds == 4 && s_cuts == 6);
    /* Rows that are not a rotation compare nothing. */
    mm3_native_fog_frame_begin(); setup(); MEM32(device + 4) = packet + 0x100;
    turn_camera(0); MEMF(camera + 0x1F24) = 2; sub_000F8DCE();
    assert(MEM32(packet) == after && !s_cut_have);
    cut_frame(quarter); assert(MEM32(packet) == after && s_cuts == 6);
}
int main(int argc, char **argv)
{
    unsigned i;
    assert(argc == 2);
    assert(!_putenv_s("MM3_NATIVE_FOG", argv[1]));
    arena = VirtualAlloc(NULL, 0x84000000ull, MEM_RESERVE, PAGE_NOACCESS);
    assert(arena); g_xbox_mem_offset = (ptrdiff_t)arena;
    commit(0x10000, 0x60000); commit(0x351000, 0x2000); commit(0x80010000, 0x1000);
    setup();
    if (!strcmp(argv[1], "0")) { check_fallback(); assert(!space_calls); cut_cases(0); goto done; }
    sub_000F8DCE();
    check_packet(packet);
    assert(!space_calls);
    g_ecx = horizon; g_esp = stack; g_eax = 0xABCD0000;
    sub_000F8750(); assert(!horizon_calls && g_esp == stack + 8 && g_eax == 0xABCD0001);
    assert(!s_native_fog_camera);
    g_esp = stack; sub_000F8750(); assert(horizon_calls == 1);
    setup(); sub_000F8DCE(); mm3_native_fog_frame_begin();
    assert(!s_native_fog_camera);
    g_ecx = horizon; g_esp = stack; sub_000F8750(); assert(horizon_calls == 1);
    /* A different camera retains its own panorama. */
    setup(); sub_000F8DCE(); g_ecx = horizon;
    g_esp = stack; MEM32(horizon + 0x28) = camera + 4;
    sub_000F8750(); assert(horizon_calls == 1);
    /* PUT short of room before the threshold, or already past it: ask the
     * title for space as its own writers do, then write the packet there
     * with every guest register and the stack as the caller left them. The
     * horizon strip stays replaced: no retail fog frame. */
    for (i = 0; i < 3; ++i) {
        setup(); space_room = 1;
        if (i == 0) MEM32(device + 4) = packet + 44;
        if (i == 1) MEM32(device + 4) = packet - 4;
        if (i == 2) MEM32(device + 4) = packet;
        sub_000F8DCE();
        assert(space_calls == 1);
        check_packet(space);
        assert(MEM32(packet) == 0xA5A5A5A5);
        g_ecx = horizon; g_esp = stack; sub_000F8750(); assert(!horizon_calls);
    }
    /* Recording mode, a missing size and space that still does not fit keep
     * the retail function, with its original arguments and registers. */
    setup(); MEM32(device + 4) = packet + 44; MEM32(device + 8) = 4;
    space_room = 1; check_fallback(); assert(!space_calls);
    setup(); MEM32(device + 4) = packet + 44; MEM32(0x352E00) = 0;
    space_room = 1; check_fallback(); assert(!space_calls);
    setup(); MEM32(device + 4) = packet + 44;
    check_fallback(); assert(space_calls == 1);
    /* No packet space, unavailable renderer, invalid pointers and matrices
     * must execute the retail function with its original arguments intact. */
    for (i = 0; i < 14; ++i) {
        setup();
        if (i == 0) MEM32(device + 4) = packet + 44;
        if (i == 1) available = 0;
        if (i == 2) MEM32(self + 12) = 0;
        if (i == 3) MEM32(0x351F48) = 0;
        if (i == 4) MEM32(device) = packet + 1;
        if (i == 5) MEM32(device + 4) = packet - 4;
        if (i == 6) MEMF(camera + 0x1ED8) = 0;
        if (i == 7) MEMF(camera + 0x1FB0) = NAN;
        if (i == 8) MEMF(camera + 0x1EE4) = 0;
        if (i == 9) MEMF(camera + 0x1F34) = INFINITY;
        if (i == 10) { MEM32(device) = 0x83FFFFE0u; MEM32(device + 4) = 0x84000010u; }
        if (i == 11) MEMF(camera + 0x1ED8) = 1.0e-35f;
        if (i == 12) MEMF(camera + 0x1FB0) = 1.0e-40f;
        if (i == 13) MEMF(camera + 0x1F34) = 1.0e30f;
        check_fallback();
        /* Invalid input never asks for space; only a short ring does. */
        assert(space_calls == (unsigned)(i == 0 || i == 5 || i == 10));
    }
    /* A failure following a good pass clears the horizon-omission decision. */
    setup(); sub_000F8DCE(); assert(s_native_fog_camera == camera);
    g_ecx = self; g_esp = stack; MEM32(device + 4) = MEM32(device);
    check_fallback();
    /* The pushbuffer can live in the real contiguous-memory alias. */
    setup(); MEM32(device) = 0x80010000; MEM32(device + 4) = 0x80010030;
    sub_000F8DCE(); assert(!fog_calls && MEM32(0x80010004) == 0x58424647);
    assert(MEM32(device) == 0x80010030 && g_esp == stack + 16);
    assert(!fog_range(0xFFFFFFF0u, 48) && !fog_range(0x07FFFFF0u, 48));
    /* The sky's fog ramp (handle +0x28 -> D3D texture at +0x14: a 64x32
     * A8R8G8B8 in contiguous memory) goes ahead of the fog packet with the
     * pass's row argument and the authored-distance remap. */
    for (i = 0; i < 4; ++i) {
        const uint32_t handle = 0x14000, texture = 0x14100;
        setup(); MEM32(device + 4) = packet + 0x100;
        MEM32(self + 0x28) = handle; MEM32(handle + 0x14) = texture;
        MEM32(texture) = 0x01040001; MEM32(texture + 4) = 0x00782000;
        MEM32(texture + 8) = 0x00209B83; MEM32(texture + 12) = 0x05610629;
        MEMF(stack + 8) = .25f;
        if (i == 1) MEM32(texture) = 0x01050001;         /* a surface, not a texture */
        if (i == 2) MEM32(texture + 12) = 0x05610C29;    /* DXT1, not the ramp's layout */
        if (i == 3) MEM32(texture + 4) = 0x04000000;     /* outside contiguous memory */
        sub_000F8DCE();
        assert(!fog_calls && g_esp == stack + 16 && MEM32(stack + 4) == 0xFFAABBCC);
        if (i) { assert(MEM32(packet) == 0x402C0100 && MEM32(device) == packet + 48); continue; }
        assert(MEM32(packet) == 0x40180100 && MEM32(packet + 4) == 0x52424647);
        assert(MEM32(packet + 8) == 0x80782000 && MEM32(packet + 12) == 0x05610629);
        assert(MEMF(packet + 16) == .25f && MEMF(packet + 20) == 2.0f && MEMF(packet + 24) == .78f);
        assert(MEM32(packet + 28) == 0x402C0100 && MEM32(packet + 32) == 0x58424647);
        assert(MEM32(packet + 52) == 0xFFAABBCC && MEMF(packet + 72) == 6.25f);
        assert(MEM32(device) == packet + 76 && MEM32(packet + 76) == 0xA5A5A5A5);
    }
    /* Room for the fog packet but not the ramp as well: ask for space. */
    setup(); space_room = 1; MEM32(device + 4) = packet + 60;
    MEM32(self + 0x28) = 0x14000; MEM32(0x14000 + 0x14) = 0x14100;
    MEM32(0x14100) = 0x01040001; MEM32(0x14104) = 0x00782000; MEM32(0x1410C) = 0x05610629;
    sub_000F8DCE();
    assert(space_calls == 1 && MEM32(space) == 0x40180100 && MEM32(device) == space + 76);
    /* Every 30 s: passes, passes that needed space, retail fallbacks. */
    s_fog_window = 0; test_clock = 1000;
    s_fog_packets = s_fog_made_space = s_fog_fallbacks = 0;
    setup(); sub_000F8DCE();
    assert(s_fog_window == 1000 && s_fog_packets == 1);
    setup(); space_room = 1; MEM32(device + 4) = packet; sub_000F8DCE();
    setup(); MEM32(self + 12) = 0; check_fallback();
    assert(s_fog_packets == 2 && s_fog_made_space == 1 && s_fog_fallbacks == 1);
    test_clock += 29999;
    setup(); sub_000F8DCE();
    assert(s_fog_window == 1000 && s_fog_packets == 3);
    test_clock += 1;
    setup(); sub_000F8DCE();
    assert(s_fog_window == test_clock && s_fog_packets == 1 && !s_fog_made_space && !s_fog_fallbacks);
    cut_cases(1);
done:
    assert(VirtualFree(arena, 0, MEM_RELEASE));
    puts("PASS native fog ordered packet, sky fog ramp ahead of it, camera projection, title push-buffer space instead of a retail fog frame, fallback, horizon ownership, high RAM alias, 30 s report, camera-cut hold packet and guest ABI");
    return 0;
}
