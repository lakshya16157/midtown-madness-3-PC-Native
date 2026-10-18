#include <math.h>
#include <stdint.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_car_lighting.h"

/* Native CMP render methods: ordinary groups pass their matrix unchanged;
 * transform groups compose the current relative matrix with their parent.
 * All 3,405 lamp nodes in the 264 installed models have only these ancestors.
 * Do not invoke virtual render/update methods: they can submit meshes,
 * particles and flares, and would undo the body's visibility rejection. */
enum { CAR_LIGHT_WALK_NODES = 1024, CAR_LIGHT_WALK_DEPTH = 32 };

static int car_light_address(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address <= 0x08000000u - size;
}

static int car_light_compose(const float local[12], const float parent[12], float out[12])
{
    unsigned row, column, k;
    for (k = 0; k < 12; ++k)
        if (!isfinite(local[k]) || !isfinite(parent[k])) return 0;
    /* Match native 36FD9's row-vector local * parent convention. */
    for (row = 0; row < 4; ++row)
        for (column = 0; column < 3; ++column) {
            float value = row == 3 ? parent[9 + column] : 0.0f;
            for (k = 0; k < 3; ++k) value += local[row * 3 + k] * parent[k * 3 + column];
            if (!isfinite(value)) return 0;
            out[row * 3 + column] = value;
        }
    return 1;
}

static void car_light_walk(uint32_t node, uint32_t graph, uint32_t selector,
                           uint32_t mask, const float parent[12],
                           unsigned depth, unsigned *remaining)
{
    uint32_t vtable, render, begin, end, at;
    float matrix[12], local[12];
    const float *world = parent;
    if (!*remaining) return;
    --*remaining;
    if (depth > CAR_LIGHT_WALK_DEPTH || !car_light_address(node, 0x54)) return;
    vtable = MEM32(node);
    if (!car_light_address(vtable, 0x44)) return;
    render = MEM32(vtable + 0x40);
    if (render == 0x000CF2DAu) {
        unsigned kind;
        if (!car_light_address(node, 0x84) || MEM32(node + 4) != graph) return;
        kind = MEM32(node + 0x34);
        /* Rotating/emergency nodes update their own phase in native drawing;
         * keep those on that path rather than replaying an old phase here. */
        if (kind != 4 && kind != 9) mm3_car_lighting_emit(node, mask, parent);
        return;
    }
    if (render == 0x000DE780u) {
        uint32_t flags;
        if (!car_light_address(node, 0x9C)) return;
        flags = MEM32(node + (selector ? 0x38 : 0x34));
        if (flags & 1u) return; /* Explicitly hidden/broken subtree. */
        memcpy(local, (const void *)XBOX_PTR(node + 0x6C), sizeof local);
        if (flags & 4u) {
            /* The native absolute branch also clears the child effect mask. */
            return;
        }
        if (!car_light_compose(local, parent, matrix)) return;
        world = matrix;
    } else if (render != 0x000DF751u) return;
    begin = MEM32(node + 0xC);
    end = MEM32(node + 0x10);
    if (end < begin || ((end - begin) & 3u) || end - begin > CAR_LIGHT_WALK_NODES * 4u ||
        !car_light_address(begin, end - begin)) return;
    for (at = begin; at < end && *remaining; at += 4)
        car_light_walk(MEM32(at), graph, selector, mask, world, depth + 1, remaining);
}

void mm3_car_lighting_model(uint32_t graph, uint32_t selector)
{
    uint32_t world, actor, begin, end, at, mask;
    unsigned remaining = CAR_LIGHT_WALK_NODES, i;
    float matrix[12];
    if (!car_light_address(graph, 0x128)) return;
    world = MEM32(0x003C55A0u);
    if (!car_light_address(world, 0x2A9) || MEM8(world + 0x2A8) || !MEM32(world + 0x24C)) return;
    actor = MEM32(graph + 4);
    /* This explicit owner gate lies after the native body cull. Preserve it
     * even though this source-only pass runs before that cull. */
    if (!car_light_address(actor, 0xED) || MEM8(actor + 0xEC)) return;
    mask = MEM32(graph + 0x124);
    if (!(mask & 0x16Fu)) return;
    memcpy(matrix, (const void *)XBOX_PTR(graph + 0x3C), sizeof matrix);
    for (i = 0; i < 12; ++i) if (!isfinite(matrix[i])) return;
    begin = MEM32(graph + 0xC);
    end = MEM32(graph + 0x10);
    if (end < begin || ((end - begin) & 3u) || end - begin > CAR_LIGHT_WALK_NODES * 4u ||
        !car_light_address(begin, end - begin)) return;
    for (at = begin; at < end && remaining; at += 4)
        car_light_walk(MEM32(at), graph, selector, mask, matrix, 0, &remaining);
}
