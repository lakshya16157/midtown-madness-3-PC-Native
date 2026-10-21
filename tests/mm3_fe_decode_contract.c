/* Actual production FE pair publisher; private temporary gate is injected by
 * the runner before its method store for deterministic contention coverage. */
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fenv.h>
#include <xmmintrin.h>
#include "apu_regs.h"

static HANDLE gate_reached, gate_release;
static volatile LONG gate_enabled;
static void test_publish_gate(uint32_t method, uint32_t argument)
{
    if (method == NV1BA0_PIO_SET_CURRENT_VOICE && argument == 8 &&
        InterlockedCompareExchange(&gate_enabled, 0, 0)) {
        SetEvent(gate_reached);
        if (WaitForSingleObject(gate_release, 3000) != WAIT_OBJECT_0) exit(4);
    }
}
#include "fe_decode_under_test.h"

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL FE pair line %u: %s\n", __LINE__, #condition); exit(1); \
} } while (0)
typedef struct Fixture {
    APUFEPublication publication;
    uint32_t fectl, method, argument, current_voice;
} Fixture;
static Fixture f;
static void reset(void)
{
    memset(&f, 0, sizeof f);
    f.method = 0x11223344; f.argument = 0x55667788;
}
static APUFEPublishResult publish(uint32_t method, uint32_t argument, bool idle_enabled)
{
    return apu_fe_publish(&f.publication, &f.fectl, &f.method, &f.argument,
                          method, argument, idle_enabled);
}
static void ordinary(void)
{
    publish(NV1BA0_PIO_SET_CURRENT_VOICE, 8, true);
    f.current_voice = 8; /* Existing ordinary method body still executes. */
}
static void write_mode(uint32_t value)
{
    apu_fe_write_mode(&f.publication, &f.fectl, value);
}
static void sequential(void)
{
    APUFEPublishResult r;
    reset();
    r = publish(SE2FE_IDLE_VOICE, 7, true);
    CHECK(r.wrote_pair && r.trapped);
    CHECK((f.fectl & NV_PAPU_FECTL_FEMETHMODE) == NV_PAPU_FECTL_FEMETHMODE_TRAPPED);
    CHECK(f.method == SE2FE_IDLE_VOICE && f.argument == 7);
    ordinary();
    CHECK(f.method == SE2FE_IDLE_VOICE && f.argument == 7 && f.current_voice == 8);
    r = publish(SE2FE_IDLE_VOICE, 9, true);
    CHECK(!r.wrote_pair && !r.trapped && f.argument == 7);
    /* Clearing ISTS is not a FECTL write and cannot release this pair. */
    ordinary(); CHECK(f.method == SE2FE_IDLE_VOICE && f.argument == 7);
    write_mode(NV_PAPU_FECTL_FEMETHMODE_HALTED);
    r = publish(SE2FE_IDLE_VOICE, 9, true);
    CHECK(!r.wrote_pair && !r.trapped && f.argument == 7);
    CHECK(f.fectl == NV_PAPU_FECTL_FEMETHMODE_HALTED);
    write_mode(NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING);
    r = publish(SE2FE_IDLE_VOICE, 9, true);
    CHECK(r.wrote_pair && r.trapped && f.argument == 9);
    write_mode(NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING);
    ordinary();
    CHECK(f.method == NV1BA0_PIO_SET_CURRENT_VOICE && f.argument == 8);

    reset();
    r = publish(SE2FE_IDLE_VOICE, 7, false);
    CHECK(!r.trapped && f.fectl == 0);
    ordinary(); CHECK(f.method == NV1BA0_PIO_SET_CURRENT_VOICE && f.argument == 8);
    reset(); write_mode(NV_PAPU_FECTL_FEMETHMODE_HALTED);
    ordinary();
    CHECK(f.fectl == NV_PAPU_FECTL_FEMETHMODE_HALTED);
    CHECK(f.method == NV1BA0_PIO_SET_CURRENT_VOICE && f.argument == 8);
    reset();
    errno = EDOM; SetLastError(0x12345678);
    unsigned csr = _mm_getcsr(); fenv_t before = {0}, after = {0};
    CHECK(!fegetenv(&before));
    publish(SE2FE_IDLE_VOICE, 7, true); ordinary();
    CHECK(errno == EDOM && GetLastError() == 0x12345678 && _mm_getcsr() == csr);
    CHECK(!fegetenv(&after) && !memcmp(&before, &after, sizeof before));
}

typedef struct Work { unsigned operation; HANDLE started, done; } Work;
static DWORD WINAPI contender(void *opaque)
{
    Work *w = opaque;
    SetEvent(w->started);
    if (w->operation == 0) ordinary();
    else if (w->operation == 1) publish(SE2FE_IDLE_VOICE, 7, true);
    else write_mode(NV_PAPU_FECTL_FEMETHMODE_HALTED);
    SetEvent(w->done);
    return 0;
}
static void contention(unsigned operation)
{
    Work first = {0}, second = {operation};
    HANDLE a, b;
    reset();
    gate_reached = CreateEventW(NULL, TRUE, FALSE, NULL);
    gate_release = CreateEventW(NULL, TRUE, FALSE, NULL);
    first.started = CreateEventW(NULL, TRUE, FALSE, NULL);
    first.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    second.started = CreateEventW(NULL, TRUE, FALSE, NULL);
    second.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    InterlockedExchange(&gate_enabled, 1);
    a = CreateThread(NULL, 0, contender, &first, 0, NULL);
    CHECK(a && WaitForSingleObject(gate_reached, 3000) == WAIT_OBJECT_0);
    b = CreateThread(NULL, 0, contender, &second, 0, NULL);
    CHECK(b && WaitForSingleObject(second.started, 3000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(second.done, 20) == WAIT_TIMEOUT);
    SetEvent(gate_release);
    CHECK(WaitForSingleObject(a, 3000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(b, 3000) == WAIT_OBJECT_0);
    InterlockedExchange(&gate_enabled, 0);
    if (operation == 1) {
        CHECK(f.method == SE2FE_IDLE_VOICE && f.argument == 7);
        CHECK((f.fectl & NV_PAPU_FECTL_FEMETHMODE) == NV_PAPU_FECTL_FEMETHMODE_TRAPPED);
    } else {
        CHECK(f.fectl == NV_PAPU_FECTL_FEMETHMODE_HALTED);
        CHECK(f.method == NV1BA0_PIO_SET_CURRENT_VOICE && f.argument == 8);
        CHECK(!publish(SE2FE_IDLE_VOICE, 7, true).trapped);
    }
    CloseHandle(a); CloseHandle(b);
    CloseHandle(first.started); CloseHandle(first.done);
    CloseHandle(second.started); CloseHandle(second.done);
    CloseHandle(gate_reached); CloseHandle(gate_release);
}
static void export_pairs(const char *path)
{
    FILE *out = fopen(path, "wb");
    CHECK(out);
    reset(); publish(SE2FE_IDLE_VOICE, 7, true);
    uint32_t values[4] = {f.fectl, f.method, f.argument, f.current_voice};
    CHECK(fwrite(values, sizeof values, 1, out) == 1);
    ordinary();
    values[0] = f.fectl; values[1] = f.method;
    values[2] = f.argument; values[3] = f.current_voice;
    CHECK(fwrite(values, sizeof values, 1, out) == 1);
    CHECK(!fclose(out));
}
int main(int argc, char **argv)
{
    sequential();
    contention(1); contention(2);
    if (argc == 2) export_pairs(argv[1]);
    puts("PASS: production pair latch, HALTED/ISTS/FECTL lifecycle, first-writer/idle/mode contention, and host FP/errors");
    return 0;
}
