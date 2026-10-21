/* Retail 4x4 matrix multiply (259982). The measured hot leaf uses eight x87
 * scratch slots but makes no calls. Keep those values in native scalars, as
 * in mm3_visibility.c, instead of rotating and indexing the TLS stack for
 * every instruction. Preserve the retail load/store order, its two alias
 * paths, ordered PC-rounded arithmetic, stack writes and all eight final
 * x87 slots. In particular, do not reassociate these sums or introduce FMA.
 * tests/mm3_matrix_regression.py compares complete state and guest memory.
 */
#include "recomp_types.h"

extern void sub_00259982_original(void);

static int matrix_partial_overlap(uint32_t first, uint32_t second)
{
    return first != second && (uint64_t)first < (uint64_t)second + 64 &&
        (uint64_t)second < (uint64_t)first + 64;
}

static int matrix_fast_inputs(void)
{
    uint32_t out = MEM32(g_esp + 4), left = MEM32(g_esp + 8);
    uint32_t right = MEM32(g_esp + 12), n;
    uint64_t bottom = (uint64_t)g_esp - 84, end = (uint64_t)g_esp + 16;
    if (matrix_partial_overlap(out, left) || matrix_partial_overlap(out, right) ||
        matrix_partial_overlap(left, right) || (g_df && out == left && out == right))
        return 0;
    /* Inputs that overlap the guest frame can observe its retail pushes and
     * REP copy. Leave those unusual cases to the unchanged generated body. */
    if (((uint64_t)out < end && (uint64_t)out + 64 > bottom) ||
        ((uint64_t)left < end && (uint64_t)left + 64 > bottom) ||
        ((uint64_t)right < end && (uint64_t)right + 64 > bottom))
        return 0;
    for (n = 0; n != 16; ++n) {
        /* |input| <= 2^60 guarantees every ordered sum stays finite when
         * stored as float. Preserve generated NaN payload selection and all
         * overflow cases with the original body, rather than assuming host
         * compilers choose identical operands for commutative arithmetic. */
        if ((MEM32(left + n * 4) & 0x7fffffffu) > 0x5d800000u ||
            (MEM32(right + n * 4) & 0x7fffffffu) > 0x5d800000u)
            return 0;
    }
    return 1;
}

void sub_00259982(void)
{
    const unsigned top = (unsigned)g_fp_top;
    uint32_t frame = g_ebp, dst, left, right;
    double a0, a1, a2, a3, p0, p1, p2, p3, s13, sum;
    unsigned outer, inner;

    if (!matrix_fast_inputs()) {
        sub_00259982_original();
        return;
    }

    PUSH32(g_esp, frame);
    frame = g_esp;
    g_ebp = g_seh_ebp = frame;
    g_esp -= 0x40;
    g_eax = MEM32(frame + 8);
    PUSH32(g_esp, g_ebx);
    PUSH32(g_esp, g_esi);
    g_esi = MEM32(frame + 0x10);
    PUSH32(g_esp, g_edi);

    if (g_esi == g_eax && MEM32(frame + 0xC) != g_eax) {
        /* Destination aliases only the right input: hold each right-hand
         * column before storing its four output cells, exactly as retail. */
        dst = MEM32(frame + 8);
        left = MEM32(frame + 0xC);
        right = MEM32(frame + 0x10);
        for (outer = 0; outer != 4; ++outer) {
            a0 = MEMF(right + outer * 4);
            a1 = MEMF(right + outer * 4 + 0x10);
            a2 = MEMF(right + outer * 4 + 0x20);
            a3 = MEMF(right + outer * 4 + 0x30);
            for (inner = 0; inner != 4; ++inner) {
                p0 = RECOMP_FP_PC(a0 * MEMF(left + inner * 16));
                p1 = RECOMP_FP_PC(a1 * MEMF(left + inner * 16 + 4));
                p2 = RECOMP_FP_PC(a2 * MEMF(left + inner * 16 + 8));
                p3 = RECOMP_FP_PC(a3 * MEMF(left + inner * 16 + 12));
                p2 = RECOMP_FP_PC(p2 + p0);
                s13 = RECOMP_FP_PC(p1 + p3);
                sum = RECOMP_FP_PC(p2 + s13);
                MEMF(dst + inner * 16 + outer * 4) = (float)sum;
            }
        }
        g_ecx = left;
        g_edx = right;
    } else {
        if (g_esi == g_eax) {
            /* Both inputs alias the output: retain REP MOVSD's guest stack
             * copy and argument rewrite, including its direction flag. */
            PUSH32(g_esp, 0x10);
            POP32(g_esp, g_ecx);
            g_edi = frame - 64;
            if (!g_df) {
                uint8_t *d = (uint8_t *)XBOX_PTR(g_edi);
                uint8_t *s = (uint8_t *)XBOX_PTR(g_esi);
                uint32_t n = g_ecx * 4, i;
                if (d + n <= s || s + n <= d) memcpy(d, s, n);
                else for (i = 0; i < g_ecx; ++i)
                    MEM32(g_edi + i * 4) = MEM32(g_esi + i * 4);
                g_esi += g_ecx * 4;
                g_edi += g_ecx * 4;
            } else {
                uint32_t i;
                for (i = 0; i < g_ecx; ++i)
                    MEM32(g_edi - i * 4) = MEM32(g_esi - i * 4);
                g_esi -= g_ecx * 4;
                g_edi -= g_ecx * 4;
            }
            g_ecx = frame - 64;
            MEM32(frame + 0x10) = g_ecx;
        }
        dst = MEM32(frame + 8);
        left = MEM32(frame + 0xC);
        right = MEM32(frame + 0x10);
        for (outer = 0; outer != 4; ++outer) {
            a0 = MEMF(left);
            a1 = MEMF(left + 4);
            a2 = MEMF(left + 8);
            a3 = MEMF(left + 12);
            for (inner = 0; inner != 4; ++inner) {
                p0 = RECOMP_FP_PC(a0 * MEMF(right + inner * 4));
                p1 = RECOMP_FP_PC(a1 * MEMF(right + inner * 4 + 0x10));
                p2 = RECOMP_FP_PC(a2 * MEMF(right + inner * 4 + 0x20));
                p3 = RECOMP_FP_PC(a3 * MEMF(right + inner * 4 + 0x30));
                p2 = RECOMP_FP_PC(p2 + p0);
                s13 = RECOMP_FP_PC(p1 + p3);
                sum = RECOMP_FP_PC(p2 + s13);
                MEMF(dst + inner * 4) = (float)sum;
            }
            left += 16;
            dst += 16;
        }
        g_ecx = left;
        g_edx = right;
    }

    POP32(g_esp, g_edi);
    POP32(g_esp, g_esi);
    POP32(g_esp, g_ebx);
    g_esp = frame;
    POP32(g_esp, frame);
    g_esp += 16; /* ret 12 */

    /* Four batches of four FLDs rotate TOP by sixteen. The last held input
     * and last dot product overwrite every slot, including popped slots. */
    g_fp_stack[top] = a3;
    g_fp_stack[(top + 1) & 7u] = a2;
    g_fp_stack[(top + 2) & 7u] = a1;
    g_fp_stack[(top + 3) & 7u] = a0;
    g_fp_stack[(top + 4) & 7u] = p0;
    g_fp_stack[(top + 5) & 7u] = p3;
    g_fp_stack[(top + 6) & 7u] = s13;
    g_fp_stack[(top + 7) & 7u] = sum;
}
