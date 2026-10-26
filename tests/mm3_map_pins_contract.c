/* src/mm3_map_pins.c behind the native resource chain: resource+24 ->
 * descriptor, +14 -> D3DTexture, +4 -> Data, a physical address the guest
 * sees at 0x80000000 + Data; the low page at Data is another page and stays
 * empty here. Reads ring.bin into the data unless the mode is "empty" (pixels
 * not streamed in yet), runs the pre-draw hook ("show": pins shown; "twice":
 * two draws) and writes the data back to out.bin. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
static int shown;

int mm3_hud_north_marker(void) { return shown; }
void mm3_map_ring_texture_pins(uint32_t resource);

int main(int argc, char **argv)
{
    const uint32_t resource = 0x10000, descriptor = 0x10100, texture = 0x10200, data = 0x20000;
    const char *mode = argc > 1 ? argv[1] : "hide";
    unsigned char *memory = VirtualAlloc(NULL, 0x80040000u, MEM_RESERVE, PAGE_NOACCESS);
    unsigned char *pixels;
    FILE *f;
    if (!memory || !VirtualAlloc(memory, 0x40000u, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(memory + 0x80000000u, 0x40000u, MEM_COMMIT, PAGE_READWRITE)) return 4;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    pixels = memory + 0x80000000u + data;
    shown = !strcmp(mode, "show");
    MEM32(resource + 0x24) = descriptor;
    MEM32(descriptor + 0x14) = texture;
    MEM32(texture + 4) = data;
    if (strcmp(mode, "empty")) {
        f = fopen("ring.bin", "rb");
        if (!f || fread(pixels, 1, 16384, f) != 16384) return 2;
        fclose(f);
    }
    mm3_map_ring_texture_pins(resource);
    if (!strcmp(mode, "twice")) mm3_map_ring_texture_pins(resource);
    mm3_map_ring_texture_pins(0);            /* no resource */
    mm3_map_ring_texture_pins(2);            /* a test or broken pointer: nothing */
    MEM32(texture + 4) = 0x03FFF000u;        /* data running past contiguous memory: nothing */
    mm3_map_ring_texture_pins(resource);
    for (unsigned i = 0; i < 16384; ++i)
        if (memory[data + i]) return 5;      /* the low page at Data is never the texture */
    f = fopen("out.bin", "wb");
    if (!f) return 3;
    fwrite(pixels, 1, 16384, f);
    fclose(f);
    return 0;
}
