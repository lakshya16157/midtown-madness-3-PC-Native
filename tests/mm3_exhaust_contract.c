/* Triangle exhaust (src/mm3_exhaust.c): emission, motion, fading and the
 * packet it hands the renderer, on a stand-in car body and camera. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_exhaust.h"
#include <video/nv2a_d3d11.h>

ptrdiff_t g_xbox_mem_offset;
static unsigned char memory[0x420000];
static uint32_t published_seq, published_count;
static NvWorldVertex published[NV2A_WORLD_TRIS_MAX];
enum { BODY = 0x100000, BUS = 0x101000, CAMERA = 0x200000, TIME_OF_DAY = 0x407C30 };

void nv2a_d3d_world_tris_publish(uint32_t seq, const NvWorldVertex *v, uint32_t count)
{
    assert(count && count % 3 == 0 && count <= NV2A_WORLD_TRIS_MAX);
    published_seq = seq;
    published_count = count;
    memcpy(published, v, count * sizeof *v);
}

/* An upright car heading +z (rows right, up, forward, position). */
static void place(uint32_t body, float x, float z, float vz, float up_y)
{
    float rows[12] = {1, 0, 0, 0, up_y, 0, 0, 0, 1, x, 0.5f, z};
    memset(memory + body, 0, 0x1C4);
    MEMF(body + 0x28) = vz;
    memcpy(memory + body + 0xA4, rows, sizeof rows);
}

/* Eye at (0, 1.5, -10) looking along +z: identity rotation, translation -eye.
 * The projection is scaled by the W depth scale, as the game keeps it. */
static void camera(void)
{
    float view[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, -1.5f, 10, 1}};
    memset(memory + CAMERA, 0, 0x1F60);
    MEMF(CAMERA + 0x1ED8) = 1000.0f;
    MEMF(CAMERA + 0x1EE4) = 1500.0f; MEMF(CAMERA + 0x1EF8) = 2000.0f; MEMF(CAMERA + 0x1F10) = 1000.0f;
    memcpy(memory + CAMERA + 0x1F24, view, sizeof view);
}

static uint32_t packet(uint32_t words[18])
{
    published_count = 0;
    if (!mm3_exhaust_packet(CAMERA, words)) return 0;
    assert(words[0] == published_seq);
    return published_count / 3;
}

/* Steps at 60 Hz; the body moves on at its own speed, as in play. */
static void steps(uint32_t car, uint32_t body, int count)
{
    while (count--) {
        MEMF(body + 0xD0) += MEMF(body + 0x28) / 60.0f;
        mm3_exhaust_step(car, body, 1.0f / 60.0f);
    }
}

static float mean(int axis)
{
    double sum = 0;
    uint32_t i;
    for (i = 0; i < published_count; ++i) sum += axis == 0 ? published[i].x : axis == 1 ? published[i].y : published[i].z;
    return (float)(sum / published_count);
}

static float alpha_max(void)
{
    uint32_t i, a = 0;
    for (i = 0; i < published_count; ++i) if (published[i].rgba >> 24 > a) a = published[i].rgba >> 24;
    return (float)a / 255.0f;
}

int main(int argc, char **argv)
{
    uint32_t words[18], tris, first;
    float view[12], z_before, spread_before, spread_after;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    camera();
    place(BODY, 0, 0, 0, 1);
    if (argc > 1 && !strcmp(argv[1], "off")) {
        mm3_exhaust_lamp(1, (const float *)(memory + BODY + 0xA4));
        steps(0x5000, BODY, 60);
        assert(!mm3_exhaust_enabled() && !packet(words) && !published_count);
        puts("PASS triangle exhaust off: no triangles, no packet (MM3_EXHAUST_TRIANGLES unset)");
        return 0;
    }
    assert(mm3_exhaust_enabled());
    /* Nothing yet; the first packet tells it where the camera is. */
    assert(!packet(words));
    /* A car standing still puffs a few at a time (7 a second, ~1 s each). */
    steps(0x5000, BODY, 90);
    tris = packet(words);
    assert(tris >= 4 && tris <= 13);
    memcpy(view, words + 2, sizeof view);
    assert(view[0] == 1 && view[4] == 1 && view[8] == 1 && view[10] == -1.5f && view[11] == 10);
    assert(*(float *)&words[1] == 1.0f / 1000.0f && *(float *)&words[14] == 1.5f && *(float *)&words[15] == 2.0f);
    /* Behind the car (typical back 2.2 m) and lit by day. */
    assert(mean(2) < -1.5f && mean(2) > -5.0f);
    assert((published[0].rgba & 0xFF) >= 140 && ((published[0].rgba >> 8) & 0xFF) == (published[0].rgba & 0xFF));
    /* Paused: no steps, the same triangles. */
    first = published_seq;
    z_before = mean(2);
    assert(packet(words) == tris && mean(2) == z_before && published_seq == first + 1);
    /* At 30 m/s: many more, left in a trail behind the moving car (it is
     * 30 m on after a second). */
    place(BODY, 0, 0, 30, 1);
    steps(0x5000, BODY, 60);
    tris = packet(words);
    assert(tris >= 30 && tris <= 96);
    assert(mean(2) < 30.0f - 4.0f && mean(2) > 0.0f);
    /* Out of range they stop coming, and the ones out there keep to their
     * own paths in the world, grow and fade until none is left. */
    place(BODY, 0, 500, 30, 1);
    spread_before = alpha_max();
    steps(0x5000, BODY, 20);
    tris = packet(words);
    assert(tris > 0 && mean(2) < 40.0f && alpha_max() < spread_before);
    steps(0x5000, BODY, 60);
    spread_after = (float)packet(words);
    assert(spread_after == 0);
    /* A long vehicle: its tail lamps 4.6 m back move the pipe. */
    place(BUS, 4, 0, 0, 1);
    steps(0x6000, BUS, 1);
    {
        float lamp[12] = {1, 0, 0, 0, 1, 0, 0, 0, -1, 4.8f, 1.4f, -4.6f};
        mm3_exhaust_lamp(1, lamp);
        lamp[9] = 3.2f;
        mm3_exhaust_lamp(2, lamp);
        mm3_exhaust_lamp(0, lamp);                /* a headlamp is no back */
    }
    steps(0x6000, BUS, 90);
    packet(words);
    assert(mean(2) < -4.0f && mean(2) > -8.0f && mean(0) < 4.0f);
    /* At night they are dimmer. */
    MEM32(TIME_OF_DAY) = 3;
    packet(words);
    assert((published[0].rgba & 0xFF) < 100);
    MEM32(TIME_OF_DAY) = 0;
    /* A rolled car puffs nothing. */
    place(BODY, -4, 0, 0, -1);
    steps(0x7000, BODY, 120);
    packet(words);
    assert(mean(0) > 0.0f);                       /* only the bus's, at x 4 */
    /* A camera that is not a perspective view sends nothing. */
    MEMF(CAMERA + 0x1F10) = 0;
    assert(!packet(words));
    MEMF(CAMERA + 0x1F10) = 1000.0f; MEMF(CAMERA + 0x1F20) = 5.0f;
    assert(!packet(words));
    camera();
    /* A car no longer stepped while others are leaves with its triangles. */
    Sleep(600);
    steps(0x7000, BODY, 2);
    tris = packet(words);
    assert(!tris || mean(0) < 0.0f);
    puts("PASS triangle exhaust: idle and speed-scaled emission behind the car, world-space motion, "
         "growth and fade to nothing, freeze while paused, lamp-placed pipe, night dimming, rolled cars "
         "and departed cars, perspective cameras only");
    return 0;
}
