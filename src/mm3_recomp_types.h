/* Midtown Madness 3's title layer over the runtime's generic recomp_types.h.
 * The generator and CMake copy that runtime header beside this file as
 * recomp_base.h. Only MM3-specific choices live here. */
#ifndef MM3_RECOMP_TYPES_H
#define MM3_RECOMP_TYPES_H

#include <stdint.h>

/* MM3's rep movs copies go through the hardware-window-safe memcpy. */
#define RECOMP_GUEST_MEMCPY 1

#if defined(__has_include) && __has_include("recomp_base.h")
#include "recomp_base.h"
#else
#include "../tools/xboxrecomp/templates/runtime/recomp_types.h"
#endif

/* ================================================================
 * Per-function register and memory-base cache (generated code)
 *
 * Every guest memory access is volatile, and MSVC treats a volatile access as
 * a full compiler barrier: after each one, generated code reloaded
 * g_xbox_mem_offset and walked the TLS chain again (_tls_index, gs:[58h], the
 * thread's block) for every register it touched. That was about a quarter of
 * all generated instructions (a third in the x87-heavy functions).
 *
 * The build inserts RECOMP_GEN_PROLOGUE at the top of every generated
 * function (CMakeLists.txt, MM3_GEN_REGISTER_CACHE, via
 * scripts/mm3_gen_register_cache.py, which leaves data decoded as code on the
 * thread-local names: mm3_gen_cache_off.h / _on.h). It takes the addresses
 * of this thread's register variables and the guest base once, into const
 * locals the compiler keeps in registers. Every access still goes to the same
 * thread-local storage, so state stays shared with callees and the kernel
 * exactly as before. Valid because a guest function runs start to finish on
 * one host thread (no fibers) and the base never changes after memory
 * initialisation. The helpers below are defined before the names are
 * redefined, so they still reach the real variables.
 * ================================================================ */
#if defined(RECOMP_GENERATED_CODE) && defined(MM3_GEN_REGISTER_CACHE)
#define RECOMP_GEN_ADDR(type, name) \
    RECOMP_ALWAYS_INLINE type *recomp_addr_##name(void) { return &name; }
RECOMP_GEN_ADDR(uint32_t, g_eax) RECOMP_GEN_ADDR(uint32_t, g_ecx) RECOMP_GEN_ADDR(uint32_t, g_edx)
RECOMP_GEN_ADDR(uint32_t, g_esp) RECOMP_GEN_ADDR(uint32_t, g_ebx) RECOMP_GEN_ADDR(uint32_t, g_esi)
RECOMP_GEN_ADDR(uint32_t, g_edi) RECOMP_GEN_ADDR(uint32_t, g_ebp) RECOMP_GEN_ADDR(uint32_t, g_eflags)
RECOMP_GEN_ADDR(uint32_t, g_seh_ebp) RECOMP_GEN_ADDR(uint32_t, g_fs_base) RECOMP_GEN_ADDR(int, g_df)
RECOMP_GEN_ADDR(int, g_fp_top) RECOMP_GEN_ADDR(uint16_t, g_fp_control_word)
RECOMP_GEN_ADDR(int, g_fp_cmp) RECOMP_GEN_ADDR(uint16_t, g_fp_cc)
RECOMP_GEN_ADDR(RecompXmm, g_xmm0) RECOMP_GEN_ADDR(RecompXmm, g_xmm1) RECOMP_GEN_ADDR(RecompXmm, g_xmm2)
RECOMP_GEN_ADDR(RecompXmm, g_xmm3) RECOMP_GEN_ADDR(RecompXmm, g_xmm4) RECOMP_GEN_ADDR(RecompXmm, g_xmm5)
RECOMP_GEN_ADDR(RecompXmm, g_xmm6) RECOMP_GEN_ADDR(RecompXmm, g_xmm7)
RECOMP_GEN_ADDR(RecompMmx, g_mm0) RECOMP_GEN_ADDR(RecompMmx, g_mm1) RECOMP_GEN_ADDR(RecompMmx, g_mm2)
RECOMP_GEN_ADDR(RecompMmx, g_mm3) RECOMP_GEN_ADDR(RecompMmx, g_mm4) RECOMP_GEN_ADDR(RecompMmx, g_mm5)
RECOMP_GEN_ADDR(RecompMmx, g_mm6) RECOMP_GEN_ADDR(RecompMmx, g_mm7)
RECOMP_ALWAYS_INLINE double *recomp_addr_g_fp_stack(void) { return g_fp_stack; }
#undef RECOMP_GEN_ADDR

#define RECOMP_GEN_PTR(type, name) type *const recomp_p_##name = recomp_addr_##name();
#define RECOMP_GEN_PROLOGUE \
    RECOMP_GEN_PTR(uint32_t, g_eax) RECOMP_GEN_PTR(uint32_t, g_ecx) RECOMP_GEN_PTR(uint32_t, g_edx) \
    RECOMP_GEN_PTR(uint32_t, g_esp) RECOMP_GEN_PTR(uint32_t, g_ebx) RECOMP_GEN_PTR(uint32_t, g_esi) \
    RECOMP_GEN_PTR(uint32_t, g_edi) RECOMP_GEN_PTR(uint32_t, g_ebp) RECOMP_GEN_PTR(uint32_t, g_eflags) \
    RECOMP_GEN_PTR(uint32_t, g_seh_ebp) RECOMP_GEN_PTR(uint32_t, g_fs_base) RECOMP_GEN_PTR(int, g_df) \
    RECOMP_GEN_PTR(int, g_fp_top) RECOMP_GEN_PTR(uint16_t, g_fp_control_word) \
    RECOMP_GEN_PTR(int, g_fp_cmp) RECOMP_GEN_PTR(uint16_t, g_fp_cc) \
    RECOMP_GEN_PTR(RecompXmm, g_xmm0) RECOMP_GEN_PTR(RecompXmm, g_xmm1) RECOMP_GEN_PTR(RecompXmm, g_xmm2) \
    RECOMP_GEN_PTR(RecompXmm, g_xmm3) RECOMP_GEN_PTR(RecompXmm, g_xmm4) RECOMP_GEN_PTR(RecompXmm, g_xmm5) \
    RECOMP_GEN_PTR(RecompXmm, g_xmm6) RECOMP_GEN_PTR(RecompXmm, g_xmm7) \
    RECOMP_GEN_PTR(RecompMmx, g_mm0) RECOMP_GEN_PTR(RecompMmx, g_mm1) RECOMP_GEN_PTR(RecompMmx, g_mm2) \
    RECOMP_GEN_PTR(RecompMmx, g_mm3) RECOMP_GEN_PTR(RecompMmx, g_mm4) RECOMP_GEN_PTR(RecompMmx, g_mm5) \
    RECOMP_GEN_PTR(RecompMmx, g_mm6) RECOMP_GEN_PTR(RecompMmx, g_mm7) \
    RECOMP_GEN_PTR(double, g_fp_stack) \
    const uintptr_t recomp_mem_base = (uintptr_t)g_xbox_mem_offset; \
    (void)recomp_mem_base; RECOMP_GEN_UNUSED
#define RECOMP_GEN_UNUSED \
    (void)recomp_p_g_eax; (void)recomp_p_g_ecx; (void)recomp_p_g_edx; (void)recomp_p_g_esp; \
    (void)recomp_p_g_ebx; (void)recomp_p_g_esi; (void)recomp_p_g_edi; (void)recomp_p_g_ebp; \
    (void)recomp_p_g_eflags; (void)recomp_p_g_seh_ebp; (void)recomp_p_g_fs_base; (void)recomp_p_g_df; \
    (void)recomp_p_g_fp_top; (void)recomp_p_g_fp_control_word; (void)recomp_p_g_fp_cmp; \
    (void)recomp_p_g_fp_cc; (void)recomp_p_g_xmm0; (void)recomp_p_g_xmm1; (void)recomp_p_g_xmm2; \
    (void)recomp_p_g_xmm3; (void)recomp_p_g_xmm4; (void)recomp_p_g_xmm5; (void)recomp_p_g_xmm6; \
    (void)recomp_p_g_xmm7; (void)recomp_p_g_mm0; (void)recomp_p_g_mm1; (void)recomp_p_g_mm2; \
    (void)recomp_p_g_mm3; (void)recomp_p_g_mm4; (void)recomp_p_g_mm5; (void)recomp_p_g_mm6; \
    (void)recomp_p_g_mm7; (void)recomp_p_g_fp_stack;

/* x87 state in locals for functions doing a lot of x87 work
 * (scripts/mm3_gen_x87_locals.py, MM3_GEN_X87_LOCALS). The slots are held
 * relative to the top on entry, so in straight-line code their indices are
 * constants. Written back before every call and return, read again after
 * every call; nothing else on this thread sees the state in between. */
#define RECOMP_X87_LOAD_SLOTS() do { \
    x87_slot[0] = recomp_p_g_fp_stack[(x87_base + 0) & 7]; x87_slot[1] = recomp_p_g_fp_stack[(x87_base + 1) & 7]; \
    x87_slot[2] = recomp_p_g_fp_stack[(x87_base + 2) & 7]; x87_slot[3] = recomp_p_g_fp_stack[(x87_base + 3) & 7]; \
    x87_slot[4] = recomp_p_g_fp_stack[(x87_base + 4) & 7]; x87_slot[5] = recomp_p_g_fp_stack[(x87_base + 5) & 7]; \
    x87_slot[6] = recomp_p_g_fp_stack[(x87_base + 6) & 7]; x87_slot[7] = recomp_p_g_fp_stack[(x87_base + 7) & 7]; \
} while (0)
#define RECOMP_X87_ENTER \
    int x87_base = *recomp_p_g_fp_top, x87_rel = 0, x87_cmp = *recomp_p_g_fp_cmp; \
    uint16_t x87_cw = *recomp_p_g_fp_control_word, x87_cc = *recomp_p_g_fp_cc; \
    double x87_slot[8]; \
    RECOMP_X87_LOAD_SLOTS();
#define RECOMP_X87_FLUSH() do { \
    recomp_p_g_fp_stack[(x87_base + 0) & 7] = x87_slot[0]; recomp_p_g_fp_stack[(x87_base + 1) & 7] = x87_slot[1]; \
    recomp_p_g_fp_stack[(x87_base + 2) & 7] = x87_slot[2]; recomp_p_g_fp_stack[(x87_base + 3) & 7] = x87_slot[3]; \
    recomp_p_g_fp_stack[(x87_base + 4) & 7] = x87_slot[4]; recomp_p_g_fp_stack[(x87_base + 5) & 7] = x87_slot[5]; \
    recomp_p_g_fp_stack[(x87_base + 6) & 7] = x87_slot[6]; recomp_p_g_fp_stack[(x87_base + 7) & 7] = x87_slot[7]; \
    *recomp_p_g_fp_top = (x87_base + x87_rel) & 7; *recomp_p_g_fp_control_word = x87_cw; \
    *recomp_p_g_fp_cmp = x87_cmp; *recomp_p_g_fp_cc = x87_cc; \
} while (0)
#define RECOMP_X87_RELOAD() do { \
    x87_base = (*recomp_p_g_fp_top - x87_rel) & 7; RECOMP_X87_LOAD_SLOTS(); \
    x87_cw = *recomp_p_g_fp_control_word; x87_cmp = *recomp_p_g_fp_cmp; x87_cc = *recomp_p_g_fp_cc; \
} while (0)

#include "mm3_gen_cache_on.h"
#endif


#endif /* MM3_RECOMP_TYPES_H */
