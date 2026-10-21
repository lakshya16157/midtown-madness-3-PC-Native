/* Real direct bridge + real VEH instruction decoder, with a controlled APU
 * register backend. No game or audio device is needed for this contract. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <intrin.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu.h"
#include "mm3_apu_mmio.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"APU native bridge line%d: %s\n",__LINE__,#c); exit(2); } } while (0)
extern MCPXAPUState *g_apu_state;
extern bool apu_hook_handle_mmio(PCONTEXT, uintptr_t, uint32_t, int);
ptrdiff_t g_xbox_mem_offset;
static uint32_t registers[0x80000 / 4];
static uint8_t *memory;
static unsigned reads, writes, dirty_backend;
static uint32_t last_address, last_value;

static void dirty_environment(void)
{
    if (!dirty_backend) return;
    errno = ERANGE;
    SetLastError(0xBAD123u);
    fesetround(FE_DOWNWARD);
    feraiseexcept(FE_INVALID | FE_INEXACT);
    _mm_setcsr(_mm_getcsr() ^ 0x8040u);
}

uint64_t mcpx_apu_mmio_read(MCPXAPUState *state, uint64_t address, unsigned size)
{
    CHECK(state == (MCPXAPUState *)(uintptr_t)0x12340000 && size == 4 && address < 0x80000);
    ++reads; last_address = (uint32_t)address;
    dirty_environment();
    return registers[address / 4];
}

void mcpx_apu_mmio_write(MCPXAPUState *state, uint64_t address, uint64_t value, unsigned size)
{
    CHECK(state == (MCPXAPUState *)(uintptr_t)0x12340000 && size == 4 && address < 0x80000);
    ++writes; last_address = (uint32_t)address; last_value = (uint32_t)value;
    registers[address / 4] = (uint32_t)value;
    dirty_environment();
}

static LONG CALLBACK handler(PEXCEPTION_POINTERS exception)
{
    if (exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t host = exception->ExceptionRecord->ExceptionInformation[1];
    if (host < (uintptr_t)memory || host >= (uintptr_t)memory + 0x80000)
        return EXCEPTION_CONTINUE_SEARCH;
    return apu_hook_handle_mmio(exception->ContextRecord, host,
        (uint32_t)(host - (uintptr_t)g_xbox_mem_offset),
        exception->ExceptionRecord->ExceptionInformation[0] == 1)
        ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

static void check_environment(void)
{
    static const int modes[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    static const uint32_t addresses[] = {0xFE820010u,0xFE8202F8u,0xFE8202FCu,
        0xFE820360u,0xFE820364u,0xFE820368u,0xFE82037Cu};
    dirty_backend = 1;
    for (unsigned r = 0; r < 4; ++r) for (unsigned flush = 0; flush < 2; ++flush) {
        fesetround(modes[r]); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_DIVBYZERO);
        _mm_setcsr((_mm_getcsr() & ~0x8040u) | (flush ? 0x8040u : 0));
        int flags = fetestexcept(FE_ALL_EXCEPT);
        unsigned csr = _mm_getcsr();
        for (unsigned a = 0; a < sizeof(addresses) / sizeof(addresses[0]); ++a) {
            uint32_t address = addresses[a], value = 0xABCDEF00u + a;
            errno = EDOM; SetLastError(0x123456u);
            mm3_apu_mmio_write32(address, value);
            CHECK(last_address == address - 0xFE800000u && last_value == value);
            CHECK(errno == EDOM && GetLastError() == 0x123456u);
            CHECK(fegetround() == modes[r] && fetestexcept(FE_ALL_EXCEPT) == flags && _mm_getcsr() == csr);
            CHECK(mm3_apu_mmio_read32(address) == value);
            CHECK(errno == EDOM && GetLastError() == 0x123456u);
            CHECK(fegetround() == modes[r] && fetestexcept(FE_ALL_EXCEPT) == flags && _mm_getcsr() == csr);
        }
    }
    dirty_backend = 0;
    fesetround(FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT);
}

static void check_memory_fallback(uint32_t address)
{
    unsigned before_reads = reads, before_writes = writes;
    uint32_t value = 0x13579BDFu;
    uint8_t *pointer = (uint8_t *)((uintptr_t)g_xbox_mem_offset + address);
    memcpy(pointer, &value, 4);
    CHECK(mm3_apu_mmio_read32(address) == value);
    mm3_apu_mmio_write32(address, ~value);
    memcpy(&value, pointer, 4); CHECK(value == ~0x13579BDFu);
    CHECK(before_reads == reads && before_writes == writes);
}

static __declspec(noinline) uint32_t legacy_loop(unsigned count)
{
    volatile uint32_t *input = (volatile uint32_t *)(memory + 0x20010);
    volatile uint32_t *output = (volatile uint32_t *)(memory + 0x202F8);
    uint32_t result = 0;
    for (unsigned i = 0; i < count; ++i) { uint32_t value = *input; *output = value; result ^= value; }
    return result;
}
static __declspec(noinline) uint32_t native_loop(unsigned count)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t value = mm3_apu_mmio_read32(0xFE820010u);
        mm3_apu_mmio_write32(0xFE8202F8u, value); result ^= value;
    }
    return result;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    int native = !strcmp(argv[1], "native");
    CHECK(native || !strcmp(argv[1], "disabled") || !strcmp(argv[1], "missing") || !strcmp(argv[1], "trace"));
    CHECK(!_putenv_s("MM3_NATIVE_APU_MMIO", native ? "1" : !strcmp(argv[1],"disabled") ? "0" : "1"));
    CHECK(!_putenv_s("RECOMP_APU_TRACE", !strcmp(argv[1], "trace") ? "1" : ""));
    memory = VirtualAlloc(NULL, 0x81000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); CHECK(memory);
    g_xbox_mem_offset = (ptrdiff_t)((uintptr_t)memory - 0xFE800000u);
    g_apu_state = !strcmp(argv[1], "missing") ? NULL : (MCPXAPUState *)(uintptr_t)0x12340000;
    CHECK(mm3_apu_mmio_get_enabled() == native);
    CHECK(!mm3_apu_mmio_set_enabled(-1) && !mm3_apu_mmio_set_enabled(2));
    CHECK(mm3_apu_mmio_get_enabled() == native);
    if (native) {
        check_environment();
        check_memory_fallback(0xFE880000u);
        check_memory_fallback(0xFE820011u);
        PVOID cookie = AddVectoredExceptionHandler(1, handler); CHECK(cookie);
        DWORD old_protection; CHECK(VirtualProtect(memory, 0x80000, PAGE_NOACCESS, &old_protection));
        registers[0x20010 / 4] = 0x80u;
        LARGE_INTEGER frequency, a, b, c; QueryPerformanceFrequency(&frequency);
        for (unsigned trial = 0; trial < 5; ++trial) {
            unsigned r0=reads,w0=writes;
            QueryPerformanceCounter(&a); uint32_t old=legacy_loop(20000); QueryPerformanceCounter(&b);
            uint32_t fast=native_loop(20000); QueryPerformanceCounter(&c);
            CHECK(old == fast && reads-r0 == 40000 && writes-w0 == 40000);
            printf("trial=%u accesses=40000 veh_ms=%.6f direct_ms=%.6f\n",trial,
                (b.QuadPart-a.QuadPart)*1000.0/frequency.QuadPart,
                (c.QuadPart-b.QuadPart)*1000.0/frequency.QuadPart);
        }
        /* Compare both paths against the same live device without a reset. */
        CHECK(mm3_apu_mmio_set_enabled(0) && !mm3_apu_mmio_get_enabled());
        unsigned r0 = reads, w0 = writes;
        CHECK(native_loop(100) == legacy_loop(100));
        CHECK(reads-r0 == 200 && writes-w0 == 200);
        CHECK(mm3_apu_mmio_set_enabled(1) && mm3_apu_mmio_get_enabled());
        CHECK(VirtualProtect(memory, 0x80000, old_protection, &old_protection));
        CHECK(RemoveVectoredExceptionHandler(cookie));
    } else {
        check_memory_fallback(0xFE820010u);
        if (!strcmp(argv[1], "disabled"))
            CHECK(mm3_apu_mmio_set_enabled(1) && mm3_apu_mmio_get_enabled());
        else CHECK(!mm3_apu_mmio_set_enabled(1) && !mm3_apu_mmio_get_enabled());
        CHECK(mm3_apu_mmio_set_enabled(0) && !mm3_apu_mmio_get_enabled());
    }
    CHECK(VirtualFree(memory,0,MEM_RELEASE));
    puts("PASS: direct backend semantics, host FP/error preservation and exact legacy fallback");
    return 0;
}
