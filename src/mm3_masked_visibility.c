/* Retail masked OBB/frustum leaf (1C7B15). Its seven x87 scratch slots have
 * statically known positions; scalar locals avoid a TLS TOP update and TLS
 * array access for each instruction. Keep every ordered PC-rounded operation,
 * float scratch store, mask update, status word and final popped slot. The
 * existing building-diagnostics wrapper remains the public entry point.
 *
 * Unusual aliases, large/nonfinite operands and long lists retain the actual
 * generated body. Do not enable fast-math, reassociate sums or introduce FMA.
 */
#include "recomp_types.h"

extern void sub_001C7B15_generated(void);

static inline int masked_visibility_overlap(uint32_t a, uint32_t an,
                                            uint32_t b, uint32_t bn)
{
    return (uint64_t)a < (uint64_t)b + bn &&
        (uint64_t)b < (uint64_t)a + an;
}

static inline int masked_visibility_ram(uint32_t address, uint32_t bytes)
{
    /* MM3 main.c maps this entire 128 MiB base arena as committed backing,
     * independently of its reported 64 MiB physical RAM. Avoid speculative
     * reads from mirrors, MMIO, null pages and extended virtual reservations. */
    return address >= 0x10000u &&
        (uint64_t)address + bytes <= 0x08000000u;
}

static int masked_visibility_fast_inputs(void)
{
    uint32_t count, n, j, bits;
    uint32_t frame_bottom, frame_size = 64;
    if (g_esp < 56 ||
        !masked_visibility_ram(g_esp - 56, frame_size) ||
        !masked_visibility_ram(g_ecx, 60))
        return 0;
    count = MEM32(g_esp + 4);
    /* A regular frustum has six planes. Above 32, the native mask becomes
     * zero but the guest still visits the remaining records. Preserve that
     * uncommon contract without scanning an unbounded input list here. */
    if (count > 32 || (count &&
        (!masked_visibility_ram(g_edx, count * 16) ||
         !masked_visibility_ram(g_ebx, 4))))
        return 0;
    frame_bottom = g_esp - 56;
    if (masked_visibility_overlap(g_ecx, 60, frame_bottom, frame_size) ||
        masked_visibility_overlap(g_edx, count * 16, frame_bottom, frame_size) ||
        (count && masked_visibility_overlap(g_ebx, 4, frame_bottom, frame_size)) ||
        (count && masked_visibility_overlap(g_ebx, 4, g_ecx, 60)) ||
        (count && masked_visibility_overlap(g_ebx, 4, g_edx, count * 16)))
        return 0;

    /* Three factors contribute to a radius. |input| <= 2^40 keeps every
     * ordered product/sum finite even at the guest float scratch stores.
     * Integer bit checks preserve FP exception flags and NaN payloads. */
    for (n = 0; n != 15; ++n) {
        if (n >= 9 && n < 12) continue; /* Center is read only for set bits. */
        if ((MEM32(g_ecx + n * 4) & 0x7fffffffu) > 0x53800000u)
            return 0;
    }
    if (!count) return 1;
    bits = MEM32(g_ebx);
    if (count < 32) bits &= (1u << count) - 1u;
    if (!bits) return 1;
    for (n = 9; n != 12; ++n)
        if ((MEM32(g_ecx + n * 4) & 0x7fffffffu) > 0x53800000u)
            return 0;
    for (n = 0; n != count; ++n) {
        if (!(bits & (1u << n))) continue;
        for (j = 0; j != 4; ++j)
            if ((MEM32(g_edx + n * 16 + j * 4) & 0x7fffffffu) > 0x53800000u)
                return 0;
    }
    return 1;
}

void sub_001C7B15_original(void)
{
    const unsigned top = (unsigned)g_fp_top;
    double f1, f2, f3, f4, f5, f6, f7;
    uint32_t frame = g_ebp, bit = 1, mask, plane = g_edx;

    if (!masked_visibility_fast_inputs()) {
        sub_001C7B15_generated();
        return;
    }
    f1 = g_fp_stack[(top + 1) & 7u];
    f2 = g_fp_stack[(top + 2) & 7u];
    f3 = g_fp_stack[(top + 3) & 7u];

    PUSH32(g_esp, frame);
    frame = g_esp;
    g_ebp = g_seh_ebp = frame;
    g_esp -= 0x2C;
    f7 = MEMF(g_ecx + 0x30);
    MEM32(frame - 4) &= 0;
    PUSH32(g_esp, g_esi);
    f6 = RECOMP_FP_PC(f7 * MEMF(g_ecx));
    MEMF(frame - 44) = (float)f6;
    PUSH32(g_esp, g_edi);
    f6 = RECOMP_FP_PC(f7 * MEMF(g_ecx + 4));
    MEMF(frame - 40) = (float)f6;
    f7 = RECOMP_FP_PC(f7 * MEMF(g_ecx + 8));
    f6 = MEMF(g_ecx + 0x34);
    f5 = RECOMP_FP_PC(f6 * MEMF(g_ecx + 0xC));
    MEMF(frame - 32) = (float)f5;
    f5 = RECOMP_FP_PC(f6 * MEMF(g_ecx + 0x10));
    MEMF(frame - 28) = (float)f5;
    f6 = RECOMP_FP_PC(f6 * MEMF(g_ecx + 0x14));
    f5 = MEMF(g_ecx + 0x38);
    f4 = RECOMP_FP_PC(f5 * MEMF(g_ecx + 0x18));
    MEMF(frame - 20) = (float)f4;
    f4 = RECOMP_FP_PC(f5 * MEMF(g_ecx + 0x1C));
    MEMF(frame - 16) = (float)f4;
    f5 = RECOMP_FP_PC(f5 * MEMF(g_ecx + 0x20));
    MEMF(frame - 12) = (float)f5;
    if (!MEM32(frame + 8)) goto accepted;

    do {
        mask = MEM32(g_ebx);
        if (!(bit & mask)) goto next_plane;

        /* Center distance: retail Z,Y,X summation, then plane offset. */
        f5 = RECOMP_FP_PC((double)MEMF(g_ecx + 0x2C) * MEMF(plane + 8));
        f4 = RECOMP_FP_PC((double)MEMF(g_ecx + 0x28) * MEMF(plane + 4));
        f5 = RECOMP_FP_PC(f5 + f4);
        f4 = RECOMP_FP_PC((double)MEMF(plane) * MEMF(g_ecx + 0x24));
        f5 = RECOMP_FP_PC(f5 + f4);
        f5 = RECOMP_FP_PC(f5 + MEMF(plane + 0xC));

        /* Axis 2 was rounded to float in the prologue. Axis 0/1 Z remain
         * in x87 slots f7/f6 and must retain their PC-controlled precision. */
        f4 = RECOMP_FP_PC((double)MEMF(frame - 20) * MEMF(plane));
        f3 = RECOMP_FP_PC((double)MEMF(frame - 12) * MEMF(plane + 8));
        f4 = RECOMP_FP_PC(f4 + f3);
        f3 = RECOMP_FP_PC((double)MEMF(frame - 16) * MEMF(plane + 4));
        f4 = fabs(RECOMP_FP_PC(f4 + f3));
        f3 = RECOMP_FP_PC((double)MEMF(frame - 32) * MEMF(plane));
        f2 = RECOMP_FP_PC(f6 * MEMF(plane + 8));
        f3 = RECOMP_FP_PC(f3 + f2);
        f2 = RECOMP_FP_PC((double)MEMF(frame - 28) * MEMF(plane + 4));
        f3 = fabs(RECOMP_FP_PC(f3 + f2));
        { double temporary = f3; f3 = f4; f4 = temporary; }
        f2 = f4;
        f3 = RECOMP_FP_PC(f3 + f2);
        f2 = RECOMP_FP_PC((double)MEMF(frame - 44) * MEMF(plane));
        f1 = RECOMP_FP_PC(f7 * MEMF(plane + 8));
        f2 = RECOMP_FP_PC(f2 + f1);
        f1 = RECOMP_FP_PC((double)MEMF(frame - 40) * MEMF(plane + 4));
        f2 = fabs(RECOMP_FP_PC(f2 + f1));
        MEMF(frame - 8) = (float)f2;
        f3 = RECOMP_FP_PC(f3 + f2);
        f4 = f3;
        f3 = f5;
        g_fp_cmp = RECOMP_FCMP(f3, f4);
        g_fp_cc = RECOMP_FCMP_CC(g_fp_cmp);
        g_eax = (g_eax & 0xFFFF0000u) |
            (uint16_t)((((top + 4) & 7u) << 11) | g_fp_cc);
        if (!(HI8(g_eax) & 0x41)) {
            SET_LO8(g_eax, 1);
            goto finished;
        }

        f3 = -f4;
        f2 = f5;
        g_fp_cmp = RECOMP_FCMP(f2, f3);
        g_fp_cc = RECOMP_FCMP_CC(g_fp_cmp);
        g_eax = (g_eax & 0xFFFF0000u) |
            (uint16_t)((((top + 4) & 7u) << 11) | g_fp_cc);
        if (!RECOMP_PARITY8(HI8(g_eax) & 5))
            MEM32(g_ebx) = g_eax = ~bit & mask;

next_plane:
        MEM32(frame - 4) += 1;
        g_eax = MEM32(frame - 4);
        bit += bit;
        plane += 0x10;
    } while (g_eax < MEM32(frame + 8));

accepted:
    SET_LO8(g_eax, 0);
finished:
    g_edx = plane;
    POP32(g_esp, g_edi);
    POP32(g_esp, g_esi);
    g_esp = frame;
    POP32(g_esp, frame);
    g_esp += 8; /* ret 4 */
    g_fp_stack[(top + 1) & 7u] = f1;
    g_fp_stack[(top + 2) & 7u] = f2;
    g_fp_stack[(top + 3) & 7u] = f3;
    g_fp_stack[(top + 4) & 7u] = f4;
    g_fp_stack[(top + 5) & 7u] = f5;
    g_fp_stack[(top + 6) & 7u] = f6;
    g_fp_stack[(top + 7) & 7u] = f7;
}
