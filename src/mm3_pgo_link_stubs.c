/* Link targets for whole-program (MM3_PGO) builds only.
 *
 * recomp_runtime_entries.c tail-jumps to 0x0009B4E7 and 0x0009B584 only in
 * unreachable code that follows another tail jump. A normal /O2 compile drops
 * that code before the object is written; with /GL the object keeps the
 * references until link-time code generation removes them, and the linker
 * resolves symbols first. These definitions satisfy it. They follow the
 * unresolved-target stubs of recomp_stubs_unresolved.c, consuming the return
 * address a caller pushed, and are never reached. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

#ifndef RECOMP_UNRESOLVED_STUB_OBSERVE
#define RECOMP_UNRESOLVED_STUB_OBSERVE(va) ((void)0)
#endif

void sub_0009B4E7(void) { RECOMP_UNRESOLVED_STUB_OBSERVE(0x0009B4E7u); g_esp += 4; }
void sub_0009B584(void) { RECOMP_UNRESOLVED_STUB_OBSERVE(0x0009B584u); g_esp += 4; }
