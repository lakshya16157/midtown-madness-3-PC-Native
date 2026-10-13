/* Exercise the real embedded-resource hooks with guest addresses only.
 * The resource table is a small binary fixture; original guest methods and
 * its normal allocator/deleting destructor are spies. No game files change. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "../src/mm3_menu_resources.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp, g_fs_base;

static unsigned char memory[0x1000], expected_memory[0x1000];
static const uint32_t object = 0x100, filename = 0x200;
static const uint32_t destination = 0x400, stack = 0xC00;
static unsigned original_calls[5], allocations, frees, destructor_file_calls;
static uint32_t allocation_result, expected_file_member, expected_delete_flag;
static int allocated;

typedef struct Registers {
    uint32_t ax, cx, dx, sp, bx, si, di, bp, seh, fs;
} Registers;

static Registers registers(void)
{
    Registers value = { g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi,
                        g_edi, g_ebp, g_seh_ebp, g_fs_base };
    return value;
}

static void check_registers(Registers value, uint32_t result, uint32_t cleanup)
{
    assert(g_eax == result && g_ecx == value.cx && g_edx == value.dx);
    assert(g_esp == value.sp + cleanup);
    assert(g_ebx == value.bx && g_esi == value.si && g_edi == value.di);
    assert(g_ebp == value.bp && g_seh_ebp == value.seh && g_fs_base == value.fs);
}

static void call_frame(uint32_t instance, uint32_t arg1, uint32_t arg2)
{
    g_eax = 0xDEADBEEFu;
    g_ecx = instance; g_edx = 0x11223344u; g_esp = stack;
    g_ebx = 0x22334455u; g_esi = 0x33445566u; g_edi = 0x44556677u;
    g_ebp = 0x55667788u; g_seh_ebp = 0x66778899u; g_fs_base = 0x778899AAu;
    MEM32(stack) = 0x00123456u;
    MEM32(stack + 4u) = arg1;
    MEM32(stack + 8u) = arg2;
}

static Registers fallback_registers;
static void fallback(unsigned method, uint32_t cleanup)
{
    Registers actual = registers();
    assert(memcmp(&actual, &fallback_registers, sizeof actual) == 0);
    assert(memcmp(memory, expected_memory, sizeof memory) == 0);
    ++original_calls[method];
    g_eax = 0xAA000000u + method;
    g_ecx = 0xBB000000u + method;
    g_edx = 0xCC000000u + method;
    g_esp += cleanup;
}

void sub_00119AEE_original(void) { fallback(0, 8u); }
void sub_00102168_original(void) { fallback(1, 12u); }
void sub_00102191_original(void) { fallback(2, 8u); }
void sub_001021B5_original(void) { fallback(3, 4u); }

void sub_0016FF04(void)
{
    assert(g_esp == stack - 8u);
    assert(MEM32(g_esp) == 0x00119B03u);
    assert(MEM32(g_esp + 4u) == 16u);
    assert(MEM32(stack) == 0x00123456u && MEM32(stack + 4u) == filename);
    ++allocations;
    if (allocation_result) {
        assert(allocation_result == object && !allocated);
        allocated = 1;
    }
    /* Guest native allocator returns through EAX and consumes its size arg.
     * Clobber the shared GP registers so saved caller state is exercised. */
    g_eax = allocation_result;
    g_ecx = 0xA1u; g_edx = 0xA2u;
    g_ebx = 0xA3u; g_esi = 0xA4u; g_edi = 0xA5u;
    g_ebp = 0xA6u; g_seh_ebp = 0xA7u;
    g_esp += 8u; /* Native allocator RET 4. */
}

void sub_0006EA22_original(void)
{
    Registers actual = registers();
    assert(memcmp(&actual, &fallback_registers, sizeof actual) == 0);
    assert(MEM32(g_ecx + 8u) == expected_file_member);
    assert(MEM32(g_esp + 4u) == expected_delete_flag);
    assert(memcmp(memory, expected_memory, sizeof memory) == 0);
    ++original_calls[4];
    if (expected_file_member) ++destructor_file_calls;
    if (expected_delete_flag & 1u) {
        assert(g_ecx == object && allocated);
        allocated = 0;
        ++frees; /* The original guest destructor owns the guest allocation. */
        memset((void *)XBOX_PTR(g_ecx), 0xDD, 16u);
    }
    g_eax = object;
    g_ecx = 0xBB000004u;
    g_edx = 0xCC000004u;
    g_esp += 8u;
}

static void load(const char *name, unsigned index)
{
    Registers before;
    unsigned calls = allocations;
    assert(strlen(name) < 128u);
    strcpy((char *)XBOX_PTR(filename), name);
    call_frame(0x180, filename, 0x1122u);
    before = registers();
    allocation_result = object;
    sub_00119AEE();
    assert(allocations == calls + 1u && !original_calls[0]);
    check_registers(before, object, 8u);
    assert(MEM32(object) == 0x003791F0u);
    assert(MEM32(object + 4u) == object);
    assert(MEM32(object + 8u) == 0xFFFFFF00u + index);
    assert(MEM32(object + 12u) == 0u);
    assert(MEM8(object - 1u) == 0xA5u && MEM8(object + 16u) == 0xA5u);
    assert(MEM32(stack) == 0x00123456u && MEM32(stack + 4u) == filename);
}

static void destroy(uint32_t flag)
{
    unsigned calls = original_calls[4], before_frees = frees;
    call_frame(object, flag, 0x123u);
    fallback_registers = registers();
    expected_file_member = 0u;
    expected_delete_flag = flag;
    memcpy(expected_memory, memory, sizeof memory);
    memset(expected_memory + object + 8u, 0, 4u);
    sub_0006EA22();
    assert(original_calls[4] == calls + 1u);
    assert(frees == before_frees + (flag & 1u));
    assert(!destructor_file_calls);
    assert(g_eax == object && g_ecx == 0xBB000004u && g_edx == 0xCC000004u);
    assert(g_esp == stack + 8u);
}

static void check_tell(uint32_t position)
{
    Registers before;
    call_frame(object, 0u, 0u);
    before = registers();
    /* Unchanged native sub_00102164 is MOV EAX,[ECX+0xC]; RET. Maintaining
     * this field lets all original typed/string readers observe the cursor. */
    g_eax = MEM32(g_ecx + 12u);
    g_esp += 4u;
    check_registers(before, position, 4u);
}

static void read_bytes(uint32_t count, uint32_t position, int valid)
{
    Registers before;
    uint32_t start = MEM32(object + 12u);
    const MM3MenuResource *resource = &mm3_menu_resources[MEM32(object + 8u) - 0xFFFFFF00u];
    memset((void *)XBOX_PTR(destination), 0xA5, 64u);
    call_frame(object, destination + 1u, count);
    before = registers();
    sub_00102168();
    check_registers(before, valid ? count : 0u, 12u);
    assert(MEM32(object + 12u) == position);
    assert(MEM8(destination) == 0xA5u);
    if (valid) {
        assert(memcmp((const void *)XBOX_PTR(destination + 1u), resource->data + start, count) == 0);
        assert(MEM8(destination + 1u + count) == 0xA5u);
    } else {
        for (unsigned i = 0; i < 64u; ++i) assert(MEM8(destination + i) == 0xA5u);
    }
    check_tell(position);
}

static void skip_bytes(int32_t delta, uint32_t position, int valid)
{
    Registers before;
    call_frame(object, (uint32_t)delta, 0xA5u);
    before = registers();
    sub_00102191();
    check_registers(before, valid ? (uint32_t)delta : 0u, 8u);
    assert(MEM32(object + 12u) == position);
    check_tell(position);
}

static void check_size(unsigned index)
{
    Registers before;
    uint32_t cursor = MEM32(object + 12u);
    call_frame(object, 0xA5u, 0xA5u);
    before = registers();
    sub_001021B5();
    check_registers(before, mm3_menu_resources[index].size, 4u);
    assert(MEM32(object + 12u) == cursor);
}

static void check_fallback(void (*method)(void), unsigned index, uint32_t cleanup)
{
    unsigned before = original_calls[index], before_allocations = allocations;
    call_frame(object, filename, 0x104u);
    fallback_registers = registers();
    memcpy(expected_memory, memory, sizeof memory);
    method();
    assert(original_calls[index] == before + 1u && allocations == before_allocations);
    assert(g_eax == 0xAA000000u + index && g_ecx == 0xBB000000u + index);
    assert(g_edx == 0xCC000000u + index && g_esp == stack + cleanup);
    assert(g_ebx == fallback_registers.bx && g_esi == fallback_registers.si);
    assert(g_edi == fallback_registers.di && g_ebp == fallback_registers.bp);
    assert(g_seh_ebp == fallback_registers.seh && g_fs_base == fallback_registers.fs);
    assert(memcmp(memory, expected_memory, sizeof memory) == 0);
}

int main(void)
{
    static const char *const paths[] = {
        "MainMenu.men", "+MainMenu.men", "Menu/Frontend/MainMenu.men",
        "+Menu/Frontend/MainMenu.men", "+mEnU/fRoNtEnD/mAiNmEnU.MeN"
    };
    static const char *const other_paths[] = {
        "", "+", "NotMainMenu.men", "Menu/Other/MainMenu.men",
        "+Menu/Frontend/MainMenu.men.extra", "+Menu/Frontend/"
    };
    static const uint32_t foreign_streams[][2] = {
        { 0x003791F0u, 0x300u },
        { 0x003791F0u, 0xFFFFFEFFu },
        { 0x003791F0u, 0xFFFFFF00u + MM3_MENU_RESOURCE_COUNT },
        { 0x003791F0u, 0xFFFFFFFFu },
        { 0x00383594u, 0xFFFFFF00u }
    };
    Registers before;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    memset(memory, 0xA5, sizeof memory);

    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        load(paths[i], 0);
        check_size(0);
        destroy(1u);
        assert(!allocated);
    }
    load("+Menu/Frontend/GraphicsSettings.men", 1);
    read_bytes(mm3_menu_resources[1].size, mm3_menu_resources[1].size, 1);
    check_size(1);
    destroy(1u);

    load("MainMenu.men", 0);
    check_tell(0u);
    read_bytes(0u, 0u, 1);
    read_bytes(3u, 3u, 1);
    read_bytes(3u, 6u, 1);
    check_size(0); /* Size is independent of the current cursor. */
    skip_bytes(-2, 4u, 1);
    skip_bytes(-5, 4u, 0);
    skip_bytes(INT32_MIN, 4u, 0);
    skip_bytes(INT32_MAX, 4u, 0);
    skip_bytes(6, 4u, 0);
    read_bytes(6u, 4u, 0);
    read_bytes(UINT32_MAX, 4u, 0);
    read_bytes(5u, 9u, 1); /* Exact EOF includes binary NUL and high bytes. */
    read_bytes(0u, 9u, 1);
    read_bytes(1u, 9u, 0);
    skip_bytes(0, 9u, 1);
    skip_bytes(1, 9u, 0);
    skip_bytes(-9, 0u, 1);
    skip_bytes(-1, 0u, 0);
    MEM32(object + 12u) = UINT32_MAX;
    read_bytes(1u, UINT32_MAX, 0);
    read_bytes(0u, UINT32_MAX, 0);
    skip_bytes(1, UINT32_MAX, 0); /* No wrap to zero. */
    skip_bytes(INT32_MIN, UINT32_MAX, 0);
    MEM32(object + 12u) = 0u;
    destroy(0u); /* Non-deleting destruction still clears the host-byte tag. */
    assert(allocated && MEM32(object + 8u) == 0u);
    destroy(1u); /* Only the normal deleting destructor frees guest memory. */
    assert(!allocated);

    allocation_result = 0u;
    strcpy((char *)XBOX_PTR(filename), "MainMenu.men");
    call_frame(0x180, filename, 0u);
    before = registers();
    memcpy(expected_memory, memory, sizeof memory);
    sub_00119AEE();
    check_registers(before, 0u, 8u);
    assert(!allocated && !original_calls[0]);
    assert(memcmp((const void *)XBOX_PTR(object), expected_memory + object, 16u) == 0);

    for (unsigned i = 0; i < sizeof other_paths / sizeof other_paths[0]; ++i) {
        strcpy((char *)XBOX_PTR(filename), other_paths[i]);
        check_fallback(sub_00119AEE, 0, 8u);
    }
    for (unsigned i = 0; i < sizeof foreign_streams / sizeof foreign_streams[0]; ++i) {
        MEM32(object) = foreign_streams[i][0];
        MEM32(object + 8u) = foreign_streams[i][1];
        MEM32(object + 12u) = 0x12345678u;
        check_fallback(sub_00102168, 1, 12u);
        check_fallback(sub_00102191, 2, 8u);
        check_fallback(sub_001021B5, 3, 4u);
        call_frame(object, 0u, 0u);
        fallback_registers = registers();
        expected_file_member = foreign_streams[i][1];
        expected_delete_flag = 0u;
        memcpy(expected_memory, memory, sizeof memory);
        sub_0006EA22();
        assert(MEM32(object + 8u) == foreign_streams[i][1]);
    }
    assert(destructor_file_calls == sizeof foreign_streams / sizeof foreign_streams[0]);
    assert(memcmp(mm3_menu_resources[0].data, "\x00\x47\xF0\x0D\xFF\x80\x41\x00\x7E", 9u) == 0);
    puts("PASS: embedded menu path matching; native allocator ABI and failure; "
         "binary reads, tell, size and signed skip bounds; foreign stream passthrough; "
         "sentinel cleared before original guest-owned deleting destructor");
    return 0;
}
