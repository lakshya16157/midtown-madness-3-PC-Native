#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <stdio.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
extern int mm3_minimap_improved(void);
extern int mm3_minimap_origin(uint32_t, float *);
extern int mm3_minimap_heading(uint32_t, float *);

/* 201BCA publishes the native race's remaining ordered checkpoint list and
 * the visible Undercover stops once per player HUD update. Copy positions
 * and identities; never dereference a saved guest object. */
enum { RACE_PLAYERS = 2, RACE_CHECKPOINTS = 128 };
typedef struct { float x, y, z; } RoadPoint;
typedef struct { uint32_t object; RoadPoint position; } RaceCheckpoint;
/* An Undercover stop the native HUD is showing. Its collection and class
 * identify the stops that share the selected target's course. */
typedef struct { uint32_t object, kind, list; RoadPoint position; } RaceObjective;
enum { ORDER_SINGLE, ORDER_FORWARD, ORDER_REVERSE };
static struct {
    RaceCheckpoint checkpoints[RACE_CHECKPOINTS];
    RaceObjective objectives[RACE_CHECKPOINTS];
    RoadPoint origin, objective_origin;
    uint32_t current_object, objective_current;
    unsigned count, current, valid, objective, objective_count, order;
} s_race[RACE_PLAYERS];
static uint32_t s_reported_objective[RACE_PLAYERS];
static unsigned s_reported_targets[RACE_PLAYERS], s_reported_order[RACE_PLAYERS];

enum { ROAD_NODES = 65536, ROAD_EDGES = 262144, COURSE_POINTS = 16384,
       ACTIVE_POINTS = 4096, NO_NODE = UINT32_MAX };
typedef struct { RoadPoint position; uint32_t first; unsigned routable; } RoadNode;
typedef struct {
    uint32_t to, next, road;
    float length, direction_x, direction_z, width;
} RoadEdge;
static struct {
    RoadNode *nodes;
    RoadEdge *edges;
    float *distance;
    uint32_t *previous, *heap, *heap_position;
    unsigned node_count, edge_count, heap_count, generation, direction_edges;
} s_roads;
typedef struct {
    unsigned first, count;
    float min_x, max_x, min_z, max_z, width;
} RoadLine;
static struct {
    RoadPoint *points;
    RoadLine *lines;
    unsigned count, generation, edges;
} s_road_lines;
/* Complete park/plaza paths are map artwork, independent of the car graph.
 * Keeping their copied samples separate also keeps route snapping unchanged. */
static struct {
    RoadPoint *points;
    RoadLine *lines;
    unsigned count, point_count;
} s_map_areas;
static int s_road_layer;
typedef struct {
    RaceCheckpoint checkpoints[RACE_CHECKPOINTS];
    RoadPoint course[COURSE_POINTS], active[ACTIVE_POINTS], active_origin, active_heading, course_approach;
    RoadPoint incoming[RACE_CHECKPOINTS];
    unsigned starts[RACE_CHECKPOINTS], lengths[RACE_CHECKPOINTS];
    unsigned capacities[RACE_CHECKPOINTS], course_rebuilt, course_reused;
    unsigned count, generation, course_count, active_count;
    uint32_t active_target, course_target;
} RacePaths;
static RacePaths s_paths[RACE_PLAYERS];
/* Routing and drawing already share game-thread scratch state. Preserve the
 * previous packed course while compacting/rebuilding it, without allocating
 * on the checkpoint update or putting a large point array on the stack. */
static RacePaths s_previous_path;

static float road_distance(RoadPoint a, RoadPoint b)
{
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return sqrtf(x*x + y*y + z*z);
}

static int road_point_valid(RoadPoint p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z) &&
        fabsf(p.x) < 100000.0f && fabsf(p.y) < 100000.0f && fabsf(p.z) < 100000.0f;
}

static void road_map_areas_clear(void)
{
    free(s_map_areas.points); free(s_map_areas.lines);
    memset(&s_map_areas, 0, sizeof s_map_areas);
}

static void road_graph_clear(void)
{
    unsigned generation = s_roads.generation + 1;
    road_map_areas_clear();
    free(s_roads.nodes); free(s_roads.edges); free(s_roads.distance);
    free(s_roads.previous); free(s_roads.heap); free(s_roads.heap_position);
    free(s_road_lines.points); free(s_road_lines.lines);
    memset(&s_road_lines, 0, sizeof s_road_lines);
    memset(&s_roads, 0, sizeof s_roads);
    s_roads.generation = generation;
    memset(s_paths, 0, sizeof s_paths);
}

static int road_graph_allocate(unsigned nodes)
{
    unsigned i;
    if (!nodes || nodes > ROAD_NODES) return 0;
    s_roads.nodes = (RoadNode *)calloc(nodes, sizeof(RoadNode));
    s_roads.edges = (RoadEdge *)malloc(ROAD_EDGES * sizeof(RoadEdge));
    /* Direction-aware routing uses a directed edge as its state, so arrival
     * from different streets at one junction keeps its own cost/heading. */
    s_roads.distance = (float *)malloc(ROAD_EDGES * sizeof(float));
    s_roads.previous = (uint32_t *)malloc(ROAD_EDGES * sizeof(uint32_t));
    s_roads.heap = (uint32_t *)malloc(ROAD_EDGES * sizeof(uint32_t));
    s_roads.heap_position = (uint32_t *)malloc(ROAD_EDGES * sizeof(uint32_t));
    if (!s_roads.nodes || !s_roads.edges || !s_roads.distance || !s_roads.previous ||
        !s_roads.heap || !s_roads.heap_position) { road_graph_clear(); return 0; }
    s_roads.node_count = nodes;
    for (i = 0; i < nodes; ++i) {
        s_roads.nodes[i].first = NO_NODE; s_roads.nodes[i].routable = 1;
    }
    return 1;
}

static float road_width_valid(float width)
{
    return isfinite(width) && width >= 1.0f && width <= 80.0f ? width : 6.0f;
}

static int road_graph_edge_width(unsigned a, unsigned b, uint32_t road_a, uint32_t road_b, float width)
{
    float length;
    unsigned index;
    if (a >= s_roads.node_count || b >= s_roads.node_count || a == b) return 0;
    length = road_distance(s_roads.nodes[a].position, s_roads.nodes[b].position);
    if (!isfinite(length)) return 0;
    /* Zero-length joins are real when two road sections share an endpoint. */
    if (length < .001f) length = .001f;
    if (s_roads.edge_count + 2 > ROAD_EDGES) return 0;
    index = s_roads.edge_count;
    s_roads.edges[index].to = b; s_roads.edges[index].length = length;
    s_roads.edges[index].road = road_a;
    s_roads.edges[index].width = road_width_valid(width);
    s_roads.edges[index].next = s_roads.nodes[a].first;
    s_roads.nodes[a].first = index;
    s_roads.edges[index + 1].to = a; s_roads.edges[index + 1].length = length;
    s_roads.edges[index + 1].road = road_b;
    s_roads.edges[index + 1].width = s_roads.edges[index].width;
    s_roads.edges[index + 1].next = s_roads.nodes[b].first;
    s_roads.nodes[b].first = index + 1;
    s_roads.edge_count += 2;
    return 1;
}

static int road_graph_edge_roads(unsigned a, unsigned b, uint32_t road_a, uint32_t road_b)
{
    return road_graph_edge_width(a, b, road_a, road_b, 6.0f);
}

static int road_graph_edge(unsigned a, unsigned b)
{
    return road_graph_edge_roads(a, b, NO_NODE, NO_NODE);
}

static void road_heap_update(unsigned node)
{
    unsigned position = s_roads.heap_position[node];
    if (position == NO_NODE) position = s_roads.heap_count++;
    while (position) {
        unsigned parent = (position - 1) / 2, other = s_roads.heap[parent];
        if (s_roads.distance[other] <= s_roads.distance[node]) break;
        s_roads.heap[position] = other; s_roads.heap_position[other] = position;
        position = parent;
    }
    s_roads.heap[position] = node; s_roads.heap_position[node] = position;
}

static unsigned road_heap_pop(void)
{
    unsigned result = s_roads.heap[0], node = s_roads.heap[--s_roads.heap_count];
    unsigned position = 0;
    if (s_roads.heap_count) {
        while (position * 2 + 1 < s_roads.heap_count) {
            unsigned child = position * 2 + 1;
            if (child + 1 < s_roads.heap_count &&
                s_roads.distance[s_roads.heap[child + 1]] < s_roads.distance[s_roads.heap[child]]) ++child;
            if (s_roads.distance[node] <= s_roads.distance[s_roads.heap[child]]) break;
            s_roads.heap[position] = s_roads.heap[child];
            s_roads.heap_position[s_roads.heap[position]] = position;
            position = child;
        }
        s_roads.heap[position] = node; s_roads.heap_position[node] = position;
    }
    s_roads.heap_position[result] = NO_NODE;
    return result;
}

/* Cache street chains of one width once per city. Keeping the spline samples
 * lets the existing screen-space simplifier merge subpixel samples before
 * native submission, rather than emitting a quad for every 6m road edge. */
static int road_lines_build(void)
{
    unsigned *degree, i, pass, point_count = 0;
    unsigned char *seen;
    if (s_road_lines.generation == s_roads.generation &&
        s_road_lines.edges == s_roads.edge_count && s_road_lines.points) return 1;
    free(s_road_lines.points); free(s_road_lines.lines);
    memset(&s_road_lines, 0, sizeof s_road_lines);
    if (!s_roads.edge_count) return 0;
    degree = (unsigned *)calloc(s_roads.node_count, sizeof *degree);
    seen = (unsigned char *)calloc(s_roads.edge_count, 1);
    s_road_lines.points = (RoadPoint *)malloc((s_roads.edge_count + 1) * sizeof(RoadPoint));
    s_road_lines.lines = (RoadLine *)calloc(s_roads.edge_count / 2 + 1, sizeof(RoadLine));
    if (!degree || !seen || !s_road_lines.points || !s_road_lines.lines) {
        free(degree); free(seen); free(s_road_lines.points); free(s_road_lines.lines);
        memset(&s_road_lines, 0, sizeof s_road_lines); return 0;
    }
    for (i = 0; i < s_roads.node_count; ++i) {
        unsigned edge;
        for (edge = s_roads.nodes[i].first; edge != NO_NODE; edge = s_roads.edges[edge].next) ++degree[i];
    }
    /* Junction/end chains first, then any closed degree-two loops. */
    for (pass = 0; pass < 2; ++pass) for (i = 0; i < s_roads.node_count; ++i) {
        unsigned first_edge;
        if (!pass && degree[i] == 2) continue;
        for (first_edge = s_roads.nodes[i].first; first_edge != NO_NODE; first_edge = s_roads.edges[first_edge].next) {
            unsigned edge = first_edge, current = i;
            RoadLine *line;
            if (seen[edge]) continue;
            line = &s_road_lines.lines[s_road_lines.count++];
            line->first = point_count;
            line->width = s_roads.edges[edge].width;
            line->min_x = line->max_x = s_roads.nodes[i].position.x;
            line->min_z = line->max_z = s_roads.nodes[i].position.z;
            s_road_lines.points[point_count++] = s_roads.nodes[i].position;
            for (;;) {
                RoadPoint p;
                unsigned next;
                seen[edge] = seen[edge ^ 1] = 1;
                current = s_roads.edges[edge].to; p = s_roads.nodes[current].position;
                s_road_lines.points[point_count++] = p;
                line->min_x = fminf(line->min_x, p.x); line->max_x = fmaxf(line->max_x, p.x);
                line->min_z = fminf(line->min_z, p.z); line->max_z = fmaxf(line->max_z, p.z);
                if (degree[current] != 2 || point_count - line->first >= COURSE_POINTS) break;
                next = s_roads.nodes[current].first;
                if (next == (edge ^ 1)) next = s_roads.edges[next].next;
                if (next == NO_NODE || seen[next] || s_roads.edges[next].width != line->width) break;
                edge = next;
            }
            line->count = point_count - line->first;
        }
    }
    s_road_lines.generation = s_roads.generation; s_road_lines.edges = s_roads.edge_count;
    free(degree); free(seen); return 1;
}

typedef struct {
    RoadPoint position;
    unsigned a, b, edge;
    uint32_t road;
    float t, length;
} RoadSnap;

static int road_snap_direction(RoadPoint position, RoadPoint heading, RoadSnap *snap)
{
    unsigned a, edge;
    float best = FLT_MAX, heading_length = hypotf(heading.x, heading.z);
    if (!road_point_valid(position)) return 0;
    for (a = 0; a < s_roads.node_count; ++a)
        for (edge = s_roads.nodes[a].first; edge != NO_NODE; edge = s_roads.edges[edge].next) {
            unsigned b = s_roads.edges[edge].to;
            RoadPoint p = s_roads.nodes[a].position, q = s_roads.nodes[b].position, projected;
            float x, y, z, length2, t, score;
            if (!s_roads.nodes[a].routable) continue;
            if (b <= a) continue;
            x = q.x - p.x; y = q.y - p.y; z = q.z - p.z;
            length2 = x*x + y*y + z*z;
            if (length2 < .0001f) continue;
            t = ((position.x - p.x)*x + (position.y - p.y)*y + (position.z - p.z)*z) / length2;
            if (t < 0) t = 0; if (t > 1) t = 1;
            projected.x = p.x + t*x; projected.y = p.y + t*y; projected.z = p.z + t*z;
            x = position.x - projected.x; y = position.y - projected.y; z = position.z - projected.z;
            /* Height separates stacked roads and bridges from the street below. */
            score = x*x + 9*y*y + z*z;
            if (isfinite(heading_length) && heading_length > .01f) {
                float street_x = q.x - p.x, street_z = q.z - p.z;
                float street_length = hypotf(street_x, street_z);
                /* At a crossing, prefer the road aligned with the car. The
                 * bounded 6m bias cannot pull it onto a distant parallel road. */
                if (street_length > .01f)
                    score += 36.0f * (1.0f - fminf(1.0f, fabsf((heading.x*street_x + heading.z*street_z) /
                                                              (heading_length*street_length))));
            }
            if (score < best) {
                best = score; snap->position = projected; snap->a = a; snap->b = b;
                snap->t = t; snap->length = sqrtf(length2); snap->edge = edge;
                snap->road = s_roads.edges[edge].road == s_roads.edges[edge ^ 1].road ?
                    s_roads.edges[edge].road : NO_NODE;
            }
        }
    /* An off-road checkpoint/player must not snap to an unrelated distant road. */
    return best < 200.0f * 200.0f;
}

static int road_snap(RoadPoint position, RoadSnap *snap)
{
    return road_snap_direction(position, (RoadPoint){0, 0, 0}, snap);
}

static unsigned road_path(RoadPoint start, RoadPoint finish, RoadPoint *points, unsigned capacity)
{
    RoadSnap from, to;
    unsigned i, last = NO_NODE, count = 0;
    float best = FLT_MAX;
    if (capacity < 2 || !s_roads.edge_count || !road_snap(start, &from) || !road_snap(finish, &to)) return 0;
    if (from.a == to.a && from.b == to.b) {
        points[0] = from.position; points[1] = to.position;
        return road_distance(points[0], points[1]) > .01f ? 2 : 0;
    }
    s_roads.heap_count = 0;
    for (i = 0; i < s_roads.node_count; ++i) {
        s_roads.distance[i] = FLT_MAX; s_roads.previous[i] = NO_NODE;
        s_roads.heap_position[i] = NO_NODE;
    }
    s_roads.distance[from.a] = from.t * from.length;
    s_roads.distance[from.b] = (1 - from.t) * from.length;
    road_heap_update(from.a); road_heap_update(from.b);
    while (s_roads.heap_count) {
        unsigned node = road_heap_pop(), edge;
        float distance = s_roads.distance[node], extra;
        if (distance >= best) break;
        if (node == to.a || node == to.b) {
            extra = node == to.a ? to.t * to.length : (1 - to.t) * to.length;
            if (distance + extra < best) { best = distance + extra; last = node; }
        }
        for (edge = s_roads.nodes[node].first; edge != NO_NODE; edge = s_roads.edges[edge].next) {
            unsigned next = s_roads.edges[edge].to;
            float candidate = distance + s_roads.edges[edge].length;
            if (candidate < s_roads.distance[next]) {
                s_roads.distance[next] = candidate; s_roads.previous[next] = node;
                road_heap_update(next);
            }
        }
    }
    if (last == NO_NODE) return 0; /* Never substitute a line through buildings. */
    for (i = last; i != NO_NODE; i = s_roads.previous[i]) {
        if (count + 2 >= capacity) return 0;
        points[count++] = s_roads.nodes[i].position;
    }
    for (i = 0; i < count / 2; ++i) {
        RoadPoint swap = points[i]; points[i] = points[count - 1 - i]; points[count - 1 - i] = swap;
    }
    memmove(points + 1, points, count * sizeof *points);
    points[0] = from.position; points[count + 1] = to.position;
    /* Remove duplicated snapped endpoints, retaining every genuine road bend. */
    for (i = 1, last = 1; i < count + 2; ++i)
        if (road_distance(points[last - 1], points[i]) > .01f) points[last++] = points[i];
    return last >= 2 ? last : 0;
}

static void road_graph_directions(void)
{
    unsigned edge;
    if (s_roads.direction_edges == s_roads.edge_count) return;
    for (edge = 0; edge < s_roads.edge_count; ++edge) {
        RoadPoint a = s_roads.nodes[s_roads.edges[edge ^ 1].to].position;
        RoadPoint b = s_roads.nodes[s_roads.edges[edge].to].position;
        float x = b.x - a.x, z = b.z - a.z, length = hypotf(x, z);
        s_roads.edges[edge].direction_x = length > .01f ? x / length : 0;
        s_roads.edges[edge].direction_z = length > .01f ? z / length : 0;
    }
    for (edge = 0; edge < s_roads.edge_count; ++edge) {
        unsigned side;
        RoadEdge *join = s_roads.edges + edge;
        if (fabsf(join->direction_x) + fabsf(join->direction_z) > .01f) continue;
        /* Retail endpoints often coincide with their junction. Infer the
         * connector's heading from its street endpoint instead of dropping
         * the incoming heading at a zero-length join. Inspect geometry only,
         * so an inferred connector cannot contaminate another connector. */
        for (side = 0; side < 2; ++side) {
            unsigned node = side ? join->to : s_roads.edges[edge ^ 1].to;
            unsigned other, found = 0;
            float dx = 0, dz = 0;
            RoadPoint a = s_roads.nodes[node].position;
            for (other = s_roads.nodes[node].first; other != NO_NODE; other = s_roads.edges[other].next) {
                RoadPoint b;
                float x, z, length;
                if (other == edge || other == (edge ^ 1)) continue;
                b = s_roads.nodes[s_roads.edges[other].to].position;
                x = b.x - a.x; z = b.z - a.z; length = hypotf(x, z);
                if (length <= .01f) continue;
                dx = (side ? 1.0f : -1.0f) * x / length;
                dz = (side ? 1.0f : -1.0f) * z / length;
                ++found;
            }
            if (found == 1) { join->direction_x = dx; join->direction_z = dz; break; }
        }
    }
    s_roads.direction_edges = s_roads.edge_count;
}

static float road_direction_cost(float x, float z, unsigned edge, int strict, int same_road)
{
    RoadEdge *out = s_roads.edges + edge;
    float dot;
    if (fabsf(x) + fabsf(z) < .01f ||
        fabsf(out->direction_x) + fabsf(out->direction_z) < .01f) return 0;
    dot = fmaxf(-1.0f, fminf(1.0f, x*out->direction_x + z*out->direction_z));
    /* A real bend within one native street is not a junction U-turn. */
    if (dot < -.70710678f && !same_road) return strict ? FLT_MAX : 240.0f;
    return 18.0f * (1.0f - dot) * (1.0f - dot);
}

static unsigned road_path_direction_search(const RoadSnap *from, const RoadSnap *to, RoadPoint heading,
                                           int same_road, int strict, RoadPoint *points, unsigned capacity,
                                           int *reachable)
{
    unsigned i, last = NO_NODE, count = 0, direct = 0;
    float best = FLT_MAX;
    *reachable = 0;
    s_roads.heap_count = 0;
    for (i = 0; i < s_roads.edge_count; ++i) {
        s_roads.distance[i] = FLT_MAX; s_roads.previous[i] = NO_NODE;
        s_roads.heap_position[i] = NO_NODE;
    }
    if (from->edge == to->edge) {
        unsigned edge = to->t >= from->t ? from->edge : (from->edge ^ 1);
        float extra = road_direction_cost(heading.x, heading.z, edge, strict, same_road);
        if (extra < FLT_MAX) { best = fabsf(to->t - from->t) * from->length + extra; direct = 1; }
    }
    for (i = 0; i < 2; ++i) {
        unsigned edge = i ? from->edge : (from->edge ^ 1);
        float length = (i ? 1.0f - from->t : from->t) * from->length;
        float extra = length > .01f ? road_direction_cost(heading.x, heading.z, edge, strict, same_road) : 0;
        /* A checkpoint on another street should not send the car backwards
         * along its current street when a forward route is available. */
        if (strict && !same_road && length > .01f &&
            heading.x*s_roads.edges[edge].direction_x + heading.z*s_roads.edges[edge].direction_z < -.25f) continue;
        if (extra == FLT_MAX) continue;
        s_roads.distance[edge] = length + extra; road_heap_update(edge);
    }
    while (s_roads.heap_count) {
        unsigned incoming = road_heap_pop(), node = s_roads.edges[incoming].to, edge;
        float distance = s_roads.distance[incoming], x = s_roads.edges[incoming].direction_x;
        float z = s_roads.edges[incoming].direction_z;
        int at_origin = s_roads.previous[incoming] == NO_NODE &&
            road_distance(s_roads.nodes[node].position, from->position) <= .01f;
        if (distance >= best) break;
        if (at_origin) { x = heading.x; z = heading.z; }
        if (node == to->a || node == to->b) {
            float length = node == to->a ? to->t * to->length : (1.0f - to->t) * to->length;
            unsigned terminal = node == to->a ? to->edge : (to->edge ^ 1);
            int street_bend = s_roads.edges[incoming].road != NO_NODE &&
                s_roads.edges[incoming].road == s_roads.edges[terminal].road;
            float extra = length > .01f ? road_direction_cost(x, z, terminal, strict, street_bend) : 0;
            if (length > .01f && terminal == (incoming ^ 1) && !at_origin && strict) extra = FLT_MAX;
            if (at_origin && strict && !same_road && length > .01f &&
                x*s_roads.edges[terminal].direction_x + z*s_roads.edges[terminal].direction_z < -.25f) extra = FLT_MAX;
            if (extra < FLT_MAX && distance + length + extra < best) {
                best = distance + length + extra; last = incoming; direct = 0;
            }
        }
        for (edge = s_roads.nodes[node].first; edge != NO_NODE; edge = s_roads.edges[edge].next) {
            int street_bend = s_roads.edges[incoming].road != NO_NODE &&
                s_roads.edges[incoming].road == s_roads.edges[edge].road;
            float extra, candidate;
            if (strict && edge == (incoming ^ 1) && !at_origin) continue;
            if (at_origin && strict && !same_road &&
                x*s_roads.edges[edge].direction_x + z*s_roads.edges[edge].direction_z < -.25f) continue;
            extra = road_direction_cost(x, z, edge, strict, street_bend);
            if (extra == FLT_MAX) continue;
            /* Recover at a dead end only after the forward search fails.
             * Charge the manoeuvre once, even on a zero-length join. */
            if (!strict && edge == (incoming ^ 1) && !at_origin) extra = fmaxf(extra, 240.0f);
            candidate = distance + s_roads.edges[edge].length + extra;
            if (candidate < s_roads.distance[edge]) {
                s_roads.distance[edge] = candidate; s_roads.previous[edge] = incoming; road_heap_update(edge);
            }
        }
    }
    if (direct) {
        *reachable = 1;
        points[0] = from->position; points[1] = to->position;
        return road_distance(points[0], points[1]) > .01f ? 2 : 0;
    }
    if (last == NO_NODE) return 0;
    *reachable = 1;
    for (i = last; i != NO_NODE; i = s_roads.previous[i]) {
        if (count + 2 >= capacity) return 0;
        points[count++] = s_roads.nodes[s_roads.edges[i].to].position;
    }
    for (i = 0; i < count / 2; ++i) {
        RoadPoint swap = points[i]; points[i] = points[count - 1 - i]; points[count - 1 - i] = swap;
    }
    memmove(points + 1, points, count * sizeof *points);
    points[0] = from->position; points[count + 1] = to->position;
    for (i = 1, last = 1; i < count + 2; ++i)
        if (road_distance(points[last - 1], points[i]) > .01f) points[last++] = points[i];
    return last >= 2 ? last : 0;
}

static unsigned road_path_direction(RoadPoint start, RoadPoint finish, RoadPoint heading,
                                    RoadPoint *points, unsigned capacity)
{
    RoadSnap from, to;
    unsigned count;
    int same_road, reachable;
    float length = hypotf(heading.x, heading.z);
    if (!isfinite(length) || length <= .01f) return road_path(start, finish, points, capacity);
    if (capacity < 2 || !s_roads.edge_count) return 0;
    heading.x /= length; heading.z /= length; heading.y = 0;
    if (!road_snap_direction(start, heading, &from) || !road_snap(finish, &to)) return 0;
    same_road = from.edge == to.edge || (from.road != NO_NODE && from.road == to.road);
    road_graph_directions();
    count = road_path_direction_search(&from, &to, heading, same_road, 1, points, capacity, &reachable);
    if (reachable) return count;
    /* Disconnected/one-ended roads need a legal recovery instead of losing
     * guidance. A reverse route still follows the native road graph. */
    return road_path_direction_search(&from, &to, heading, same_road, 0, points, capacity, &reachable);
}

typedef struct {
    uint32_t object, controls;
    unsigned control_count, steps, first, last;
    float bias, tension, continuity;
} RoadCurve;

typedef struct {
    uint32_t area, geometry;
    unsigned count, first;
} RoadTrafficPath;

static unsigned road_object_index(uint32_t object, uint32_t base, unsigned count, unsigned stride);

static float road_template_width(uint32_t profile)
{
    float centers[64], lane = FLT_MAX, minimum, scale;
    uint32_t first, end, path;
    unsigned count = 0, i;
    if (!profile) return 6.0f;
    first = MEM32(profile + 4); end = MEM32(profile + 8);
    minimum = MEMF(profile + 0x20); scale = MEMF(profile + 0x38);
    if (!first || end < first || (end - first) % 7 || (end - first) / 7 > 4096 ||
        !isfinite(minimum) || !isfinite(scale) || scale <= 0 || scale > 50000) return 6.0f;
    for (path = first; path < end; path += 7) {
        uint32_t controls;
        unsigned samples, at;
        float x;
        if ((MEM8(path + 6) & 15) != 1) continue;
        samples = MEM16(path); controls = MEM32(path + 2);
        if (!controls || !samples || samples > 4096 || count == 64) return 6.0f;
        /* Local X is lateral. Native 1545E7 also reads car-path endpoints;
         * the complete template bounds include sidewalks and scenery. */
        x = minimum + (float)MEM16(controls + (samples - 1) * 6) * (1.0f / 65535.0f) * scale;
        if (!isfinite(x)) return 6.0f;
        for (at = count; at && centers[at - 1] > x; --at) centers[at] = centers[at - 1];
        centers[at] = x; ++count;
    }
    if (!count) return 6.0f;
    {
        unsigned unique = 1;
        for (i = 1; i < count; ++i)
            if (centers[i] - centers[unique - 1] >= .05f) centers[unique++] = centers[i];
        count = unique;
    }
    /* Lane centers give the carriageway span. Extend by one representative
     * lane spacing for the two outer half-lanes. A single lane, or an ambiguous
     * profile, uses an estimated 3.5m lane; no native lane-width scalar exists. */
    for (i = 1; i < count; ++i) {
        float gap = centers[i] - centers[i - 1];
        if (gap >= 1.5f && gap <= 8.0f && gap < lane) lane = gap;
    }
    if (lane == FLT_MAX) lane = 3.5f;
    return road_width_valid(centers[count - 1] - centers[0] + lane);
}

static float road_curve_width(const RoadCurve *curve, float t)
{
    uint32_t road = curve->object, positions, profiles, end;
    unsigned count, i, selected = 0;
    float distance;
    if (!road || !isfinite(t)) return 6.0f;
    positions = MEM32(road + 0x80); profiles = MEM32(road + 0x90); end = MEM32(road + 0x94);
    if (!positions || !profiles || end < profiles || (end - profiles) % 4) return 6.0f;
    count = (end - profiles) / 4;
    if (!count || count > 256 || MEM32(road + 0x84) < positions ||
        (MEM32(road + 0x84) - positions) / 4 != count ||
        (MEM32(road + 0x84) - positions) % 4 ||
        !isfinite(MEMF(road + 0xCC)) || MEMF(road + 0xCC) <= 0) return 6.0f;
    distance = fmaxf(0, fminf(1, t)) * MEMF(road + 0xCC);
    for (i = 0; i < count; ++i) {
        float start = MEMF(positions + i * 4);
        if (!isfinite(start)) return 6.0f;
        if (start > distance) break;
        selected = i;
    }
    return road_template_width(MEM32(profiles + selected * 4));
}

static RoadPoint road_traffic_point(uint32_t area, uint32_t geometry, unsigned index)
{
    uint32_t controls = MEM32(geometry + 2);
    unsigned count = MEM16(geometry), axis;
    float scale = MEMF(area + 0xE0), component[3];
    for (axis = 0; axis < 3; ++axis) {
        float value;
        /* Native 13532D expands a two-control straight path to six samples.
         * Other paths already contain their native bend samples. */
        if (count == 2) {
            float a = (float)MEM16(controls + axis * 2);
            float b = (float)MEM16(controls + 6 + axis * 2);
            value = a + (b - a) * ((float)index * .2f);
        } else value = (float)MEM16(controls + index * 6 + axis * 2);
        component[axis] = MEMF(area + 0xD4 + axis * 4) +
            (value * (1.0f / 65535.0f)) * scale;
    }
    return (RoadPoint){component[0], component[1], component[2]};
}

/* Washington areas reviewed against its original HUD artwork. Select whole
 * native areas rather than clipping their paths at review-grid boundaries:
 * this preserves full garden networks, block outlines and their entrances.
 * Area 141 completes the authored continuations from area 142. */
static const unsigned s_map_area_indices[] = {
    59, 60, 61, 62, 64, 65, 66, 69, 71, 72, 118, 133, 135, 136,
    139, 140, 141, 142, 143, 153, 154, 155, 156, 157, 161, 166,
    167, 168, 169, 170, 172, 190, 191, 192, 193, 194
};

static int road_map_areas_build(uint32_t city)
{
    /* Native 14BAE9/1C702C load the authored city.ai OBB at manager +0.
     * Its center/extents identify the reviewed Washington data, alongside
     * the area count; Paris can share area indices but must not inherit it. */
    static const uint32_t washington_bounds[] = {
        0xC47896DAu, 0x418BEEBBu, 0xC405C168u,
        0x45361E52u, 0x428034E8u, 0x4521A816u
    };
    uint32_t base;
    unsigned i, pass, point_count = 0, line_count = 0;
    road_map_areas_clear();
    if (!city || MEM32(city + 0xAC) != 198) return 0;
    for (i = 0; i < sizeof washington_bounds / sizeof *washington_bounds; ++i)
        if (MEM32(city + 0x24 + i * 4) != washington_bounds[i]) return 0;
    base = MEM32(city + 0x4C);
    if (!base) return 0;
    for (pass = 0; pass < 2; ++pass) {
        for (i = 0; i < sizeof s_map_area_indices / sizeof *s_map_area_indices; ++i) {
            uint32_t area = base + s_map_area_indices[i] * 0xE8;
            uint32_t first = MEM32(area + 0x78), end = MEM32(area + 0x7C), geometry;
            float scale = MEMF(area + 0xE0);
            if (!first || end < first || (end - first) % 7 || (end - first) / 7 > 4096 ||
                !isfinite(scale) || scale <= 0 || scale > 50000) goto invalid;
            for (geometry = first; geometry < end; geometry += 7) {
                unsigned samples, j;
                RoadLine *line;
                if ((MEM8(geometry + 6) & 15) != 0) continue;
                samples = MEM16(geometry);
                if (samples < 2 || samples > 4096 || !MEM32(geometry + 2)) goto invalid;
                if (samples == 2) samples = 6;
                if (!pass) {
                    if (line_count >= 4096 || samples > ROAD_NODES - point_count) goto invalid;
                    ++line_count; point_count += samples;
                    continue;
                }
                line = s_map_areas.lines + s_map_areas.count++;
                line->first = s_map_areas.point_count; line->count = samples; line->width = 3.5f;
                for (j = 0; j < samples; ++j) {
                    RoadPoint point = road_traffic_point(area, geometry, j);
                    if (!road_point_valid(point)) goto invalid;
                    s_map_areas.points[s_map_areas.point_count++] = point;
                    if (!j) {
                        line->min_x = line->max_x = point.x;
                        line->min_z = line->max_z = point.z;
                    } else {
                        line->min_x = fminf(line->min_x, point.x); line->max_x = fmaxf(line->max_x, point.x);
                        line->min_z = fminf(line->min_z, point.z); line->max_z = fmaxf(line->max_z, point.z);
                    }
                }
            }
        }
        if (!pass) {
            if (!line_count || !point_count) goto invalid;
            s_map_areas.points = (RoadPoint *)malloc(point_count * sizeof *s_map_areas.points);
            s_map_areas.lines = (RoadLine *)calloc(line_count, sizeof *s_map_areas.lines);
            if (!s_map_areas.points || !s_map_areas.lines) goto invalid;
        }
    }
    return 1;
invalid:
    road_map_areas_clear();
    return 0;
}

/* Polygon-area objects also own native traffic paths. In Paris these carry
 * the roundabout rings and their entry/exit arcs; using only street splines
 * loses those roads. The low nibble selects cars in native 1545E7/15467A.
 * Type zero contains pedestrian paths, including paths through the islands. */
static int road_traffic_paths(uint32_t base, unsigned count, RoadTrafficPath *paths,
                              unsigned *path_count, unsigned *nodes)
{
    unsigned i, j;
    *path_count = 0;
    for (i = 0; i < count; ++i) {
        uint32_t area = base + i * 0xE8, first = MEM32(area + 0x78), end = MEM32(area + 0x7C);
        unsigned geometries;
        if (!first && !end) continue;
        if (!first || end < first || (end - first) % 7) return 0;
        geometries = (end - first) / 7;
        if (geometries > 4096 || !isfinite(MEMF(area + 0xE0)) ||
            MEMF(area + 0xE0) <= 0 || MEMF(area + 0xE0) > 50000) return 0;
        for (j = 0; j < geometries; ++j) {
            uint32_t geometry = first + j * 7;
            unsigned samples;
            if ((MEM8(geometry + 6) & 15) != 1) continue;
            samples = MEM16(geometry);
            if (samples < 2 || samples > 4096 || !MEM32(geometry + 2)) return 0;
            if (samples == 2) samples = 6;
            if (*path_count >= 4096 || samples > ROAD_NODES - *nodes) return 0;
            paths[*path_count] = (RoadTrafficPath){area, geometry, samples, *nodes};
            ++*path_count; *nodes += samples;
        }
    }
    return 1;
}

static int road_traffic_joins(const RoadTrafficPath *paths, unsigned count,
                              const RoadCurve *curves, unsigned road_count,
                              uint32_t links, unsigned link_count,
                              uint32_t road_base, uint32_t junction_base, unsigned junction_count)
{
    unsigned i, side, j;
    for (i = 0; i < count; ++i) for (side = 0; side < 2; ++side) {
        unsigned endpoint = paths[i].first + (side ? paths[i].count - 1 : 0);
        RoadPoint position = s_roads.nodes[endpoint].position;
        float best = 8.0f;
        unsigned nearest = NO_NODE;
        if (!side && road_distance(position, s_roads.nodes[paths[i].first + paths[i].count - 1].position) < .05f &&
            !road_graph_edge_width(endpoint, paths[i].first + paths[i].count - 1,
                                   paths[i].geometry, paths[i].geometry, 3.5f)) return 0;
        /* Join only a native path endpoint to an explicitly shared sample.
         * Nearby crossing lanes/bridges must not create new intersections. */
        for (j = 0; j < count; ++j) if (j != i && paths[j].area == paths[i].area) {
            unsigned k;
            for (k = paths[j].first; k < paths[j].first + paths[j].count; ++k) {
                if (k > endpoint && (k == paths[j].first || k == paths[j].first + paths[j].count - 1)) continue;
                if (road_distance(position, s_roads.nodes[k].position) < .05f &&
                    !road_graph_edge_width(endpoint, k, paths[i].geometry, paths[j].geometry, 3.5f)) return 0;
            }
        }
        /* Area links can meet a street interior. Search that explicitly
         * linked spline's samples instead of guessing its first/last node.
         * The 8m bound covers a lane's lateral offset and 6m street sampling,
         * while refusing long straight connections through an island. */
        for (j = 0; j < link_count; ++j) {
            uint32_t a = MEM32(links + j * 8), b = MEM32(links + j * 8 + 4), object;
            unsigned road, junction, k, first, last;
            if (a == paths[i].area) object = b;
            else if (b == paths[i].area) object = a;
            else continue;
            road = road_object_index(object, road_base, road_count, 0xDC);
            junction = road_object_index(object, junction_base, junction_count, 0xAC);
            if (road != NO_NODE) { first = curves[road].first; last = curves[road].last; }
            else if (junction != NO_NODE) first = last = junction;
            else continue;
            for (k = first; k <= last; ++k) {
                float distance = road_distance(position, s_roads.nodes[k].position);
                if (distance < best) { best = distance; nearest = k; }
            }
        }
        if (nearest != NO_NODE &&
            !road_graph_edge_width(endpoint, nearest, paths[i].geometry, NO_NODE, 3.5f)) return 0;
    }
    return 1;
}

static void road_traffic_routing(unsigned street_nodes)
{
    unsigned pending = 0, i;
    /* Keep every original street's existing snapping behavior, including
     * isolated streets. A newly copied car-path component is eligible only
     * after an explicit nearby entrance reaches it from that street graph.
     * Unconnected native area movements still render without stealing the
     * player's/checkpoint's snap and making an otherwise valid route fail. */
    for (i = 0; i < street_nodes; ++i) s_roads.heap[pending++] = i;
    while (pending) {
        unsigned node = s_roads.heap[--pending], edge;
        for (edge = s_roads.nodes[node].first; edge != NO_NODE; edge = s_roads.edges[edge].next) {
            unsigned next = s_roads.edges[edge].to;
            if (s_roads.nodes[next].routable) continue;
            s_roads.nodes[next].routable = 1; s_roads.heap[pending++] = next;
        }
    }
}

static RoadPoint road_control(uint32_t controls, unsigned index)
{
    RoadPoint point;
    uint32_t address = controls + index * 12;
    point.x = MEMF(address); point.y = MEMF(address + 4); point.z = MEMF(address + 8);
    return point;
}

static RoadPoint road_curve_point(const RoadCurve *curve, float t)
{
    /* Native sub_000C191C: Kochanek–Bartels Hermite, with two ghost controls.
     * sub_00154FC8 uses this exact spline at zero lateral/vertical offset for
     * the road centre, including hills and overpasses. */
    float s = (curve->control_count - 3) * t, u, h00, h10, h01, h11;
    float out0, out1, in0, in1, factor = .5f * (1 - curve->tension);
    unsigned segment = (unsigned)s;
    RoadPoint p[4], result;
    unsigned i;
    if (segment >= curve->control_count - 3) { segment = curve->control_count - 4; u = 1; }
    else u = s - segment;
    for (i = 0; i < 4; ++i) p[i] = road_control(curve->controls, segment + i);
    out0 = factor * (1 + curve->bias) * (1 + curve->continuity);
    out1 = factor * (1 - curve->bias) * (1 - curve->continuity);
    in0 = factor * (1 + curve->bias) * (1 - curve->continuity);
    in1 = factor * (1 - curve->bias) * (1 + curve->continuity);
    h00 = 2*u*u*u - 3*u*u + 1; h10 = u*u*u - 2*u*u + u;
    h01 = -2*u*u*u + 3*u*u; h11 = u*u*u - u*u;
#define ROAD_COMPONENT(axis) \
    result.axis = h00*p[1].axis + h10*(out0*(p[1].axis-p[0].axis) + out1*(p[2].axis-p[1].axis)) + \
                  h01*p[2].axis + h11*(in0*(p[2].axis-p[1].axis) + in1*(p[3].axis-p[2].axis))
    ROAD_COMPONENT(x); ROAD_COMPONENT(y); ROAD_COMPONENT(z);
#undef ROAD_COMPONENT
    return result;
}

static unsigned road_object_index(uint32_t object, uint32_t base, unsigned count, unsigned stride)
{
    if (!base || object < base || (object - base) % stride || (object - base) / stride >= count) return NO_NODE;
    return (object - base) / stride;
}

/* Circuit-race design data (tools/races): once per city and build, write the
 * street graph just built to logs/city-roads-<roads>r-<junctions>j.json --
 * junction positions, each road's sampled centre line and every edge with its
 * width -- unless that file already exists. Car paths through plazas are left
 * out; circuits use streets. Read-only on guest state. */
static FILE *road_open(const char *path, const char *mode)
{
#ifdef _MSC_VER
    FILE *file = NULL;
    return fopen_s(&file, path, mode) ? NULL : file;
#else
    return fopen(path, mode);
#endif
}

static void road_graph_dump(const RoadCurve *curves, unsigned road_count, unsigned junction_count,
                            unsigned street_nodes, uint32_t road_base)
{
    char path[96];
    FILE *file;
    unsigned i, j, edges = 0;
    snprintf(path, sizeof path, "logs/city-roads-%ur-%uj.json", road_count, junction_count);
    file = road_open(path, "rb");
    if (file) { fclose(file); return; }
    file = road_open(path, "wb");
    if (!file) return;
    fprintf(file, "{\"roads\":%u,\"junctions\":%u,\"street_nodes\":%u,\"nodes\":[", road_count,
            junction_count, street_nodes);
    for (i = 0; i < street_nodes; ++i) {
        RoadPoint p = s_roads.nodes[i].position;
        fprintf(file, "%s[%.2f,%.2f,%.2f]", i ? "," : "", p.x, p.y, p.z);
    }
    fprintf(file, "],\"road_nodes\":[");
    for (i = 0; i < road_count; ++i)
        fprintf(file, "%s[%u,%u]", i ? "," : "", curves[i].first, curves[i].last);
    fprintf(file, "],\"edges\":[");
    for (i = 0; i < street_nodes; ++i)
        for (j = s_roads.nodes[i].first; j != NO_NODE; j = s_roads.edges[j].next) {
            const RoadEdge *edge = s_roads.edges + j;
            unsigned road = edge->road >= road_base && (edge->road - road_base) % 0xDC == 0 &&
                            (edge->road - road_base) / 0xDC < road_count ? (edge->road - road_base) / 0xDC : NO_NODE;
            if (edge->to >= street_nodes || edge->to < i) continue;
            fprintf(file, "%s[%u,%u,%d,%.2f]", edges++ ? "," : "", i, edge->to,
                    road == NO_NODE ? -1 : (int)road, edge->width);
        }
    fprintf(file, "]}\n");
    fclose(file);
    fprintf(stderr, "[RACE_ROUTE] wrote the street graph to %s (%u nodes, %u edges)\n", path, street_nodes, edges);
}

void mm3_race_guidance_roads(uint32_t city, uint32_t loaded)
{
    uint32_t arrays[4], links;
    unsigned counts[4], road_count, junction_count, traffic_count, street_nodes, total, i, j;
    RoadCurve *curves;
    RoadTrafficPath *traffic;
    road_graph_clear();
    if (!loaded || !city) return;
    /* sub_0014BAE9 has finished resolving the city.ai arrays. Snapshot world
     * geometry and the native paired object links now, before teardown can
     * free them. Never infer connections from nearby streets or crossings. */
    for (i = 0; i < 4; ++i) {
        arrays[i] = MEM32(city + 0x40 + 4*i); counts[i] = MEM32(city + 0xA0 + 4*i);
        if (counts[i] > 4096 || (counts[i] && !arrays[i])) return;
    }
    /* +4C holds areas such as plazas and roundabouts. Copy their car paths,
     * with explicit links into streets, rather than area-centre shortcuts. */
    /* +44 special splines have no car lane paths and form separate rail-like
     * loops. Cars use +40 street splines and +4C car paths instead. */
    road_count = counts[0]; junction_count = counts[2];
    links = MEM32(city + 0x58);
    if (!road_count || !links || MEM32(city + 0xB0) > 32768) return;
    curves = (RoadCurve *)calloc(road_count, sizeof *curves);
    if (!curves) return;
    traffic = (RoadTrafficPath *)calloc(4096, sizeof *traffic);
    if (!traffic) { free(curves); return; }
    total = junction_count;
    for (i = 0; i < road_count; ++i) {
        uint32_t road = arrays[0] + i * 0xDC, spline = MEM32(road + 0x78), end;
        float length = MEMF(road + 0x74);
        RoadCurve *curve = curves + i;
        if (!spline || !isfinite(length) || length <= 0 || length > 50000) goto invalid;
        curve->object = road; curve->controls = MEM32(spline + 4); end = MEM32(spline + 8);
        if (!curve->controls || end < curve->controls || (end - curve->controls) % 12) goto invalid;
        curve->control_count = (end - curve->controls) / 12;
        if (curve->control_count < 4 || curve->control_count > 4096) goto invalid;
        curve->bias = MEMF(spline + 0x10); curve->tension = MEMF(spline + 0x14);
        curve->continuity = MEMF(spline + 0x18);
        if (!isfinite(curve->bias) || !isfinite(curve->tension) || !isfinite(curve->continuity)) goto invalid;
        curve->steps = (unsigned)ceilf(length / 6.0f);
        curve->first = total; total += curve->steps + 1; curve->last = total - 1;
        if (total > ROAD_NODES) goto invalid;
    }
    street_nodes = total;
    if (!road_traffic_paths(arrays[3], counts[3], traffic, &traffic_count, &total)) goto invalid;
    if (!road_graph_allocate(total)) goto invalid;
    for (i = 0; i < junction_count; ++i) {
        uint32_t object = arrays[2] + i * 0xAC;
        s_roads.nodes[i].position = road_control(object + 0x34, 0);
        if (!road_point_valid(s_roads.nodes[i].position)) goto invalid;
    }
    for (i = 0; i < road_count; ++i) {
        RoadCurve *curve = curves + i;
        for (j = 0; j <= curve->steps; ++j) {
            s_roads.nodes[curve->first + j].position = road_curve_point(curve, (float)j / curve->steps);
            if (!road_point_valid(s_roads.nodes[curve->first + j].position)) goto invalid;
            if (j && !road_graph_edge_width(curve->first + j - 1, curve->first + j,
                                           curve->object, curve->object,
                                           road_curve_width(curve, ((float)j - .5f) / curve->steps))) goto invalid;
        }
    }
    for (i = 0; i < traffic_count; ++i) {
        RoadTrafficPath *path = traffic + i;
        for (j = 0; j < path->count; ++j) {
            s_roads.nodes[path->first + j].position = road_traffic_point(path->area, path->geometry, j);
            s_roads.nodes[path->first + j].routable = 0;
            if (!road_point_valid(s_roads.nodes[path->first + j].position)) goto invalid;
            if (j && !road_graph_edge_width(path->first + j - 1, path->first + j,
                                           path->geometry, path->geometry, 3.5f)) goto invalid;
        }
    }
    for (i = 0; i < MEM32(city + 0xB0); ++i) {
        unsigned ids[2] = {NO_NODE, NO_NODE}, road_ids[2] = {NO_NODE, NO_NODE}, side;
        for (side = 0; side < 2; ++side) {
            uint32_t object = MEM32(links + i*8 + side*4);
            for (j = 0; j < 3; ++j) {
                if (j == 1) continue;
                unsigned index = road_object_index(object, arrays[j], counts[j], j < 2 ? 0xDC : 0xAC);
                if (index == NO_NODE) continue;
                if (j == 0) road_ids[side] = index;
                else ids[side] = index;
                break;
            }
        }
        /* sub_0014E660 resolves the two directional object IDs. Slot zero
         * attaches to the road's spline start; slot one attaches to its end.
         * This also handles loops and roads with only a single native link. */
        for (side = 0; side < 2; ++side) if (road_ids[side] != NO_NODE)
            ids[side] = side ? curves[road_ids[side]].last : curves[road_ids[side]].first;
        if (ids[0] != NO_NODE && ids[1] != NO_NODE && ids[0] != ids[1]) {
            uint32_t road_a = road_ids[0] != NO_NODE ? curves[road_ids[0]].object : NO_NODE;
            uint32_t road_b = road_ids[1] != NO_NODE ? curves[road_ids[1]].object : NO_NODE;
            float road_width = 0;
            for (side = 0; side < 2; ++side) if (road_ids[side] != NO_NODE)
                road_width = fmaxf(road_width, road_curve_width(curves + road_ids[side], side ? 1.0f : 0.0f));
            if (road_a == NO_NODE) road_a = road_b;
            if (road_b == NO_NODE) road_b = road_a;
            if (!road_graph_edge_width(ids[0], ids[1], road_a, road_b, road_width)) goto invalid;
        }
    }
    if (!road_traffic_joins(traffic, traffic_count, curves, road_count, links, MEM32(city + 0xB0),
                            arrays[0], arrays[2], junction_count)) goto invalid;
    road_traffic_routing(street_nodes);
    road_graph_dump(curves, road_count, junction_count, street_nodes, arrays[0]);
    if (road_map_areas_build(city))
        fprintf(stderr, "[RACE_ROUTE] map-only Washington areas=%u paths=%u samples=%u\n",
                (unsigned)(sizeof s_map_area_indices / sizeof *s_map_area_indices),
                s_map_areas.count, s_map_areas.point_count);
    fprintf(stderr, "[RACE_ROUTE] city=%08X roads=%u junctions=%u traffic_paths=%u nodes=%u links=%u edges=%u\n",
            city, road_count, junction_count, traffic_count, s_roads.node_count, MEM32(city + 0xB0), s_roads.edge_count / 2);
    free(traffic); free(curves);
    return;
invalid:
    fprintf(stderr, "[RACE_ROUTE] rejected invalid city road geometry at %08X\n", city);
    free(traffic); free(curves); road_graph_clear();
}

void mm3_race_guidance_frame_begin(void)
{
    /* 120091 updates every active player before any HUD/map drawing. Clear
     * the whole previous epoch, including players removed from split screen. */
    memset(s_race, 0, sizeof s_race);
}

void mm3_race_guidance_begin(uint32_t player)
{
    if (player < RACE_PLAYERS) {
        s_race[player].count = s_race[player].valid = 0;
        s_race[player].objective = 0;
        s_race[player].current_object = 0;
        s_race[player].objective_count = 0;
        s_race[player].objective_current = 0;
        /* Both retail Blitz and Checkpoint scripts finish at total_targets.
         * A repeated start gate is explicit in the list, never inferred from
         * event mode or from the first gate in a shrinking pending list. */
    }
}

void mm3_race_guidance_checkpoint(uint32_t player, uint32_t object,
                                  uint32_t position, uint32_t origin,
                                  uint32_t current)
{
    unsigned count;
    RoadPoint point, player_origin;
    if (player >= RACE_PLAYERS || !object || !position || !origin) return;
    count = s_race[player].count;
    if (count >= RACE_CHECKPOINTS) return;
    /* 20BF9C exposes the checkpoint's world position at subobject +0x48.
     * Use the same player origin as the native marker projection, including
     * when the native +0x70 visibility flag suppresses the icon. */
    point.x = MEMF(position); point.y = MEMF(position + 4); point.z = MEMF(position + 8);
    player_origin.x = MEMF(origin); player_origin.y = MEMF(origin + 4); player_origin.z = MEMF(origin + 8);
    if (!road_point_valid(point) || !road_point_valid(player_origin)) return;
    s_race[player].checkpoints[count].object = object;
    s_race[player].checkpoints[count].position = point;
    s_race[player].origin = player_origin;
    s_race[player].current_object = current;
    if (object == current) s_race[player].current = count;
    s_race[player].count = count + 1;
}

void mm3_race_guidance_target(uint32_t player, uint32_t object,
                              uint32_t position, uint32_t origin,
                              uint32_t current, uint32_t list)
{
    unsigned i, count;
    RoadPoint point, player_origin;
    if (player >= RACE_PLAYERS || !current || !object || !position || !origin) return;
    /* +EC/+F0 publish every Undercover stop the HUD is showing, including
     * pickup/drop-off zones. Only visible stops reach this call, so collecting
     * them never exposes a stop the player cannot already see. The end of the
     * update decides which of them belong to the selected target's course. */
    for (i = 0; i < s_race[player].count; ++i)
        if (s_race[player].checkpoints[i].object == object) return;
    count = s_race[player].objective_count;
    for (i = 0; i < count; ++i)
        if (s_race[player].objectives[i].object == object) return;
    if (count >= RACE_CHECKPOINTS) return;
    point = road_control(position, 0); player_origin = road_control(origin, 0);
    if (!road_point_valid(point) || !road_point_valid(player_origin)) return;
    s_race[player].objectives[count].object = object;
    s_race[player].objectives[count].kind = MEM32(object);
    s_race[player].objectives[count].list = list;
    s_race[player].objectives[count].position = point;
    s_race[player].objective_origin = player_origin;
    s_race[player].objective_current = current;
    s_race[player].objective_count = count + 1;
}

static void race_objective_add(unsigned player, const RaceObjective *stop)
{
    unsigned count = s_race[player].count;
    s_race[player].checkpoints[count].object = stop->object;
    s_race[player].checkpoints[count].position = stop->position;
    s_race[player].count = count + 1;
}

/* Missions such as the paper routes show every zone at once and point the
 * arrow at the lowest unvisited one; visited zones are hidden. The native
 * collection is therefore in mission order (or exactly reversed) whenever the
 * selected target sits at one end of its visible stops, and the course runs
 * from the target through the others. A target in the middle gives no sign of
 * the ordering, so it keeps the single-stop route. Hidden or unselected
 * stops, and stops of another collection or class, never join the course. */
static void race_objective_course(unsigned player)
{
    unsigned i, count = 0, selected = 0, order = ORDER_SINGLE;
    unsigned group[RACE_CHECKPOINTS];
    const RaceObjective *stops = s_race[player].objectives;
    const RaceObjective *current = 0;
    for (i = 0; i < s_race[player].objective_count; ++i)
        if (stops[i].object == s_race[player].objective_current) { current = stops + i; break; }
    if (!current) return;
    for (i = 0; i < s_race[player].objective_count; ++i)
        if (stops[i].kind == current->kind && stops[i].list == current->list) {
            if (stops + i == current) selected = count;
            group[count++] = i;
        }
    if (count > 1 && selected == 0) order = ORDER_FORWARD;
    else if (count > 1 && selected == count - 1) order = ORDER_REVERSE;
    s_race[player].count = 0;
    if (order == ORDER_FORWARD)
        for (i = 0; i < count; ++i) race_objective_add(player, stops + group[i]);
    else if (order == ORDER_REVERSE)
        for (i = count; i-- > 0;) race_objective_add(player, stops + group[i]);
    else race_objective_add(player, current);
    s_race[player].origin = s_race[player].objective_origin;
    s_race[player].current_object = s_race[player].objective_current;
    s_race[player].objective = 1;
    s_race[player].order = order;
}

void mm3_race_guidance_end(uint32_t player)
{
    unsigned i;
    if (player >= RACE_PLAYERS) return;
    race_objective_course(player);
    /* A race may temporarily have no current target, including its finish
     * state. The course still exists and must continue drawing. */
    s_race[player].current = s_race[player].count;
    /* Native 20DA6F removes each passed gate from this list. The final
     * singleton still needs a car-to-checkpoint path. */
    s_race[player].valid = s_race[player].count > 0;
    {
        uint32_t objective = s_race[player].objective ? s_race[player].current_object : 0;
        unsigned targets = objective ? s_race[player].count : 0;
        unsigned order = objective ? s_race[player].order : ORDER_SINGLE;
        if (s_reported_objective[player] != objective ||
            (objective && (s_reported_targets[player] != targets || s_reported_order[player] != order))) {
            s_reported_objective[player] = objective;
            s_reported_targets[player] = targets; s_reported_order[player] = order;
            fprintf(stderr, "[RACE_ROUTE] player=%u objective=%08X targets=%u order=%s\n",
                    player, objective, s_race[player].count,
                    order == ORDER_FORWARD ? "forward" : order == ORDER_REVERSE ? "reverse" : "single");
        }
    }
    for (i = 0; i < s_race[player].count; ++i)
        if (s_race[player].checkpoints[i].object == s_race[player].current_object) {
            s_race[player].current = i;
            return;
        }
}

static RoadPoint race_path_tangent(const RoadPoint *points, unsigned count)
{
    RoadPoint heading = {0, 0, 0};
    unsigned i;
    if (count < 2) return heading;
    for (i = count - 1; i > 0; --i) {
        float length;
        heading.x = points[count - 1].x - points[i - 1].x;
        heading.z = points[count - 1].z - points[i - 1].z;
        length = hypotf(heading.x, heading.z);
        if (length > .01f) { heading.x /= length; heading.z /= length; break; }
    }
    return heading;
}

static void race_paths_update(unsigned player, RoadPoint heading)
{
    unsigned i, changed;
    RoadPoint approach;
    s_paths[player].course_rebuilt = s_paths[player].course_reused = 0;
    changed = s_paths[player].generation != s_roads.generation ||
        s_paths[player].count != s_race[player].count;
    for (i = 0; !changed && i < s_race[player].count; ++i)
        changed = s_paths[player].checkpoints[i].object != s_race[player].checkpoints[i].object ||
            road_distance(s_paths[player].checkpoints[i].position, s_race[player].checkpoints[i].position) > .01f;
    if (s_race[player].current >= s_race[player].count) {
        s_paths[player].active_count = 0; s_paths[player].active_target = 0;
    } else if (changed || s_paths[player].active_target != s_race[player].current_object ||
        road_distance(s_paths[player].active_origin, s_race[player].origin) >= 4.0f ||
        road_distance(s_paths[player].active_heading, heading) >= .5176381f) {
        /* Four metres of movement or a 30-degree turn refreshes the route.
         * Update before the course so its first future leg shares this approach. */
        s_paths[player].active_origin = s_race[player].origin;
        s_paths[player].active_target = s_race[player].current_object;
        s_paths[player].active_heading = heading;
        s_paths[player].active_count = road_path_direction(s_race[player].origin,
            s_race[player].checkpoints[s_race[player].current].position, heading,
            s_paths[player].active, ACTIVE_POINTS);
    }
    approach = race_path_tangent(s_paths[player].active, s_paths[player].active_count);
    /* A wrong turn can approach the same checkpoint from another road. Its
     * following leg must be rebuilt for that direction, not for the old path. */
    if (s_paths[player].course_target != s_paths[player].active_target ||
        road_distance(s_paths[player].course_approach, approach) >= .5176381f) changed = 1;
    if (changed) {
        unsigned routed = 0;
        RoadPoint incoming = {0, 0, 0};
        memcpy(&s_previous_path, &s_paths[player], sizeof s_previous_path);
        /* Keep the approach to the first remaining gate when its predecessor
         * is removed, so the following leg does not invent a reversal. */
        if (s_paths[player].generation == s_roads.generation)
            for (i = 0; i + 1 < s_paths[player].count; ++i)
                if (s_paths[player].checkpoints[i + 1].object == s_race[player].checkpoints[0].object) {
                    incoming = race_path_tangent(s_paths[player].course + s_paths[player].starts[i],
                                                 s_paths[player].lengths[i]);
                    break;
                }
        s_paths[player].generation = s_roads.generation;
        s_paths[player].count = s_race[player].count;
        s_paths[player].course_target = s_paths[player].active_target;
        s_paths[player].course_approach = approach;
        memcpy(s_paths[player].checkpoints, s_race[player].checkpoints,
               s_race[player].count * sizeof(RaceCheckpoint));
        memset(s_paths[player].lengths, 0, sizeof s_paths[player].lengths);
        s_paths[player].course_count = 0;
        for (i = 0; i + 1 < s_race[player].count; ++i) {
            unsigned next = i + 1, start = s_paths[player].course_count;
            unsigned count = 0, old, capacity = COURSE_POINTS - start;
            if (i == s_race[player].current && s_paths[player].active_count)
                incoming = race_path_tangent(s_paths[player].active, s_paths[player].active_count);
            for (old = 0; s_previous_path.generation == s_roads.generation &&
                 old + 1 < s_previous_path.count; ++old) {
                RaceCheckpoint *a = &s_previous_path.checkpoints[old];
                RaceCheckpoint *b = &s_previous_path.checkpoints[old + 1];
                /* A leg depends on its arrival direction as well as its two
                 * gates. Reuse only identical inputs; a changed arrival can
                 * reroute a prefix until the downstream directions converge. */
                if (a->object != s_race[player].checkpoints[i].object ||
                    b->object != s_race[player].checkpoints[next].object ||
                    a->position.x != s_race[player].checkpoints[i].position.x ||
                    a->position.y != s_race[player].checkpoints[i].position.y ||
                    a->position.z != s_race[player].checkpoints[i].position.z ||
                    b->position.x != s_race[player].checkpoints[next].position.x ||
                    b->position.y != s_race[player].checkpoints[next].position.y ||
                    b->position.z != s_race[player].checkpoints[next].position.z ||
                    incoming.x != s_previous_path.incoming[old].x ||
                    incoming.y != s_previous_path.incoming[old].y ||
                    incoming.z != s_previous_path.incoming[old].z) continue;
                /* A failed route may have run out of point space. Retry if
                 * the available capacity changed; successful routes remain
                 * valid with at least their original search capacity. */
                if (capacity < s_previous_path.capacities[old] ||
                    (!s_previous_path.lengths[old] && capacity != s_previous_path.capacities[old])) continue;
                count = s_previous_path.lengths[old];
                memcpy(s_paths[player].course + start,
                       s_previous_path.course + s_previous_path.starts[old], count * sizeof(RoadPoint));
                ++s_paths[player].course_reused;
                break;
            }
            if (old + 1 >= s_previous_path.count || s_previous_path.generation != s_roads.generation) {
                count = road_path_direction(s_race[player].checkpoints[i].position,
                    s_race[player].checkpoints[next].position, incoming,
                    s_paths[player].course + start, capacity);
                ++s_paths[player].course_rebuilt;
            }
            s_paths[player].incoming[i] = incoming; s_paths[player].capacities[i] = capacity;
            s_paths[player].starts[i] = start; s_paths[player].lengths[i] = count;
            s_paths[player].course_count += count;
            incoming = race_path_tangent(s_paths[player].course + start, count);
            if (count) ++routed;
        }
        fprintf(stderr, "[RACE_ROUTE] player=%u checkpoints=%u routed_legs=%u/%u course_points=%u rebuilt_legs=%u reused_legs=%u\n",
                player, s_race[player].count, routed,
                s_race[player].count - 1, s_paths[player].course_count,
                s_paths[player].course_rebuilt, s_paths[player].course_reused);
    }
}

extern void sub_001C1585(void);

/* Untextured native UI quads discard source UVs while packing their GPU
 * vertices. Carry this explicit route identity into unused packed UV0, then
 * the renderer consumes it and uploads the original zero UVs. This survives
 * queued command processing and batches shared with ordinary HUD quads. */
enum { ROUTE_TAG_U = 23117, ROUTE_TAG_V = -17493 };

uint32_t mm3_race_route_quad_tagged(uint32_t texture, uint32_t quad)
{
    unsigned i;
    if (texture || !quad) return 0;
    for (i = 0; i < 4; ++i) {
        uint32_t vertex = quad + 0xC + i * 0x24;
        if (MEMF(vertex + 0xC) != (float)ROUTE_TAG_U ||
            MEMF(vertex + 0x10) != (float)ROUTE_TAG_V) return 0;
    }
    return 1;
}

void mm3_race_route_tag_packed(uint32_t vertex)
{
    MEM16(vertex + 8) = (uint16_t)(int16_t)ROUTE_TAG_U;
    MEM16(vertex + 0xA) = (uint16_t)(int16_t)ROUTE_TAG_V;
}

static float road_draw_half_width(float width, float factor)
{
    float result;
    const float minimum = 1.0f;
    if (!isfinite(factor) || factor <= 0.0f) return minimum;
    /* Use the same world-to-map scale as the street centers. Retain a 2px
     * minimum before the fixed driving camera enlarges it; zooming out at
     * speed must not turn narrow streets into subpixel gaps. Wider roads
     * retain their physical lane widths and the common marker projection. */
    result = road_width_valid(width) * factor * .5f;
    return isfinite(result) ? fmaxf(minimum, result) : minimum;
}

static float road_map_half_width(float width, float factor, unsigned path)
{
    /* Cartographic widths keep parallel lanes and broad avenues from merging
     * into solid gray blocks. This affects only the background map artwork;
     * the copied lane widths and blue guidance retain their native geometry. */
    if (path) return fminf(.45f, fmaxf(.25f, width * factor * .25f));
    return fminf(2.4f, .70f * road_draw_half_width(width, factor));
}

static float road_map_alpha(const RoadLine *line, float factor, unsigned path)
{
    float detail, extent;
    if (!path) {
        float hierarchy = fminf(1, fmaxf(0, (line->width - 3.5f) / 16.0f));
        return .66f + .14f * hierarchy;
    }
    /* DC's optional park/plaza network is context, not another street layer.
     * Fade its fine details continuously with zoom and omit paths too short
     * to read. Keep the full cached network so zooming back in restores it. */
    detail = fminf(1, fmaxf(0, (factor - .10f) / .30f));
    extent = fmaxf(line->max_x - line->min_x, line->max_z - line->min_z) * factor;
    extent = fminf(1, fmaxf(0, (extent - 2.0f) / 4.0f));
    return .32f * detail * detail * (3 - 2 * detail) *
        extent * extent * (3 - 2 * extent);
}

static void route_quad(uint32_t renderer, uint32_t clip, const float xy[4][2], float alpha)
{
    uint32_t saved_eax, saved_ecx, saved_edx, saved_esp, saved_ebx, saved_esi, saved_edi;
    uint32_t saved_ebp, saved_seh_ebp, quad;
    double saved_fp[8];
    int saved_top, saved_cmp, saved_df;
    uint16_t saved_cc, saved_control;
    unsigned i;
    const int improved = mm3_minimap_improved();
    saved_eax = g_eax; saved_ecx = g_ecx; saved_edx = g_edx; saved_esp = g_esp;
    saved_ebx = g_ebx; saved_esi = g_esi; saved_edi = g_edi;
    saved_ebp = g_ebp; saved_seh_ebp = g_seh_ebp;
    saved_top = g_fp_top; saved_cmp = g_fp_cmp; saved_df = g_df;
    saved_cc = g_fp_cc; saved_control = g_fp_control_word;
    memcpy(saved_fp, g_fp_stack, sizeof saved_fp);
    /* Same 0xA0 native quad structure as 2DC95; scratch belongs to this
     * synchronous submission and is below the live guest stack frame. */
    g_esp -= 0xA0; quad = g_esp;
    memset((void *)XBOX_PTR(quad), 0, 0xA0);
    MEM32(quad + 4) = 7; MEM32(quad + 8) = 8;
    for (i = 0; i < 4; ++i) {
        uint32_t vertex = quad + 0xC + i * 0x24;
        MEMF(vertex) = xy[i][0]; MEMF(vertex + 4) = xy[i][1];
        MEMF(vertex + 0xC) = (float)ROUTE_TAG_U;
        MEMF(vertex + 0x10) = (float)ROUTE_TAG_V;
        MEMF(vertex + 0x14) = s_road_layer ? .62f : (improved ? .12f : 1.0f);
        MEMF(vertex + 0x18) = s_road_layer ? .65f : (improved ? .58f : .72f);
        MEMF(vertex + 0x1C) = s_road_layer ? .69f : (improved ? 1.0f : .15f);
        MEMF(vertex + 0x20) = alpha;
    }
    PUSH32(g_esp, quad); PUSH32(g_esp, 0);
    g_eax = clip; g_edx = renderer;
    PUSH32(g_esp, 0x0011CB45u);
    RECOMP_ABI_CALL(0x001C1585u, sub_001C1585);
    g_eax = saved_eax; g_ecx = saved_ecx; g_edx = saved_edx; g_esp = saved_esp;
    g_ebx = saved_ebx; g_esi = saved_esi; g_edi = saved_edi;
    g_ebp = saved_ebp; g_seh_ebp = saved_seh_ebp;
    g_fp_top = saved_top; g_fp_cmp = saved_cmp; g_df = saved_df;
    g_fp_cc = saved_cc; g_fp_control_word = saved_control;
    memcpy(g_fp_stack, saved_fp, sizeof saved_fp);
}

static void route_segment(uint32_t renderer, uint32_t clip,
                           float ax, float ay, float bx, float by, float alpha, float thickness)
{
    float dx = bx - ax, dy = by - ay, length = hypotf(dx, dy), px, py;
    float xy[4][2];
    if (!isfinite(length) || length < .01f || !isfinite(thickness) || thickness <= 0) return;
    px = -dy * thickness / length; py = dx * thickness / length;
    xy[0][0] = ax + px; xy[0][1] = ay + py;
    xy[1][0] = bx + px; xy[1][1] = by + py;
    xy[2][0] = bx - px; xy[2][1] = by - py;
    xy[3][0] = ax - px; xy[3][1] = ay - py;
    route_quad(renderer, clip, xy, alpha);
}

typedef struct { float x, y; } RoutePoint;
static struct {
    RoutePoint points[COURSE_POINTS];
    unsigned char keep[COURSE_POINTS];
    struct { unsigned first, last; } ranges[COURSE_POINTS];
} s_route_draw;

static void route_simplify(unsigned count)
{
    unsigned pending = 1, inspected = 0;
    memset(s_route_draw.keep, 0, count);
    s_route_draw.keep[0] = s_route_draw.keep[count - 1] = 1;
    s_route_draw.ranges[0].first = 0; s_route_draw.ranges[0].last = count - 1;
    while (pending) {
        unsigned first = s_route_draw.ranges[--pending].first;
        unsigned last = s_route_draw.ranges[pending].last, i, furthest = first;
        RoutePoint a = s_route_draw.points[first], b = s_route_draw.points[last];
        double dx = (double)b.x - a.x, dy = (double)b.y - a.y;
        double length2 = dx*dx + dy*dy, best = .25 * .25;
        for (i = first + 1; i < last; ++i) {
            double x = (double)s_route_draw.points[i].x - a.x;
            double y = (double)s_route_draw.points[i].y - a.y;
            double t = length2 ? (x*dx + y*dy) / length2 : 0, distance;
            /* RDP can be quadratic on a pathological zigzag. Bound the
             * extra CPU work, then safely draw all original samples. */
            if (++inspected > count * 64) {
                memset(s_route_draw.keep, 1, count); return;
            }
            if (t < 0) t = 0; if (t > 1) t = 1;
            x -= t*dx; y -= t*dy; distance = x*x + y*y;
            if (distance > best) { best = distance; furthest = i; }
        }
        if (furthest == first) continue;
        s_route_draw.keep[furthest] = 1;
        s_route_draw.ranges[pending].first = first;
        s_route_draw.ranges[pending++].last = furthest;
        s_route_draw.ranges[pending].first = furthest;
        s_route_draw.ranges[pending++].last = last;
    }
}

static int route_clip_axis(double origin, double delta, double extent,
                           double *lo, double *hi)
{
    double first, last;
    if (delta == 0) return origin >= -extent && origin <= extent;
    first = (-extent - origin) / delta;
    last = (extent - origin) / delta;
    if (first > last) { double swap = first; first = last; last = swap; }
    if (first > *lo) *lo = first;
    if (last < *hi) *hi = last;
    return *lo < *hi;
}

static void route_polyline_join(uint32_t renderer, uint32_t clip,
                              unsigned before, unsigned pivot, unsigned after,
                              float radius_x, float radius_y, float cx, float cy,
                              float alpha, float half_width)
{
    RoutePoint a = s_route_draw.points[before], b = s_route_draw.points[pivot];
    RoutePoint c = s_route_draw.points[after];
    float dx0 = b.x - a.x, dy0 = b.y - a.y, dx1 = c.x - b.x, dy1 = c.y - b.y;
    float length0 = hypotf(dx0, dy0), length1 = hypotf(dx1, dy1), cross, side;
    float xy[4][2];
    if (!isfinite(half_width) || half_width <= 0 ||
        fabsf(b.x) > radius_x + half_width || fabsf(b.y) > radius_y + half_width ||
        !isfinite(length0) || !isfinite(length1) ||
        length0 < .01f || length1 < .01f) return;
    dx0 /= length0; dy0 /= length0; dx1 /= length1; dy1 /= length1;
    cross = dx0 * dy1 - dy0 * dx1;
    if (!isfinite(cross) || fabsf(cross) < .000001f) return;
    side = cross > 0 ? -half_width : half_width;
    {
        float angle = atan2f(cross, dx0 * dx1 + dy0 * dy1);
        float start_x, start_y, finish_x, finish_y;
        unsigned step, steps = (unsigned)fminf(32.0f, ceilf(fabsf(angle) *
            fmaxf(1.2732395447f, sqrtf(half_width / .8f))));
        /* Both streets and blue guidance need round exterior elbows. A
         * single bevel triangle visibly cuts the corner off wider roads.
         * Scale the arc samples with width: r*(1-cos(step/2)) <= r*step^2/8
         * bounds the chord error to .1 map pixel, with at most 45 degrees
         * per triangle. The 32-triangle ceiling bounds malformed huge widths.
         * Fill only the missing exterior sector, avoiding a full translucent
         * circle over the two street segments and preserving the road width. */
        if (cross > 0) {
            start_x = -dy1 * side; start_y = dx1 * side;
            finish_x = -dy0 * side; finish_y = dx0 * side;
            angle = -angle;
        } else {
            start_x = -dy0 * side; start_y = dx0 * side;
            finish_x = -dy1 * side; finish_y = dx1 * side;
        }
        xy[0][0] = xy[3][0] = cx + b.x; xy[0][1] = xy[3][1] = cy + b.y;
        xy[1][0] = cx + b.x + start_x; xy[1][1] = cy + b.y + start_y;
        for (step = 1; step <= steps; ++step) {
            float arc = angle * (float)step / (float)steps;
            float cosine = cosf(arc), sine = sinf(arc);
            xy[2][0] = cx + b.x + (step == steps ? finish_x : start_x*cosine - start_y*sine);
            xy[2][1] = cy + b.y + (step == steps ? finish_y : start_x*sine + start_y*cosine);
            route_quad(renderer, clip, xy, alpha);
            xy[1][0] = xy[2][0]; xy[1][1] = xy[2][1];
        }
    }
}

static void route_polyline_width(uint32_t renderer, uint32_t clip, const RoadPoint *points,
                           unsigned count, RoadPoint origin, float factor,
                            float cosine, float sine, float radius_x, float radius_y,
                            float cx, float cy, float alpha, float half_width)
{
    unsigned i, previous = 0, before = NO_NODE, first_after = NO_NODE;
    if (count < 2 || count > COURSE_POINTS) return;
    for (i = 0; i < count; ++i) {
        float x = -(points[i].x - origin.x) * factor, y = (points[i].z - origin.z) * factor;
        s_route_draw.points[i].x = x*cosine + y*sine;
        s_route_draw.points[i].y = y*cosine - x*sine;
        if (!isfinite(s_route_draw.points[i].x) || !isfinite(s_route_draw.points[i].y)) return;
    }
    /* The graph retains its 6m samples for accurate routing. Submit only
     * screen-space bends: every removed point is within 0.25 pixel of the
     * retained segment, including backtracking and coincident endpoints.
     * Iterative RDP uses bounded host scratch instead of recursive frames. */
    route_simplify(count);
    for (i = 1; i < count; ++i) {
        float ax, ay, bx, by;
        double dx, dy, lo = 0, hi = 1;
        if (!s_route_draw.keep[i]) continue;
        ax = s_route_draw.points[previous].x; ay = s_route_draw.points[previous].y;
        bx = s_route_draw.points[i].x; by = s_route_draw.points[i].y;
        dx = (double)bx - ax; dy = (double)by - ay;
        /* Native zero-length links can repeat a junction sample. Keep the
         * last real tangent so such a sample cannot interrupt its join. */
        if (dx*dx + dy*dy < .0001f) continue;
        if (before != NO_NODE)
            route_polyline_join(renderer, clip, before, previous, i,
                                radius_x, radius_y, cx, cy, alpha, half_width);
        if (first_after == NO_NODE) first_after = i;
        before = previous;
        previous = i;
        /* Clip in the already rotated screen axes so corners remain useful
         * at every heading. The native inset still leaves the route clear of
         * the faded edge. Slab intervals retain outside-to-outside crossings
         * without quadratic cancellation on very long coalesced streets. */
        if (dx*dx + dy*dy < .0001f ||
            !route_clip_axis(ax, dx, radius_x + (s_road_layer ? half_width : 0), &lo, &hi) ||
            !route_clip_axis(ay, dy, radius_y + (s_road_layer ? half_width : 0), &lo, &hi)) continue;
        route_segment(renderer, clip, (float)((double)cx + ax + lo*dx), (float)((double)cy + ay + lo*dy),
                      (float)((double)cx + ax + hi*dx), (float)((double)cy + ay + hi*dy), alpha, half_width);
    }
    /* Cache loops repeat their actual endpoint. Join that seam only; nearby
     * endpoints in different street chains never acquire invented links. */
    if (before != NO_NODE && first_after != NO_NODE &&
        points[0].x == points[count - 1].x && points[0].y == points[count - 1].y &&
        points[0].z == points[count - 1].z)
        route_polyline_join(renderer, clip, before, count - 1, first_after,
                           radius_x, radius_y, cx, cy, alpha, half_width);
}

static void route_polyline(uint32_t renderer, uint32_t clip, const RoadPoint *points,
                           unsigned count, RoadPoint origin, float factor,
                           float cosine, float sine, float radius_x, float radius_y,
                           float cx, float cy, float alpha)
{
    route_polyline_width(renderer, clip, points, count, origin, factor, cosine, sine,
                         radius_x, radius_y, cx, cy, alpha, s_road_layer ? .85f : 1.25f);
}

void mm3_race_map_route(uint32_t node, uint32_t table,
                         uint32_t renderer, uint32_t clip, uint32_t layout)
{
    unsigned player, i, layer;
    int improved = mm3_minimap_improved();
    RoadPoint origin;
    float scale, width, angle, cosine, sine, radius_x, radius_y, cx, cy, factor;
    if (!node || !renderer || !layout || table < 0x407F78 ||
        table >= 0x407F78 + 0x10 * RACE_PLAYERS || ((table - 0x407F78) & 15)) return;
    player = (table - 0x407F78) / 0x10;
    if (!s_roads.edge_count) return;
    if (s_race[player].valid) origin = s_race[player].origin;
    else if (!improved || !mm3_minimap_origin(node, &origin.x)) return;
    if (!road_point_valid(origin)) return;
    scale = MEMF(node + 0x50); width = MEMF(node + 0x58) - MEMF(node + 0x54);
    angle = MEMF(node + 0x4C); cx = MEMF(node + 0x94); cy = MEMF(node + 0x98);
    radius_x = MEMF(layout + 8) * .5f - MEMF(node + 0xB4) * 8.0f;
    radius_y = MEMF(layout + 12) * .5f - MEMF(node + 0xB4) * 8.0f;
    if (!isfinite(scale) || scale <= 0 || !isfinite(width) || width <= 0 ||
        !isfinite(angle) || !isfinite(cx) || !isfinite(cy) ||
        !isfinite(radius_x) || radius_x <= 0 || !isfinite(radius_y) || radius_y <= 0) return;
    factor = scale * 1024.0f / width; cosine = cosf(angle); sine = sinf(angle);
    if (!isfinite(factor) || factor <= 0) return;
    if (improved) {
        float reach = hypotf(radius_x, radius_y) / factor;
        s_road_layer = 1;
        for (layer = 0; layer < 2; ++layer) {
            RoadPoint *points;
            RoadLine *lines;
            unsigned count;
            unsigned path = !layer;
            /* Put subdued paths underneath streets, including at crossings. */
            if (!path) {
                if (!road_lines_build()) continue;
                points = s_road_lines.points; lines = s_road_lines.lines; count = s_road_lines.count;
            } else {
                points = s_map_areas.points; lines = s_map_areas.lines; count = s_map_areas.count;
            }
            for (i = 0; i < count; ++i) {
                RoadLine *line = lines + i;
                float alpha = road_map_alpha(line, factor, path);
                float half_width = road_map_half_width(line->width, factor, path);
                float line_reach = reach + half_width / factor;
                if (alpha < .035f) continue;
                if (line->max_x < origin.x - line_reach || line->min_x > origin.x + line_reach ||
                    line->max_z < origin.z - line_reach || line->min_z > origin.z + line_reach) continue;
                route_polyline_width(renderer, clip, points + line->first, line->count,
                    origin, factor, cosine, sine, radius_x, radius_y, cx, cy, alpha,
                    half_width);
            }
        }
        s_road_layer = 0;
    }
    /* Launcher Mods "Route navigation line on the map" (MM3_ROUTE_NAVIGATION):
     * off keeps the street map but draws no course or car-to-target route. */
    if (!s_race[player].valid || !mm3_mod_route_navigation()) return;
    {
        float player_angle;
        RoadPoint heading = {0, 0, 0};
        /* The enlarged map may face north; use the player's live HUD heading
         * rather than treating the map camera orientation as car direction. */
        if (mm3_minimap_heading(node, &player_angle) && isfinite(player_angle)) {
            heading.x = -sinf(player_angle); heading.z = -cosf(player_angle);
        }
        race_paths_update(player, heading);
    }
    for (i = 0; i + 1 < s_race[player].count; ++i) {
        /* Keep the complete pending course connected. The native target can
         * be a late gate in a free-order race, and the brighter car-to-target
         * reroute can approach it from another street. Replacing its incoming
         * course leg would leave a gap when that target becomes active. */
        route_polyline(renderer, clip, s_paths[player].course + s_paths[player].starts[i],
                       s_paths[player].lengths[i], s_race[player].origin, factor,
                       cosine, sine, radius_x, radius_y, cx, cy, .8f);
    }
    route_polyline(renderer, clip, s_paths[player].active, s_paths[player].active_count,
                   s_race[player].origin, factor, cosine, sine, radius_x, radius_y, cx, cy, .98f);
}
