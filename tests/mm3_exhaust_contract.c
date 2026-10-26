/* Exhaust smoke (src/mm3_exhaust.c): a car is matched to its model by the
 * lamps it draws and smokes from that model's pipes; puffs move on their own,
 * grow and fade; the packet it hands the renderer. On a stand-in car body
 * and camera, with the real model table. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_exhaust.h"
#include "mm3_exhaust_models.h"
#include <video/nv2a_d3d11.h>

ptrdiff_t g_xbox_mem_offset;
static unsigned char memory[0x420000];
static uint32_t published_seq, published_count;
static NvWorldVertex published[NV2A_WORLD_TRIS_MAX];
enum { BODY = 0x100000, OTHER = 0x101000, CAMERA = 0x200000, TIME_OF_DAY = 0x407C30, PUFF = 24 };
static float model_rows[3][3], model_origin[3];

void nv2a_d3d_world_tris_publish(uint32_t seq, const NvWorldVertex *v, uint32_t count)
{
    assert(count && count % PUFF == 0 && count <= NV2A_WORLD_TRIS_MAX);
    published_seq = seq;
    published_count = count;
    memcpy(published, v, count * sizeof *v);
}

/* The car body: rows right, up, forward (yawed by `yaw` about up), at x, z. */
static void place(uint32_t body, float x, float z, float vz, float yaw, float up_y)
{
    float c = cosf(yaw), s = sinf(yaw);
    float rows[12] = {c, 0, -s, 0, up_y, 0, s, 0, c, x, 0.5f, z};
    memset(memory + body, 0, 0x1C4);
    MEMF(body + 0x20) = s * vz;
    MEMF(body + 0x28) = c * vz;
    memcpy(memory + body + 0xA4, rows, sizeof rows);
    memcpy(model_rows, rows, sizeof model_rows);
    memcpy(model_origin, rows + 9, sizeof model_origin);
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

static int model_named(const char *name)
{
    int m;
    for (m = 0; m < (int)(sizeof mm3_exhaust_models / sizeof mm3_exhaust_models[0]); ++m)
        if (!strcmp(mm3_exhaust_models[m].name, name)) return m;
    assert(!"model missing");
    return -1;
}

static void to_world(const float p[3], int translate, float out[3])
{
    unsigned b;
    for (b = 0; b < 3; ++b)
        out[b] = p[0] * model_rows[0][b] + p[1] * model_rows[1][b] + p[2] * model_rows[2][b] +
                 (translate ? model_origin[b] : 0.0f);
}

/* The car draws each lamp of model `m`, as the game does: W = R M. */
static void draw_lamps(uint32_t owner, int m)
{
    const Mm3ExhaustModel *model = &mm3_exhaust_models[m];
    unsigned i, a, b, k;
    for (i = 0; i < model->lamps; ++i) {
        const Mm3ExhaustLamp *lamp = &mm3_exhaust_lamps[model->lamp + i];
        float w[12];
        for (a = 0; a < 3; ++a)
            for (b = 0; b < 3; ++b) {
                w[a * 3 + b] = 0;
                for (k = 0; k < 3; ++k) w[a * 3 + b] += lamp->m[a * 3 + k] * model_rows[k][b];
            }
        to_world(lamp->m + 9, 1, w + 9);
        mm3_exhaust_lamp(owner, lamp->kind, w);
        mm3_exhaust_lamp(owner, lamp->kind, w);   /* a second pass: one entry */
    }
}

static uint32_t packet(uint32_t words[18])
{
    published_count = 0;
    if (!mm3_exhaust_packet(CAMERA, words)) return 0;
    assert(words[0] == published_seq);
    return published_count / PUFF;
}

static void steps(uint32_t car, uint32_t body, int count)
{
    while (count--) {
        MEMF(body + 0xC8) += MEMF(body + 0x20) / 60.0f;
        MEMF(body + 0xD0) += MEMF(body + 0x28) / 60.0f;
        model_origin[0] = MEMF(body + 0xC8); model_origin[2] = MEMF(body + 0xD0);
        mm3_exhaust_step(car, body, 1.0f / 60.0f);
    }
}

/* Puff `n`'s centre (the first vertex of each of its triangles). */
static const NvWorldVertex *centre(uint32_t n) { return &published[n * PUFF]; }

static float nearest_pipe(int m, const NvWorldVertex *v)
{
    const Mm3ExhaustModel *model = &mm3_exhaust_models[m];
    float best = 1e9f;
    unsigned i;
    for (i = 0; i < model->pipes; ++i) {
        float w[3], d;
        to_world(mm3_exhaust_pipes[model->pipe + i].p, 1, w);
        d = sqrtf((w[0] - v->x) * (w[0] - v->x) + (w[1] - v->y) * (w[1] - v->y) + (w[2] - v->z) * (w[2] - v->z));
        if (d < best) best = d;
    }
    return best;
}

static float mean_z(void)
{
    double sum = 0;
    uint32_t n, puffs = published_count / PUFF;
    for (n = 0; n < puffs; ++n) sum += centre(n)->z;
    return (float)(sum / puffs);
}

int main(int argc, char **argv)
{
    uint32_t words[18], puffs, n, idle, used[4] = {0};
    int corvette, sedan;
    float view[12];
    g_xbox_mem_offset = (ptrdiff_t)memory;
    camera();
    corvette = model_named("Corvette");
    sedan = model_named("Ambient_Sedan");
    assert(mm3_exhaust_models[corvette].pipes == 4);
    place(BODY, 0, 0, 0, 0.3f, 1);
    if (argc > 1 && !strcmp(argv[1], "off")) {
        draw_lamps(0x7000, corvette);
        steps(0x5000, BODY, 60);
        assert(!mm3_exhaust_enabled() && !packet(words) && !published_count);
        puts("PASS exhaust smoke off: no smoke, no packet (MM3_EXHAUST_TRIANGLES unset)");
        return 0;
    }
    assert(mm3_exhaust_enabled());
    /* The first packet says where the camera is; the drawn lamps make the
     * car a Corvette (its four authored ExhaustSmoke pipes). */
    steps(0x5000, BODY, 1);
    draw_lamps(0x7000, corvette);
    assert(!packet(words));
    /* Fresh smoke leaves the Corvette's own pipes. */
    steps(0x5000, BODY, 15);
    puffs = packet(words);
    assert(puffs >= 1);
    {
        float closest = 1e9f;
        for (n = 0; n < puffs; ++n)
            if (nearest_pipe(corvette, centre(n)) < closest) closest = nearest_pipe(corvette, centre(n));
        assert(closest < 0.12f);
    }
    for (n = 0; n < puffs; ++n) {
        const Mm3ExhaustModel *model = &mm3_exhaust_models[corvette];
        unsigned i;
        assert(nearest_pipe(corvette, centre(n)) < 0.6f);
        for (i = 0; i < model->pipes; ++i) {
            float w[3];
            to_world(mm3_exhaust_pipes[model->pipe + i].p, 1, w);
            if (fabsf(w[0] - centre(n)->x) + fabsf(w[2] - centre(n)->z) < 0.15f) used[i] = 1;
        }
        /* A soft disc: opaque-ish centre, clear rim, light grey by day. */
        assert(centre(n)->rgba >> 24 > 0 && published[n * PUFF + 1].rgba >> 24 == 0);
        assert((centre(n)->rgba & 0xFF) >= 170 && ((centre(n)->rgba >> 8) & 0xFF) == (centre(n)->rgba & 0xFF));
    }
    memcpy(view, words + 2, sizeof view);
    assert(view[0] == 1 && view[4] == 1 && view[8] == 1 && view[10] == -1.5f && view[11] == 10);
    assert(*(float *)&words[1] == 1.0f / 1000.0f && *(float *)&words[14] == 1.5f && *(float *)&words[15] == 2.0f);
    /* Traffic drawn in the next lane, with no simulated body here, does not
     * lend the Corvette its pipe (the user's double exhaust, 2026-10-26). */
    {
        float rows[3][3], origin[3];
        memcpy(rows, model_rows, sizeof rows);
        memcpy(origin, model_origin, sizeof origin);
        for (n = 0; n < 3; ++n) model_origin[n] += 2.6f * model_rows[0][n];
        draw_lamps(0x7400, sedan);
        memcpy(model_rows, rows, sizeof rows);
        memcpy(model_origin, origin, sizeof origin);
        draw_lamps(0x7000, corvette);
        packet(words);
        steps(0x5000, BODY, 30);
        puffs = packet(words);
        assert(puffs >= 1);
        for (n = 0; n < puffs; ++n) assert(nearest_pipe(corvette, centre(n)) < 0.6f);
    }
    /* A car standing still: a light haze, ~9 puffs a second, ~1.3-2.2 s each. */
    draw_lamps(0x7000, corvette);
    steps(0x5000, BODY, 120);
    idle = packet(words);
    assert(idle >= 10 && idle <= 30);
    assert(used[0] + used[1] + used[2] + used[3] >= 2);   /* more than one pipe */
    /* Densest at the pipe: a puff keeps its smoke as it spreads, so the
     * spread-out ones further off are fainter. */
    {
        float near_sum = 0, far_sum = 0;
        unsigned near_n = 0, far_n = 0;
        for (n = 0; n < idle; ++n) {
            float d = nearest_pipe(corvette, centre(n)), a = (float)(centre(n)->rgba >> 24);
            if (d < 0.2f) { near_sum += a; ++near_n; }
            else if (d > 0.45f) { far_sum += a; ++far_n; }
        }
        assert(near_n && far_n && near_sum / near_n > 2.0f * far_sum / far_n);
    }
    /* Paused: no steps, the same smoke. */
    assert(packet(words) == idle);
    /* Accelerating to 30 m/s: much more, left behind the car in a trail. */
    for (n = 0; n < 60; ++n) {
        place(BODY, MEMF(BODY + 0xC8), MEMF(BODY + 0xD0), 0.5f * (float)n, 0.3f, 1);
        draw_lamps(0x7000, corvette);
        steps(0x5000, BODY, 1);
        packet(words);
    }
    puffs = packet(words);
    assert(puffs > idle * 2);
    assert(mean_z() < MEMF(BODY + 0xD0) - 3.0f);
    /* More puffs keep the trail whole, not thicker: the trail a few metres
     * back is a faint haze. */
    for (n = 0; n < puffs; ++n)
        if (centre(n)->z < MEMF(BODY + 0xD0) - 4.0f) assert((centre(n)->rgba >> 24) < 40);
    /* Out of range it stops; the smoke out there keeps its own path, then
     * thins out to nothing. */
    place(BODY, 0, 900, 30, 0.3f, 1);
    steps(0x5000, BODY, 30);
    puffs = packet(words);
    assert(puffs > 0 && mean_z() < 60.0f);
    steps(0x5000, BODY, 150);
    assert(!packet(words));
    /* Traffic without exhaust parts: its pipe from the body's rear-bottom. */
    place(OTHER, 4, 0, 0, 0, 1);
    steps(0x6000, OTHER, 1);
    draw_lamps(0x7100, sedan);
    packet(words);
    steps(0x6000, OTHER, 30);
    puffs = packet(words);
    assert(puffs >= 1 && nearest_pipe(sedan, centre(puffs - 1)) < 0.25f);
    /* At night it is dimmer. */
    MEM32(TIME_OF_DAY) = 3;
    packet(words);
    assert((centre(0)->rgba & 0xFF) < 100);
    MEM32(TIME_OF_DAY) = 0;
    /* A car whose lamps fit no model smokes from a typical pipe. */
    {
        float odd[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, -4, 3, 1};
        place(BODY, -4, 0, 0, 0, 1);
        steps(0x7000, BODY, 1);
        mm3_exhaust_lamp(0x7200, 1, odd);
        odd[9] = -3.1f;
        mm3_exhaust_lamp(0x7200, 1, odd);
        packet(words);
        steps(0x7000, BODY, 30);
        puffs = packet(words);
        assert(puffs >= 1);
    }
    /* A rolled car does not smoke. */
    place(BODY, -8, 0, 0, 0, -1);
    steps(0x7300, BODY, 120);
    /* A camera that is not a perspective view sends nothing. */
    MEMF(CAMERA + 0x1F10) = 0;
    assert(!packet(words));
    MEMF(CAMERA + 0x1F10) = 1000.0f; MEMF(CAMERA + 0x1F20) = 5.0f;
    assert(!packet(words));
    camera();
    for (n = 0; n < published_count / PUFF; ++n) assert(centre(n)->x > -7.0f);
    /* A car no longer stepped while others are leaves with its smoke. */
    Sleep(600);
    steps(0x6000, OTHER, 2);
    puffs = packet(words);
    for (n = 0; n < puffs; ++n) assert(centre(n)->x > 2.0f);
    puts("PASS exhaust smoke: cars matched to their models by their lamps (Corvette's four pipes, traffic "
         "alongside keeps its pipe, a sedan's "
         "body-placed pipe, a typical pipe for unknown lamps), soft puffs from the pipes, idle haze and "
         "speed/throttle-scaled smoke, own world-space paths, fade to nothing, pause, night, rolled and "
         "departed cars, perspective cameras only");
    return 0;
}
