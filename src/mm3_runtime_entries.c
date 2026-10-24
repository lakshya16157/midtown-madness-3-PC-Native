#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "mm3_lua_natives.h"
void sub_00083A6C(void);

typedef void (*mm3_recomp_func_t)(void);
mm3_recomp_func_t recomp_lookup_manual_base(uint32_t va);
mm3_recomp_func_t mm3_lookup_memory_tail(uint32_t va);
mm3_recomp_func_t mm3_lookup_recovered_entry(uint32_t va);
mm3_recomp_func_t recomp_lookup(uint32_t va);
extern uint32_t g_xbox_code_lo, g_xbox_code_hi;

/* Keep title-specific recovered entries separate from the shared toolkit
 * and the existing manual ABI/SEH compatibility implementations. */
static mm3_recomp_func_t lookup_manual_uncached(uint32_t va)
{
    if (va == 0x00083A6Cu) return sub_00083A6C;
    if (va == MM3_LUA_AI_ALL_CARS_VA) return mm3_lua_ai_all_cars;
    mm3_recomp_func_t tail = mm3_lookup_memory_tail(va);
    if (tail) return tail;
    mm3_recomp_func_t recovered = mm3_lookup_recovered_entry(va);
    return recovered ? recovered : recomp_lookup_manual_base(va);
}

/* Every indirect call (RECOMP_ICALL) asks here first, then the generated
 * table (recomp_lookup). Neither answer ever changes, so the first answer for
 * a code address is kept in one table shared by all threads, indexed by
 * address: the manual entry if there is one, otherwise the generated function.
 * A vtable call then costs a bounds check and one load, and RECOMP_ICALL never
 * needs its second lookup. Results are those of the two lookups in order;
 * other addresses (kernel thunks) and calls before the code range is known
 * keep a per-thread cache of the manual answer alone. */
#define RESOLVED_SPAN_LIMIT 0x04000000u
static mm3_recomp_func_t *volatile s_resolved;
static uint32_t s_resolved_base, s_resolved_span;

static mm3_recomp_func_t *resolved_table(void)
{
#ifdef _WIN32
    static volatile LONG tried;
    mm3_recomp_func_t *table = s_resolved;
    uint32_t span;
    if (table || !g_xbox_code_hi || g_xbox_code_hi <= g_xbox_code_lo) return table;
    if (InterlockedCompareExchange(&tried, 1, 0)) return s_resolved;
    span = g_xbox_code_hi - g_xbox_code_lo;
    if (span > RESOLVED_SPAN_LIMIT) return NULL;
    /* Reserved and committed zero pages: only pages of called addresses are
     * ever touched. */
    table = (mm3_recomp_func_t *)VirtualAlloc(NULL, (SIZE_T)span * sizeof *table,
                                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!table) return NULL;
    s_resolved_base = g_xbox_code_lo;
    s_resolved_span = span;
    InterlockedExchangePointer((PVOID volatile *)&s_resolved, table);
    return table;
#else
    return NULL;
#endif
}

static mm3_recomp_func_t lookup_manual_cached(uint32_t va)
{
#ifdef _MSC_VER
    static __declspec(thread) struct { uint32_t va; mm3_recomp_func_t fn; } cache[1024];
#else
    static _Thread_local struct { uint32_t va; mm3_recomp_func_t fn; } cache[1024];
#endif
    unsigned slot = (va ^ (va >> 10)) & 1023u;
    if (cache[slot].va != va || !va) {
        cache[slot].fn = lookup_manual_uncached(va);
        cache[slot].va = va;
    }
    return cache[slot].fn;
}

mm3_recomp_func_t recomp_lookup_manual(uint32_t va)
{
    mm3_recomp_func_t *table = s_resolved, fn;
    uint32_t offset;
    if (!table) table = resolved_table();
    offset = va - s_resolved_base;
    if (!table || offset >= s_resolved_span) return lookup_manual_cached(va);
    fn = table[offset];
    if (fn) return fn;
    fn = lookup_manual_uncached(va);
    if (!fn) fn = recomp_lookup(va);
    /* Every thread that races here stores the same pointer. */
    if (fn) table[offset] = fn;
    return fn;
}
