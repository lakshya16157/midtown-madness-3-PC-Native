#include "render_policy.h"
#include "mods.h"

int mm3_render_cubic_filter(const NvD3DState *st, int source_is_target, uint32_t levels)
{
    const NvD3DTexture *t = &st->tex[0];
    unsigned min_filter = (t->filter >> 16) & 255u;
    /* MM3's authored horizon panorama, magnified through destination alpha. */
    return mm3_mod_on(MM3_MOD_SKY) && !source_is_target && !t->cube && t->color == 0x0Fu &&
        t->width == 256 && t->height == 256 && levels > 4 &&
        (min_filter == 2 || min_filter == 4 || min_filter == 6) &&
        ((t->filter >> 24) & 15u) != 1 && !st->depth_test && st->blend_enable &&
        st->blend_sfactor == 0x304 && st->blend_dfactor == 0x305;
}

/* The authored display passes need position and texture interpolation shifted
 * together; offscreen ripple targets keep pixel-zero edge snapping. */
void mm3_render_vertex_alignment(uint32_t width, uint32_t height, float *snap, float *offset)
{
    if (width == 640 && height == 480) {
        *snap = 0.0f;
        *offset = 0.53125f;
    }
}
