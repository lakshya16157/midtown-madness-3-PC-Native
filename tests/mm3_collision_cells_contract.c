#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/mm3_collision_cells.c"
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_esi, g_esp;
static unsigned char memory[0x100000];
static unsigned char valid_snapshot[sizeof memory];
static unsigned calls;
static uint32_t result_header;
static void setup(void);
static uint16_t original_list[65535], original_grid[65536][2];
static uint32_t random_state = 0xB71D6509u;
static uint32_t next_random(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}
static void snapshot_ranges(unsigned length, unsigned cells)
{
    memcpy(original_list, memory + 0x20000, length * 2u);
    memcpy(original_grid, memory + 0x50000, cells * 4u);
}
/* The oracle reads ORIGINAL slots, not sanitizer output. Every clamped range
 * must retain exactly its original valid face set, including overlapping and
 * invalid-only subranges. Duplicates have no effect on collision geometry. */
static void check_range_sets(unsigned faces, unsigned length, unsigned cells)
{
    unsigned cell, i;
    assert(faces <= 32);
    for (i = 0; i < length; ++i)
        if (original_list[i] < faces)
            assert(MEM16(0x20000 + i * 2u) == original_list[i]);
    for (cell = 0; cell < cells; ++cell) {
        unsigned char expected[32] = {0}, actual[32] = {0};
        unsigned lo = original_grid[cell][0], hi = original_grid[cell][1];
        unsigned new_lo = MEM16(0x50000 + cell * 4u);
        unsigned new_hi = MEM16(0x50002 + cell * 4u);
        if (hi > length) hi = length;
        if (lo > hi) lo = hi;
        for (i = lo; i < hi; ++i)
            if (original_list[i] < faces) expected[original_list[i]] = 1;
        assert(new_lo <= new_hi && new_hi <= length);
        for (i = new_lo; i < new_hi; ++i) {
            unsigned face = MEM16(0x20000 + i * 2u);
            assert(face < faces);
            actual[face] = 1;
        }
        if (memcmp(expected, actual, faces)) {
            fprintf(stderr, "Collision face-set changed for original [%u,%u), repaired [%u,%u)\n",
                    lo, hi, new_lo, new_hi);
            assert(!"sanitizer introduced or lost collision geometry");
        }
    }
}
static void test_face_sets(void)
{
    unsigned i, trial;
    setup();
    MEM32(0x11008) = 0x20000; MEM32(0x11010) = 0x50000;
    MEM32(0x1100C) = 7; MEM32(0x11044) = 7;
    {
        const uint16_t list[] = {0xFFFF, 2, 0xFFFF, 1, 0, 0xFFFF, 0xFFFF};
        const uint16_t ranges[][2] = {{0,1},{2,3},{5,7},{0,7},{2,7},{1,4},{6,6}};
        memcpy(memory + 0x20000, list, sizeof list);
        memcpy(memory + 0x50000, ranges, sizeof ranges);
    }
    snapshot_ranges(7, 7);
    collision_sanitize(0x11000);
    check_range_sets(3, 7, 7);
    for (trial = 0; trial < 256; ++trial) {
        unsigned length = next_random() % 97u, cells = 1 + next_random() % 64u;
        unsigned faces = 1 + next_random() % 17u;
        setup();
        MEM32(0x11004) = faces; MEM32(0x11008) = 0x20000;
        MEM32(0x11010) = 0x50000; MEM32(0x1100C) = length;
        MEM32(0x11044) = cells;
        for (i = 0; i < length; ++i)
            MEM16(0x20000 + i * 2u) = (uint16_t)(
                next_random() % 3u ? next_random() % faces : 0xFFFFu);
        for (i = 0; i < cells; ++i) {
            MEM16(0x50000 + i * 4u) = (uint16_t)(i % 8 ? next_random() % (length + 17) : 0);
            MEM16(0x50002 + i * 4u) = (uint16_t)(i % 8 ? next_random() % (length + 17) : length);
        }
        snapshot_ranges(length, cells);
        collision_sanitize(0x11000);
        check_range_sets(faces, length, cells);
    }
}
void sub_0015A929_original(void)
{
    ++calls;
    MEM32(g_esi) = result_header;
    g_eax = 0x12340001u;
    g_esp += 4u;
}
static void setup(void)
{
    memset(memory, 0xA5, sizeof memory);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    g_esi = 0x10000; g_esp = 0xF0000; result_header = 0x11000;
    MEM32(g_esi) = 0;
    MEM32(0x11004) = 3; MEM32(0x11008) = 0x12000;
    MEM32(0x1100C) = 6; MEM32(0x11010) = 0x13000;
    MEM32(0x11044) = 2; MEM32(0x11048) = 1; MEM32(0x1104C) = 1;
    MEM16(0x12000) = 0xFFA4; MEM16(0x12002) = 2;
    MEM16(0x12004) = 0xFFFF; MEM16(0x12006) = 1;
    MEM16(0x12008) = 0; MEM16(0x1200A) = 0xFFFF;
    MEM16(0x13000) = 0; MEM16(0x13002) = 99;
    MEM16(0x13004) = 99; MEM16(0x13006) = 1;
}
int main(void)
{
    unsigned i;
    setup(); sub_0015A929();
    assert(calls == 1 && g_eax == 0x12340001u && g_esp == 0xF0004);
    for (i = 0; i < 6; ++i) assert(MEM16(0x12000 + 2*i) < 3);
    assert(MEM16(0x12002) == 2 && MEM16(0x12006) == 1);
    assert(MEM16(0x13000) == 1 && MEM16(0x13002) == 5 && MEM16(0x13004) == 1);
    assert(MEM16(0x11FFE) == 0xA5A5 && MEM16(0x1200C) == 0xA5A5);
    /* Cache hits must not scan/repair again. */
    MEM16(0x12000) = 0xFFFF; sub_0015A929(); assert(MEM16(0x12000) == 0xFFFF);
    /* Reject dimensions whose product would overflow or make unbounded work. */
    setup(); MEM32(0x11044) = 0xFFFFFFFF; collision_sanitize(0x11000);
    assert(MEM16(0x12000) == 0xFFA4);
    setup(); MEM32(0x11048) = 0; collision_sanitize(0x11000);
    assert(MEM16(0x12000) == 0xFFA4);
    /* Reject spans crossing the guest RAM boundary without dereferencing. */
    setup(); MEM32(0x11008) = 0x07FFFFFE; collision_sanitize(0x11000);
    assert(MEM16(0x12000) == 0xFFA4);
    collision_sanitize(0x07FFFFF0); collision_sanitize(0xFFFFFFFF);
    setup(); MEM32(0x11010) = 0x12000; collision_sanitize(0x11000);
    assert(MEM16(0x12000) == 0xFFA4);
    setup(); MEM32(0x11008) = 0x11004; collision_sanitize(0x11000);
    assert(MEM32(0x11004) == 3);
    test_face_sets();
    /* A valid cell is untouched, including scratch. Holding the scratch lock
     * here also proves this fast path never tries to acquire it. */
    setup();
    for (i = 0; i < 6; ++i) MEM16(0x12000 + 2u*i) = (uint16_t)(i % 3u);
    MEM16(0x13000)=0; MEM16(0x13002)=6; MEM16(0x13004)=2; MEM16(0x13006)=4;
    for (i = 0; i < 65535; ++i) collision_bounds[i] = 0xDEADBEEFu;
    memcpy(valid_snapshot, memory, sizeof memory);
    COLLISION_LOCK(); collision_sanitize(0x11000); COLLISION_UNLOCK();
    assert(!memcmp(valid_snapshot, memory, sizeof memory));
    for (i = 0; i < 65535; ++i) assert(collision_bounds[i] == 0xDEADBEEFu);
    /* Maximum overlapping ranges stay linear; all-invalid input is bounded. */
    setup(); MEM32(0x11044) = 65536; MEM32(0x1100C) = 65535;
    MEM32(0x11008) = 0x20000; MEM32(0x11010) = 0x50000;
    memset(memory + 0x20000, 0xFF, 131070);
    for (i = 0; i < 65536; ++i) { MEM16(0x50000+4*i)=0; MEM16(0x50002+4*i)=65535; }
    collision_sanitize(0x11000);
    assert(MEM16(0x20000) == 0 && MEM16(0x3FFFC) == 0);
    for (i = 0; i < 65536; ++i)
        assert(MEM16(0x50000+4*i)==65535 && MEM16(0x50002+4*i)==65535);
    /* Maximum overlapping ranges with sparse original valid endpoints. */
    MEM16(0x20000 + 3u*2u) = 2; MEM16(0x20000 + 65530u*2u) = 1;
    memset(memory + 0x20000, 0xFF, 6);
    memset(memory + 0x20008, 0xFF, (65530u - 4u) * 2u);
    memset(memory + 0x3FFF6, 0xFF, 8);
    for (i = 0; i < 65536; ++i) { MEM16(0x50000+4*i)=0; MEM16(0x50002+4*i)=65535; }
    collision_sanitize(0x11000);
    for (i = 0; i < 65536; ++i)
        assert(MEM16(0x50000+4*i)==3 && MEM16(0x50002+4*i)==65531);
    assert(MEM16(0x20006)==2 && MEM16(0x3FFF4)==1);
    for (i = 0; i < 65535; ++i)
        assert(MEM16(0x20000 + 2u*i) == (i < 65530 ? 2 : 1));
    /* Zero-length lists can have no list pointer and become empty ranges. */
    setup(); MEM32(0x11008)=0; MEM32(0x1100C)=0; collision_sanitize(0x11000);
    assert(MEM16(0x13000)==0 && MEM16(0x13002)==0 &&
           MEM16(0x13004)==0 && MEM16(0x13006)==0);
    /* Zero faces is rejected, preserving the existing metadata policy. */
    setup(); MEM32(0x11004)=0; collision_sanitize(0x11000);
    assert(MEM16(0x12000)==0xFFA4 && MEM16(0x13002)==99);
    /* Maximum face count preserves the last valid face without sentinel alias. */
    setup(); MEM32(0x11004)=65535; MEM32(0x1100C)=3; MEM32(0x11044)=1;
    MEM16(0x12000)=65535; MEM16(0x12002)=65534; MEM16(0x12004)=65535;
    MEM16(0x13000)=0; MEM16(0x13002)=3; collision_sanitize(0x11000);
    assert(MEM16(0x13000)==1 && MEM16(0x13002)==2 && MEM16(0x12002)==65534);
    puts("collision exact face sets, overlap bounds, untouched valid cells, cache hits and guest ABI: PASS");
    return 0;
}
