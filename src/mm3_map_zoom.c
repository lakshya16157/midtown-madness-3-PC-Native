#include <math.h>
#include "recomp_types.h"
#include "nv2a_minimap_projection.h"
extern int mm3_minimap_improved(void);

#define MAP_SPEED_SPAN 0.60f

void mm3_map_ui_capacity(void)
{
    /* 11DCBF runs before the title packs this frame's UI. Request its own
     * large-buffer path: it frees/reallocates the shared 16-byte vertex pool
     * at that safe boundary and retains the allocation across later frames.
     * Expanded city roads otherwise exhaust the retail 5,000 vertices before
     * checkpoint, route and other HUD submissions reach the same pool. */
    if (mm3_minimap_improved() && MEM32(0x3A0EC0u) < 160000u)
        MEM8(0x3C5694u) = 1;
}

/* The title owns these map nodes and calls the helpers on its update/
 * draw thread. Keep only identities and scalars here; no saved guest pointer
 * is followed after a scene change. 11CA25 resets a node's layout on entry. */
enum { MAP_ZOOM_SLOTS = 16 };
static struct {
    uint32_t node;
    float target, span, baseline;
    float origin[3];
    unsigned origin_valid;
} s_map_zoom[MAP_ZOOM_SLOTS];
static unsigned s_map_zoom_replacement;

/* The pushbuffer thread cannot follow live map nodes. Publish each map's
 * authored centre (half-pixels) and smoothed speed in one atomic word, so
 * split-screen maps retain their own camera and reads cannot tear. */
static volatile uint32_t s_map_motion[MAP_ZOOM_SLOTS];
static volatile uint32_t s_map_heading[MAP_ZOOM_SLOTS];
static volatile uint32_t s_map_expansion[MAP_ZOOM_SLOTS];

/* Native UI geometry uses coordinates relative to the render viewport's
 * centre. Only 116535 knows the translation used by its queued vertices.
 * Carry the local window to that immediate packing boundary. */
static RECOMP_TLS struct {
    uint32_t quad, window_node;
    uint16_t camera;
    unsigned camera_valid;
    float window_cx, window_cy;
    float x0, y0, width, height, cx, cy, c, s;
} s_square_map;

static void map_scalar_store(volatile uint32_t *scalar, uint32_t value)
{
    uint32_t before = RECOMP_ATOMIC_CAS32(scalar, 0, 0);
    while (RECOMP_ATOMIC_CAS32(scalar, before, value) != before)
        before = RECOMP_ATOMIC_CAS32(scalar, 0, 0);
}

static void map_motion_store(unsigned slot, uint32_t value)
{
    map_scalar_store(&s_map_motion[slot], value);
}

static void map_heading_publish(unsigned slot, uint32_t player_record)
{
    map_scalar_store(&s_map_heading[slot], player_record ? MEM32(player_record) : 0x7FC00000u);
}

static float map_expansion(uint32_t node)
{
    float current = MEMF(node + 0xB4), maximum = MEMF(node + 0xBC), expansion;
    double fraction;
    if (!isfinite(current) || !isfinite(maximum) || maximum <= 1.0f) return 0;
    /* B4 interpolates between the small map's 1 and BC (3 normally, 2 in
     * split screen). Window dimensions can change independently of this
     * native Ctrl transition, so they cannot identify its camera state. */
    fraction = ((double)current - 1.0) / ((double)maximum - 1.0);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    expansion = (float)fraction;
    return expansion;
}

static float map_expansion_publish(unsigned slot, uint32_t node)
{
    float expansion = map_expansion(node);
    uint32_t bits;
    memcpy(&bits, &expansion, sizeof bits);
    map_scalar_store(&s_map_expansion[slot], 0x80000000u | bits);
    return expansion;
}

static unsigned map_camera_quantize(float value)
{
    if (!isfinite(value) || value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return (unsigned)floorf(value * 255.0f + .5f);
}

static void map_motion_publish(unsigned slot, float cx, float cy)
{
    float motion = (s_map_zoom[slot].span - 1.0f) / MAP_SPEED_SPAN;
    uint32_t x, y, speed;
    unsigned i;
    if (!isfinite(cx) || !isfinite(cy) || cx < -1024 || cx > 1023.5f ||
        cy < -1024 || cy > 1023.5f) { map_motion_store(slot, 0); return; }
    if (motion < 0) motion = 0;
    if (motion > 1) motion = 1;
    x = (uint32_t)floorf((cx + 1024.0f) * 2.0f + .5f);
    y = (uint32_t)floorf((cy + 1024.0f) * 2.0f + .5f);
    speed = (uint32_t)floorf(motion * 127.0f + .5f);
    /* Scene changes can allocate a new map at an old HUD centre. Retire
     * its former snapshot instead of letting an old identity win lookup. */
    for (i = 0; i < MAP_ZOOM_SLOTS; ++i) {
        uint32_t snapshot = RECOMP_ATOMIC_CAS32(&s_map_motion[i], 0, 0);
        if (i != slot && (snapshot & 0x80000000u) &&
            fabsf(cx - ((float)(snapshot & 4095u) * .5f - 1024.0f)) <= .75f &&
            fabsf(cy - ((float)((snapshot >> 12) & 4095u) * .5f - 1024.0f)) <= .75f)
            map_motion_store(i, 0);
    }
    map_motion_store(slot, 0x80000000u | (speed << 24) | (y << 12) | x);
}

float mm3_minimap_motion(float cx, float cy)
{
    unsigned i;
    if (!isfinite(cx) || !isfinite(cy)) return 0;
    for (i = 0; i < MAP_ZOOM_SLOTS; ++i) {
        uint32_t snapshot = RECOMP_ATOMIC_CAS32(&s_map_motion[i], 0, 0);
        if (!(snapshot & 0x80000000u)) continue;
        if (fabsf(cx - ((float)(snapshot & 4095u) * .5f - 1024.0f)) <= .75f &&
            fabsf(cy - ((float)((snapshot >> 12) & 4095u) * .5f - 1024.0f)) <= .75f)
            return (float)((snapshot >> 24) & 127u) / 127.0f;
    }
    return 0;
}

float mm3_minimap_expansion(float cx, float cy)
{
    unsigned i;
    if (!isfinite(cx) || !isfinite(cy)) return 0;
    for (i = 0; i < MAP_ZOOM_SLOTS; ++i) {
        uint32_t window = RECOMP_ATOMIC_CAS32(&s_map_motion[i], 0, 0);
        uint32_t bits;
        float expansion;
        if (!(window & 0x80000000u) ||
            fabsf(cx - ((float)(window & 4095u) * .5f - 1024.0f)) > .75f ||
            fabsf(cy - ((float)((window >> 12) & 4095u) * .5f - 1024.0f)) > .75f)
            continue;
        bits = RECOMP_ATOMIC_CAS32(&s_map_expansion[i], 0, 0);
        if (!(bits & 0x80000000u)) return 0;
        bits &= 0x7FFFFFFFu;
        memcpy(&expansion, &bits, sizeof expansion);
        return isfinite(expansion) && expansion >= 0.0f && expansion <= 1.0f ? expansion : 0;
    }
    return 0;
}

static unsigned map_zoom_slot(uint32_t node)
{
    unsigned i, empty = MAP_ZOOM_SLOTS;
    for (i = 0; i < MAP_ZOOM_SLOTS; ++i) {
        if (s_map_zoom[i].node == node) return i;
        if (!s_map_zoom[i].node && empty == MAP_ZOOM_SLOTS) empty = i;
    }
    if (empty == MAP_ZOOM_SLOTS)
        empty = s_map_zoom_replacement++ % MAP_ZOOM_SLOTS;
    s_map_zoom[empty].node = node;
    s_map_zoom[empty].target = s_map_zoom[empty].span = 1.0f;
    s_map_zoom[empty].baseline = 0.0f;
    s_map_zoom[empty].origin_valid = 0;
    map_motion_store(empty, 0);
    map_heading_publish(empty, 0);
    map_scalar_store(&s_map_expansion[empty], 0);
    return empty;
}

void mm3_map_zoom_reset(uint32_t node)
{
    unsigned slot;
    if (!node) return;
    slot = map_zoom_slot(node);
    s_map_zoom[slot].target = s_map_zoom[slot].span = 1.0f;
    s_map_zoom[slot].baseline = 0.0f;
    s_map_zoom[slot].origin_valid = 0;
    map_motion_store(slot, 0);
    map_heading_publish(slot, 0);
    map_scalar_store(&s_map_expansion[slot], 0);
}

int mm3_minimap_heading(uint32_t node, float *angle)
{
    unsigned i;
    if (!node || !angle) return 0;
    /* Race routing calls this on the title thread. Read the car's own
     * heading, because an expanded/compass map can stop rotating. */
    for (i = 0; i < MAP_ZOOM_SLOTS; ++i)
        if (s_map_zoom[i].node == node) {
            uint32_t bits = RECOMP_ATOMIC_CAS32(&s_map_heading[i], 0, 0);
            float heading;
            memcpy(&heading, &bits, sizeof heading);
            if (!isfinite(heading)) return 0;
            *angle = heading;
            return 1;
        }
    return 0;
}

int mm3_minimap_origin(uint32_t node, float *origin)
{
    unsigned i;
    if (!node || !origin) return 0;
    for (i = 0; i < MAP_ZOOM_SLOTS; ++i)
        if (s_map_zoom[i].node == node && s_map_zoom[i].origin_valid) {
            memcpy(origin, s_map_zoom[i].origin, sizeof s_map_zoom[i].origin);
            return 1;
        }
    return 0;
}

void mm3_map_zoom_window(uint32_t node, uint32_t clip)
{
    unsigned slot;
    float width, height;
    s_square_map.window_node = 0;
    s_square_map.camera_valid = 0;
    if (!node || !clip) return;
    slot = map_zoom_slot(node);
    width = MEMF(clip + 8); height = MEMF(clip + 12);
    if (!mm3_minimap_improved() || !s_map_zoom[slot].origin_valid ||
        !isfinite(width) || !isfinite(height) || width < 1 || height < 1) {
        map_motion_store(slot, 0);
        map_scalar_store(&s_map_expansion[slot], 0); return;
    }
    s_square_map.window_node = node;
    s_square_map.window_cx = MEMF(clip) + width * .5f;
    s_square_map.window_cy = MEMF(clip + 4) + height * .5f;
}

void mm3_map_square_publish_window(float half_width, float half_height)
{
    uint32_t node = s_square_map.window_node;
    unsigned slot;
    float expansion, motion;
    s_square_map.window_node = 0;
    if (!node) return;
    slot = map_zoom_slot(node);
    if (!isfinite(half_width) || !isfinite(half_height) ||
        half_width < 0 || half_height < 0) {
        map_motion_store(slot, 0);
        map_scalar_store(&s_map_expansion[slot], 0); return;
    }
    /* 116535 adds these same half-viewport dimensions to every vertex.
     * Publishing the local centre (-206,144 for the small map, 0,0 for
     * Ctrl's overview) cannot match the renderer's screen-space mask window.
     * It then misses expansion and tilts/crops the overview as a driving map. */
    expansion = map_expansion_publish(slot, node);
    motion = (s_map_zoom[slot].span - 1.0f) / MAP_SPEED_SPAN;
    s_square_map.camera = (uint16_t)((map_camera_quantize(motion) << 8) |
        map_camera_quantize(expansion));
    s_square_map.camera_valid = 1;
    map_motion_publish(slot, s_square_map.window_cx + half_width,
        s_square_map.window_cy + half_height);
}

void mm3_map_square_tag_packed(uint32_t vertex)
{
    if (!vertex || !s_square_map.camera_valid) return;
    /* Improved city quads establish the window but their backdrop is not
     * drawn. Carry this draw's camera in their unused packed primary UVs;
     * callbacks alone can see a newer Ctrl layout than the queued geometry.
     * Keep secondary mask UVs intact so its bounds remain reconstructible. */
    MEM16(vertex + 8) = 23213u;
    MEM16(vertex + 0xA) = s_square_map.camera;
}

/* Native 11DB57 rotates the road geometry. Keep its square axis aligned and
 * retain only its scalars until the immediately following native UI packing
 * call. 116535 generates city UVs itself, and computes mask UVs from the screen
 * clip rectangle; its source quad's UV fields are not read. */
void mm3_map_square_geometry(uint32_t vertices, uint32_t center, float angle)
{
    uint32_t first = vertices + 0x0Cu;
    float x0 = MEMF(first), y0 = MEMF(first + 4);
    float width = MEMF(first + 0x24u) - x0;
    float height = MEMF(first + 0x6Cu + 4) - y0;
    float cx = MEMF(center), cy = MEMF(center + 4);
    s_square_map.quad = 0;
    s_square_map.window_node = 0;
    s_square_map.camera_valid = 0;
    if (!isfinite(angle) || !isfinite(width) || !isfinite(height) ||
        !isfinite(cx) || !isfinite(cy) || fabsf(width) < 0.001f || fabsf(height) < 0.001f)
        return;
    s_square_map.quad = vertices;
    s_square_map.x0 = x0; s_square_map.y0 = y0;
    s_square_map.width = width; s_square_map.height = height;
    s_square_map.cx = cx; s_square_map.cy = cy;
    s_square_map.c = cosf(angle); s_square_map.s = sinf(angle);
}

uint32_t mm3_map_square_begin(uint32_t texture, uint32_t quad)
{
    uint32_t match = mm3_minimap_improved() && texture && quad && quad == s_square_map.quad;
    s_square_map.quad = 0; /* Never classify a reused stack allocation. */
    if (!match) {
        s_square_map.window_node = 0;
        s_square_map.camera_valid = 0;
    }
    return match;
}

void mm3_map_square_city_uv(uint32_t texture, uint32_t arguments)
{
    float du = MEMF(texture + 0x50), dv = MEMF(texture + 0x54);
    float u0 = MEMF(texture + 0x60), v0 = MEMF(texture + 0x64);
    float u = MEMF(arguments + 12), v = MEMF(arguments + 16);
    float dx, dy;
    if (!isfinite(du) || !isfinite(dv) || fabsf(du) < 0.000001f || fabsf(dv) < 0.000001f)
        return;
    dx = s_square_map.x0 + (u - u0) * s_square_map.width / du - s_square_map.cx;
    dy = s_square_map.y0 + (v - v0) * s_square_map.height / dv - s_square_map.cy;
    /* 000C6ADC rotates clockwise. Fixed screen geometry therefore samples
     * the city with its counterclockwise inverse. Preserve both mask UVs,
     * position, color and native packed-vertex conversion/guest ABI. */
    MEMF(arguments + 12) = u0 + (s_square_map.cx + s_square_map.c * dx -
        s_square_map.s * dy - s_square_map.x0) * du / s_square_map.width;
    MEMF(arguments + 16) = v0 + (s_square_map.cy + s_square_map.s * dx +
        s_square_map.c * dy - s_square_map.y0) * dv / s_square_map.height;
}

float mm3_map_square_marker_distance(uint32_t node, uint32_t layout, float x, float y)
{
    float inset = MEMF(node + 0xB4) * MEMF(0x385E08u);
    float rx = MEMF(layout + 8) * 0.5f - inset;
    float ry = MEMF(layout + 12) * 0.5f - inset;
    float angle = MEMF(node + 0x4C), c = cosf(angle), s = sinf(angle);
    if (!mm3_minimap_improved() || !(rx > 0.0f) || !(ry > 0.0f) || !isfinite(angle)) return hypotf(x, y);
    /* Native range/clamp runs before the final camera rotation. Measure in
     * the visible map's screen axes; returning equivalent X-radius units
     * preserves its original hide/clamp styles and pulsing-icon margin. */
    return fmaxf(fabsf(c * x + s * y), fabsf(c * y - s * x) * rx / ry);
}

void mm3_map_zoom_update(uint32_t node, float dt)
{
    unsigned slot;
    float blend;
    if (!node || !isfinite(dt) || dt <= 0.0f) return;
    if (!mm3_minimap_improved()) { mm3_map_zoom_reset(node); return; }
    /* Use simulation time, so pausing freezes the zoom. A long load/resume
     * update cannot snap it to the new target in a single frame. */
    if (dt > 0.1f) dt = 0.1f;
    slot = map_zoom_slot(node);
    blend = 1.0f - expf(-dt / 0.6f);
    s_map_zoom[slot].span +=
        (s_map_zoom[slot].target - s_map_zoom[slot].span) * blend;
}

void mm3_map_zoom_apply(uint32_t node, uint32_t player_record)
{
    unsigned slot;
    float speed, t, scale;
    if (!node) return;
    slot = map_zoom_slot(node);
    s_map_zoom[slot].origin_valid = 0;
    if (!player_record) {
        map_motion_store(slot, 0); map_heading_publish(slot, 0);
        map_scalar_store(&s_map_expansion[slot], 0); return;
    }
    if (!mm3_minimap_improved()) {
        mm3_map_zoom_reset(node);
        map_heading_publish(slot, player_record);
        return;
    }
    map_heading_publish(slot, player_record);
    /* Same absolute car position used by the native city texture window.
     * Copy it at the live draw boundary so Cruise has roads without race data. */
    s_map_zoom[slot].origin[0] = MEMF(player_record + 0x10);
    s_map_zoom[slot].origin[1] = MEMF(player_record + 0x14);
    s_map_zoom[slot].origin[2] = MEMF(player_record + 0x18);
    s_map_zoom[slot].origin_valid = isfinite(s_map_zoom[slot].origin[0]) &&
        isfinite(s_map_zoom[slot].origin[1]) && isfinite(s_map_zoom[slot].origin[2]);
    /* 218844 takes absolute forward velocity, multiplies by 3.6 (3860F8)
     * and stores integer km/h at +4. This same per-player HUD record supplies
     * varPl1Speed/varPl2Speed, independent of the displayed speed units. */
    speed = fabsf((float)SMEM32(player_record + 4));
    t = (speed - 20.0f) / 100.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    s_map_zoom[slot].target = 1.0f + MAP_SPEED_SPAN * t * t * (3.0f - 2.0f * t);
    scale = MEMF(node + 0x50);
    /* 11FE40 fits stock zoom to checkpoint proximity. That fit can zoom in
     * while accelerating and cancel the speed zoom. Preserve this layout's
     * initial framing (including native split-screen scaling), then hold it
     * as the speed curve's baseline. Roads and 11CAE8's marker positions all
     * read +50; the window calculations remain native. This changes world
     * coverage within the fixed HUD frame, not the frame or icon dimensions.
     * The renderer enlarges that frame by NV2A_MINIMAP_SCALE. Compensate the
     * world scale so a street keeps its authored size instead of inheriting
     * that enlargement. Ctrl changes the window, never this world scale:
     * restoring native scale/speed here made the expanded map zoom back in,
     * dropping streets and moving visible checkpoints to the clamp edge.
     * Keeping one world camera makes every expansion reveal more of the city
     * and preserves speed zoom throughout the transition and its reversal. */
    if (isfinite(scale) && scale > 0.0f) {
        if (!s_map_zoom[slot].baseline) s_map_zoom[slot].baseline = scale;
        MEMF(node + 0x50) = s_map_zoom[slot].baseline /
            (NV2A_MINIMAP_SCALE * s_map_zoom[slot].span);
    }
}

float mm3_map_zoom_checkpoint_scale(uint32_t node, uint32_t flags)
{
    unsigned style = flags & 0x1Fu;
    float expansion, icon_scale;
    /* 11CAE8 styles 0 and 3 use Checkpoint; 1/2/4 use You and 5 uses
     * TagMagIcon. Scale the checkpoint's pulse-adjusted half-size before
     * native quad creation, rotation and translation, keeping its centre.
     * Keep icon pixel sizes stable while world coverage zooms with speed,
     * cancelling the same renderer enlargement as the world scale. */
    if (!mm3_minimap_improved() || !node || style > 5) return 1.0f;
    expansion = map_expansion(node);
    icon_scale = 1.0f / (NV2A_MINIMAP_SCALE +
        (1.0f - NV2A_MINIMAP_SCALE) * expansion);
    return icon_scale * ((style == 0 || style == 3) ? 0.75f : 1.0f);
}

void mm3_map_zoom_compass_geometry(uint32_t node, uint32_t vertices)
{
    float scale = mm3_map_zoom_checkpoint_scale(node, 1), cx = 0, cy = 0;
    unsigned i;
    if (!vertices || scale == 1.0f) return;
    /* 11D808 draws MapNorth directly, outside 11CAE8's marker dispatch.
     * Shrink its native quad about its own centre before the
     * authored map rotation; its position, UVs, color and clip stay intact. */
    for (i = 0; i < 4; ++i) {
        cx += MEMF(vertices + 0xC + i * 0x24) * .25f;
        cy += MEMF(vertices + 0x10 + i * 0x24) * .25f;
    }
    for (i = 0; i < 4; ++i) {
        uint32_t vertex = vertices + 0xC + i * 0x24;
        MEMF(vertex) = cx + (MEMF(vertex) - cx) * scale;
        MEMF(vertex + 4) = cy + (MEMF(vertex + 4) - cy) * scale;
    }
}
