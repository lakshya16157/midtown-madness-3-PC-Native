/* Slipstream drafting and the roll guard (src/mm3_vehicle_dynamics.c), run on
 * the car integrate hook against a fake guest: interface+8 car, car+0x3C
 * simulation, simulation+4 body with velocity, angular velocity/momentum and
 * transform at the native offsets. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static unsigned char memory[0x400000];
static unsigned integrated;
static const uint32_t stack = 0xFF00;

void sub_001D7441_original(void) { ++integrated; g_esp += 8; }
/* The exhaust smoke is off here (tests/mm3_exhaust_regression.py). */
int mm3_exhaust_enabled(void) { return 0; }
void mm3_exhaust_step(uint32_t car, uint32_t body, float dt) { (void)car; (void)body; (void)dt; }
void sub_001D7441(void);
void mm3_vehicle_dynamics_player(uint32_t player);

/* Car k: interface, car, simulation and body in their own 0x1000 block. */
static uint32_t iface(unsigned k) { return 0x100000u + k * 0x4000u; }
static uint32_t body(unsigned k) { return iface(k) + 0x3000u; }

static void make_car(unsigned k, float x, float z, float vx, float vz)
{
    uint32_t i = iface(k), car = i + 0x1000u, sim = i + 0x2000u, b = body(k);
    memset((void *)XBOX_PTR(i), 0, 0x4000);
    MEM32(i + 8) = car; MEM32(car + 0x3C) = sim; MEM32(sim + 4) = b;
    MEMF(b + 0x14) = 1.0f / 1500.0f;
    MEMF(b + 0x20) = vx; MEMF(b + 0x28) = vz;
    /* Upright, heading along +z: right +x, up +y, forward +z. */
    MEMF(b + 0xA4) = 1.0f; MEMF(b + 0xB4) = 1.0f; MEMF(b + 0xC4) = 1.0f;
    MEMF(b + 0xC8) = x; MEMF(b + 0xCC) = 0.5f; MEMF(b + 0xD0) = z;
    MEMF(b + 0x98) = 1.0f / 2500.0f; MEMF(b + 0x9C) = 1.0f / 2800.0f; MEMF(b + 0xA0) = 1.0f / 900.0f;
}

static void step(unsigned k, float dt)
{
    g_esp = stack;
    MEM32(g_esp) = 0x001D7000u;
    MEMF(g_esp + 4) = dt;
    g_ecx = iface(k);
    unsigned before = integrated;
    sub_001D7441();
    assert(integrated == before + 1 && g_esp == stack + 8);
}

/* Both cars move on by one step (the hook does not integrate). */
static void move(unsigned k, float dt)
{
    MEMF(body(k) + 0xC8) += MEMF(body(k) + 0x20) * dt;
    MEMF(body(k) + 0xD0) += MEMF(body(k) + 0x28) * dt;
}

/* The follower's speed gained from the slipstream in 1 s, its own pull
 * (`own` m/s^2: engine, drag and brakes, before each step) left out. */
static float draft_gain_own(float dt, float gap, float lateral, float own)
{
    float start = 40.0f, t;
    Sleep(300);                                        /* the last run's cars go stale */
    make_car(0, 0.0f, 0.0f, 0.0f, start);              /* follower */
    make_car(1, lateral, gap, 0.0f, start);            /* car in front */
    for (t = 0.0f; t < 1.0f - dt * 0.5f; t += dt) {
        step(1, dt);
        MEMF(body(0) + 0x28) += own * dt;
        step(0, dt);
        MEMF(body(1) + 0x28) = start;                  /* the leader holds its speed */
        move(0, dt); move(1, dt);
    }
    assert(MEMF(body(1) + 0x28) == start);
    return MEMF(body(0) + 0x28) - start - own * (float)(int)(1.0f / dt + 0.5f) * dt;
}

static float draft_gain(float dt, float gap, float lateral)
{
    return draft_gain_own(dt, gap, lateral, 0.0f);
}

/* A car leaning `degrees` to its left (right side up) about +z, rolling
 * further at `rate` rad/s. */
static void lean_car(unsigned k, float degrees, float rate)
{
    uint32_t b = body(k);
    float a = degrees * 3.14159265f / 180.0f;
    make_car(k, 500.0f + k * 50.0f, 0.0f, 0.0f, 25.0f);
    /* right = (cos a, sin a, 0), up = (-sin a, cos a, 0), forward = +z. */
    MEMF(b + 0xA4) = cosf(a); MEMF(b + 0xA8) = sinf(a);
    MEMF(b + 0xB0) = -sinf(a); MEMF(b + 0xB4) = cosf(a);
    /* d(right.y)/dt = (w x right).y = w.z * right.x: positive w.z leans further. */
    MEMF(b + 0x58) = rate; MEMF(b + 0x64) = rate / MEMF(b + 0xA0);
    MEMF(b + 0x54) = 0.3f; MEMF(b + 0x60) = 0.3f / MEMF(b + 0x9C);   /* some yaw */
}

static float roll_after(float dt, float seconds)
{
    float t;
    lean_car(2, 30.0f, 2.0f);
    for (t = 0.0f; t < seconds - dt * 0.5f; t += dt) step(2, dt);
    return MEMF(body(2) + 0x58);
}

int main(int argc, char **argv)
{
    float g60, g30, side, far_gain, contact, near_gain, coast, power, hit, w, yaw;
    int t;
    uint32_t b;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    if (argc > 1 && !strcmp(argv[1], "off")) {
        /* Unset, as without the launcher: the Xbox game's handling. */
        _putenv_s("MM3_DRAFTING", "");
        _putenv_s("MM3_ROLL_GUARD", "");
        assert(draft_gain(1.0f / 60.0f, 9.0f, 0.0f) == 0.0f);
        assert(roll_after(1.0f / 60.0f, 0.5f) == 2.0f);
        puts("PASS vehicle dynamics off by default: no MM3_DRAFTING / MM3_ROLL_GUARD leaves every car as is");
        return 0;
    }
    _putenv_s("MM3_DRAFTING", "1");
    _putenv_s("MM3_ROLL_GUARD", "1");
    /* In the wake 9 m behind at 144 km/h: about 1.6 * (40 / 41.7)^2 m/s in 1 s. */
    g60 = draft_gain(1.0f / 60.0f, 9.0f, 0.0f);
    g30 = draft_gain(1.0f / 30.0f, 9.0f, 0.0f);
    assert(g60 > 1.3f && g60 < 1.7f);
    assert(fabsf(g60 - g30) < 0.05f * g60);
    side = draft_gain(1.0f / 60.0f, 9.0f, 2.5f);        /* beside the wake */
    far_gain = draft_gain(1.0f / 60.0f, 35.0f, 0.0f);   /* too far behind */
    assert(side == 0.0f && far_gain == 0.0f);
    assert(draft_gain(1.0f / 60.0f, 20.0f, 0.0f) < 0.6f * g60);   /* fading with distance */
    /* Bumper to bumper (centres 5 m apart): no wake to sit in, so nothing
     * presses the car into the one in front; 7 m: part of it. */
    contact = draft_gain(1.0f / 60.0f, 5.0f, 0.0f);
    near_gain = draft_gain(1.0f / 60.0f, 7.0f, 0.0f);
    assert(contact == 0.0f);
    assert(near_gain > 0.05f && near_gain < 0.6f * g60);
    /* Lifted off (drag and engine braking, -0.8 m/s^2) or braking: no tow,
     * which kept a coasting car pressed against the car in front. Under
     * power (+1 m/s^2) the full slipstream. */
    coast = draft_gain_own(1.0f / 60.0f, 9.0f, 0.0f, -0.8f);
    power = draft_gain_own(1.0f / 60.0f, 9.0f, 0.0f, 1.0f);
    assert(fabsf(coast) < 0.02f);
    assert(fabsf(draft_gain_own(1.0f / 60.0f, 9.0f, 0.0f, -6.0f)) < 0.02f);
    assert(power > 1.2f && power < 1.7f);
    assert(fabsf(draft_gain_own(1.0f / 30.0f, 9.0f, 0.0f, -0.8f)) < 0.02f);
    /* Just hit (a sudden 4 m/s sideways change): no slipstream for a second. */
    make_car(4, 0.0f, 0.0f, 0.0f, 40.0f);
    make_car(5, 0.0f, 9.0f, 0.0f, 40.0f);
    for (t = 0; t < 3; ++t) { step(5, 1.0f / 60.0f); step(4, 1.0f / 60.0f); }
    assert(MEMF(body(4) + 0x28) > 40.0f);              /* drafting by then */
    MEMF(body(4) + 0x20) = 4.0f;
    hit = MEMF(body(4) + 0x28);
    for (t = 0; t < 3; ++t) { step(5, 1.0f / 60.0f); step(4, 1.0f / 60.0f); }
    assert(MEMF(body(4) + 0x28) == hit);
    printf("PASS drafting: +%.2f m/s per second at 60 fps, +%.2f at 30 fps, %.2f 7 m back, none touching, "
           "beside, 35 m back, coasting (%.3f), braking or just hit; %.2f under power\n",
           g60, g30, near_gain, coast, power);

    /* Roll guard: the lean stops growing, only about the forward axis, and the
     * momentum still gives the angular velocity. */
    w = roll_after(1.0f / 60.0f, 0.25f);
    b = body(2);
    yaw = MEMF(b + 0x54);
    assert(w < 0.5f);
    assert(yaw == 0.3f && MEMF(b + 0x50) == 0.0f);
    assert(fabsf(MEMF(b + 0x58) - MEMF(b + 0xA0) * MEMF(b + 0x64)) < 1e-4f);
    assert(fabsf(roll_after(1.0f / 30.0f, 0.25f) - w) < 0.2f);
    /* Under 20 degrees, nothing changes. */
    lean_car(2, 15.0f, 2.0f);
    step(2, 1.0f / 60.0f);
    assert(MEMF(body(2) + 0x58) == 2.0f);
    /* Standing on a 25 degree side slope, not rolling: nothing changes. */
    lean_car(2, 25.0f, 0.0f);
    MEMF(body(2) + 0x54) = 0.0f; MEMF(body(2) + 0x60) = 0.0f;
    step(2, 1.0f / 60.0f);
    assert(MEMF(body(2) + 0x58) == 0.0f);
    /* Leaning back already: nothing changes. */
    lean_car(2, 30.0f, -2.0f);
    step(2, 1.0f / 60.0f);
    assert(MEMF(body(2) + 0x58) == -2.0f);
    /* After a hit (a sudden 4 m/s sideways change) the guard stands down. */
    lean_car(3, 30.0f, 2.0f);
    step(3, 1.0f / 60.0f);
    MEMF(body(3) + 0x58) = 2.0f; MEMF(body(3) + 0x64) = 2.0f / MEMF(body(3) + 0xA0);
    MEMF(body(3) + 0x20) = 4.0f;
    step(3, 1.0f / 60.0f);
    assert(MEMF(body(3) + 0x58) == 2.0f);
    Sleep(1100);
    MEMF(body(3) + 0x20) = 4.0f;
    step(3, 1.0f / 60.0f);
    step(3, 1.0f / 60.0f);
    assert(MEMF(body(3) + 0x58) < 2.0f);
    printf("PASS roll guard: roll rate 2.0 -> %.2f rad/s in 0.25 s (yaw kept), off after an impact\n", w);
    mm3_vehicle_dynamics_player(0);                     /* unmapped player: ignored */
    return 0;
}
