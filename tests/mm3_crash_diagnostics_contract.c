#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_crash_diagnostics.h"

#define MM3_MEMORY_MAP_SIZE (128u * 1024u * 1024u)
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stdout, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static uint8_t *memory;
static ptrdiff_t g_xbox_mem_offset;
static uint32_t g_esp, g_eax, g_ebp, g_seh_ebp, g_fs_base;
static uint32_t g_ecx, g_ebx, g_esi, g_edi;
static void *xbox_GetMemoryBase(void) { return memory; }

#include "production_crash_report.inc"

static void set_word(uint32_t address, uint32_t value)
{
    memcpy(memory + address, &value, sizeof value);
}

static void report(DWORD code, ULONG_PTR access)
{
    EXCEPTION_RECORD record = {0};
    CONTEXT context = {0};
    EXCEPTION_POINTERS info = {&record, &context};
    record.ExceptionCode = code;
    record.ExceptionAddress = (void *)mm3_crash_report;
    record.NumberParameters = 2;
    record.ExceptionInformation[0] = access;
    record.ExceptionInformation[1] = (ULONG_PTR)(memory + 0x20000u);
    context.Rip = (DWORD64)(uintptr_t)mm3_crash_report;
    context.Rsp = (DWORD64)(uintptr_t)&info;
    CHECK(mm3_crash_report(&info) == EXCEPTION_CONTINUE_SEARCH);
}

int main(int argc, char **argv)
{
    uint32_t value = 0;
    uint8_t before[64];
    DWORD previous;
    MEMORY_BASIC_INFORMATION region;
    CHECK(argc == 2);
    CHECK(freopen(argv[1], "wb", stderr));
    memory = VirtualAlloc(NULL, MM3_MEMORY_MAP_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    CHECK(memory);
    CHECK(VirtualAlloc(memory, 0x800000u, MEM_COMMIT, PAGE_READWRITE) == memory);
    CHECK(VirtualAlloc(memory + MM3_MEMORY_MAP_SIZE - 0x1000u, 0x1000u,
                       MEM_COMMIT, PAGE_READWRITE));
    g_xbox_mem_offset = (ptrdiff_t)memory;
    set_word(0x10000u, 0x12345678u);
    CHECK(mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                              0x10000u, &value, sizeof value));
    CHECK(value == 0x12345678u);
    CHECK(!mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE, 0,
                               &value, sizeof value));
    CHECK(!mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                               0xFFFFFFFFull + 0x580u, &value, sizeof value));
    CHECK(!mm3_crash_guest_read((ptrdiff_t)(UINTPTR_MAX - 0x100u),
                               MM3_MEMORY_MAP_SIZE, 0x10000u, &value, sizeof value));
    CHECK(!mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                               0x900000u, &value, sizeof value));
    set_word(MM3_MEMORY_MAP_SIZE - 4u, 0x87654321u);
    CHECK(mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                              MM3_MEMORY_MAP_SIZE - 4u, &value, sizeof value));
    CHECK(value == 0x87654321u);
    CHECK(!mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                               MM3_MEMORY_MAP_SIZE - 3u, &value, sizeof value));
    CHECK(VirtualProtect(memory + 0x20000u, 0x1000u, PAGE_NOACCESS, &previous));
    CHECK(!mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                               0x20000u, &value, sizeof value));
    CHECK(!mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                               0x1FFFEu, &value, sizeof value));
    CHECK(VirtualProtect(memory + 0x30000u, 0x1000u,
                         PAGE_READWRITE | PAGE_GUARD, &previous));
    CHECK(!mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                               0x30000u, &value, sizeof value));
    CHECK(VirtualQuery(memory + 0x30000u, &region, sizeof region));
    CHECK(region.Protect & PAGE_GUARD);
    CHECK(VirtualProtect(memory + 0x40000u, 0x1000u, PAGE_READONLY, &previous));
    CHECK(mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                              0x3FFFEu, &value, sizeof value));

    /* Run the complete production report against real mapped guest data. */
    g_esp = g_ebp = 0x10000u;
    set_word(0x10000u, 0x10020u); set_word(0x10004u, 0xABCDEF01u);
    set_word(0x10020u, 0); set_word(0x10024u, 0xABCDEF02u);
    g_seh_ebp = 0x10100u; set_word(g_seh_ebp + 8u, 0x10200u);
    set_word(0x10218u, 0x10218102u); set_word(0x10780u, 0x10780107u);
    g_fs_base = 0x10300u; set_word(g_fs_base + 0x20u, 0x10400u);
    g_esi = 0x10500u; g_edi = 2u;
    memory[0x429DB2u] = 1u;
    set_word(0x431610u, 4u); set_word(0x3C5CDCu, 0x10600u);
    set_word(0x10654u, 0x10800u); set_word(0x108D8u, 0x10900u);
    memcpy(before, memory + g_esp, sizeof before);
    report(EXCEPTION_ACCESS_VIOLATION, 1u);
    CHECK(!memcmp(before, memory + g_esp, sizeof before));
    report(EXCEPTION_ACCESS_VIOLATION, 0u);
    report(EXCEPTION_ACCESS_VIOLATION, 8u);
    report(EXCEPTION_IN_PAGE_ERROR, 0u);

    /* Corrupt pointers, unmapped/noaccess/guard pages, and absent exception
     * metadata must still flush a complete report and preserve guard state. */
    g_esp = g_ebp = g_seh_ebp = g_fs_base = g_esi = 0xFFFFFFFCu;
    report(EXCEPTION_ACCESS_VIOLATION, 1u);
    g_esp = g_ebp = g_seh_ebp = g_fs_base = g_esi = 0x20000u;
    report(EXCEPTION_ACCESS_VIOLATION, 1u);
    g_esp = g_ebp = g_seh_ebp = g_fs_base = g_esi = 0x30000u;
    report(EXCEPTION_ACCESS_VIOLATION, 1u);
    CHECK(VirtualQuery(memory + 0x30000u, &region, sizeof region));
    CHECK(region.Protect & PAGE_GUARD);
    CHECK(mm3_crash_report(NULL) == EXCEPTION_CONTINUE_SEARCH);

    set_word(0x3C5CDCu, 0);
    report(EXCEPTION_ACCESS_VIOLATION, 0u);
    set_word(0x3C5CDCu, 0x10600u); set_word(0x10654u, 0);
    report(EXCEPTION_ACCESS_VIOLATION, 0u);

    set_word(0x10000u, 0x10000u);
    mm3_crash_guest_frames(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE, 0x10000u);
    for (unsigned i = 0; i < 17u; ++i) {
        set_word(0x10000u + i * 16u, 0x10010u + i * 16u);
        set_word(0x10004u + i * 16u, 0x12300000u + i);
    }
    mm3_crash_guest_frames(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE, 0x10000u);
    fflush(stderr);
    CHECK(VirtualFree(memory, 0, MEM_RELEASE));
    puts("PASS: production crash reports survive invalid/uncommitted/noaccess/guard guest pointers, distinguish absent race/player pointers, preserve guard pages, identify access/thread/pause context and bound guest call chains");
    return 0;
}
