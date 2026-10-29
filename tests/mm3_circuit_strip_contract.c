/* The race picture strip (src/mm3_circuit_races.c): outside Circuit mode the
 * circuits' pictures (races 20-29) are left out of Checkpoint's strip; in
 * Circuit mode every circuit box is drawn with its own race number. The main
 * menu's profile box leaves switched-off circuits out of its Checkpoint line
 * (run with MM3_EXTRA_CIRCUITS=0 and ten Paris circuits installed). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp_types.h"
#include "mm3_circuit_races.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char memory[0x100000];
static unsigned drawn;
static uint32_t drawn_box;
enum { STACK = 0x18000, ITERATOR = 0x19000, NODE = 0x1A000, RETURN = 0x00103D8Fu };

/* The box draw: stdcall, nine words and four floats (RET 0x34). */
void sub_001038D0_original(void)
{
    assert(MEM32(g_esp) == RETURN);
    ++drawn;
    drawn_box = MEM32(g_esp + 0x18);
    g_esp += 4 + 0x34;
}
void sub_00103BE1_original(void) { assert(!"strip walk not expected"); }
void sub_001238B7_original(void) { assert(!"race setup not expected"); }
void sub_00120A9A_original(void) { assert(!"menu path not expected"); }
void sub_001038D0(void);
void sub_001E0F69(void);
void sub_001E4F97(void);

/* The race list's count for (city in EAX, mode): RET 8. */
static uint32_t list_count;
void sub_001E0F69_original(void)
{
    g_eax = list_count;
    g_esp += 12;
}

/* Races won: the set bits of the city's 64-bit medal mask for the mode
 * (profile + 0x80 + (3 * city + mode slot) * 8); RET 12. */
void sub_001E4F97_original(void)
{
    uint32_t profile = MEM32(g_esp + 4), city = MEM32(g_esp + 8), mode = MEM32(g_esp + 12), bit, won = 0;
    uint32_t mask = profile + 0x80 + (3 * city + (mode == 3 ? 2 : 1)) * 8;
    for (bit = 0; bit < 64; ++bit) won += (MEM32(mask + (bit / 32) * 4) >> (bit % 32)) & 1;
    g_eax = won;
    g_esp += 16;
}

enum { PROFILE = 0x1C000 };

static uint32_t races(uint32_t caller, uint32_t city)
{
    g_esp = STACK;
    MEM32(STACK) = caller;
    g_eax = city;
    sub_001E0F69();
    assert(g_esp == STACK + 12);
    return g_eax;
}

static uint32_t won(uint32_t caller, uint32_t city, uint32_t mode)
{
    g_esp = STACK;
    MEM32(STACK) = caller;
    MEM32(STACK + 4) = PROFILE;
    MEM32(STACK + 8) = city;
    MEM32(STACK + 12) = mode;
    sub_001E4F97();
    assert(g_esp == STACK + 16);
    return g_eax;
}

/* Draw box `box` for a picture of `race`; returns 1 when it was drawn. */
static int box(uint32_t race, uint32_t box_number, uint32_t iterator)
{
    unsigned before = drawn;
    MEM32(NODE + 0xC) = race;
    MEM32(ITERATOR) = NODE;
    g_esp = STACK;
    MEM32(STACK) = RETURN;
    MEM32(STACK + 0x14) = iterator;
    MEM32(STACK + 0x18) = box_number;
    sub_001038D0();
    assert(g_esp == STACK + 4 + 0x34);   /* the caller's stack, drawn or not */
    return drawn != before;
}

int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)memory;
    /* Checkpoint (and every mode but Circuit): retail and DLC races drawn,
     * circuits left out. */
    mm3_circuit_set_mode(0);
    assert(box(0, 0, ITERATOR) && drawn_box == 0);
    assert(box(19, 19, ITERATOR) && drawn_box == 19);
    assert(!box(20, 20, ITERATOR) && !box(29, 29, ITERATOR));
    assert(box(25, 25, 0));                      /* unreadable iterator: as retail */
    /* Circuit mode: the walk starts at the first circuit; box n is race 20 + n. */
    mm3_circuit_set_mode(1);
    assert(box(20, 0, ITERATOR) && drawn_box == 20);
    assert(box(29, 9, ITERATOR) && drawn_box == 29);
    mm3_circuit_set_mode(0);
    assert(!box(21, 21, ITERATOR));
    puts("PASS circuit strip: Checkpoint leaves the circuits' pictures out (stack as after RET 0x34); "
         "Circuit draws each circuit with its own race number");

    /* Profile box, circuits switched off: Paris lists 30 races (retail, DLC,
     * ten circuits) and has won races 0-11 and circuits 20-24. */
    list_count = 30;
    MEM32(PROFILE + 0x80 + 2 * 8) = 0x01F00FFFu;   /* Paris Checkpoint */
    MEM32(PROFILE + 0x80 + 2 * 8 + 4) = 0;
    assert(races(0x0010F994u, 0) == 20);
    assert(won(0x0010F974u, 0, 3) == 12);
    assert(MEM32(PROFILE + 0x80 + 2 * 8) == 0x01F00FFFu);  /* the mask is put back */
    /* Other callers (the Checkpoint menu, Blitz's line) and a city without
     * circuits keep the game's numbers. */
    assert(races(0x001238F8u, 0) == 30 && races(0x0010F933u, 0) == 30 && races(0x0010F994u, 1) == 30);
    assert(won(0x0010F90Du, 0, 2) == 0 && won(0x0011F12Bu, 0, 3) == 17 && won(0x0010F974u, 1, 3) == 0);
    puts("PASS profile box: switched-off circuits left out of the Checkpoint line (races and wins), "
         "other callers and cities unchanged");
    return 0;
}
