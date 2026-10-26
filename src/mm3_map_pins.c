#include <stdio.h>
#include <string.h>
#include "recomp_types.h"

/* The round minimap's compass pins. The game's ring texture MapCircle
 * (Menu/Gfx/HUD/MapCircle.cdds, DXT5 128x128) carries red pins printed at
 * west, east and south; north's pin is the separate N sprite, MapNorth.
 * "Hide the minimap compass pins" (MM3_HUD_NORTH_MARKER=0) collapses the N
 * sprite (mm3_map_zoom.c) and, here, gives the ring its pin-free north
 * section in place of the other three: each DXT5 block is copied from the
 * north band a quarter or half turn round, its endpoints kept and its 16
 * texel indices permuted, so the ring stays exact. The native quad draw
 * takes its texture coordinates from the corners, never from the vertices,
 * so the texture is the only place to do it.
 *
 * The map node creates the texture (sub_0011C742) before its pixels stream
 * in, so this runs just before each ring draw (sub_0011D808 at 0x11DBB1,
 * scripts/mm3_map_zoom_seam.cmake): the retail data (CRC-32 6E109683) is
 * changed once; data already changed, not yet loaded or unknown is left. */

extern int mm3_hud_north_marker(void);

#define RING_BLOCKS 32u                  /* 128 / 4 */
#define RING_BYTES (RING_BLOCKS * RING_BLOCKS * 16u)
#define RING_CRC 0x6E109683u             /* the retail MapCircle data */

int mm3_map_ring_pins_hidden(void)
{
    return !mm3_hud_north_marker();
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
    static uint32_t table[256];
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    if (!table[1]) {
        uint32_t n, value;
        int bit;
        for (n = 0; n < 256; ++n) {
            for (value = n, bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
            table[n] = value;
        }
    }
    for (i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

enum { WEST, EAST, SOUTH };

/* A DXT5 block turned: texel (i, j) of the result is texel (si, sj) of the
 * north block. Alpha indices are 3 bits a texel (bytes 2-7), colour indices
 * 2 bits (bytes 12-15), both row by row. */
static void turned_block(const uint8_t *north, uint8_t *out, int side)
{
    uint64_t alpha = 0, source_alpha = 0;
    unsigned i, j, k;
    for (k = 0; k < 6; ++k) source_alpha |= (uint64_t)north[2 + k] << (8 * k);
    memcpy(out, north, 16);
    memset(out + 12, 0, 4);
    for (j = 0; j < 4; ++j)
        for (i = 0; i < 4; ++i) {
            unsigned si = side == WEST ? 3 - j : side == EAST ? j : 3 - i;
            unsigned sj = side == WEST ? i : side == EAST ? 3 - i : 3 - j;
            unsigned from = sj * 4 + si, to = j * 4 + i;
            alpha |= ((source_alpha >> (3 * from)) & 7u) << (3 * to);
            out[12 + j] |= (uint8_t)(((north[12 + sj] >> (2 * si)) & 3u) << (2 * i));
        }
    for (k = 0; k < 6; ++k) out[2 + k] = (uint8_t)(alpha >> (8 * k));
}

/* West (x, y) shows north (127 - y, x); east (y, 127 - x); south
 * (127 - x, 127 - y). In blocks: a band four blocks deep round each pin. */
static void hide_pins(const uint8_t *ring, uint8_t *out)
{
    unsigned a, b;
#define BLOCK(buffer, bx, by) ((buffer) + ((by) * RING_BLOCKS + (bx)) * 16u)
    memcpy(out, ring, RING_BYTES);
    for (a = 12; a < 20; ++a)
        for (b = 0; b < 4; ++b) {
            turned_block(BLOCK(ring, 31 - a, b), BLOCK(out, b, a), WEST);
            turned_block(BLOCK(ring, a, b), BLOCK(out, 31 - b, a), EAST);
            turned_block(BLOCK(ring, 31 - a, b), BLOCK(out, a, 31 - b), SOUTH);
        }
#undef BLOCK
}

static int guest_range(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address < 0x08000000u && size <= 0x08000000u - address;
}

/* The ring draw's texture argument is the map node's resource (+0x20):
 * resource+24 -> descriptor, +14 -> D3DTexture, +4 -> its Data, as
 * sub_0011C742 resolves it (mm3_widescreen.c). Data is a physical address
 * in contiguous memory, seen at 0x80000000 + Data (mm3_native_fog.c); the
 * guest page at Data itself is another page. */
void mm3_map_ring_texture_pins(uint32_t resource)
{
    static uint8_t ring[RING_BYTES], out[RING_BYTES];
    static unsigned reported;
    uint32_t descriptor, texture, data, crc;
    if (!mm3_map_ring_pins_hidden() || !guest_range(resource, 0x28u)) return;
    descriptor = MEM32(resource + 0x24u);
    if (!guest_range(descriptor, 0x18u)) return;
    texture = MEM32(descriptor + 0x14u);
    if (!guest_range(texture, 8u)) return;
    data = MEM32(texture + 4u) & 0x7FFFFFFFu;
    if (data >= 0x04000000u || RING_BYTES > 0x04000000u - data) return;
    data += 0x80000000u;
    memcpy(ring, (const void *)XBOX_PTR(data), RING_BYTES);
    crc = crc32(ring, RING_BYTES);
    if (crc != RING_CRC) return;          /* hidden already, not loaded yet, or not the retail ring */
    hide_pins(ring, out);
    memcpy((void *)XBOX_PTR(data), out, RING_BYTES);
    if (!reported) {
        reported = 1;
        fprintf(stderr, "[MM3_HUD] minimap compass pins hidden: N sprite collapsed, E/S/W cleared from the ring at %08X\n",
                data);
    }
}
