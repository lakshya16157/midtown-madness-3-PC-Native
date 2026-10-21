/* Mission zone observer (src/mm3_zone_diagnostics.c) without the game: a
 * player's zone list at +0x48 (nodes: next, prev, zone), zones whose part at
 * +0x18 holds the enabled byte +0x71, centre +0x48 and radius +0x5C, the
 * +0x14A gate and seven reference points are laid out by hand. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char log_text[8192];
static size_t log_used;
static uint64_t test_clock;
#define MM3_ZONE_LOG(...) (log_used += (size_t)snprintf(log_text + log_used, sizeof log_text - log_used, __VA_ARGS__))
#define MM3_ZONE_CLOCK() test_clock
#include "mm3_zone_diagnostics.c"

ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo = 0x12000, g_xbox_code_hi = 0x400000;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp, g_fs_base, g_eflags;
RECOMP_TLS int g_fp_top, g_df, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word, g_fp_cc;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7;
static unsigned char memory[0x200000];
static const uint32_t player = 0x100000, head = 0x101000, points = 0x104000;
static const uint32_t node_a = 0x101100, node_b = 0x101200, zone_a = 0x102000, zone_b = 0x103000;

static void expect(const char *text)
{
    if (!strstr(log_text, text)) {
        fprintf(stderr, "missing \"%s\" in:\n%s\n", text, log_text);
        assert(0);
    }
    log_used = 0;
    log_text[0] = 0;
}

static void expect_none(void)
{
    if (log_used) {
        fprintf(stderr, "unexpected output:\n%s\n", log_text);
        assert(0);
    }
}

static void link(uint32_t const *nodes, unsigned count)
{
    uint32_t previous = head;
    unsigned i;
    for (i = 0; i != count; ++i) {
        MEM32(previous) = nodes[i];
        MEM32(nodes[i] + 4) = previous;
        previous = nodes[i];
    }
    MEM32(previous) = head;
    MEM32(head + 4) = previous;
}

static void zone(uint32_t object, float x, float radius, int enabled)
{
    MEM8(object + 0x18 + 0x71) = (uint8_t)enabled;
    MEMF(object + 0x18 + 0x48) = x;
    MEMF(object + 0x18 + 0x4C) = 0.0f;
    MEMF(object + 0x18 + 0x50) = 0.0f;
    MEMF(object + 0x18 + 0x5C) = radius;
}

/* The car's seven points 1 m apart along x, the first at x. */
static void car_at(float x)
{
    unsigned k;
    for (k = 0; k != 7; ++k) {
        MEMF(points + k * 12u) = x + (float)k;
        MEMF(points + k * 12u + 4u) = 0.0f;
        MEMF(points + k * 12u + 8u) = 0.0f;
    }
}

int main(void)
{
    uint32_t both[2] = { node_a, node_b }, only_b[1] = { node_b };
    unsigned i;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    MEM32(player + 0x48) = head;
    MEM32(node_a + 8) = zone_a;
    MEM32(node_b + 8) = zone_b;
    MEM8(player + 0x14A) = 1;
    car_at(-500.0f);

    /* No zones: silent, and no slot is taken. */
    link(NULL, 0);
    mm3_zone_status(player, points);
    expect_none();
    mm3_zone_status(0x8, points);
    mm3_zone_status(player, 0x8);
    expect_none();

    /* A zone mission starts. */
    zone(zone_a, 100.0f, 10.0f, 1);
    zone(zone_b, 300.0f, 11.0f, 1);
    link(both, 2);
    mm3_zone_status(player, points);
    expect("[MM3_ZONE] player 00100000: 2 zones enabled (was 0), zone check on\n");
    mm3_zone_status(player, points);
    expect_none();

    /* The native rule: any of the seven points within the radius (3D). They
     * run from x = 96 to 102, so the nearest is at the centre. */
    car_at(96.0f);
    mm3_zone_status(player, points);
    expect("car inside a zone at (100.0, 0.0, 0.0), 0.0 m from its centre (radius 10.0 m), zone check on\n");
    mm3_zone_status(player, points);
    expect_none();

    /* The pickup counts: the script disables the zone. errno, the Windows
     * last error and the floating-point environment survive a logged call. */
    zone(zone_a, 100.0f, 10.0f, 0);
    errno = ERANGE;
    SetLastError(9876);
    mm3_zone_status(player, points);
    assert(errno == ERANGE && GetLastError() == 9876);
    expect("1 zone enabled (was 2), zone check on\n");

    /* The vehicle is not placed: the native check would be skipped. */
    MEM8(player + 0x14A) = 0;
    mm3_zone_status(player, points);
    expect("zone check off with 1 zone enabled\n");
    car_at(295.0f);
    mm3_zone_status(player, points);
    expect("car inside a zone at (300.0, 0.0, 0.0), 0.0 m from its centre (radius 11.0 m), zone check off\n");

    /* Back on, but the zone stays enabled for 2 s: reported once. */
    MEM8(player + 0x14A) = 1;
    mm3_zone_status(player, points);
    expect("zone check on with 1 zone enabled\n");
    test_clock += 1999;
    mm3_zone_status(player, points);
    expect_none();
    test_clock += 1;
    mm3_zone_status(player, points);
    expect("still inside a zone after 2 s with the zone check on; the zone was not disabled\n");
    test_clock += 5000;
    mm3_zone_status(player, points);
    expect_none();

    /* The mission ends. */
    link(only_b, 1);
    zone(zone_b, 300.0f, 11.0f, 0);
    mm3_zone_status(player, points);
    expect("0 zones enabled (was 1), zone check on\n");
    link(NULL, 0);
    mm3_zone_status(player, points);
    expect_none();

    /* A corrupt list ends the walk. */
    zone(zone_a, 100.0f, 10.0f, 1);
    link(both, 2);
    MEM32(node_a) = 0x8;
    mm3_zone_status(player, points);
    expect("1 zone enabled (was 0), zone check on\n");

    /* Bounded output. */
    for (i = 0; i != 400; ++i) {
        MEM8(player + 0x14A) = (uint8_t)(i & 1);
        mm3_zone_status(player, points);
        log_used = 0;
    }
    assert(s_zone_lines == ZONE_LINE_LIMIT);
    puts("PASS: zone observer silent without zones, native radius rule over seven points, count/gate/entry/stuck "
         "lines, errno/LastError kept, corrupt list bounded, 200-line cap");
    return 0;
}
