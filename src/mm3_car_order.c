#include <stdio.h>
#include <time.h>
#include "recomp_types.h"
#include "mm3_mod_flags.h"

extern void sub_001DE752_original(void);

/* IDs and order from the pristine PAL Cars/Tweak/SortOrder. The two hidden
 * retail bonus vehicles belong to the original catalog too. Every other ID
 * is an extra, including downloaded cars and future installed variants.
 * Keep in sync with ORIGINAL_CAR_IDS in tools/car-mods/prepare.py. */
static const uint32_t original_car_ids[] = {
    106, 115, 103, 101, 105, 102, 6, 7, 50, 113, 104, 19, 111,
    108, 110, 107, 109, 10, 21, 14, 23, 12, 1, 24, 9, 2, 16,
    3, 8, 25, 18, 11, 15, 666, 667
};

static unsigned original_rank(uint32_t id)
{
    unsigned i;
    for (i = 0; i < sizeof original_car_ids / sizeof original_car_ids[0]; ++i)
        if (original_car_ids[i] == id) return i;
    return (unsigned)(sizeof original_car_ids / sizeof original_car_ids[0]);
}

static int guest_range(uint32_t address, uint32_t size)
{
    return address >= 0x10000u && size < 0x08000000u &&
           address <= 0x08000000u - size;
}

static unsigned speed_rating(uint32_t entry)
{
    uint32_t speed = MEM32(entry + 0x270u);
    /* TOPSPEED is the native 0..9 menu rating, not a guessed real-world speed
     * or the editable speedometer maximum. Unknown ratings sort last. */
    return speed <= 9u ? speed : 10u;
}

static int comes_before(uint32_t a, uint32_t b)
{
    unsigned a_rank = original_rank(MEM32(a));
    unsigned b_rank = original_rank(MEM32(b));
    unsigned extras = (unsigned)(sizeof original_car_ids / sizeof original_car_ids[0]);

    if (a_rank != extras || b_rank != extras) {
        /* Keep the retail available / visible-locked groups and the
         * relative order of original cars. */
        if (a_rank != extras && b_rank != extras)
            return 0;
        return a_rank != extras;
    }
    if (speed_rating(a) != speed_rating(b))
        return speed_rating(a) < speed_rating(b);
    /* Stable across DLC enumeration order and repeat menu rebuilds. */
    return MEM32(a) < MEM32(b);
}

static void order_vector(uint32_t vector)
{
    uint32_t start, end, cursor;
    if (!guest_range(vector, 16u)) return;
    start = MEM32(vector + 4u);
    end = MEM32(vector + 8u);
    if (end < start || (end - start) % 4u || end - start > 8192u ||
        !guest_range(start, end - start)) return;
    /* Validate the complete list before editing any pointer. No catalog
     * entries, ownership, unlock flags, classes or profile IDs are changed. */
    for (cursor = start; cursor < end; cursor += 4u)
        if (!guest_range(MEM32(cursor), 0x2A4u)) return;
    for (cursor = start + 4u; cursor < end; cursor += 4u) {
        uint32_t entry = MEM32(cursor), position = cursor;
        while (position > start && comes_before(entry, MEM32(position - 4u))) {
            MEM32(position) = MEM32(position - 4u);
            position -= 4u;
        }
        MEM32(position) = entry;
    }
}

/* PAL vehicle-menu enumerator: EBX is the catalog, [ESP+4] is the output
 * vector, EAX returns that vector, and RET 4 consumes its argument. The retail
 * implementation continues to decide membership, race classes and locks.
 * Reorder both its cache and its returned copy so cached/rebuilt lists agree.
 * The catalog's own list (+4) keeps the order the game loaded it in: race
 * opponents are picked from it (sub_001DE012).
 * Host helpers never touch guest registers, preserving every original effect. */
/* Cars added by the installer (Car Workshop variants, IDs from 9000) are
 * left out of the menu's list with MM3_EXTRA_CARS=0 (the launcher's Original
 * game); the Xbox's own and the official downloadable cars stay. Only the
 * returned list and its cache shrink; the catalog keeps every entry. */
#define FIRST_EXTRA_CAR_ID 9000u
static void hide_extra_cars(uint32_t vector)
{
    uint32_t start, end, cursor, kept;
    if (!guest_range(vector, 16u)) return;
    start = MEM32(vector + 4u);
    end = MEM32(vector + 8u);
    if (end < start || (end - start) % 4u || end - start > 8192u || !guest_range(start, end - start)) return;
    for (cursor = kept = start; cursor < end; cursor += 4u) {
        uint32_t entry = MEM32(cursor);
        if (guest_range(entry, 4u) && MEM32(entry) >= FIRST_EXTRA_CAR_ID) continue;
        MEM32(kept) = entry;
        kept += 4u;
    }
    MEM32(vector + 8u) = kept;
}

void sub_001DE752(void)
{
    uint32_t catalog = g_ebx;
    uint32_t output = MEM32(g_esp + 4u);
    sub_001DE752_original();
    if (guest_range(catalog, 0x24u)) order_vector(catalog + 0x14u);
    order_vector(output);
    if (!mm3_mod_extra_cars()) {
        if (guest_range(catalog, 0x24u)) hide_extra_cars(catalog + 0x14u);
        hide_extra_cars(output);
    }
}

/* Race opponents' cars. Retail Checkpoint races call getRandomVehicleFromGroup
 * (this Lua handler) from logic_general.lua randomCarInGroup with the player's
 * class: it gathers the catalog's cars (+4) of class +0x30 that are not hidden
 * (+0x28), in catalog order, and pick 1 takes the last (the rival's "best
 * car"), pick 2 one of the last two, else any. The installer's extra cars
 * share the retail classes and load after the originals, so they became the
 * best car of their class: a Koenigsegg race's rival drove a Regera, a Viper
 * race's a Panoz. The Xbox had none of them. Hide each extra car from the
 * pick while its class still has a car of the Xbox's own (retail or
 * downloadable); circuit races may still pick them when the launcher's
 * MM3_AI_ALL_CARS allows it (tools/races/logic_circuit.lua). */
extern void sub_001DE012_original(void);
extern int mm3_circuit_mode_on(void);

#define VEHICLE_CATALOG 0x003C5C94u
#define CAR_CLASSES 64u
#define CATALOG_CARS 2048u

static int circuit_extras_allowed(void)
{
    const char *v = getenv("MM3_AI_ALL_CARS");
    return mm3_circuit_mode_on() && !(v && !strcmp(v, "0"));
}

/* "Opponents drive your car" (launcher Gameplay mods, MM3_SAME_CAR): every
 * opponent pick returns the player's car. Only Checkpoint races (circuits
 * included) pick opponents' cars; Career jobs name each car, so missions keep
 * theirs. The player's car is the lobby's player 1 as lobbyGetModel finds it
 * (sub_001F99C1: list at 0x407D38+0x380, record at node+8 with the player
 * number at +0x149): catalog ID +0x1AC, else +0x1B0. For the pick every other
 * catalog car is hidden (+0x28) and the player's shown, then each flag is put
 * back. A pick for another class (a circuit rival's faster group) finds
 * nothing, and the circuit script falls back to the player's class. Paris
 * Checkpoint 06 re-rolls until the car is not a Firetruck or Freightliner,
 * which cannot pass under its bridge: more than SAME_CAR_BURST picks within a
 * second get the game's own picks, so such a loop ends. */
#define LOBBY 0x00407D38u
#define SAME_CAR_BURST 64u

static uint32_t player_car(uint32_t start, uint32_t end)
{
    uint32_t head = MEM32(LOBBY + 0x380u), node, record = 0, cursor;
    unsigned steps, slot;
    if (!guest_range(head, 4u)) return 0;
    for (node = MEM32(head), steps = 0; node != head && steps < 16u; node = MEM32(node), ++steps) {
        if (!guest_range(node, 12u)) return 0;
        if (guest_range(MEM32(node + 8u), 0x1B8u) && MEM8(MEM32(node + 8u) + 0x149u) == 1u) {
            record = MEM32(node + 8u);
            break;
        }
    }
    if (!record) return 0;
    for (slot = 0; slot < 2u; ++slot)
        for (cursor = start; cursor < end; cursor += 4u)
            if (MEM32(MEM32(cursor)) == MEM32(record + 0x1ACu + 4u * slot)) return MEM32(cursor);
    return 0;
}

static int same_car_burst_over(void)
{
    static clock_t last;
    static unsigned burst;
    clock_t now = clock();
    if (burst && now - last > CLOCKS_PER_SEC) burst = 0;
    last = now;
    return ++burst > SAME_CAR_BURST;
}

static int same_car_pick(uint32_t start, uint32_t end)
{
    static struct { uint32_t entry; uint8_t hidden; } changed[CATALOG_CARS];
    static int logged, refused;
    uint32_t mine = player_car(start, end), cursor;
    unsigned count = 0, i;
    if (!mine) return 0;
    if (same_car_burst_over()) {
        if (!refused) {
            refused = 1;
            fprintf(stderr, "[MM3_SAME_CAR] the race keeps refusing your car (Paris Checkpoint 06 with a "
                            "Firetruck or Freightliner): its opponents get the game's own cars\n");
        }
        return 0;
    }
    for (cursor = start; cursor < end; cursor += 4u) {
        uint32_t entry = MEM32(cursor);
        uint8_t hide = entry != mine;
        if (!MEM8(entry + 0x28u) == !hide) continue;
        changed[count].entry = entry;
        changed[count++].hidden = MEM8(entry + 0x28u);
        MEM8(entry + 0x28u) = hide;
    }
    if (!logged) {
        logged = 1;
        fprintf(stderr, "[MM3_SAME_CAR] race opponents drive your car (catalog ID %u)\n", MEM32(mine));
    }
    sub_001DE012_original();
    for (i = 0; i < count; ++i) MEM8(changed[i].entry + 0x28u) = changed[i].hidden;
    return 1;
}

void sub_001DE012(void)
{
    static uint32_t hidden[CATALOG_CARS];
    static int logged;
    uint8_t xbox_class[CAR_CLASSES] = {0};
    uint32_t catalog = MEM32(VEHICLE_CATALOG), start, end, cursor;
    unsigned count = 0, i;
    if (!guest_range(catalog, 0x24u)) {
        sub_001DE012_original();
        return;
    }
    start = MEM32(catalog + 8u);
    end = MEM32(catalog + 12u);
    if (end < start || (end - start) % 4u || end - start > 4u * CATALOG_CARS ||
        !guest_range(start, end - start)) {
        sub_001DE012_original();
        return;
    }
    for (cursor = start; cursor < end; cursor += 4u) {
        uint32_t entry = MEM32(cursor);
        if (!guest_range(entry, 0x2A4u)) {
            sub_001DE012_original();
            return;
        }
        if (MEM32(entry) < FIRST_EXTRA_CAR_ID && !MEM8(entry + 0x28u) &&
            MEM32(entry + 0x30u) < CAR_CLASSES)
            xbox_class[MEM32(entry + 0x30u)] = 1;
    }
    if (mm3_mod_same_car() && same_car_pick(start, end)) return;
    if (circuit_extras_allowed()) {
        sub_001DE012_original();
        return;
    }
    for (cursor = start; cursor < end; cursor += 4u) {
        uint32_t entry = MEM32(cursor), car_class = MEM32(entry + 0x30u);
        if (MEM32(entry) >= FIRST_EXTRA_CAR_ID && !MEM8(entry + 0x28u) &&
            car_class < CAR_CLASSES && xbox_class[car_class]) {
            MEM8(entry + 0x28u) = 1;
            hidden[count++] = entry;
        }
    }
    if (count && !logged) {
        logged = 1;
        fprintf(stderr, "[MM3_AI_CARS] race opponents drive the Xbox cars: %u installer cars left out of their picks\n", count);
    }
    sub_001DE012_original();
    for (i = 0; i < count; ++i) MEM8(hidden[i] + 0x28u) = 0;
}

/* The main menu's profile box, "Cars: owned/total" (sub_0010F9A3). The total
 * is the length of the menu's list (sub_001DEF6A -> sub_001DE752 above); the
 * owned count, sub_001DE995 ([ESP+4] = the catalog, +8 begin, +0xC end;
 * RET 4), walks every catalog car and counts each whose unlock requirements
 * (+0x284) the profile meets. The installer's cars have none, so with them
 * left out of the menu (MM3_EXTRA_CARS=0, the Original look and Xbox
 * gameplay) the box read "Cars: 50/46". For that count they are moved past
 * the end of the list, which is put back as it was afterwards. */
extern void sub_001DE995_original(void);

void sub_001DE995(void)
{
    static uint32_t saved[CATALOG_CARS];
    uint32_t list = MEM32(g_esp + 4u), start, end, kept, moved;
    unsigned count, i;
    if (mm3_mod_extra_cars() || !guest_range(list, 16u)) {
        sub_001DE995_original();
        return;
    }
    start = MEM32(list + 8u);
    end = MEM32(list + 12u);
    if (end < start || (end - start) % 4u || end - start > 4u * CATALOG_CARS ||
        !guest_range(start, end - start)) {
        sub_001DE995_original();
        return;
    }
    count = (end - start) / 4u;
    for (i = 0; i < count; ++i) saved[i] = MEM32(start + 4u * i);
    kept = start;
    for (i = 0; i < count; ++i)
        if (!guest_range(saved[i], 4u) || MEM32(saved[i]) < FIRST_EXTRA_CAR_ID) {
            MEM32(kept) = saved[i];
            kept += 4u;
        }
    if (kept == end) {
        sub_001DE995_original();
        return;
    }
    for (moved = kept, i = 0; i < count; ++i)
        if (guest_range(saved[i], 4u) && MEM32(saved[i]) >= FIRST_EXTRA_CAR_ID) {
            MEM32(moved) = saved[i];
            moved += 4u;
        }
    MEM32(list + 12u) = kept;
    sub_001DE995_original();
    MEM32(list + 12u) = end;
    for (i = 0; i < count; ++i) MEM32(start + 4u * i) = saved[i];
}
