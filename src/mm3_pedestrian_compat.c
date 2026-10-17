#include "recomp_types.h"

/* The native walker completion query joins two zero-flag tests: a state
 * decrement and a null path cursor. The generated fallback lost both tests,
 * so finished walkers never released the mission's passenger wait. Preserve
 * the original register effects, including ECX's adjustment in state one. */
void sub_001466D9(void)
{
    uint32_t value = MEM32(g_ecx + 0xC4u) - 1u;
    int done;
    if (!value) {
        value = MEM32(g_ecx + 0xC8u);
        g_ecx += 0x30u;
        done = value == g_ecx || value == 0u;
    } else {
        --value;
        if (!value) {
            done = 1;
        } else {
            --value;
            if (!value) {
                done = 1;
            } else {
                --value;
                done = value == 0u;
            }
        }
    }
    g_eax = (value & 0xFFFFFF00u) | (done ? 1u : 0u);
    g_esp += 4u;
}
