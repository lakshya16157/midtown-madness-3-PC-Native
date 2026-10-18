#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/mm3_native_fog.c"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_esp;
static unsigned fog_calls, horizon_calls;
static int available = 1;
static unsigned char *arena;
static const uint32_t self = 0x10000, horizon = 0x12000, stack = 0x20000;
static const uint32_t camera = 0x30000, device = 0x40000, packet = 0x50000;
int nv2a_d3d_distance_fog_ready(void) { return available; }
void sub_000F8DCE_original(void) { ++fog_calls; assert(g_ecx == self && g_esp == stack); g_eax = 0x55660000; g_esp += 16; }
void sub_000F8750_original(void) { ++horizon_calls; assert(g_ecx == horizon && g_esp == stack); g_eax = 0x77880000; g_esp += 8; }
static void commit(uint32_t address, size_t bytes)
{ assert(VirtualAlloc(arena + address, bytes, MEM_COMMIT, PAGE_READWRITE) == arena + address); }
static void setup(void)
{
    memset(arena + 0x10000, 0xA5, 0x60000);
    MEM32(self + 12) = camera; MEM32(horizon + 0x28) = camera;
    MEM32(0x351F48) = device;
    MEM32(device) = packet; MEM32(device + 4) = packet + 48;
    MEMF(camera + 0x1ED8) = .25f; MEMF(camera + 0x1FB0) = 1000;
    MEMF(camera + 0x1EE4) = 2; MEMF(camera + 0x1EF8) = 4;
    MEMF(camera + 0x1F34) = 2; MEMF(camera + 0x1F38) = 4; MEMF(camera + 0x1F3C) = .5f;
    MEM32(stack) = 0x12345678; MEM32(stack + 4) = 0xFFAABBCC;
    g_ecx = self; g_esp = stack; g_eax = 0x12345678;
    fog_calls = horizon_calls = 0; available = 1; s_native_fog_camera = 0;
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
int main(int argc, char **argv)
{
    unsigned i;
    assert(argc == 2);
    assert(!_putenv_s("MM3_NATIVE_FOG", argv[1]));
    arena = VirtualAlloc(NULL, 0x84000000ull, MEM_RESERVE, PAGE_NOACCESS);
    assert(arena); g_xbox_mem_offset = (ptrdiff_t)arena;
    commit(0x10000, 0x60000); commit(0x351000, 0x1000); commit(0x80010000, 0x1000);
    setup();
    if (!strcmp(argv[1], "0")) { check_fallback(); goto done; }
    sub_000F8DCE();
    assert(!fog_calls && g_esp == stack + 16 && g_ecx == self && g_eax == 0x12345601);
    assert(MEM32(device) == packet + 48 && MEM32(packet - 4) == 0xA5A5A5A5 && MEM32(packet + 48) == 0xA5A5A5A5);
    assert(MEM32(packet) == 0x402C0100 && MEM32(packet + 4) == 0x58424647);
    assert(MEMF(packet + 8) == 4 && MEMF(packet + 12) == 1000);
    assert(MEMF(packet + 16) == 300 && MEMF(packet + 20) == 900);
    assert(MEM32(packet + 24) == 0xFFAABBCC && MEM32(packet + 28) == 7);
    assert(MEMF(packet + 32) == .25f && MEMF(packet + 36) == .25f);
    assert(MEMF(packet + 40) == .5f && MEMF(packet + 44) == 6.25f);
    assert(MEM32(stack) == 0x12345678 && MEM32(stack + 4) == 0xFFAABBCC);
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
done:
    assert(VirtualFree(arena, 0, MEM_RELEASE));
    puts("PASS native fog ordered packet, camera projection, exact ring capacity, fallback, horizon ownership, high RAM alias and guest ABI");
    return 0;
}
