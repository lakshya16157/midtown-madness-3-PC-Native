/* The real PAL packed-vertex writer and x87 integer conversion are extracted
 * by mm3_name_markers_regression.py. No game, renderer or save is needed. */
#include <assert.h>
#include <stdio.h>
#include "../src/mm3_name_markers.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;

static unsigned char memory[0x410000];
static const uint32_t vertex = 0x5000, stack = 0xD000, frame = 0xF000;
static unsigned marker_flags, marker_calls, pack_calls, recurse;
static uint32_t caller = 0x0024E239, mask;

void sub_001F0399(void);
#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
#include "production_name_markers.inc"
#undef eax
#undef ecx
#undef edx
#undef esp
#undef ebx
#undef esi
#undef edi

void sub_001093BB_original(void)
{
    ++marker_calls;
    assert(s_name_marker_scope && !s_name_marker_projected);
    mm3_name_marker_projected(marker_flags);
    if (recurse) {
        unsigned flags = marker_flags;
        recurse = 0;
        marker_flags = 8;
        sub_001093BB();
        assert(s_name_marker_scope && s_name_marker_projected);
        marker_flags = flags;
        return;
    }
    ++pack_calls;
    sub_00069807();
}

typedef struct {
    uint32_t regs[9];
    double fp[8];
    int top, cmp;
    uint16_t cc, control;
} State;

static State state(void)
{
    State result = {0};
    uint32_t regs[] = {g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi,
                       g_edi, g_ebp, g_seh_ebp};
    memcpy(result.regs, regs, sizeof regs);
    memcpy(result.fp, g_fp_stack, sizeof result.fp);
    result.top = g_fp_top; result.cmp = g_fp_cmp;
    result.cc = g_fp_cc; result.control = g_fp_control_word;
    return result;
}

static void initialize(float u1, float v1)
{
    memset(memory, 0, sizeof memory);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    g_eax = 0x1234; g_ecx = 0x2345; g_edx = 0x3456;
    g_esp = stack; g_ebx = 0x4567; g_esi = vertex; g_edi = 0x5678;
    g_ebp = g_seh_ebp = frame;
    g_fp_top = 0; g_fp_cmp = 0; g_fp_cc = 0; g_fp_control_word = 0x37F;
    memset(g_fp_stack, 0, sizeof g_fp_stack);
    MEMF(0x3B8588) = 32768.0f;
    MEMF(0x385E30) = 0.0005537098622880876f;
    MEMF(0x385E2C) = -32768.0f;
    MEMF(0x385E28) = 32767.0f;
    MEMF(0x3B8584) = 16383.5f;
    MEM32(stack) = caller;
    MEM32(g_ebx + 0x4C) = mask;
    MEMF(stack + 4) = 120.0f; MEMF(stack + 8) = 160.0f;
    MEM32(stack + 12) = 0xBF123456;
    MEMF(stack + 16) = 0.25f; MEMF(stack + 20) = 0.75f;
    MEMF(stack + 24) = u1; MEMF(stack + 28) = v1;
    s_name_marker_scope = s_name_marker_projected = 0;
    recurse = 0;
}

static void check(unsigned flags, float u1, float v1, int tagged)
{
    unsigned char packed[16];
    State expected, actual;
    initialize(u1, v1);
    sub_00069807_original();
    memcpy(packed, memory + vertex, sizeof packed);
    expected = state();
    assert(MEM32(vertex + 4) == 0xBF123456);
    assert(MEM16(vertex + 8) != 0 && MEM16(vertex + 10) != 0);
    initialize(u1, v1);
    marker_flags = flags;
    sub_001093BB();
    actual = state();
    assert(memcmp(&expected, &actual, sizeof expected) == 0);
    assert(memcmp(packed, memory + vertex, 12) == 0);
    if (tagged) {
        assert(MEM16(vertex + 12) == (uint16_t)(int16_t)23417);
        assert(MEM16(vertex + 14) == (uint16_t)(int16_t)-17293);
    } else assert(memcmp(packed, memory + vertex, 16) == 0);
    assert(!s_name_marker_scope && !s_name_marker_projected);
}

int main(void)
{
    unsigned flags;
    unsigned i, corner;
    static const uint32_t textured_sites[] = {
        0x1169A2, 0x116A7F, 0x116B4A, 0x116BDE, 0x116C78,
        0x116D0E, 0x116D9D, 0x116E2F, 0x116EBE
    };
    static const uint32_t zero_uv1_sites[] = {
        0x1171D4, 0x11726E, 0x11731C, 0x24DEDF, 0x24DF89,
        0x24E033, 0x24E0DD, 0x24E239, 0x24E2A3, 0x24E315, 0x24E376
    };
    for (flags = 0; flags < 16; ++flags) check(flags, 0, 0, (flags & 7) != 0);
    check(7, 0.25f, 0.0f, 0);
    check(7, 0.0f, 0.75f, 0);
    check(7, 0.25f, 0.75f, 0);
    for (i = 0; i < sizeof textured_sites / sizeof *textured_sites; ++i) {
        caller = textured_sites[i];
        mask = 0;
        check(7, 0, 0, 1);
        mask = 0x1000;
        for (corner = 0; corner < 4; ++corner)
            check(7, (float)(corner & 1), (float)(corner >> 1), 0);
    }
    for (i = 0; i < sizeof zero_uv1_sites / sizeof *zero_uv1_sites; ++i) {
        caller = zero_uv1_sites[i];
        /* These paths use EBX for unrelated data; never interpret it as
         * the material whose mask flag the textured emitter holds there. */
        mask = 0x1000;
        check(7, 0, 0, 1);
    }
    caller = 0x109727; /* An unrecognized producer stays untouched. */
    check(7, 0, 0, 0);
    caller = 0x24E239;
    initialize(0, 0);
    mm3_name_marker_projected(7); /* A stale entry state outside the owner. */
    sub_00069807();
    assert(MEM32(vertex + 12) == 0);
    initialize(0, 0);
    marker_flags = 7; recurse = 1;
    sub_001093BB();
    assert(!s_name_marker_scope && !s_name_marker_projected);
    assert(MEM32(vertex + 12) == 0); /* Nested offscreen meter stayed native. */
    assert(marker_calls == 78 && pack_calls == 77);
    puts("PASS: native packed glyphs, scope, offscreen meter, masks, UV0/color and guest ABI");
    return 0;
}
