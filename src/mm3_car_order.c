#include "recomp_types.h"

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

static int comes_before(uint32_t a, uint32_t b, int catalog_order)
{
    unsigned a_rank = original_rank(MEM32(a));
    unsigned b_rank = original_rank(MEM32(b));
    unsigned extras = (unsigned)(sizeof original_car_ids / sizeof original_car_ids[0]);

    if (a_rank != extras || b_rank != extras) {
        /* Restore catalog order for old installs that ranked all cars by
         * overall score. In the filtered menu, keep the retail available /
         * visible-locked groups and the relative order of original cars. */
        if (a_rank != extras && b_rank != extras)
            return catalog_order && a_rank < b_rank;
        return a_rank != extras;
    }
    if (speed_rating(a) != speed_rating(b))
        return speed_rating(a) < speed_rating(b);
    /* Stable across DLC enumeration order and repeat menu rebuilds. */
    return MEM32(a) < MEM32(b);
}

static void order_vector(uint32_t vector, int catalog_order)
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
        while (position > start && comes_before(entry, MEM32(position - 4u), catalog_order)) {
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
 * Host helpers never touch guest registers, preserving every original effect. */
void sub_001DE752(void)
{
    uint32_t catalog = g_ebx;
    uint32_t output = MEM32(g_esp + 4u);
    if (guest_range(catalog, 0x24u)) order_vector(catalog + 4u, 1);
    sub_001DE752_original();
    if (guest_range(catalog, 0x24u)) order_vector(catalog + 0x14u, 0);
    order_vector(output, 0);
}
