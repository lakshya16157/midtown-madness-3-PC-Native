#include <windows.h>
#include <stdio.h>
#include "recomp_types.h"
#include "mm3_cheat_keys.h"

/* howhardcanitbe0..9: the race opponents' difficulty (5 = the Xbox game).
 *
 * A race opponent is driven by a RaceCar AI (vtable 0x37FAB0; police and
 * traffic have their own AIs and are left alone). Each frame its Normal state
 * (sub_0014194C, thiscall: ECX = state, state+4 = the AI, ret 4) plans the
 * drive from the AI's personality (AI+0x94, an object shared by every car of
 * that personality, AI/Personalities/RaceCars/*.aip):
 *   +0xD4 m_maxDeceleration     how hard it expects to brake: where it starts
 *                               braking for a corner, (v - corner)^2 / this
 *   +0xD0 m_maxTurnAcceleration corner speed = sqrt(curve * this)
 *   +0x78 m_turnSpeed           speed limit in a turn
 *   +0xB4 m_rubberBandFactor    throttle cap when the gap is large (easy tiers
 *   +0xB8 m_catchUpFactor       hold back with 0.5-0.9) and the catch-up speed
 * Above 5 those are scaled for the call and put back (so cars sharing a
 * personality never see each other's values): later braking, a little more
 * corner speed, and the easy tiers' holding back fades to none at 9. Below 5
 * the other way.
 * The car itself has the engine's torque factor at car+0x90 and the brakes'
 * at car+0x94 (1.0; the scripts' playerSpeedCoeff / playerBrakeCoeff set
 * them, sub_00218067 / sub_0021803C; retail checkpoint races boost behind
 * opponents with 1.25-1.5). The AI's vehicle is AI+0x28 (the vehicle
 * interface, vtable 0x383768, +8 the car). Whatever the scripts set is kept
 * as the base and the level's factor applied on top. */
extern void sub_0014194C_original(void);

#define RACE_AI_VTABLE 0x0037FAB0u
#define VEHICLE_VTABLE 0x00383768u

typedef struct {
    float engine, brake, decel, corner, catch_up;
} Factors;

static Factors factors(int level)
{
    float d = (float)(level - 5);
    Factors f;
    f.engine = d >= 0.0f ? 1.0f + 0.10f * d : 1.0f + 0.08f * d;   /* 9: x1.4, 0: x0.6 */
    f.brake = d >= 0.0f ? 1.0f + 0.15f * d : 1.0f + 0.05f * d;    /* 9: x1.6, 0: x0.75 */
    f.decel = d >= 0.0f ? 1.0f + 0.05f * d : 1.0f + 0.06f * d;    /* 9: x1.2, 0: x0.7 */
    f.corner = d >= 0.0f ? 1.0f + 0.025f * d : 1.0f + 0.04f * d;  /* 9: x1.1, 0: x0.8 */
    f.catch_up = d / 4.0f;          /* share of the holding back removed; below 0 added */
    return f;
}

static int guest(uint32_t address)
{
    return address >= 0x10000u && address < 0x08000000u && !(address & 3u);
}

static int own_car(uint32_t car)
{
    uint32_t manager = MEM32(0x003C5CDCu), slot, i, count;
    if (!guest(manager)) return 0;
    count = MEM32(manager + 0x50u);       /* the human players' cameras, +0x54 on */
    for (i = 0; i < count && i < 2; ++i) {
        uint32_t player, vehicle;
        if (!guest(slot = MEM32(manager + 0x54u + i * 4u)) || !guest(player = MEM32(slot + 0xD8u)) ||
            !guest(vehicle = MEM32(player + 0x19Cu)) || MEM32(vehicle) != VEHICLE_VTABLE) continue;
        if (MEM32(vehicle + 8u) == car) return 1;
    }
    return 0;
}

/* The scripts' own value of each car's engine and brake factor, told apart
 * from ours by what was written last. A car not seen for two seconds is
 * forgotten (a new race may reuse its memory). */
static struct Car {
    uint32_t car;
    DWORD seen;
    float base[2], written[2];
} s_cars[32];

static struct Car *car_entry(uint32_t car, DWORD now)
{
    struct Car *free_entry = NULL;
    for (int i = 0; i < 32; ++i) {
        if (s_cars[i].car == car && now - s_cars[i].seen < 2000u) return &s_cars[i];
        if (!free_entry || now - s_cars[i].seen > now - free_entry->seen) free_entry = &s_cars[i];
    }
    free_entry->car = car;
    free_entry->base[0] = free_entry->base[1] = -1.0f;
    return free_entry;
}

static void car_factors(uint32_t car, const Factors *f)
{
    DWORD now = GetTickCount();
    struct Car *c = car_entry(car, now);
    float scale[2] = {f->engine, f->brake};
    c->seen = now;
    for (int i = 0; i < 2; ++i) {
        uint32_t field = car + 0x90u + 4u * (uint32_t)i;
        float value = MEMF(field);
        if (c->base[i] < 0.0f || value != c->written[i]) c->base[i] = value;   /* the scripts' */
        c->written[i] = c->base[i] * scale[i];
        MEMF(field) = c->written[i];
    }
}

static float less_holding_back(float factor, float share)
{
    if (!(factor > 0.0f && factor <= 1.0f)) return factor;
    return share >= 0.0f ? factor + (1.0f - factor) * share : factor * (1.0f + 0.3f * share);
}

void sub_0014194C(void)
{
    static int logged = -1;
    uint32_t ai = MEM32(g_ecx + 4u), personality, vehicle, car;
    int level = mm3_cheat_ai_level();
    float saved[5];
    Factors f;
    if (!guest(ai) || MEM32(ai) != RACE_AI_VTABLE || !guest(personality = MEM32(ai + 0x94u))) {
        sub_0014194C_original();
        return;
    }
    f = factors(level);
    vehicle = MEM32(ai + 0x28u);
    if (guest(vehicle) && MEM32(vehicle) == VEHICLE_VTABLE && guest(car = MEM32(vehicle + 8u)) && !own_car(car))
        car_factors(car, &f);
    if (logged != level) {
        logged = level;
        fprintf(stderr, "[MM3_AI_LEVEL] opponents' difficulty %d: engine x%.2f, brakes x%.2f, braking point x%.2f, "
                "corner speed x%.3f, rubber band %s %.2f\n", level, f.engine, f.brake, f.decel, f.corner,
                f.catch_up >= 0.0f ? "holding back removed" : "throttle cap x",
                f.catch_up >= 0.0f ? f.catch_up : 1.0f + 0.3f * f.catch_up);
        fflush(stderr);
    }
    if (level == 5) {
        sub_0014194C_original();
        return;
    }
    saved[0] = MEMF(personality + 0xD4u);
    saved[1] = MEMF(personality + 0xD0u);
    saved[2] = MEMF(personality + 0x78u);
    saved[3] = MEMF(personality + 0xB4u);
    saved[4] = MEMF(personality + 0xB8u);
    MEMF(personality + 0xD4u) = saved[0] * f.decel;
    MEMF(personality + 0xD0u) = saved[1] * f.corner * f.corner;
    MEMF(personality + 0x78u) = saved[2] * f.corner;
    MEMF(personality + 0xB4u) = less_holding_back(saved[3], f.catch_up);
    MEMF(personality + 0xB8u) = less_holding_back(saved[4], f.catch_up);
    sub_0014194C_original();
    MEMF(personality + 0xD4u) = saved[0];
    MEMF(personality + 0xD0u) = saved[1];
    MEMF(personality + 0x78u) = saved[2];
    MEMF(personality + 0xB4u) = saved[3];
    MEMF(personality + 0xB8u) = saved[4];
}
