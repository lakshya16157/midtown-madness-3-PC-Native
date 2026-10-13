#include <math.h>
#include "recomp_types.h"

/* The title owns these map nodes and calls the helpers on its update/
 * draw thread. Keep only identities and scalars here; no saved guest pointer
 * is followed after a scene change. 11CA25 resets a node's layout on entry. */
enum { MAP_ZOOM_SLOTS = 16 };
static struct {
    uint32_t node;
    float target, span, baseline;
} s_map_zoom[MAP_ZOOM_SLOTS];
static unsigned s_map_zoom_replacement;

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
    return empty;
}

void mm3_map_zoom_reset(uint32_t node)
{
    unsigned slot;
    if (!node) return;
    slot = map_zoom_slot(node);
    s_map_zoom[slot].target = s_map_zoom[slot].span = 1.0f;
    s_map_zoom[slot].baseline = 0.0f;
}

/* Native 11DB57 rotates the road geometry. Keep its square axis aligned and
 * retain only its scalars until the immediately following native UI packing
 * call. 116535 generates city UVs itself, and computes mask UVs from the screen
 * clip rectangle; its source quad's UV fields are not read. */
static RECOMP_TLS struct {
    uint32_t quad;
    float x0, y0, width, height, cx, cy, c, s;
} s_square_map;

void mm3_map_square_geometry(uint32_t vertices, uint32_t center, float angle)
{
    uint32_t first = vertices + 0x0Cu;
    float x0 = MEMF(first), y0 = MEMF(first + 4);
    float width = MEMF(first + 0x24u) - x0;
    float height = MEMF(first + 0x6Cu + 4) - y0;
    float cx = MEMF(center), cy = MEMF(center + 4);
    s_square_map.quad = 0;
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
    uint32_t match = texture && quad && quad == s_square_map.quad;
    s_square_map.quad = 0; /* Never classify a reused stack allocation. */
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
    if (!(rx > 0.0f) || !(ry > 0.0f) || !isfinite(angle)) return hypotf(x, y);
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
    if (!node || !player_record) return;
    slot = map_zoom_slot(node);
    /* 218844 takes absolute forward velocity, multiplies by 3.6 (3860F8)
     * and stores integer km/h at +4. This same per-player HUD record supplies
     * varPl1Speed/varPl2Speed, independent of the displayed speed units. */
    speed = fabsf((float)SMEM32(player_record + 4));
    t = (speed - 20.0f) / 100.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    s_map_zoom[slot].target = 1.0f + 0.25f * t * t * (3.0f - 2.0f * t);
    scale = MEMF(node + 0x50);
    /* 11FE40 fits stock zoom to checkpoint proximity. That fit can zoom in
     * while accelerating and cancel the speed zoom. Preserve this layout's
     * initial framing (including native split-screen scaling), then hold it
     * as the speed curve's baseline. Roads and 11CAE8's marker positions all
     * read +50; the window calculations remain native. Checkpoint icon
     * dimensions use the same span through mm3_map_zoom_checkpoint_scale. */
    if (isfinite(scale) && scale > 0.0f) {
        if (!s_map_zoom[slot].baseline) s_map_zoom[slot].baseline = scale;
        MEMF(node + 0x50) = s_map_zoom[slot].baseline / s_map_zoom[slot].span;
    }
}

float mm3_map_zoom_checkpoint_scale(uint32_t node, uint32_t flags)
{
    unsigned slot, style = flags & 0x1Fu;
    const float icon_scale = 0.75f;
    /* 11CAE8 styles 0 and 3 use Checkpoint; 1/2/4 use You and 5 uses
     * TagMagIcon. Scale the checkpoint's pulse-adjusted half-size before
     * native quad creation, rotation and translation, keeping its centre.
     * Use a smaller base size even before the speed zoom starts. */
    if (!node || (style != 0 && style != 3)) return 1.0f;
    slot = map_zoom_slot(node);
    if (!s_map_zoom[slot].baseline) return icon_scale;
    return icon_scale / s_map_zoom[slot].span;
}
