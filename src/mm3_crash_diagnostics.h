#ifndef MM3_CRASH_DIAGNOSTICS_H
#define MM3_CRASH_DIAGNOSTICS_H

#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* Guest pointers may be exactly what caused the exception. A range check
 * alone does not establish that the guest heap/stack pages are committed.
 * Reject guard pages before reading, and let the OS copy the bytes so a
 * concurrent unmap cannot cause a second exception inside the crash report. */
static int mm3_crash_guest_read(ptrdiff_t memory_offset, uint32_t map_size,
                                uint64_t address, void *output, size_t size)
{
    uintptr_t host, end, cursor, offset = (uintptr_t)memory_offset;
    SIZE_T copied = 0;
    if (!output || !size || address < 0x10000u || address >= map_size ||
        size > (uint64_t)map_size - address || address > UINTPTR_MAX - offset)
        return 0;
    host = offset + (uintptr_t)address;
    if (size > UINTPTR_MAX - host) return 0;
    end = host + size;
    for (cursor = host; cursor < end;) {
        MEMORY_BASIC_INFORMATION region;
        uintptr_t region_end;
        DWORD protection;
        if (!VirtualQuery((const void *)cursor, &region, sizeof region) ||
            region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return 0;
        protection = region.Protect & 0xFFu;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
            return 0;
        if (region.RegionSize > UINTPTR_MAX - (uintptr_t)region.BaseAddress)
            return 0;
        region_end = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (region_end <= cursor) return 0;
        cursor = region_end < end ? region_end : end;
    }
    return ReadProcessMemory(GetCurrentProcess(), (const void *)host,
                             output, size, &copied) && copied == size;
}

typedef struct MM3CrashWord {
    uint32_t value;
    int readable;
} MM3CrashWord;

static MM3CrashWord mm3_crash_guest_word(FILE *log, ptrdiff_t memory_offset,
                                        uint32_t map_size, const char *name,
                                        uint64_t address)
{
    MM3CrashWord word = {0, 0};
    word.readable = mm3_crash_guest_read(memory_offset, map_size, address,
                                       &word.value, sizeof word.value);
    if (word.readable)
        fprintf(log, "[CRASH_GUEST] %s address=0x%llX value=0x%08X\n",
                name, (unsigned long long)address, word.value);
    else
        fprintf(log, "[CRASH_GUEST] %s address=0x%llX unreadable\n",
                name, (unsigned long long)address);
    return word;
}

static void mm3_crash_guest_indirect(FILE *log, ptrdiff_t memory_offset,
                                    uint32_t map_size, const char *name,
                                    MM3CrashWord parent, uint32_t displacement)
{
    if (parent.readable && parent.value)
        mm3_crash_guest_word(log, memory_offset, map_size, name,
                             (uint64_t)parent.value + displacement);
    else if (parent.readable)
        fprintf(log, "[CRASH_GUEST] %s unavailable: parent null\n", name);
    else
        fprintf(log, "[CRASH_GUEST] %s unavailable: parent unreadable\n", name);
}

/* FPO guest functions may use EBP as scratch. Report the reason a chain ends
 * rather than treating arbitrary memory as an unlimited guest call stack. */
static void mm3_crash_guest_frames(FILE *log, ptrdiff_t memory_offset,
                                  uint32_t map_size, uint32_t frame)
{
    unsigned index;
    for (index = 0; index < 16u; ++index) {
        uint32_t pair[2];
        if (!frame) {
            fprintf(log, "[CRASH_GUEST_FRAME] stop=root count=%u\n", index);
            return;
        }
        if ((frame & 3u) ||
            !mm3_crash_guest_read(memory_offset, map_size, frame, pair, sizeof pair)) {
            fprintf(log, "[CRASH_GUEST_FRAME] stop=invalid_or_unreadable ebp=0x%08X count=%u\n",
                    frame, index);
            return;
        }
        fprintf(log, "[CRASH_GUEST_FRAME] frame=%u ebp=0x%08X parent=0x%08X return=0x%08X\n",
                index, frame, pair[0], pair[1]);
        if (pair[0] && pair[0] <= frame) {
            fprintf(log, "[CRASH_GUEST_FRAME] stop=nonascending_parent count=%u\n", index + 1u);
            return;
        }
        frame = pair[0];
    }
    fprintf(log, "[CRASH_GUEST_FRAME] stop=limit count=16\n");
}

#endif
