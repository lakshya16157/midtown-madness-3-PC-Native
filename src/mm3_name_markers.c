#include "recomp_types.h"

/* MmxOpponentMarkerNode draws projected names, icons and damage bars using
 * the same font/material batches as ordinary HUD text. Carry their identity
 * on the vertices, before the native UI queues those shared batches. */
enum { NAME_MARKER_U = 23417, NAME_MARKER_V = -17293 };
static RECOMP_TLS unsigned s_name_marker_scope;
static RECOMP_TLS unsigned s_name_marker_projected;

extern void sub_001093BB_original(void);
extern void sub_00069807_original(void);

void mm3_name_marker_projected(uint32_t flags)
{
    /* 1FF95A publishes flags 8 for the special, fixed-position damage meter
     * when projection fails. Its visible world entries have bits 0..2.
     * Do not move that offscreen meter with the world-label transform. */
    s_name_marker_projected = (flags & 7u) != 0;
}

void sub_001093BB(void)
{
    unsigned scope = s_name_marker_scope;
    unsigned projected = s_name_marker_projected;
    s_name_marker_scope = 1;
    s_name_marker_projected = 0;
    sub_001093BB_original();
    s_name_marker_scope = scope;
    s_name_marker_projected = projected;
}

static unsigned name_marker_has_unused_uv1(void)
{
    /* 116535 (also emitted as the 116D5A continuation) holds its material
     * in EBX at these textured vertex calls. +4C selects the second UV
     * channel for the whole primitive. Checking only a vertex's zero UV1
     * would incorrectly mark the (0,0) corner of a masked quad. */
    switch (MEM32(g_esp)) {
    case 0x001169A2u: case 0x00116A7Fu:
    case 0x00116B4Au: case 0x00116BDEu: case 0x00116C78u:
    case 0x00116D0Eu: case 0x00116D9Du: case 0x00116E2Fu:
    case 0x00116EBEu:
        return MEM32(g_ebx + 0x4Cu) == 0;
    /* Untextured polygons, followed by the 24DD9E quad and 24E132 glyph
     * emitters: each explicitly supplies zero for both UV1 components. */
    case 0x001171D4u: case 0x0011726Eu: case 0x0011731Cu:
    case 0x0024DEDFu: case 0x0024DF89u: case 0x0024E033u:
    case 0x0024E0DDu: case 0x0024E239u: case 0x0024E2A3u:
    case 0x0024E315u: case 0x0024E376u:
        return 1;
    default:
        return 0;
    }
}

void sub_00069807(void)
{
    uint32_t vertex = g_esi;
    unsigned tag = s_name_marker_scope && s_name_marker_projected &&
                   name_marker_has_unused_uv1();
    sub_00069807_original();
    /* 69807 packs the 16-byte UI vertex: XY, color, UV0, UV1. Names use
     * UV0 for the font atlas and leave UV1 zero. Preserve actual mask UVs.
     * The renderer strips this marker only in its upload copy. */
    if (tag && MEM32(vertex + 12u) == 0) {
        MEM16(vertex + 12u) = (uint16_t)(int16_t)NAME_MARKER_U;
        MEM16(vertex + 14u) = (uint16_t)(int16_t)NAME_MARKER_V;
    }
}
