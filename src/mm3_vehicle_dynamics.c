#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "recomp_types.h"

/* Slipstream drafting and the roll guard, for every car on MM3's car physics.
 *
 * Each physics step the world calls two methods of a car's vehicle interface
 * (vtable 0x383768): +0x10 update (1D7425 -> 1D4794(dt): damage, forces and
 * 1D2C3A's wheel/body dynamics) and +0x14 integrate (1D7441 -> 1D47F4(car,
 * dt)). This hook runs at the start of integrate, once this step's forces are
 * in the body. Interface+8 is the car, car+0x3C its simulation and
 * simulation+4 the rigid body:
 *   body+0x14  inverse mass
 *   body+0x20  velocity (1D2C3A adds force * inverse mass * dt)
 *   body+0x50  angular velocity = body+0x98.. * body+0x5C.. per world axis
 *   body+0x5C  angular momentum (1D2C3A adds torque * dt)
 *   body+0xA4  3x4 transform: rows right, up, forward, then position (+0xC8)
 * Both features change only the velocities, by amounts scaled with the step's
 * dt, so they act the same at 30 and 60 fps.
 *
 * Drafting: a car close behind another, in its wake, has less air to push
 * and pulls a little harder: up to MM3_DRAFT_ACCEL m/s^2 (default 1.6) along
 * its direction of travel at 150 km/h and above, less at lower speed, in a
 * wake that fades from 10 to 30 m behind the car in front and widens with
 * distance. Players and AI opponents alike. MM3_DRAFTING=0 turns it off.
 *
 * Roll guard: some original cars rolled over in hard corners. Once a car on
 * its wheels leans past 20 degrees, the part of its roll rate that leans it
 * further is damped, more strongly the further it leans; past 30 degrees it
 * is also eased back towards level. A car standing still on a side slope is
 * left alone, and pitch and yaw are untouched. The guard stands down for
 * a second after an impact (a sudden horizontal speed change), in the air
 * (fast vertical motion), near upside down and when nearly stopped, so crashes
 * still tumble cars. MM3_ROLL_GUARD=0 turns it off.
 *
 * Logs: [MM3_DRAFT] each 30 s with drafting time per car, [MM3_ROLL] when the
 * guard catches a car leaning past 28 degrees (a few a minute). */
extern void sub_001D7441_original(void);

#define CARS 32
#define STALE_SECONDS 0.25
#define DRAFT_NEAR 10.0f            /* full slipstream up to this far behind */
#define DRAFT_FAR 30.0f             /* none beyond */
#define DRAFT_MIN_GAP 1.5f
#define DRAFT_SPEED 41.7f           /* m/s (150 km/h): full strength */
#define DRAFT_MIN_SPEED 12.0f       /* m/s: none below about 43 km/h */
#define ROLL_START 0.342f           /* sin 20 degrees */
#define ROLL_FULL 0.707f            /* sin 45 degrees: strongest guard */
#define ROLL_RESTORE_START 0.5f     /* sin 30 degrees: eased back from here */
#define ROLL_LOG 0.469f             /* sin 28 degrees */
#define ROLL_RESTORE 1.0f           /* per second, towards level at full lean */
#define ROLL_DAMP_MIN 3.0f          /* per second */
#define ROLL_DAMP_MAX 30.0f
#define IMPACT_ACCEL 45.0f          /* m/s^2 horizontal: a hit, not driving */
#define IMPACT_HOLD 1.0
#define REPORT_SECONDS 30.0
#define ROLL_LINES 400u

typedef struct {
    uint32_t car, player;
    double seen, impact, roll_logged;
    float position[3], velocity[3];
    float draft_seconds, draft_gain;
    int has_velocity;
} Car;

static Car s_cars[CARS];
static double s_report_at;
static unsigned s_roll_lines;

static int flag(const char *name)
{
    const char *v = getenv(name);
    return !(v && !strcmp(v, "0"));
}

static int drafting_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = flag("MM3_DRAFTING");
    return enabled;
}

static int roll_guard_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = flag("MM3_ROLL_GUARD");
    return enabled;
}

static float draft_accel(void)
{
    static float accel = -1.0f;
    if (accel < 0.0f) {
        const char *v = getenv("MM3_DRAFT_ACCEL");
        double value = v ? atof(v) : 0.0;
        accel = value > 0.0 && value <= 10.0 ? (float)value : 1.6f;
    }
    return accel;
}

static double now_seconds(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    return 0.0;
#endif
}

/* Car objects live in committed guest heap; the last good range is
 * remembered (game thread only). */
static int mapped(uint32_t va, uint32_t size)
{
#ifdef _WIN32
    static uintptr_t good_lo, good_hi;
    static ptrdiff_t good_offset;
    MEMORY_BASIC_INFORMATION info;
    uintptr_t p;
    if (va < 0x10000u || va > 0x07FFFFFFu || size > 0x08000000u - va) return 0;
    p = XBOX_PTR(va);
    if (good_offset == g_xbox_mem_offset && p >= good_lo && p + size <= good_hi) return 1;
    if (!VirtualQuery((void *)p, &info, sizeof info) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    good_lo = (uintptr_t)info.BaseAddress;
    good_hi = good_lo + info.RegionSize;
    good_offset = g_xbox_mem_offset;
    return p + size <= good_hi;
#else
    return va >= 0x10000u && va + size <= 0x400000u;
#endif
}

static Car *car_slot(uint32_t car, double now)
{
    unsigned i, slot = CARS;
    double oldest = now;
    for (i = 0; i < CARS; ++i) {
        if (s_cars[i].car == car) return &s_cars[i];
        if (s_cars[i].seen < oldest) { oldest = s_cars[i].seen; slot = i; }
    }
    if (slot == CARS) slot = car / 4u % CARS;
    memset(&s_cars[slot], 0, sizeof s_cars[slot]);
    s_cars[slot].car = car;
    s_cars[slot].impact = -1.0e9;
    s_cars[slot].roll_logged = -1.0e9;
    return &s_cars[slot];
}

/* The player's car alias (player+0x1DC, an std::string), or "". */
static void car_name(uint32_t player, char name[32])
{
    uint32_t text = player + 0x1DCu, length, capacity;
    name[0] = 0;
    if (!player || !mapped(player, 0x1F8u)) return;
    length = MEM32(text + 0x14u);
    capacity = MEM32(text + 0x18u);
    if (!length || length >= 32 || capacity < length) return;
    text = capacity < 16 ? text + 4u : MEM32(text + 4u);
    if (!mapped(text, length + 1u)) return;
    memcpy(name, (const void *)XBOX_PTR(text), length);
    name[length] = 0;
}

/* The player update (2203E5) names each car for the logs. */
void mm3_vehicle_dynamics_player(uint32_t player)
{
    uint32_t vehicle, car;
    unsigned i;
    if (!mapped(player, 0x1A0u)) return;
    vehicle = MEM32(player + 0x19Cu);
    if (!mapped(vehicle, 0x0Cu)) return;
    car = MEM32(vehicle + 8u);
    for (i = 0; i < CARS; ++i)
        if (s_cars[i].car == car) s_cars[i].player = player;
}

/* The strongest wake this car sits in, 0..1. */
static float slipstream(const Car *self, const float direction[2], double now)
{
    float best = 0.0f;
    unsigned i;
    for (i = 0; i < CARS; ++i) {
        const Car *lead = &s_cars[i];
        float dx, dz, along, lateral, width, lead_speed, s;
        if (lead == self || !lead->car || !lead->has_velocity || now - lead->seen > STALE_SECONDS) continue;
        dx = lead->position[0] - self->position[0];
        dz = lead->position[2] - self->position[2];
        if (fabsf(lead->position[1] - self->position[1]) > 3.0f) continue;
        along = dx * direction[0] + dz * direction[1];
        if (along < DRAFT_MIN_GAP || along > DRAFT_FAR) continue;
        lateral = fabsf(dx * direction[1] - dz * direction[0]);
        width = 1.2f + along * 0.03f;
        if (lateral > width) continue;
        lead_speed = sqrtf(lead->velocity[0] * lead->velocity[0] + lead->velocity[2] * lead->velocity[2]);
        if (lead_speed < DRAFT_MIN_SPEED ||
            lead->velocity[0] * direction[0] + lead->velocity[2] * direction[1] < 0.85f * lead_speed) continue;
        s = along <= DRAFT_NEAR ? 1.0f : (DRAFT_FAR - along) / (DRAFT_FAR - DRAFT_NEAR);
        s *= 1.0f - (lateral / width) * (lateral / width);
        if (s > best) best = s;
    }
    return best;
}

static void draft(Car *self, uint32_t body, float speed, const float direction[2], float dt, double now)
{
    float strength, factor, dv;
    if (speed < DRAFT_MIN_SPEED) return;
    strength = slipstream(self, direction, now);
    if (strength <= 0.0f) return;
    factor = speed / DRAFT_SPEED;
    factor *= factor;
    if (factor > 1.25f) factor = 1.25f;
    dv = draft_accel() * strength * factor * dt;
    MEMF(body + 0x20u) += direction[0] * dv;
    MEMF(body + 0x28u) += direction[1] * dv;
    self->draft_seconds += dt;
    self->draft_gain += dv;
}

static void roll_guard(Car *self, uint32_t body, float speed, float dt, double now)
{
    float right[3], forward[3], omega[3], inverse[3];
    float r, lean, s, p, rate, excess, target, k, blend, dp;
    unsigned i;
    for (i = 0; i < 3; ++i) {
        right[i] = MEMF(body + 0xA4u + i * 4u);
        forward[i] = MEMF(body + 0xBCu + i * 4u);
        omega[i] = MEMF(body + 0x50u + i * 4u);
        inverse[i] = MEMF(body + 0x98u + i * 4u);
    }
    r = right[1];
    lean = fabsf(r);
    if (!(lean > ROLL_START) || !(MEMF(body + 0xB4u) > 0.25f) || fabsf(forward[1]) > 0.5f ||
        speed < 5.0f || fabsf(MEMF(body + 0x24u)) > 4.0f || now - self->impact < IMPACT_HOLD) return;
    /* A roll at rate p about the forward axis changes right.y at p * s. */
    s = forward[2] * right[0] - forward[0] * right[2];
    if (fabsf(s) < 0.2f) return;
    p = omega[0] * forward[0] + omega[1] * forward[1] + omega[2] * forward[2];
    rate = p * s;
    excess = (lean - ROLL_START) / (ROLL_FULL - ROLL_START);
    if (excess > 1.0f) excess = 1.0f;
    target = (lean - ROLL_RESTORE_START) / (ROLL_FULL - ROLL_RESTORE_START);
    target = target <= 0.0f ? 0.0f : ROLL_RESTORE * (target > 1.0f ? 1.0f : target);
    if (r > 0.0f) target = -target;
    if ((rate - target) * r <= 0.0f) return;            /* already coming back */
    k = ROLL_DAMP_MIN + (ROLL_DAMP_MAX - ROLL_DAMP_MIN) * excess;
    blend = 1.0f - expf(-k * dt);
    dp = (target - rate) * blend / s;
    for (i = 0; i < 3; ++i) {
        if (!(inverse[i] > 1.0e-12f) || !isfinite(inverse[i])) return;
    }
    for (i = 0; i < 3; ++i) {
        MEMF(body + 0x50u + i * 4u) = omega[i] + forward[i] * dp;
        MEMF(body + 0x5Cu + i * 4u) += forward[i] * dp / inverse[i];
    }
    if (lean > ROLL_LOG && now - self->roll_logged > 5.0 && s_roll_lines < ROLL_LINES) {
        char name[32];
        car_name(self->player, name);
        ++s_roll_lines;
        self->roll_logged = now;
        fprintf(stderr, "[MM3_ROLL] %s held at %.0f deg leaning %s %.1f rad/s, %.0f km/h\n",
                name[0] ? name : "car", asinf(lean) * 57.29578f, r > 0 ? "left" : "right",
                fabsf(p), speed * 3.6f);
    }
}

static void report(double now)
{
    char line[1024];
    size_t used;
    unsigned i, listed = 0;
    if (now < s_report_at) return;
    if (s_report_at == 0.0) { s_report_at = now + REPORT_SECONDS; return; }
    s_report_at = now + REPORT_SECONDS;
    used = (size_t)snprintf(line, sizeof line, "[MM3_DRAFT] last 30 s:");
    for (i = 0; i < CARS && used < sizeof line - 96; ++i) {
        Car *c = &s_cars[i];
        char name[32];
        if (!c->car || now - c->seen > 2.0 || c->draft_seconds < 0.05f) { c->draft_seconds = c->draft_gain = 0.0f; continue; }
        car_name(c->player, name);
        used += (size_t)snprintf(line + used, sizeof line - used, " %s %.1f s +%.0f km/h;",
                                 name[0] ? name : "car", c->draft_seconds, c->draft_gain * 3.6f);
        c->draft_seconds = c->draft_gain = 0.0f;
        ++listed;
    }
    if (listed) fprintf(stderr, "%s\n", line);
}

void sub_001D7441(void)
{
    uint32_t vehicle = g_ecx, car, simulation, body;
    float dt = MEMF(g_esp + 4u), velocity[3], speed, direction[2];
    double now;
    Car *self;
    unsigned i;
    if ((!drafting_enabled() && !roll_guard_enabled()) || !(dt > 0.0f) || dt > 0.25f ||
        !mapped(vehicle, 0x0Cu) || !mapped(car = MEM32(vehicle + 8u), 0x40u) ||
        !mapped(simulation = MEM32(car + 0x3Cu), 8u) || !mapped(body = MEM32(simulation + 4u), 0x1C4u)) {
        sub_001D7441_original();
        return;
    }
    for (i = 0; i < 3; ++i) velocity[i] = MEMF(body + 0x20u + i * 4u);
    if (!isfinite(velocity[0]) || !isfinite(velocity[1]) || !isfinite(velocity[2]) ||
        !isfinite(MEMF(body + 0xC8u)) || !isfinite(MEMF(body + 0xD0u))) {
        sub_001D7441_original();
        return;
    }
    now = now_seconds();
    self = car_slot(car, now);
    if (self->has_velocity && now - self->seen < STALE_SECONDS) {
        float ax = (velocity[0] - self->velocity[0]) / dt, az = (velocity[2] - self->velocity[2]) / dt;
        if (ax * ax + az * az > IMPACT_ACCEL * IMPACT_ACCEL) self->impact = now;
    }
    speed = sqrtf(velocity[0] * velocity[0] + velocity[2] * velocity[2]);
    direction[0] = speed > 0.1f ? velocity[0] / speed : 0.0f;
    direction[1] = speed > 0.1f ? velocity[2] / speed : 0.0f;
    for (i = 0; i < 3; ++i) self->position[i] = MEMF(body + 0xC8u + i * 4u);
    self->seen = now;
    if (drafting_enabled()) draft(self, body, speed, direction, dt, now);
    if (roll_guard_enabled()) roll_guard(self, body, speed, dt, now);
    /* Remember the velocity the step integrates, drafting included, so the
     * next step's impact test sees only what happened in between. */
    for (i = 0; i < 3; ++i) self->velocity[i] = MEMF(body + 0x20u + i * 4u);
    self->has_velocity = 1;
    report(now);
    sub_001D7441_original();
}
