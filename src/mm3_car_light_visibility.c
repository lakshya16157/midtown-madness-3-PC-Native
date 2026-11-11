#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_car_lighting.h"

/* Native CMP render methods: ordinary groups pass their matrix unchanged;
 * transform groups compose the current relative matrix with their parent.
 * All 3,405 lamp nodes in the 264 installed models have only these ancestors.
 * Do not invoke virtual render/update methods: they can submit meshes,
 * particles and flares, and would undo the body's visibility rejection. */
enum { CAR_LIGHT_WALK_NODES = 1024, CAR_LIGHT_WALK_DEPTH = 32 };

/* Where lamps go: the light queue, or the self-check's record. */
static void (*s_emit)(uint32_t, uint32_t, const float[12]) = mm3_car_lighting_emit;

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
        if (kind != 4 && kind != 9) s_emit(node, mask, parent);
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

/* The walk above visits every node of a car's model (hundreds) to reach its
 * 10-20 lamps: 3.4% of the game thread in dense traffic. A model's shape does
 * not change, so the first walk of a graph records only the nodes on a path
 * to a lamp, in walk order, and later frames visit those alone. Every frame
 * still checks each recorded node (vtable, render method, owner, child range)
 * and reads the per-frame state the full walk reads (hidden flags, lamp kind,
 * local matrices), so a hidden subtree, a broken part or a graph reused for
 * another model behaves as the full walk would: on any mismatch the frame
 * takes the full walk and the graph is recorded again.
 * MM3_CAR_LIGHT_PATHS=0 always walks in full; =check runs both every frame,
 * emits the full walk's lights and logs any difference. */
/* 256 graphs: a race scene has more cars than 64, and with 64 they evicted
 * each other every frame (path_record was 2% of the game thread). */
enum { PATH_GRAPHS = 256, PATH_NODES = 192, PATH_LAMP = 1, PATH_TRANSFORM = 2, PATH_GROUP = 3 };
typedef struct {
    uint32_t node, begin, end;
    int16_t parent;                 /* index of the part above; -1: the graph */
    uint8_t type;
} PathNode;
typedef struct {
    uint32_t graph, begin, end, used;
    unsigned count;
    PathNode n[PATH_NODES];
} PathCache;
static PathCache s_paths[PATH_GRAPHS];
static uint32_t s_path_clock;

static int path_mode(void)
{
    static int mode = -1;
    if (mode < 0) {
        const char *e = getenv("MM3_CAR_LIGHT_PATHS");
        mode = e && !strcmp(e, "0") ? 0 : e && !strcmp(e, "check") ? 2 : 1;
        if (mode != 1)
            fprintf(stderr, "[MM3_CAR_LIGHT_PATHS] %s\n", mode ? "check: both walks every frame" : "off: full walk");
    }
    return mode;
}

/* Record the subtree at `node` with the full walk's checks, entering hidden
 * subtrees too (their lamps must be known when they show again). Returns 1
 * if it holds a lamp; otherwise its entries are dropped. */
static int path_record(PathCache *c, uint32_t node, uint32_t graph, int parent,
                       unsigned depth, unsigned *remaining, int *overflow)
{
    uint32_t vtable, render, begin, end, at;
    unsigned mark = c->count, me;
    int lamp = 0;
    if (!*remaining) { *overflow = 1; return 0; }
    --*remaining;
    if (depth > CAR_LIGHT_WALK_DEPTH || !car_light_address(node, 0x54)) return 0;
    vtable = MEM32(node);
    if (!car_light_address(vtable, 0x44)) return 0;
    render = MEM32(vtable + 0x40);
    if (c->count >= PATH_NODES) { *overflow = 1; return 0; }
    me = c->count;
    if (render == 0x000CF2DAu) {
        if (!car_light_address(node, 0x84) || MEM32(node + 4) != graph) return 0;
        c->n[c->count].node = node;
        c->n[c->count].begin = c->n[c->count].end = 0;
        c->n[c->count].parent = (int16_t)parent;
        c->n[c->count].type = PATH_LAMP;
        ++c->count;
        return 1;
    }
    if (render == 0x000DE780u) {
        if (!car_light_address(node, 0x9C)) return 0;
    } else if (render != 0x000DF751u) return 0;
    begin = MEM32(node + 0xC);
    end = MEM32(node + 0x10);
    if (end < begin || ((end - begin) & 3u) || end - begin > CAR_LIGHT_WALK_NODES * 4u ||
        !car_light_address(begin, end - begin)) return 0;
    c->n[c->count].node = node;
    c->n[c->count].begin = begin;
    c->n[c->count].end = end;
    c->n[c->count].parent = (int16_t)parent;
    c->n[c->count].type = (uint8_t)(render == 0x000DE780u ? PATH_TRANSFORM : PATH_GROUP);
    ++c->count;
    for (at = begin; at < end && !*overflow; at += 4)
        lamp |= path_record(c, MEM32(at), graph, (int)me, depth + 1, remaining, overflow);
    if (!lamp) c->count = mark;
    return lamp;
}

/* One frame over the recorded paths. 0 if the model no longer matches the
 * record; nothing is emitted then, so the full walk can take the frame. */
static int path_run(const PathCache *c, uint32_t graph, uint32_t selector, uint32_t mask,
                    const float root[12])
{
    float world[PATH_NODES][12];
    uint8_t live[PATH_NODES];
    uint16_t emit[PATH_NODES];
    unsigned i, emits = 0;
    for (i = 0; i < c->count; ++i) {
        const PathNode *p = &c->n[i];
        const float *parent = p->parent < 0 ? root : world[p->parent];
        uint32_t vtable;
        live[i] = 0;
        if (!car_light_address(p->node, p->type == PATH_LAMP ? 0x84 : p->type == PATH_TRANSFORM ? 0x9C : 0x54))
            return 0;
        vtable = MEM32(p->node);
        if (!car_light_address(vtable, 0x44)) return 0;
        if (MEM32(vtable + 0x40) != (p->type == PATH_LAMP ? 0x000CF2DAu :
                                     p->type == PATH_TRANSFORM ? 0x000DE780u : 0x000DF751u))
            return 0;
        if (p->type == PATH_LAMP) {
            if (MEM32(p->node + 4) != graph) return 0;
        } else if (MEM32(p->node + 0xC) != p->begin || MEM32(p->node + 0x10) != p->end) {
            return 0;
        }
        if (p->parent >= 0 && !live[p->parent]) continue;      /* under a hidden part */
        if (p->type == PATH_LAMP) {
            unsigned kind = MEM32(p->node + 0x34);
            /* Rotating/emergency nodes update their own phase in native
             * drawing (see car_light_walk). */
            if (kind != 4 && kind != 9) emit[emits++] = (uint16_t)i;
            continue;
        }
        if (p->type == PATH_TRANSFORM) {
            uint32_t flags = MEM32(p->node + (selector ? 0x38 : 0x34));
            float local[12];
            if (flags & 5u) continue;          /* hidden/broken, or absolute (mask cleared) */
            memcpy(local, (const void *)XBOX_PTR(p->node + 0x6C), sizeof local);
            if (!car_light_compose(local, parent, world[i])) continue;
        } else {
            memcpy(world[i], parent, sizeof world[i]);
        }
        live[i] = 1;
    }
    /* Emitted once the whole record checked out, so a mismatch found late
     * never leaves half a car's lights queued before the full walk. */
    for (i = 0; i < emits; ++i) {
        const PathNode *p = &c->n[emit[i]];
        s_emit(p->node, mask, p->parent < 0 ? root : world[p->parent]);
    }
    return 1;
}

static unsigned path_home(uint32_t graph)
{
    return (graph * 0x9E3779B1u) >> 24;     /* 0..255: where a graph is looked for first */
}

static PathCache *path_find(uint32_t graph, uint32_t begin, uint32_t end)
{
    unsigned i, home = path_home(graph);
    for (i = 0; i < PATH_GRAPHS; ++i) {
        PathCache *c = &s_paths[(home + i) & (PATH_GRAPHS - 1)];
        if (c->graph == graph && c->begin == begin && c->end == end && c->count) return c;
    }
    return NULL;
}

static PathCache *path_build(uint32_t graph, uint32_t begin, uint32_t end)
{
    unsigned i, remaining = CAR_LIGHT_WALK_NODES, home = path_home(graph);
    PathCache *c = &s_paths[home];
    uint32_t at;
    int overflow = 0;
    /* Its own slot, else a free or the least recently used one, searched from home. */
    for (i = 0; i < PATH_GRAPHS; ++i) {
        PathCache *e = &s_paths[(home + i) & (PATH_GRAPHS - 1)];
        if (e->graph == graph) { c = e; break; }
        if (e->used < c->used) c = e;
    }
    c->graph = graph; c->begin = begin; c->end = end; c->count = 0;
    for (at = begin; at < end && !overflow; at += 4)
        path_record(c, MEM32(at), graph, -1, 0, &remaining, &overflow);
    /* A model over the budget keeps the full walk, which stops where it
     * always stopped. */
    if (overflow) c->count = 0;
    return c->count ? c : NULL;
}

/* The self-check's record of one walk. */
enum { CHECK_LAMPS = 256 };
static struct { uint32_t node; float m[12]; } s_check[2][CHECK_LAMPS];
static unsigned s_check_n[2], s_check_side;

static void check_emit(uint32_t node, uint32_t mask, const float m[12])
{
    unsigned n = s_check_n[s_check_side];
    (void)mask;
    if (n < CHECK_LAMPS) {
        s_check[s_check_side][n].node = node;
        memcpy(s_check[s_check_side][n].m, m, sizeof s_check[0][0].m);
        s_check_n[s_check_side] = n + 1;
    }
}

static void path_check(const PathCache *c, uint32_t graph, uint32_t selector, uint32_t mask,
                       const float matrix[12], uint32_t begin, uint32_t end)
{
    static unsigned checks, diffs;
    unsigned remaining = CAR_LIGHT_WALK_NODES, k;
    uint32_t at;
    s_emit = check_emit;
    s_check_n[0] = s_check_n[1] = 0;
    s_check_side = 0;
    if (!path_run(c, graph, selector, mask, matrix)) s_check_n[0] = 0xFFFFu;
    s_check_side = 1;
    for (at = begin; at < end && remaining; at += 4)
        car_light_walk(MEM32(at), graph, selector, mask, matrix, 0, &remaining);
    s_emit = mm3_car_lighting_emit;
    ++checks;
    if (s_check_n[0] != s_check_n[1] ||
        memcmp(s_check[0], s_check[1], s_check_n[1] * sizeof s_check[1][0])) {
        if (diffs++ < 16)
            fprintf(stderr, "[MM3_CAR_LIGHT_PATHS] graph %08X: paths gave %u lamps, full walk %u (%u differ of %u checks)\n",
                    graph, s_check_n[0], s_check_n[1], diffs, checks);
    } else if ((checks & 4095u) == 0) {
        fprintf(stderr, "[MM3_CAR_LIGHT_PATHS] %u checks, %u differ\n", checks, diffs);
    }
    for (k = 0; k < s_check_n[1]; ++k)
        mm3_car_lighting_emit(s_check[1][k].node, mask, s_check[1][k].m);
}

void mm3_car_lighting_model(uint32_t graph, uint32_t selector)
{
    uint32_t world, actor, begin, end, at, mask;
    unsigned remaining = CAR_LIGHT_WALK_NODES, i;
    float matrix[12];
    int mode;
    if (!car_light_address(graph, 0x128)) return;
    world = MEM32(0x003C55A0u);
    if (!car_light_address(world, 0x2A9) || MEM8(world + 0x2A8) || !MEM32(world + 0x24C)) return;
    actor = MEM32(graph + 4);
    /* This explicit owner gate lies after the native body cull. Preserve it
     * even though this source-only pass runs before that cull. */
    if (!car_light_address(actor, 0xED) || MEM8(actor + 0xEC)) return;
    mask = mm3_car_lighting_mask(graph, MEM32(graph + 0x124), MEM32(0x00407C30u));
    if (!(mask & 0x16Fu)) return;
    memcpy(matrix, (const void *)XBOX_PTR(graph + 0x3C), sizeof matrix);
    for (i = 0; i < 12; ++i) if (!isfinite(matrix[i])) return;
    begin = MEM32(graph + 0xC);
    end = MEM32(graph + 0x10);
    if (end < begin || ((end - begin) & 3u) || end - begin > CAR_LIGHT_WALK_NODES * 4u ||
        !car_light_address(begin, end - begin)) return;
    mode = path_mode();
    if (mode) {
        PathCache *c = path_find(graph, begin, end);
        if (!c) c = path_build(graph, begin, end);
        if (c) {
            c->used = ++s_path_clock;
            if (mode == 2) {
                path_check(c, graph, selector, mask, matrix, begin, end);
                return;
            }
            if (path_run(c, graph, selector, mask, matrix)) return;
            c->count = 0;                      /* the model changed: record it again */
        }
    }
    for (at = begin; at < end && remaining; at += 4)
        car_light_walk(MEM32(at), graph, selector, mask, matrix, 0, &remaining);
}
