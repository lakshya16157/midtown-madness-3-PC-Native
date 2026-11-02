#include <string.h>
#include "render_commands.h"

/* Only the SDL3 GPU renderer draws the fog packet from this hook (render_fog.c).
 * The D3D11 renderer decodes the same packet itself (nv2a_d3d_distance_fog:
 * sky fog ramp, tree fog, depth remap), so a hook here would take it first. */
#ifdef XBOXRECOMP_SDL3
static int render_nop(const NvD3DState *state, uint32_t subchannel, uint32_t value)
{
    static uint32_t words[MM3_FOG_WORDS - 2];
    static unsigned next;
    if (!state || subchannel != 0) { next = 0; return 0; }
    if (!next) { if (value != MM3_FOG_TAG) return 0; next = 1; return 1; }
    words[next++ - 1] = value;
    if (next == MM3_FOG_WORDS - 1) {
        float values[8];
        memcpy(values, words, 4 * sizeof(float));
        memcpy(values + 4, words + 6, 4 * sizeof(float));
        mm3_render_fog(state, values[0], values[1], values[2], values[3],
                             words[4], words[5], values + 4);
        next = 0;
    }
    return 1;
}

#endif

void mm3_render_commands_init(void)
{
#ifdef XBOXRECOMP_SDL3
    nv2a_pb_set_nop_hook(render_nop);
#endif
}
