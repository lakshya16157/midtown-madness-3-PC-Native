#include "recomp_types.h"

/* The CRT's EFLAGS.ID + CPUID MMX probe (sub_00093770) is lifted by the
 * toolkit now: pushfd/popfd keep the g_eflags image, CPUID answers through
 * xbox_Cpuid, and the toolkit defines g_eflags (xbox_memory_layout.c). Only
 * the remainder tail below still needs a hand-written body. */

/* CRT remainder tail: FXCH; FPREM/FNSTSW/SAHF/JP; FSTP ST(1); RET.
 * FPREM completes the reduction and clears C2. The old lift tested the last
 * floating compare instead, so an earlier unordered compare kept even an
 * ordinary finite remainder in this loop forever after a taxi pickup.
 *
 * fmod performs the complete truncating reduction for the double-backed x87
 * model. remquo supplies the low quotient bits without overflowing x / y;
 * its nearest quotient is one above the truncating magnitude exactly when
 * its remainder is negative. x87 publishes Q2/Q1/Q0 in C0/C3/C1. */
void sub_00253748(void)
{
    unsigned top = (unsigned)g_fp_top & 7u;
    unsigned next = (top + 1u) & 7u;
    double dividend = g_fp_stack[next];
    double divisor = g_fp_stack[top];
    double remainder = fmod(dividend, divisor);
    unsigned quotient = 0;
    if (isfinite(dividend) && isfinite(divisor) && divisor != 0.0) {
        int nearest_quotient = 0;
        double nearest_remainder = remquo(fabs(dividend), fabs(divisor),
                                          &nearest_quotient);
        quotient = (unsigned)nearest_quotient;
        if (nearest_remainder < 0.0) --quotient;
        quotient &= 7u;
    }
    g_fp_cc = (uint16_t)(((quotient & 4u) ? 0x0100u : 0u) |
                         ((quotient & 2u) ? 0x4000u : 0u) |
                         ((quotient & 1u) ? 0x0200u : 0u));
    /* The reduction's C2 is clear, including when a prior compare was NaN.
     * g_fp_cmp is the lifter's SAHF parity bridge; do not retain unordered. */
    g_fp_cmp = (g_fp_cc & 0x0100u) ? -1 :
               (g_fp_cc & 0x4000u) ? 0 : 1;
    g_eax = (g_eax & 0xFFFF0000u) | (top << 11) | g_fp_cc;
    g_fp_stack[top] = remainder;
    g_fp_stack[next] = remainder;
    g_fp_top = (int)next;
    g_esp += 4u;
}
