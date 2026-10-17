#include "recomp_types.h"

/* Supply the flags register declared by the runtime header so native gameplay
 * helpers can preserve the complete guest state around temporary calls. */
RECOMP_TLS uint32_t g_eflags = 0x202u;

/* This CRT helper tests EFLAGS.ID, then CPUID.1:EDX.MMX. Both are supported
 * by the Xbox processor. Recover that result without the untranslated flag
 * instructions or querying the host processor.
 *
 * The generated ABI publishes each standard frame through g_ebp/g_seh_ebp,
 * even after returning. Keep that bridge and the original saved stack words:
 * later Bink functions inherit it. Fresh probes also expose CPUID's ECX/EDX.
 * Bink's flag must follow its original argument; skipping its optimized
 * initialization leaves its later conversion callbacks uninitialized. */
void sub_00093770(void)
{
    uint32_t entry_sp = g_esp;
    uint32_t frame = entry_sp - 4u;
    uint32_t available = MEM32(0x00393004u);

    MEM32(frame) = g_ebp;
    MEM32(frame - 4u) = g_ecx;
    MEM32(frame - 8u) = g_ebx;
    g_ebp = frame;
    g_seh_ebp = frame;
    if ((int32_t)available < 0) {
        uint32_t cpuid_a, cpuid_b;
        MEM32(frame - 12u) = g_ebx;
        recomp_cpuid(1u, g_ecx, &cpuid_a, &cpuid_b, &g_ecx, &g_edx);
        available = (g_edx & 0x00800000u) != 0u;
        MEM32(frame - 4u) = available;
        MEM32(0x00393004u) = available;
    }
    g_eax = available;
    MEM32(0x0046E580u) = available != 0u &&
                           (MEM32(entry_sp + 4u) & 0x00040000u) == 0u;
    g_esp = entry_sp + 4u; /* RET; caller owns the Bink flag argument. */
}

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
