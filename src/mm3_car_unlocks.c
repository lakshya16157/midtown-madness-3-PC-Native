#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

extern void sub_001DEA30_original(void);
extern void sub_001E50A9_original(void);

/* Profile key lookup (this = EAX, the profile's key set; the key is the stack
 * argument; RET 4, AL = found). The car-selection builder sub_001DE752 asks it
 * for "<car>.<n>" (the "." at 0x3AF02C) for n = 1.. and counts each found
 * paint job into the catalog entry's +0x7C; a hideout pickup adds that key
 * (sub_00212E8F). MM3_UNLOCK_ALL_PAINTS=1 answers "found" at that one call
 * site (return address 0x001DE940) only: the save is not written, the
 * hideouts still work and every other key keeps its stored answer. */
void sub_001E50A9(void)
{
    static int on = -1;
    uint32_t return_va = MEM32(g_esp);
    sub_001E50A9_original();
    if (on < 0) {
        const char *setting = getenv("MM3_UNLOCK_ALL_PAINTS");
        on = setting && !strcmp(setting, "1");
        if (on) {
            fprintf(stderr, "[MM3_CARS] all paint jobs available\n");
            fflush(stderr);
        }
    }
    if (on && return_va == 0x001DE940u)
        SET_LO8(g_eax, 1);
}

/* PAL vehicle availability refresh. ESI identifies an already loaded catalog
 * entry; +0x29 is its LOCKED byte. The parser and both refresh paths use this
 * function before the menu builds its vehicle list. Keep the original profile
 * evaluation, RET 4 and AL unlock-notification result. The optional override
 * exposes the entry, including hidden bonus cars in the existing list builder.
 * Catalog loading, race classes and paint/profile keys retain their semantics.
 * Only MM3_UNLOCK_ALL_CARS=1 overrides the original availability rules. */
void sub_001DEA30(void)
{
    uint32_t vehicle = g_esi;
    const char *setting = getenv("MM3_UNLOCK_ALL_CARS");

    sub_001DEA30_original();
    if (setting && !strcmp(setting, "1")) {
        static int reported;
        if (!reported) {
            fprintf(stderr, "[MM3_CARS] all installed cars available\n");
            fflush(stderr);
            reported = 1;
        }
        MEM8(vehicle + 0x29u) = 0;
    }
}
