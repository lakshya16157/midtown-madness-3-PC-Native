#include <windows.h>
#include <stdio.h>
#include <math.h>
#include "recomp_types.h"
#include "mm3_cheat_keys.h"

/* The race opponents' difficulty, 0..9: the launcher's Opponents' difficulty
 * (MM3_AI_LEVEL, src/mm3_cheat_keys.c; 5 = the Xbox game).
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
 *   +0xB0 m_rubberBandDistance  lead over the best player that starts holding back
 *   +0xB4 m_rubberBandFactor    throttle cap when the gap is large (easy tiers
 *   +0xB8 m_catchUpFactor       hold back with 0.5-0.9) and the catch-up speed
 *   +0xC0 m_speedOnNonStreamedRoadWhenBehind  speed out of the streamed city,
 *   +0xC4 m_speedOnNonStreamedRoadWhenAhead   behind or ahead of the player
 * (offsets from the title's own field table at 0x3B9xxx, sub in recomp_0012).
 * Above 5 those are scaled for the call and put back (so cars sharing a
 * personality never see each other's values): later braking, a little more
 * corner speed, and the easy tiers' holding back fades to none at 9. Below 5
 * the other way, and much more so for what lets them get away or come back:
 * at 0 they hold back from 1.4 instead of 7 ahead at 0.4x the throttle cap,
 * and come back at 0.4x the catch-up factor and 0.4x the out-of-view speed.
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
    float lead, hold, come_back, unseen_behind, unseen_ahead;   /* below 5 only, else 1 */
} Factors;

static Factors factors(int level)
{
    float d = (float)(level - 5);
    Factors f;
    f.engine = 1.0f + 0.10f * d;                                  /* 9: x1.4, 0: x0.5 */
    f.brake = d >= 0.0f ? 1.0f + 0.15f * d : 1.0f + 0.05f * d;    /* 9: x1.6, 0: x0.75 */
    f.decel = d >= 0.0f ? 1.0f + 0.05f * d : 1.0f + 0.06f * d;    /* 9: x1.2, 0: x0.7 */
    f.corner = d >= 0.0f ? 1.0f + 0.025f * d : 1.0f + 0.04f * d;  /* 9: x1.1, 0: x0.8 */
    f.catch_up = d / 4.0f;          /* share of the holding back removed (above 5) */
    {
        float t = d < 0.0f ? -d / 5.0f : 0.0f;                    /* 0 at 5, 1 at 0 */
        f.lead = 1.0f - 0.8f * t;                                  /* 0: x0.2 (7 -> 1.4) */
        f.hold = 1.0f - 0.6f * t;                                  /* 0: x0.4 (0.55 -> 0.22) */
        f.come_back = 1.0f - 0.6f * t;                             /* 0: x0.4 (0.80 -> 0.32) */
        f.unseen_behind = 1.0f - 0.6f * t;                         /* 0: x0.4 (53 -> 21 m/s) */
        f.unseen_ahead = 1.0f - 0.3f * t;                          /* 0: x0.7 (27 -> 19 m/s) */
    }
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

/* Above 5: the share of the holding back removed. Below 5: the factor. */
static float holding_back(float factor, float share, float below)
{
    if (!(factor > 0.0f && factor <= 1.0f)) return factor;
    return share >= 0.0f ? factor + (1.0f - factor) * share : factor * below;
}

static int stop_before_finish(uint32_t ai, int *hold_brake)
{
    uint32_t cp_idx = MEM32(ai + 0xA0u), lap = MEM32(ai + 0xA4u), laps = MEM32(ai + 0xA8u);
    uint32_t cp_begin = MEM32(ai + 0xB4u), cp_end = MEM32(ai + 0xB8u), cp_count;
    uint32_t cp;
    *hold_brake = 0;
    cp_count = (guest(cp_begin) && cp_end >= cp_begin + 0x7Cu) ? (cp_end - cp_begin) / 0x7Cu : 0u;
    if (!cp_count || cp_idx + 1u < cp_count || (laps > 1u && lap + 1u < laps)) return 0;
    cp = cp_begin + cp_idx * 0x7Cu;
    if (guest(cp)) {
        float dx = MEMF(ai + 0x60u) - MEMF(cp + 0x28u);
        float dy = MEMF(ai + 0x64u) - MEMF(cp + 0x2Cu);
        float dz = MEMF(ai + 0x68u) - MEMF(cp + 0x30u);
        float cr = MEMF(cp + 0x34u);
        float r = (cr > 0.0f && cr < 100.0f ? cr : 15.0f) + 25.0f;
        if (dx * dx + dy * dy + dz * dz <= r * r) {
            uint32_t mgr = MEM32(ai + 0x140u), road_idx = MEM32(ai + 0x144u), seg_idx = MEM32(ai + 0x148u);
            uint32_t roads, segs;
            if (guest(mgr) && guest(roads = MEM32(mgr + 0x20u)) &&
                guest(segs = MEM32(roads + road_idx * 0xA4u + 0x64u)) &&
                guest(segs + seg_idx * 0x74u + 0x6Cu)) {
                MEMF(segs + seg_idx * 0x74u + 0x68u) = 0.0f;
                MEMF(segs + seg_idx * 0x74u + 0x6Cu) = 0.0f;
            }
            *hold_brake = 1;
        }
    }
    return 1;
}

void sub_0014194C(void)
{
    static int logged = -1;
    uint32_t state = g_ecx, ai = MEM32(g_ecx + 4u), personality, vehicle, car = 0;
    int level = mm3_cheat_ai_level(), is_player = 0, final_cp = 0, hold_brake = 0;
    /* merged into line above */
    float saved[8];
    Factors f;
    if (!guest(ai) || MEM32(ai) != RACE_AI_VTABLE || !guest(personality = MEM32(ai + 0x94u))) {
        sub_0014194C_original();
        return;
    }
    f = factors(level);
    vehicle = MEM32(ai + 0x28u);
    if (guest(vehicle) && MEM32(vehicle) == VEHICLE_VTABLE && guest(car = MEM32(vehicle + 8u))) {
        is_player = own_car(car);
        if (!is_player) car_factors(car, &f);
    }
    if (!is_player && mm3_cheat_ai_stop_finish()) final_cp = stop_before_finish(ai, &hold_brake);
        /* handled above */
    if (logged != level) {
        logged = level;
        if (f.catch_up >= 0.0f)
            fprintf(stderr, "[MM3_AI_LEVEL] opponents' difficulty %d: engine x%.2f, brakes x%.2f, braking point x%.2f, "
                    "corner speed x%.3f, holding back removed %.2f\n", level, f.engine, f.brake, f.decel, f.corner,
                    f.catch_up);
        else
            fprintf(stderr, "[MM3_AI_LEVEL] opponents' difficulty %d: engine x%.2f, brakes x%.2f, braking point x%.2f, "
                    "corner speed x%.3f; hold back from x%.2f the lead at throttle cap x%.2f, catch-up x%.2f, "
                    "out-of-view speed x%.2f behind / x%.2f ahead\n", level, f.engine, f.brake, f.decel, f.corner,
                    f.lead, f.hold, f.come_back, f.unseen_behind, f.unseen_ahead);
        fflush(stderr);
    }
    if (level == 5 && !final_cp) {
        sub_0014194C_original();
        return;
    }
    saved[0] = MEMF(personality + 0xD4u);
    saved[1] = MEMF(personality + 0xD0u);
    saved[2] = MEMF(personality + 0x78u);
    saved[3] = MEMF(personality + 0xB4u);
    saved[4] = MEMF(personality + 0xB8u);
    saved[5] = MEMF(personality + 0xB0u);
    saved[6] = MEMF(personality + 0xC0u);
    saved[7] = MEMF(personality + 0xC4u);
    MEMF(personality + 0xD4u) = saved[0] * f.decel;
    MEMF(personality + 0xD0u) = saved[1] * f.corner * f.corner;
    MEMF(personality + 0x78u) = saved[2] * f.corner;
    MEMF(personality + 0xB4u) = holding_back(saved[3], f.catch_up, f.hold);
    MEMF(personality + 0xB8u) = holding_back(saved[4], f.catch_up, f.come_back);
    if (saved[5] > 0.0f) MEMF(personality + 0xB0u) = saved[5] * f.lead;
    if (saved[6] > 0.0f) MEMF(personality + 0xC0u) = saved[6] * f.unseen_behind;
    if (saved[7] > 0.0f) MEMF(personality + 0xC4u) = saved[7] * f.unseen_ahead;
    if (final_cp) {
        MEMF(personality + 0xC0u) = 0.0f;
        MEMF(personality + 0xC4u) = 0.0f;
    }
    sub_0014194C_original();
    if (hold_brake) {
        MEMF(ai + 0x15Cu) = 0.0f;
        MEMF(ai + 0x164u) = 0.0f;
        MEMF(ai + 0x168u) = 1.0f;
        MEMF(state + 0x08u) = 0.0f;
        MEMF(state + 0x0Cu) = 1.0f;
        MEMF(state + 0x10u) = 0.0f;
        g_eax = state;
    }
    MEMF(personality + 0xD4u) = saved[0];
    MEMF(personality + 0xD0u) = saved[1];
    MEMF(personality + 0x78u) = saved[2];
    MEMF(personality + 0xB4u) = saved[3];
    MEMF(personality + 0xB8u) = saved[4];
    MEMF(personality + 0xB0u) = saved[5];
    MEMF(personality + 0xC0u) = saved[6];
    MEMF(personality + 0xC4u) = saved[7];
}
