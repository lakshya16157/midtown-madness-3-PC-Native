/* Production hook ABI and membership contract; no game files or saves opened. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/mm3_car_order.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp, g_fs_base;

/* Guest memory up to the vehicle catalog pointer at 0x3C5C94 and the lobby
 * at 0x407D38. */
static unsigned char memory[0x409000], expected[0x409000];
static const uint32_t catalog = 0x10000, pointers = 0x10100;
static const uint32_t cache = 0x10200, output = 0x10300, results = 0x10400;
static const uint32_t stack = 0x1E000;
/* The catalog in the order the game loaded it. */
static const uint32_t ids[] = {9005, 211, 12, 9004, 201, 106, 666,
                             206, 18, 667, 214, 203, 9014, 9999};
static const uint32_t speeds[] = {8, 9, 5, 8, 8, 2, 8, 3, 8, 7, 5, 3, 7, UINT32_MAX};
static int cached, race_class, circuit_mode;
static unsigned original_calls;
static uint32_t pick_class, picked;

int mm3_circuit_mode_on(void)
{
    return circuit_mode;
}

static uint32_t entry_for_id(uint32_t id)
{
    unsigned i;
    for (i = 0; i < sizeof ids / sizeof ids[0]; ++i)
        if (ids[i] == id) return 0x11000u + i * 0x300u;
    assert(0);
    return 0;
}

static void check_ids(uint32_t start, const uint32_t *order, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i)
        assert(MEM32(MEM32(start + i * 4u)) == order[i]);
}

void sub_001DE752_original(void)
{
    uint32_t cursor, end = cache;
    unsigned locked;
    assert(g_ebx == catalog && g_esp == stack);
    assert(MEM32(stack) == 0x00121732u && MEM32(stack + 4u) == output);
    /* The catalog keeps the game's own order: race opponents come from it. */
    check_ids(pointers, ids, sizeof ids / sizeof ids[0]);
    ++original_calls;
    if (!cached) {
        /* Retail filtering visits available cars, then visible locked cars.
         * Race-class exclusion and hidden locks must survive the wrapper. */
        for (locked = 0; locked <= 1; ++locked)
            for (cursor = pointers; cursor < MEM32(catalog + 12u); cursor += 4u) {
                uint32_t entry = MEM32(cursor);
                if ((unsigned)!!MEM8(entry + 0x29u) != locked ||
                    (MEM8(entry + 0x28u) && MEM8(entry + 0x29u)) ||
                    (race_class && MEM32(entry + 0x30u) != (uint32_t)race_class)) continue;
                MEM32(end) = entry;
                end += 4u;
            }
        MEM32(catalog + 0x1Cu) = end;
    }
    end = MEM32(catalog + 0x1Cu);
    memcpy((void *)XBOX_PTR(results), (const void *)XBOX_PTR(cache), end - cache);
    MEM32(output + 8u) = results + (end - cache);
    /* Original caller-saved register effects and RET 4 are authoritative. */
    g_eax = output;
    g_ecx = 0xA1B2C3D4u;
    g_edx = 0xB2C3D4E5u;
    g_esp += 8u;
}

/* Retail getRandomVehicleFromGroup, best pick: the last car of the class
 * that is not hidden, in catalog order. */
void sub_001DE012_original(void)
{
    uint32_t list = MEM32(VEHICLE_CATALOG), cursor, last = 0;
    assert(g_esp == stack);
    ++original_calls;
    if (list)
        for (cursor = MEM32(list + 8u); cursor < MEM32(list + 12u); cursor += 4u) {
            uint32_t entry = MEM32(cursor);
            if (MEM32(entry + 0x30u) == pick_class && !MEM8(entry + 0x28u)) last = entry;
        }
    picked = last ? MEM32(last) : 0;
    g_eax = picked != 0;
    g_ecx = 0xA1B2C3D4u;
    g_esp += 4u;
}

/* Retail owned-car count of the profile box: every car of the list counts
 * (none has unlock requirements here). */
void sub_001DE995_original(void)
{
    uint32_t list = MEM32(g_esp + 4u), cursor;
    unsigned owned = 0;
    assert(list == catalog && MEM32(g_esp) == 0x0010F9F0u);
    ++original_calls;
    for (cursor = MEM32(list + 8u); cursor != MEM32(list + 12u); cursor += 4u) ++owned;
    g_eax = owned;
    g_ecx = 0xA1B2C3D4u;
    g_esp += 8u;
}

static void setup(void)
{
    unsigned i;
    memset(memory, 0xA5, sizeof memory);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    MEM32(VEHICLE_CATALOG) = catalog;
    MEM32(catalog + 8u) = pointers;
    MEM32(catalog + 12u) = pointers + sizeof ids;
    MEM32(catalog + 0x18u) = cache;
    MEM32(catalog + 0x1Cu) = cache;
    MEM32(output + 4u) = results;
    MEM32(output + 8u) = results;
    for (i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
        uint32_t entry = entry_for_id(ids[i]);
        MEM32(pointers + i * 4u) = entry;
        MEM32(entry) = ids[i];
        MEM32(entry + 4u) = 12; /* Donor/physics identity is independent. */
        MEM32(entry + 0x270u) = speeds[i];
        MEM8(entry + 0x28u) = (ids[i] == 666u || ids[i] == 667u);
        MEM8(entry + 0x29u) = (ids[i] == 666u || ids[i] == 12u || ids[i] == 206u);
        MEM32(entry + 0x30u) = ids[i] == 201u || ids[i] == 106u ? 2u : 1u;
    }
    MEM32(stack) = 0x00121732u;
    MEM32(stack + 4u) = output;
    g_esp = stack; g_ebx = catalog;
    g_eax = 0x11111111u; g_ecx = 0x22222222u; g_edx = 0x33333333u;
    g_esi = 0x44444444u; g_edi = 0x55555555u; g_ebp = 0x66666666u;
    g_seh_ebp = 0x77777777u; g_fs_base = 0x88888888u;
    cached = race_class = circuit_mode = 0;
    original_calls = 0;
}

static void run_case(const uint32_t *menu_ids, unsigned count)
{
    unsigned i;
    memcpy(expected, memory, sizeof memory);
    for (i = 0; i < count; ++i) {
        uint32_t entry = entry_for_id(menu_ids[i]);
        memcpy(expected + cache + i * 4u, &entry, 4u);
        memcpy(expected + results + i * 4u, &entry, 4u);
    }
    {
        uint32_t end_cache = cache + count * 4u, end_output = results + count * 4u;
        memcpy(expected + catalog + 0x1Cu, &end_cache, 4u);
        memcpy(expected + output + 8u, &end_output, 4u);
    }
    sub_001DE752();
    assert(original_calls == 1);
    assert(g_eax == output && g_ecx == 0xA1B2C3D4u && g_edx == 0xB2C3D4E5u);
    assert(g_esp == stack + 8u && g_ebx == catalog);
    assert(g_esi == 0x44444444u && g_edi == 0x55555555u && g_ebp == 0x66666666u);
    assert(g_seh_ebp == 0x77777777u && g_fs_base == 0x88888888u);
    check_ids(cache, menu_ids, count);
    check_ids(results, menu_ids, count);
    /* All entry bytes, profile/donor IDs, locks, flags, stack and unrelated
     * memory remain byte-for-byte identical; only vector pointers are edited. */
    assert(memcmp(memory, expected, sizeof memory) == 0);
}

/* "Cars: owned/total": the owned count, with the catalog exactly as before
 * once it returns. */
static unsigned owned_cars(void)
{
    MEM32(stack) = 0x0010F9F0u;
    MEM32(stack + 4u) = catalog;
    memcpy(expected, memory, sizeof memory);
    g_esp = stack;
    original_calls = 0;
    sub_001DE995();
    assert(original_calls == 1 && g_esp == stack + 8u && g_ecx == 0xA1B2C3D4u);
    assert(memcmp(memory, expected, sizeof memory) == 0);
    return g_eax;
}

/* MM3_EXTRA_CARS=0 (Original look, Xbox gameplay): the installer's cars are
 * left out of the menu, and out of the owned count, so the profile box never
 * reads more cars owned than there are ("Cars: 50/46"). */
static void hidden_extras(void)
{
    static const uint32_t shown[] = {106, 18, 667, 12, 203, 206, 214, 201, 211};
    setup();
    sub_001DE752();
    check_ids(cache, shown, sizeof shown / sizeof shown[0]);
    check_ids(results, shown, sizeof shown / sizeof shown[0]);
    assert(MEM32(catalog + 0x1Cu) == cache + sizeof shown && MEM32(output + 8u) == results + sizeof shown);
    setup();
    assert(owned_cars() == sizeof ids / sizeof ids[0] - 4u);
    MEM32(catalog + 12u) = MEM32(catalog + 8u);
    assert(owned_cars() == 0);
    puts("PASS hidden extras: menu list and owned count both leave the installer's cars out, "
         "catalog order restored");
}

/* The best car of a class for a race opponent. The catalog, its flags
 * included, is exactly as before once the pick returns. */
static uint32_t best_car(uint32_t car_class)
{
    memcpy(expected, memory, sizeof memory);
    pick_class = car_class;
    picked = 0;
    original_calls = 0;
    g_esp = stack; g_ebx = catalog;
    g_esi = 0x44444444u; g_edi = 0x55555555u; g_ebp = 0x66666666u;
    sub_001DE012();
    assert(original_calls == 1);
    assert(g_eax == (picked ? 1u : 0u) && g_ecx == 0xA1B2C3D4u && g_esp == stack + 4u);
    assert(g_ebx == catalog && g_esi == 0x44444444u && g_edi == 0x55555555u && g_ebp == 0x66666666u);
    assert(memcmp(memory, expected, sizeof memory) == 0);
    return picked;
}

/* The lobby's players (sub_001F99C1): list head, then player 2, then
 * player 1 with the catalog IDs of the car chosen (+0x1AC) and its fallback
 * (+0x1B0). */
static void lobby(uint32_t id, uint32_t fallback, uint8_t player)
{
    const uint32_t head = 0x15000u, second = 0x15010u, first = 0x15020u;
    const uint32_t other = 0x15100u, record = 0x15300u;
    MEM32(LOBBY + 0x380u) = head;
    MEM32(head) = second; MEM32(second) = first; MEM32(first) = head;
    MEM32(second + 8u) = other; MEM32(first + 8u) = record;
    MEM8(other + 0x149u) = 2u; MEM32(other + 0x1ACu) = 211u;
    MEM8(record + 0x149u) = player;
    MEM32(record + 0x1ACu) = id; MEM32(record + 0x1B0u) = fallback;
}

static void wait_past_burst(void)
{
    clock_t start = clock();
    while (clock() - start <= CLOCKS_PER_SEC + CLOCKS_PER_SEC / 10) {}
}

/* MM3_SAME_CAR=1: every opponent drives the player's car, whatever it is;
 * the catalog is exactly as before after each pick (best_car). */
static void same_car(void)
{
    unsigned i;
    setup();
    assert(best_car(1) == 203);                     /* no lobby yet: the game's own pick */
    lobby(12, 0, 1);
    for (i = 0; i < 7; ++i) assert(best_car(1) == 12);
    assert(best_car(2) == 0);                       /* a faster class has none: the circuit falls back */
    lobby(9004, 0, 1);
    assert(best_car(1) == 9004);                    /* an installer car too */
    lobby(666, 0, 1);
    assert(best_car(1) == 666);                     /* a car hidden from picks, its flag put back */
    lobby(12345, 214, 1);
    assert(best_car(1) == 214);                     /* the record's fallback ID */
    lobby(12, 0, 3);
    assert(best_car(1) == 203);                     /* no player 1: the game's own pick */
    /* A script that keeps refusing the car (Paris Checkpoint 06 with a
     * Firetruck) gets the game's own picks after 64 in a second. */
    wait_past_burst();
    lobby(12, 0, 1);
    for (i = 0; i < SAME_CAR_BURST; ++i) assert(best_car(1) == 12);
    assert(best_car(1) == 203 && best_car(1) == 203);
    wait_past_burst();
    assert(best_car(1) == 12);
    puts("PASS same car: opponents drive player 1's car (lobby record, fallback ID, hidden or installer "
         "car), nothing for another class, game's own pick without a player 1 or after a refusing burst");
}

int main(int argc, char **argv)
{
    static const uint32_t all[] = {106, 18, 667, 12, 203, 206, 214,
                                  9014, 201, 9004, 9005, 211, 9999};
    static const uint32_t class_one[] = {18, 667, 12, 203, 206, 214,
                                        9014, 9004, 9005, 211, 9999};
    static const uint32_t previous_cache[] = {9004, 12, 211, 106, 206};
    static const uint32_t cached_order[] = {12, 106, 206, 9004, 211};
    unsigned i;
    if (argc > 1 && !strcmp(argv[1], "same")) {
        same_car();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "hidden")) {
        hidden_extras();
        return 0;
    }
    assert(!_putenv_s("MM3_AI_ALL_CARS", ""));
    setup();
    run_case(all, sizeof all / sizeof all[0]);
    /* The returned cached list is already canonical on the next visit. */
    g_esp = stack; g_ebx = catalog; original_calls = 0; cached = 1;
    run_case(all, sizeof all / sizeof all[0]);
    setup();
    race_class = 1;
    run_case(class_one, sizeof class_one / sizeof class_one[0]);
    setup();
    cached = 1;
    for (i = 0; i < sizeof previous_cache / sizeof previous_cache[0]; ++i)
        MEM32(cache + i * 4u) = entry_for_id(previous_cache[i]);
    MEM32(catalog + 0x1Cu) = cache + sizeof previous_cache;
    run_case(cached_order, sizeof cached_order / sizeof cached_order[0]);
    setup();
    cached = 1;
    run_case(NULL, 0); /* Empty cached output remains empty. */
    setup();
    /* Malformed or unready vectors are left intact, never partially sorted. */
    memcpy(expected, memory, sizeof memory);
    order_vector(0);
    MEM32(catalog + 12u) = pointers + 1u;
    order_vector(catalog + 4u);
    MEM32(catalog + 12u) = pointers + sizeof ids;
    MEM32(pointers + 4u) = 0;
    order_vector(catalog + 4u);
    MEM32(pointers + 4u) = entry_for_id(ids[1]);
    assert(memcmp(memory, expected, sizeof memory) == 0);

    /* Race opponents: the class's best Xbox car (retail or downloadable),
     * never an installer car, whatever the circuit option says. */
    setup();
    assert(best_car(1) == 203 && best_car(2) == 106);
    assert(!_putenv_s("MM3_AI_ALL_CARS", "1"));
    assert(best_car(1) == 203);
    /* Circuit races may hand them out, unless the launcher keeps them off. */
    circuit_mode = 1;
    assert(best_car(1) == 9999);
    assert(!_putenv_s("MM3_AI_ALL_CARS", ""));
    assert(best_car(1) == 9999);
    assert(!_putenv_s("MM3_AI_ALL_CARS", "0"));
    assert(best_car(1) == 203);
    assert(!_putenv_s("MM3_AI_ALL_CARS", ""));
    circuit_mode = 0;
    /* A class without a visible Xbox car keeps its installer cars, so the
     * pick never comes back empty; a hidden bonus car does not count. */
    MEM32(entry_for_id(9999) + 0x30u) = 3;
    MEM32(entry_for_id(9014) + 0x30u) = 4;
    MEM32(entry_for_id(666) + 0x30u) = 4;
    assert(best_car(3) == 9999 && best_car(4) == 9014 && best_car(1) == 203);
    /* With the installer's cars in the menu every car counts as owned. */
    setup();
    assert(owned_cars() == sizeof ids / sizeof ids[0]);
    /* No catalog yet: the retail handler alone decides. */
    MEM32(VEHICLE_CATALOG) = 0;
    assert(best_car(1) == 0);
    puts("PASS: retail originals first; DLC/custom slow-to-fast with ID ties; "
         "cached/fresh menus, class filters, hidden locks, saved IDs and ABI preserved; "
         "catalog left in load order; race opponents keep to the Xbox cars outside circuits");
    return 0;
}
