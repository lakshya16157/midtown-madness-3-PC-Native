/* The race picture strip (src/mm3_circuit_races.c): outside Circuit mode the
 * circuits' pictures (races 20-29) are left out of Checkpoint's strip; in
 * Circuit mode every circuit box is drawn with its own race number. */
#include <assert.h>
#include <stdio.h>
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
    return 0;
}
