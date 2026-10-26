#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"
#include "mm3_car_lighting.h"
#include "mm3_exhaust.h"
#include <video/nv2a_d3d11.h>

/* Triangle exhaust. MM3's cars give off no exhaust of their own. With the
 * mod on, every car near the camera puffs small flat triangles from the
 * back of the car, more of them and faster the faster it goes. Each one
 * leaves the pipe backwards, partly carried along in the car's wake, is
 * slowed by the air and rises a little, moving on its own in the world; it
 * spins, grows and fades until it is gone.
 *
 * Cars and time come from the physics step (sub_001D7441: car body +0x20
 * velocity, +0xA4 rows right/up/forward, +0xC8 position), so the triangles
 * stop with the game when it pauses. Where the back of a car is comes from
 * its tail and brake lamps as they are drawn; until a car has shown them a
 * typical car's is assumed. Drawing is the renderer's (nv2a_world_tris.h):
 * the fog pass's camera sends the frame's triangles with an ordered packet,
 * and they are drawn over the finished 3D scene, behind whatever is nearer. */

#define CARS 32
#define TRIS 96                 /* per car: about 100 a second at most, ~1 s each */
#define NEAR_EYE 90.0f          /* metres: cars further off do not puff */
#define DRAW_RANGE 140.0f
#define RATE_IDLE 7.0f          /* triangles a second standing still */
#define RATE_PER_SPEED 1.8f     /* more per m/s */
#define RATE_MAX 95.0f
#define EJECT_IDLE 0.5f         /* m/s backwards from the pipe at rest */
#define EJECT_PER_SPEED 0.15f   /* faster per m/s of the car */
#define WAKE 0.7f               /* share of the car's speed the wake carries */
#define DRAG 1.8f               /* per second */
#define RISE 0.6f               /* m/s^2 */
#define STALE 0.5               /* seconds others stepped without this car */

typedef struct {
    float p[3], v[3], a[3], b[3];
    float age, life, size0, size1, spin, angle, shade;
} Tri;

typedef struct {
    uint32_t car;
    double seen;
    float axes[4][3];           /* right, up, forward, position */
    float rear, height, side;   /* the pipe in the car's frame */
    float lamp_rear, lamp_height, lamp_width;
    unsigned lamps, learned, next;
    float emit;
    Tri tri[TRIS];
} Car;

static Car s_cars[CARS];
static SRWLOCK s_lock = SRWLOCK_INIT;
static double s_last_step;
static float s_eye[3];
static int s_eye_valid;
static uint32_t s_seq, s_random = 0x9E3779B9u;
static NvWorldVertex s_vertices[NV2A_WORLD_TRIS_MAX];

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

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static int mapped(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && address < 0x08000000u && size <= 0x08000000u - address;
}

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
    free_slot->rear = -2.2f;    /* a typical car until its lamps are seen */
    free_slot->height = -0.25f;
    free_slot->side = -0.4f;
    return free_slot;
}

static void spawn(Car *c, const float velocity[3], float speed, float dt)
{
    Tri *t = &c->tri[c->next++ % TRIS];
    const float *right = c->axes[0], *up = c->axes[1], *forward = c->axes[2], *position = c->axes[3];
    float eject = EJECT_IDLE + EJECT_PER_SPEED * speed, back = random01();
    float n[3], length, lateral = (random01() - 0.5f) * 1.0f, lift = 0.2f + 0.4f * random01();
    unsigned i;
    for (i = 0; i < 3; ++i) {
        /* Spread over the way the car came this step, not one clump. */
        t->p[i] = position[i] + forward[i] * c->rear + up[i] * c->height + right[i] * c->side -
                  velocity[i] * dt * back + (random01() - 0.5f) * 0.06f;
        t->v[i] = velocity[i] * WAKE - forward[i] * eject + right[i] * lateral + up[i] * lift;
    }
    /* A random plane for the triangle: a, b orthonormal across normal n. */
    for (i = 0; i < 3; ++i) n[i] = random01() * 2.0f - 1.0f;
    length = sqrtf(dot3(n, n));
    if (length < 0.1f) { n[0] = 0; n[1] = 1; n[2] = 0; length = 1; }
    for (i = 0; i < 3; ++i) n[i] /= length;
    t->a[0] = fabsf(n[1]) < 0.9f ? n[2] : 0; t->a[1] = fabsf(n[1]) < 0.9f ? 0 : n[2];
    t->a[2] = fabsf(n[1]) < 0.9f ? -n[0] : -n[1];
    length = sqrtf(dot3(t->a, t->a));
    for (i = 0; i < 3; ++i) t->a[i] /= length;
    t->b[0] = n[1] * t->a[2] - n[2] * t->a[1];
    t->b[1] = n[2] * t->a[0] - n[0] * t->a[2];
    t->b[2] = n[0] * t->a[1] - n[1] * t->a[0];
    t->age = 0;
    t->life = 0.8f + 0.4f * random01();
    t->size0 = 0.05f + 0.03f * random01();
    t->size1 = 0.22f + 0.14f * random01() + 0.003f * speed;
    t->spin = (2.0f + 5.0f * random01()) * (random01() < 0.5f ? -1.0f : 1.0f);
    t->angle = 6.2831853f * random01();
    t->shade = 0.55f + 0.3f * random01();
}

void mm3_exhaust_step(uint32_t car, uint32_t body, float dt)
{
    float velocity[3], speed, drag;
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
    /* Lamps seen since the last step place the pipe: just behind the
     * rearmost lamp, a little below the lamps, off to one side. */
    if (c->lamps) {
        float rear = c->lamp_rear - 0.1f, height = c->lamp_height - 0.45f, side = -0.55f * c->lamp_width;
        if (!c->learned) { c->rear = rear; c->height = height; c->side = side; c->learned = 1; }
        else {
            c->rear += (rear - c->rear) * 0.2f;
            c->height += (height - c->height) * 0.2f;
            c->side += (side - c->side) * 0.2f;
        }
        c->lamps = 0;
    }
    drag = expf(-DRAG * dt);
    for (i = 0; i < TRIS; ++i) {
        Tri *t = &c->tri[i];
        if (t->age >= t->life) continue;
        t->age += dt;
        for (j = 0; j < 3; ++j) { t->v[j] *= drag; t->p[j] += t->v[j] * dt; }
        t->v[1] += RISE * dt;
        t->angle += t->spin * dt;
    }
    speed = sqrtf(dot3(velocity, velocity));
    /* Upright cars near the camera puff; a rolled car's engine stalls. */
    if (c->axes[1][1] > 0.3f && s_eye_valid) {
        float d[3] = {c->axes[3][0] - s_eye[0], c->axes[3][1] - s_eye[1], c->axes[3][2] - s_eye[2]};
        if (dot3(d, d) < NEAR_EYE * NEAR_EYE) {
            float rate = RATE_IDLE + RATE_PER_SPEED * speed;
            c->emit += (rate < RATE_MAX ? rate : RATE_MAX) * dt;
            while (c->emit >= 1.0f && spawned < 12) { spawn(c, velocity, speed, dt); c->emit -= 1.0f; ++spawned; }
            if (c->emit > 1.0f) c->emit = 1.0f;
        }
    }
    ReleaseSRWLockExclusive(&s_lock);
}

void mm3_exhaust_lamp(uint32_t kind, const float transform[12])
{
    const float *lamp = transform + 9;
    Car *best = NULL;
    float best_local[3] = {0}, best_distance = 1e30f;
    unsigned i;
    if (!mm3_exhaust_enabled() || (kind != 1 && kind != 2)) return;
    if (!isfinite(lamp[0]) || !isfinite(lamp[1]) || !isfinite(lamp[2])) return;
    AcquireSRWLockExclusive(&s_lock);
    /* The car whose back this lamp is: behind its centre, within its sides. */
    for (i = 0; i < CARS; ++i) {
        Car *c = &s_cars[i];
        float d[3], local[3], distance;
        if (!c->car) continue;
        d[0] = lamp[0] - c->axes[3][0]; d[1] = lamp[1] - c->axes[3][1]; d[2] = lamp[2] - c->axes[3][2];
        local[0] = dot3(d, c->axes[0]); local[1] = dot3(d, c->axes[1]); local[2] = dot3(d, c->axes[2]);
        if (local[2] > -0.8f || local[2] < -8.0f || fabsf(local[0]) > 1.6f || fabsf(local[1]) > 2.5f) continue;
        distance = dot3(d, d);
        if (distance < best_distance) { best_distance = distance; best = c; memcpy(best_local, local, sizeof local); }
    }
    if (best) {
        if (!best->lamps || best_local[2] < best->lamp_rear) best->lamp_rear = best_local[2];
        best->lamp_height = best->lamps ? best->lamp_height + (best_local[1] - best->lamp_height) / (float)(best->lamps + 1) : best_local[1];
        if (!best->lamps || fabsf(best_local[0]) > best->lamp_width) best->lamp_width = fabsf(best_local[0]);
        ++best->lamps;
    }
    ReleaseSRWLockExclusive(&s_lock);
}

static uint32_t pack(float shade, float alpha)
{
    uint32_t g = (uint32_t)(shade * 255.0f + 0.5f), a = (uint32_t)(alpha * 255.0f + 0.5f);
    if (g > 255u) g = 255u;
    if (a > 255u) a = 255u;
    return g | g << 8 | g << 16 | a << 24;
}

int mm3_exhaust_packet(uint32_t camera, uint32_t words[18])
{
    float view[12], proj[4], depth_scale, w_scale, eye[3], light;
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
    }
    light = mm3_car_lighting_dark_period(time_of_day) ? (time_of_day == 3u ? 0.4f : 0.6f) : 1.0f;
    AcquireSRWLockExclusive(&s_lock);
    memcpy(s_eye, eye, sizeof eye);
    s_eye_valid = 1;
    for (i = 0; i < CARS && count + 3 <= NV2A_WORLD_TRIS_MAX; ++i) {
        Car *c = &s_cars[i];
        if (!c->car) continue;
        /* Another car stepped without this one for a while: it left. */
        if (s_last_step - c->seen > STALE) { c->car = 0; continue; }
        for (j = 0; j < TRIS && count + 3 <= NV2A_WORLD_TRIS_MAX; ++j) {
            Tri *t = &c->tri[j];
            float x, size, fade, d[3];
            uint32_t colour;
            if (t->age >= t->life) continue;
            d[0] = t->p[0] - eye[0]; d[1] = t->p[1] - eye[1]; d[2] = t->p[2] - eye[2];
            if (dot3(d, d) > DRAW_RANGE * DRAW_RANGE) continue;
            x = t->age / t->life;
            size = t->size0 + (t->size1 - t->size0) * sqrtf(x);
            fade = 1.0f - x;
            colour = pack(t->shade * light, 0.7f * fade * sqrtf(fade));
            for (k = 0; k < 3; ++k) {
                float angle = t->angle + 2.0943951f * (float)k, ca = cosf(angle) * size, sa = sinf(angle) * size;
                NvWorldVertex *v = &s_vertices[count++];
                v->x = t->p[0] + t->a[0] * ca + t->b[0] * sa;
                v->y = t->p[1] + t->a[1] * ca + t->b[1] * sa;
                v->z = t->p[2] + t->a[2] * ca + t->b[2] * sa;
                v->rgba = colour;
            }
        }
    }
    ReleaseSRWLockExclusive(&s_lock);
    if (!count) return 0;
    nv2a_d3d_world_tris_publish(++s_seq, s_vertices, count);
    if (s_seq == 1)
        fprintf(stderr, "[MM3_EXHAUST] first triangles: %u, eye (%.1f, %.1f, %.1f), projection %.3f %.3f\n",
                count / 3u, eye[0], eye[1], eye[2], proj[0], proj[1]);
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
