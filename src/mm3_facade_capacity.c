#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "recomp_types.h"
#include "mm3_render_distance.h"

extern void sub_001633B7_original(void);

/* PAL facade streams use the capacities below for both allocation checks and
 * actual VB creation. The allocation constructor reads them before requesting
 * two 28-byte opaque/alpha VBs, two 24-byte far VBs and two 32-byte detail
 * VBs (+0x48, taken per frame by sub_00162EBA). Change them only there;
 * increasing an already allocated pool's counter would write beyond its VB.
 *
 * More visible facade cells need more space as the view area grows. Use the
 * same capped city/facade scale as the near/far zones, independent of object
 * LOD distance, so a 4x city view cannot exceed the allocated detail budget.
 * Bound the multiplier at four: the largest stream has 49,152 vertices, below the
 * guest draw commands' 16-bit starting vertex. Every stream is drawn as a
 * quad list from its own VB (start and count only), so the detail stream
 * needs no index storage. Kept at its Xbox 1,536 vertices while its zone
 * covered four times the area, it ran out each frame and the buildings
 * whose detail came last lost it, changing with the camera: distant
 * facades flickered. The eight VBs together grow by 2,777,088 bytes at the
 * default 2x distance. Detail descriptors and shaders keep their budgets. */
void sub_001633B7(void)
{
    uint32_t self = g_esi;
    float scale = mm3_facade_distance_scale();
    uint32_t factor = (uint32_t)ceilf(scale * scale);
    uint32_t opaque, alpha, far_vertices, detail;

    if (factor > 4u) factor = 4u;
    if (factor <= 1u) {
        sub_001633B7_original();
        return;
    }
    opaque = MEM32(self + 0x1ED0u);
    alpha = MEM32(self + 0x1F2Cu);
    far_vertices = MEM32(self + 0x1F88u);
    detail = MEM32(self + 0x48u);
    /* Respect custom capacities and repeated creation using expanded pools. */
    if (opaque != 0x3000u || alpha != 0x800u || far_vertices != 0x200u || detail != 0x600u) {
        sub_001633B7_original();
        return;
    }
    MEM32(self + 0x1ED0u) = opaque * factor;
    MEM32(self + 0x1F2Cu) = alpha * factor;
    MEM32(self + 0x1F88u) = far_vertices * factor;
    MEM32(self + 0x48u) = detail * factor;
    sub_001633B7_original();
    if (!LO8(g_eax)) {
        /* Failed creation follows the title's existing failure path. Keeping
         * the old limits is conservative even if a partial larger VB exists. */
        MEM32(self + 0x1ED0u) = opaque;
        MEM32(self + 0x1F2Cu) = alpha;
        MEM32(self + 0x1F88u) = far_vertices;
        MEM32(self + 0x48u) = detail;
        fprintf(stderr, "[MM3_DISTANCE] facade VB creation failed; retained stock capacities\n");
        return;
    }
    fprintf(stderr,
            "[MM3_DISTANCE] facade VB capacity multiplier %u opaque28=%u alpha28=%u far24=%u detail32=%u extra_bytes=%u\n",
            factor, opaque * factor, alpha * factor, far_vertices * factor, detail * factor,
            2u * (factor - 1u) * ((opaque + alpha) * 28u + far_vertices * 24u + detail * 32u));
}
