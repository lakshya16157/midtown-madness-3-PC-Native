#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "mm3_car_lighting.h"
#include "mm3_exhaust.h"
#include "mm3_exhaust_models.h"
#include <video/nv2a_d3d11.h>

/* Exhaust smoke. MM3's cars give off no exhaust of their own, although its
 * artists placed ExhaustSmoke emitters on many models. With the mod on,
 * every car near the camera breathes soft grey smoke from its own exhaust
 * pipes: a light haze standing still, more of it, and leaving the pipe
 * faster, the faster the car goes and the harder it accelerates. Each puff
 * moves on its own in the world: partly carried in the car's wake, slowed
 * by the air, rising and swirling a little, growing and thinning out until
 * it is gone.
 *
 * Where the pipes are: tools/car-models/exhaust_points.py lists every
 * installed model's lamp nodes and pipes (mm3_exhaust_models.h). The lamps a
 * car reports as it is drawn (sub_000CF2DA: kind and world transform, rows
 * right/up/forward/position, per owner) identify its model, and one of them
 * gives the model's place in the world: world = model lamp matrix * model
 * matrix. The pipes, placed with that matrix, are kept in the frame of the
 * car's rigid body for the physics step (sub_001D7441: body +0x20 velocity,
 * +0xA4 rows, +0xC8 position), which moves and emits the puffs, so they stop
 * with the game when it pauses. A car not yet identified uses a typical pipe.
 *
 * Drawing is the renderer's (nv2a_world_tris.h): the fog pass's camera sends
 * the frame's puffs, camera-facing soft discs made of triangles, with an
 * ordered packet; they are drawn over the finished 3D scene (car shadows
 * included), fading where they meet anything nearer. */

#define CARS 32
#define PUFFS 128               /* per car */
#define OWNERS 48
#define OWNER_LAMPS 32
#define NEAR_EYE 70.0f          /* metres: cars further off do not smoke */
#define DRAW_RANGE 110.0f
#define RATE_IDLE 9.0f          /* puffs a second standing still */
#define RATE_PER_SPEED 1.6f     /* more per m/s */
#define RATE_THROTTLE 30.0f     /* more when accelerating hard */
#define RATE_MAX 110.0f
#define EXIT_IDLE 1.6f          /* m/s out of the pipe at rest */
#define EXIT_PER_SPEED 0.12f    /* faster per m/s of the car */
#define WAKE 0.55f              /* share of the car's velocity the wake carries */
#define DRAG 2.2f               /* per second */
#define RISE 0.35f              /* m/s^2 */
#define SWIRL 0.6f              /* m/s^2, random */
#define STALE 0.5               /* seconds others stepped without this car */
#define MATCH 0.035f            /* metres: an observed lamp where the model has one */
#define RING 8                  /* sides of a puff's disc */
#define PUFF_VERTICES (RING * 3)

typedef struct {
    float p[3], v[3];
    float age, life, r0, r1, alpha, shade, spin;
} Puff;

typedef struct {
    uint32_t car;
    double seen;
    float axes[4][3];           /* right, up, forward, position */
    float speed, throttle;
    float pipe[4][3], dir[4][3];/* in the body's frame */
    unsigned pipes, next;       /* pipes 0: not placed yet */
    float emit;
    Puff puff[PUFFS];
} Car;

typedef struct {
    uint32_t owner, frame, lamps, checked;
    int model;                  /* -1 unknown, -2 not found */
    struct { uint32_t kind; float m[12]; } lamp[OWNER_LAMPS];
} Owner;

static Car s_cars[CARS];
static Owner s_owners[OWNERS];
static SRWLOCK s_lock = SRWLOCK_INIT;
static double s_last_step;
static float s_eye[3];
static int s_eye_valid;
static uint32_t s_seq, s_frame = 1, s_random = 0x9E3779B9u;
static unsigned s_logged, s_unmatched_logged;
static NvWorldVertex s_vertices[NV2A_WORLD_TRIS_MAX];
static const float default_pipe[3] = {-0.4f, -0.25f, -2.2f}, default_dir[3] = {0.0f, 0.0f, -1.0f};

int mm3_exhaust_enabled(void) { return mm3_mod_exhaust_triangles(); }

static double now_seconds(void)
{
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

static float random01(void)
{
    s_random ^= s_random << 13;
    s_random ^= s_random >> 17;
    s_random ^= s_random << 5;
    return (float)(s_random >> 8) * (1.0f / 16777216.0f);
}

static float spread(void) { return random01() * 2.0f - 1.0f; }

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static int mapped(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address < 0x08000000u && size <= 0x08000000u - address;
}

/* ------------------------------------------------------------------ models */

/* The model matrix (rows M, origin t) that puts model lamp `lamp` where the
 * car drew it: W = R M for a lamp node with model rotation R, so M = R^T W,
 * and t = position - p M. */
static void place(const float model[12], const float drawn[12], float M[3][3], float t[3])
{
    unsigned a, b, k;
    for (a = 0; a < 3; ++a)
        for (b = 0; b < 3; ++b) {
            M[a][b] = 0;
            for (k = 0; k < 3; ++k) M[a][b] += model[k * 3 + a] * drawn[k * 3 + b];
        }
    for (b = 0; b < 3; ++b)
        t[b] = drawn[9 + b] - (model[9] * M[0][b] + model[10] * M[1][b] + model[11] * M[2][b]);
}

static void to_world(const float p[3], float M[3][3], const float t[3], float out[3])
{
    unsigned b;
    for (b = 0; b < 3; ++b) out[b] = p[0] * M[0][b] + p[1] * M[1][b] + p[2] * M[2][b] + (t ? t[b] : 0.0f);
}

/* How well model `m`, placed by its lamp `j` on observed lamp `a`, explains
 * the lamps the car drew: the summed miss in metres, or -1 if more than a
 * fifth of them (beacons that turn, for one) have no model lamp of their kind
 * within MATCH. */
static float fit(const Owner *o, unsigned a, int m, unsigned j, float M[3][3], float t[3])
{
    const Mm3ExhaustModel *model = &mm3_exhaust_models[m];
    float total = 0;
    unsigned i, k, misses = 0;
    place(mm3_exhaust_lamps[model->lamp + j].m, o->lamp[a].m, M, t);
    for (i = 0; i < o->lamps; ++i) {
        float best = MATCH * MATCH;
        int found = 0;
        for (k = 0; k < model->lamps; ++k) {
            const Mm3ExhaustLamp *lamp = &mm3_exhaust_lamps[model->lamp + k];
            float w[3], d[3], e;
            if (lamp->kind != o->lamp[i].kind) continue;
            to_world(lamp->m + 9, M, t, w);
            d[0] = w[0] - o->lamp[i].m[9]; d[1] = w[1] - o->lamp[i].m[10]; d[2] = w[2] - o->lamp[i].m[11];
            e = dot3(d, d);
            if (e < best) { best = e; found = 1; }
        }
        if (!found) {
            if (++misses * 5u > o->lamps) return -1.0f;
            total += MATCH;
            continue;
        }
        total += sqrtf(best);
    }
    return total;
}

/* The anchor: a tail lamp if the car drew one, else its first lamp. */
static unsigned anchor(const Owner *o)
{
    unsigned i;
    for (i = 0; i < o->lamps; ++i)
        if (o->lamp[i].kind == 1) return i;
    return 0;
}

/* Model `m`'s best placement on this car's lamps, or -1. */
static float fit_model(const Owner *o, int m, float M[3][3], float t[3])
{
    const Mm3ExhaustModel *model = &mm3_exhaust_models[m];
    unsigned a = anchor(o), j;
    float best = -1.0f, M2[3][3], t2[3];
    for (j = 0; j < model->lamps; ++j) {
        float e;
        if (mm3_exhaust_lamps[model->lamp + j].kind != o->lamp[a].kind) continue;
        e = fit(o, a, m, j, M2, t2);
        if (e >= 0 && (best < 0 || e < best)) { best = e; memcpy(M, M2, sizeof M2); memcpy(t, t2, sizeof t2); }
    }
    return best;
}

static int identify(const Owner *o, float M[3][3], float t[3], float *error)
{
    int m, best = -1;
    float least = 0, M2[3][3], t2[3];
    for (m = 0; m < (int)(sizeof mm3_exhaust_models / sizeof mm3_exhaust_models[0]); ++m) {
        float e = fit_model(o, m, M2, t2);
        if (e < 0 || (best >= 0 && e >= least)) continue;
        best = m; least = e;
        memcpy(M, M2, sizeof M2); memcpy(t, t2, sizeof t2);
    }
    *error = least;
    return best;
}

/* A drawn car's pipes, in the frame of the body it belongs to. */
static void place_pipes(const Owner *o, float M[3][3], const float t[3])
{
    const Mm3ExhaustModel *model = &mm3_exhaust_models[o->model];
    Car *car = NULL;
    float nearest = 3.5f * 3.5f;
    unsigned i, k, n = model->pipes < 4 ? model->pipes : 4;
    for (i = 0; i < CARS; ++i) {
        float d[3];
        if (!s_cars[i].car) continue;
        d[0] = s_cars[i].axes[3][0] - t[0]; d[1] = s_cars[i].axes[3][1] - t[1]; d[2] = s_cars[i].axes[3][2] - t[2];
        if (dot3(d, d) < nearest) { nearest = dot3(d, d); car = &s_cars[i]; }
    }
    if (!car) return;
    for (i = 0; i < n; ++i) {
        const Mm3ExhaustPipe *pipe = &mm3_exhaust_pipes[model->pipe + i];
        float w[3], d[3];
        to_world(pipe->p, M, t, w);
        to_world(pipe->d, M, NULL, d);
        for (k = 0; k < 3; ++k) w[k] -= car->axes[3][k];
        for (k = 0; k < 3; ++k) {
            car->pipe[i][k] = dot3(w, car->axes[k]);
            car->dir[i][k] = dot3(d, car->axes[k]);
        }
    }
    car->pipes = n;
}

/* Called with the lock held, once a frame: each car drawn since the last
 * frame gets (or keeps) its model and has its pipes placed. */
static void owners_frame(void)
{
    unsigned i;
    for (i = 0; i < OWNERS; ++i) {
        Owner *o = &s_owners[i];
        float M[3][3], t[3], error = 0;
        if (!o->owner || o->frame != s_frame || !o->lamps) continue;
        /* Keep a known model while it still fits; look again otherwise
         * (an owner address reused by another car). */
        if (o->model >= 0 && fit_model(o, o->model, M, t) < 0) o->model = -1;
        if (o->model == -1 || (o->model == -2 && s_frame - o->checked >= 120u)) {
            int found = identify(o, M, t, &error);
            o->checked = s_frame;
            o->model = found >= 0 ? found : -2;
            if (found >= 0 && s_logged < 24u) {
                ++s_logged;
                fprintf(stderr, "[MM3_EXHAUST] car %08X is %s (%u lamps, %.1f cm off), %u pipe(s)\n",
                        o->owner, mm3_exhaust_models[found].name, o->lamps, error * 100.0f,
                        mm3_exhaust_models[found].pipes);
            } else if (found < 0 && s_unmatched_logged < 8u) {
                ++s_unmatched_logged;
                fprintf(stderr, "[MM3_EXHAUST] no model for car %08X (%u lamps); typical pipe\n",
                        o->owner, o->lamps);
            }
        }
        if (o->model >= 0) place_pipes(o, M, t);
    }
}

void mm3_exhaust_lamp(uint32_t owner, uint32_t kind, const float transform[12])
{
    Owner *o = NULL, *oldest = NULL;
    unsigned i;
    if (!mm3_exhaust_enabled() || !owner || kind > 9) return;
    for (i = 0; i < 12; ++i) if (!isfinite(transform[i])) return;
    AcquireSRWLockExclusive(&s_lock);
    for (i = 0; i < OWNERS && !o; ++i)
        if (s_owners[i].owner == owner) o = &s_owners[i];
    if (!o) {
        for (i = 0; i < OWNERS; ++i)
            if (!oldest || s_owners[i].frame < oldest->frame) oldest = &s_owners[i];
        o = oldest;
        memset(o, 0, sizeof *o);
        o->owner = owner;
        o->model = -1;
    }
    if (o->frame != s_frame) { o->frame = s_frame; o->lamps = 0; }
    /* A car is drawn in more than one pass a frame: one entry per lamp. */
    for (i = 0; i < o->lamps; ++i) {
        float d[3] = {o->lamp[i].m[9] - transform[9], o->lamp[i].m[10] - transform[10], o->lamp[i].m[11] - transform[11]};
        if (o->lamp[i].kind == kind && dot3(d, d) < 1e-6f) break;
    }
    if (i == o->lamps && o->lamps < OWNER_LAMPS) {
        o->lamp[o->lamps].kind = kind;
        memcpy(o->lamp[o->lamps].m, transform, sizeof o->lamp[0].m);
        ++o->lamps;
    }
    ReleaseSRWLockExclusive(&s_lock);
}

/* ------------------------------------------------------------- simulation */

static Car *car_slot(uint32_t car, double now)
{
    Car *free_slot = NULL, *oldest = &s_cars[0];
    unsigned i;
    for (i = 0; i < CARS; ++i) {
        if (s_cars[i].car == car) return &s_cars[i];
        if (!s_cars[i].car && !free_slot) free_slot = &s_cars[i];
        if (s_cars[i].seen < oldest->seen) oldest = &s_cars[i];
    }
    if (!free_slot) free_slot = oldest;
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->car = car;
    free_slot->seen = now;
    return free_slot;
}

static void spawn(Car *c, unsigned pipe, const float velocity[3], float dt)
{
    Puff *p = &c->puff[c->next++ % PUFFS];
    const float *local = c->pipes ? c->pipe[pipe] : default_pipe, *aim = c->pipes ? c->dir[pipe] : default_dir;
    float exit = (EXIT_IDLE + EXIT_PER_SPEED * c->speed) * (0.8f + 0.4f * random01()), back = random01();
    float dir[3], length;
    unsigned k;
    for (k = 0; k < 3; ++k)
        dir[k] = aim[0] * c->axes[0][k] + aim[1] * c->axes[1][k] + aim[2] * c->axes[2][k];
    length = sqrtf(dot3(dir, dir));
    if (!(length > 0.1f)) { for (k = 0; k < 3; ++k) dir[k] = -c->axes[2][k]; length = 1.0f; }
    for (k = 0; k < 3; ++k) {
        /* Spread over the way the pipe came this step, not one clump. */
        p->p[k] = c->axes[3][k] + local[0] * c->axes[0][k] + local[1] * c->axes[1][k] + local[2] * c->axes[2][k] +
                  spread() * 0.015f - velocity[k] * dt * back;
        p->v[k] = velocity[k] * WAKE + dir[k] / length * exit + spread() * 0.25f;
    }
    p->age = 0;
    p->life = 1.3f + 0.9f * random01();
    p->r0 = 0.04f + 0.02f * random01();
    p->r1 = 0.5f + 0.35f * random01() + 0.3f * c->throttle;
    p->alpha = 0.2f + 0.22f * c->throttle + 0.04f * random01();
    p->shade = 0.70f + 0.12f * random01();
    p->spin = 6.2831853f * random01();
}

void mm3_exhaust_step(uint32_t car, uint32_t body, float dt)
{
    float velocity[3], speed, drag, accel;
    double now;
    Car *c;
    unsigned i, j, spawned = 0;
    if (!mm3_exhaust_enabled() || !(dt > 0.0f) || dt > 0.25f || !mapped(body, 0xD4u)) return;
    for (i = 0; i < 3; ++i) velocity[i] = MEMF(body + 0x20u + i * 4u);
    if (!isfinite(velocity[0]) || !isfinite(velocity[1]) || !isfinite(velocity[2])) return;
    now = now_seconds();
    AcquireSRWLockExclusive(&s_lock);
    s_last_step = now;
    c = car_slot(car, now);
    c->seen = now;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j) c->axes[i][j] = MEMF(body + 0xA4u + i * 12u + j * 4u);
    speed = sqrtf(dot3(velocity, velocity));
    /* Throttle, as the car's smoothed gain in speed: hard acceleration
     * (about 4 m/s^2 and more) blows the most smoke. */
    accel = (speed - c->speed) / dt;
    c->throttle += ((accel > 0 ? fminf(accel / 4.0f, 1.0f) : 0.0f) - c->throttle) * fminf(dt * 4.0f, 1.0f);
    c->speed = speed;
    drag = expf(-DRAG * dt);
    for (i = 0; i < PUFFS; ++i) {
        Puff *p = &c->puff[i];
        if (p->age >= p->life) continue;
        p->age += dt;
        for (j = 0; j < 3; ++j) {
            p->v[j] = p->v[j] * drag + spread() * SWIRL * dt;
            p->p[j] += p->v[j] * dt;
        }
        p->v[1] += RISE * dt;
    }
    /* Upright cars near the camera smoke; a rolled car's engine stalls. */
    if (c->axes[1][1] > 0.3f && s_eye_valid) {
        float d[3] = {c->axes[3][0] - s_eye[0], c->axes[3][1] - s_eye[1], c->axes[3][2] - s_eye[2]};
        if (dot3(d, d) < NEAR_EYE * NEAR_EYE) {
            float rate = RATE_IDLE + RATE_PER_SPEED * speed + RATE_THROTTLE * c->throttle;
            unsigned pipes = c->pipes ? c->pipes : 1u;
            c->emit += (rate < RATE_MAX ? rate : RATE_MAX) * dt;
            while (c->emit >= 1.0f && spawned < 16) {
                spawn(c, c->next % pipes, velocity, dt);
                c->emit -= 1.0f;
                ++spawned;
            }
            if (c->emit > 1.0f) c->emit = 1.0f;
        }
    }
    ReleaseSRWLockExclusive(&s_lock);
}

/* ---------------------------------------------------------------- drawing */

static uint32_t pack(float shade, float alpha)
{
    uint32_t g = (uint32_t)(shade * 255.0f + 0.5f), a = (uint32_t)(alpha * 255.0f + 0.5f);
    if (g > 255u) g = 255u;
    if (a > 255u) a = 255u;
    return g | g << 8 | g << 16 | a << 24;
}

int mm3_exhaust_packet(uint32_t camera, uint32_t words[18])
{
    float view[12], proj[4], depth_scale, w_scale, eye[3], light, right[3], up[3];
    uint32_t count = 0, time_of_day = MEM32(0x00407C30u), i, j, k;
    if (!mm3_exhaust_enabled() || !mapped(camera, 0x1F60u)) return 0;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j) view[i * 3 + j] = MEMF(camera + 0x1F24u + i * 16u + j * 4u);
    /* The camera's projection (+0x1EE4) comes scaled by its W-buffer depth
     * scale (+0x1ED8, also its _34: w = view z * scale); _44 is 0 for a
     * perspective view. In play: _11 55124, _22 97998, _34 44162. */
    depth_scale = MEMF(camera + 0x1ED8u);
    w_scale = MEMF(camera + 0x1F10u);
    if (!isfinite(depth_scale) || depth_scale <= 0 || !isfinite(w_scale) || w_scale <= 0 ||
        fabsf(MEMF(camera + 0x1F20u)) > 1e-3f * w_scale) return 0;
    proj[0] = MEMF(camera + 0x1EE4u) / w_scale; proj[1] = MEMF(camera + 0x1EF8u) / w_scale;
    proj[2] = MEMF(camera + 0x1F04u) / w_scale; proj[3] = MEMF(camera + 0x1F08u) / w_scale;
    for (i = 0; i < 4; ++i) if (!isfinite(proj[i])) return 0;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            float d = dot3(view + i * 3, view + j * 3);
            if (!isfinite(d) || fabsf(d - (i == j ? 1.0f : 0.0f)) > 0.01f) return 0;
        }
    for (i = 0; i < 3; ++i) {
        eye[i] = -dot3(view + i * 3, view + 9);
        if (!isfinite(eye[i])) return 0;
        /* The camera's axes in the world: its right and up columns. */
        right[i] = view[i * 3];
        up[i] = view[i * 3 + 1];
    }
    light = mm3_car_lighting_dark_period(time_of_day) ? (time_of_day == 3u ? 0.35f : 0.6f) : 1.0f;
    AcquireSRWLockExclusive(&s_lock);
    memcpy(s_eye, eye, sizeof eye);
    s_eye_valid = 1;
    owners_frame();
    ++s_frame;
    for (i = 0; i < CARS && count + PUFF_VERTICES <= NV2A_WORLD_TRIS_MAX; ++i) {
        Car *c = &s_cars[i];
        if (!c->car) continue;
        /* Another car stepped without this one for a while: it left. */
        if (s_last_step - c->seen > STALE) { c->car = 0; continue; }
        for (j = 0; j < PUFFS && count + PUFF_VERTICES <= NV2A_WORLD_TRIS_MAX; ++j) {
            Puff *p = &c->puff[j];
            float x, r, a, d[3], reach, ring[RING][3];
            uint32_t centre, edge;
            if (p->age >= p->life) continue;
            d[0] = p->p[0] - eye[0]; d[1] = p->p[1] - eye[1]; d[2] = p->p[2] - eye[2];
            reach = dot3(d, d);
            if (reach > DRAW_RANGE * DRAW_RANGE) continue;
            x = p->age / p->life;
            /* Grows fast as it leaves the pipe, then spreads slowly; thins
             * out as it grows. */
            r = p->r0 + (p->r1 - p->r0) * (1.0f - expf(-3.0f * x)) / (1.0f - expf(-3.0f));
            a = p->alpha * fminf(p->age / 0.06f, 1.0f) * (1.0f - x) * (1.0f - x);
            a *= 1.0f - fminf(sqrtf(reach) / DRAW_RANGE, 1.0f);
            if (a < 0.004f) continue;
            centre = pack(p->shade * light, a);
            edge = pack(p->shade * light, 0.0f);
            for (k = 0; k < RING; ++k) {
                float angle = p->spin + 6.2831853f * (float)k / (float)RING;
                float ca = cosf(angle) * r, sa = sinf(angle) * r;
                ring[k][0] = p->p[0] + right[0] * ca + up[0] * sa;
                ring[k][1] = p->p[1] + right[1] * ca + up[1] * sa;
                ring[k][2] = p->p[2] + right[2] * ca + up[2] * sa;
            }
            for (k = 0; k < RING; ++k) {
                NvWorldVertex *v = &s_vertices[count];
                v[0].x = p->p[0]; v[0].y = p->p[1]; v[0].z = p->p[2]; v[0].rgba = centre;
                v[1].x = ring[k][0]; v[1].y = ring[k][1]; v[1].z = ring[k][2]; v[1].rgba = edge;
                v[2].x = ring[(k + 1) % RING][0]; v[2].y = ring[(k + 1) % RING][1];
                v[2].z = ring[(k + 1) % RING][2]; v[2].rgba = edge;
                count += 3;
            }
        }
    }
    ReleaseSRWLockExclusive(&s_lock);
    if (!count) return 0;
    nv2a_d3d_world_tris_publish(++s_seq, s_vertices, count);
    if (s_seq == 1)
        fprintf(stderr, "[MM3_EXHAUST] first smoke: %u puffs, eye (%.1f, %.1f, %.1f), projection %.3f %.3f\n",
                count / PUFF_VERTICES, eye[0], eye[1], eye[2], proj[0], proj[1]);
    words[0] = s_seq;
    {
        float values[17];
        values[0] = 1.0f / depth_scale;
        memcpy(values + 1, view, sizeof view);
        memcpy(values + 13, proj, sizeof proj);
        memcpy(words + 1, values, sizeof values);
    }
    return 1;
}
