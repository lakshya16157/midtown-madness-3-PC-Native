/* [MM3_POLICE] lines (src/mm3_police_log.c) against a police pool laid out
 * as the game keeps it: module 0x3C5780 -> +0xE5D8 manager -> +4 repository
 * -> actor list at +0xB4..+0xB8, each cop's car through its controller. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "recomp_types.h"

static DWORD k_now;
static char k_out[8192];
static void capture(const char *format, ...)
{
    size_t used = strlen(k_out);
    va_list args;
    va_start(args, format);
    vsnprintf(k_out + used, sizeof k_out - used, format, args);
    va_end(args);
}
#define MM3_POLICE_NOW() k_now
#define MM3_POLICE_PRINT(...) capture(__VA_ARGS__)
#include "../src/mm3_police_log.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;

/* The native AI setup and police fill, standing in for the game's. */
static int k_setup_result;
static unsigned k_fill_made;
static void pool(unsigned count);
void sub_00158291_original(void) { g_eax = (g_eax & ~0xFFu) | (unsigned)k_setup_result; g_esp += 8; }
void sub_001507DA_original(void)
{
    pool(k_fill_made);
    g_eax = (g_eax & ~0xFFu) | (k_fill_made >= g_eax);
    g_esp += 4;
}
static const uint32_t k_module = 0x100000, k_manager = 0x120000, k_repository = 0x121000,
                      k_list = 0x122000, k_player = 0x250000;

static uint32_t actor(unsigned i) { return 0x130000 + i * 0x3000; }
static uint32_t body(unsigned i) { return 0x240000 + i * 0x200; }

static void pool(unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        uint32_t a = actor(i), controller = 0x200000 + i * 0x400, vehicle = 0x210000 + i * 0x100,
                 car = 0x220000 + i * 0x100, simulation = 0x230000 + i * 0x100;
        MEM32(k_list + i * 8) = a;
        if (MEM32(a) != 0x0037EE10u) {
            MEM32(a) = 0x0037EE10u; MEM32(a + 0x2950) = 0;
            MEM32(a + 0x28) = controller; MEM32(controller + 0x19C) = vehicle;
            MEM32(vehicle) = 0x00383768u; MEM32(vehicle + 8) = car;
            MEM32(car + 0x3C) = simulation; MEM32(simulation + 4) = body(i);
        }
    }
    MEM32(k_repository + 0xB4) = k_list;
    MEM32(k_repository + 0xB8) = k_list + count * 8;
}

static void place(unsigned i, float x, float z) { MEMF(body(i) + 0xC8) = x; MEMF(body(i) + 0xD0) = z; }

/* One sample at the given time; returns what it printed. */
static const char *tick(DWORD at)
{
    k_now = at; k_out[0] = 0;
    mm3_police_log_tick(k_player);
    return k_out;
}

static void expect(const char *text, const char *part)
{
    if (!strstr(text, part)) {
        fprintf(stderr, "missing \"%s\" in:\n%s\n", part, text);
        abort();
    }
}

int main(void)
{
    const char *line;
    /* One committed read-write block, as the game maps guest memory: the
     * readable() checks would refuse ranges across a static array's
     * copy-on-write pages. */
    void *memory = VirtualAlloc(NULL, 0x410000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    MEM32(0x407C18) = 1; MEM32(0x407C1C) = 7; MEM32(0x407C68) = 4; MEMF(0x407C64) = 0.25f;
    MEMF(k_player + 0xC8) = 0.0f; MEMF(k_player + 0xD0) = 0.0f;

    /* Menus: no police manager, nothing to say. */
    assert(!*tick(1000));

    /* A city loads with four cops in the pool. */
    MEM32(0x3C5780) = k_module; MEM32(k_module + 0xE5D8) = k_manager; MEM32(k_manager + 4) = k_repository;
    MEM32(k_module + 0xE66C) = 4; MEM32(k_module + 0xE674) = 0; MEM32(0x407C20) = 0;
    pool(4);
    line = tick(2000);
    expect(line, "[MM3_POLICE] city 1 mode 7: 4 police cars in the pool of 4 wanted (Cops setting 4, "
                 "traffic 0.25, players 0/0)");
    assert(!*tick(2500));                                   /* once a second */

    /* Cop 2 is sent; it waits at (0, 0) until placed 480 m away. */
    MEM32(actor(1) + 0x2950) = 3;
    assert(!*tick(3000));
    place(1, 480.0f, 0.0f);
    expect(tick(4000), "[MM3_POLICE] cop 2 sent (mode 3), 480 m away at 480 0");
    assert(!*tick(5000));

    /* Ten seconds on it has closed 180 m, then it stops. */
    place(1, 300.0f, 0.0f);
    expect(tick(14000), "cop 2 300 m away (-180 m in 10 s, moved 180 m): closing in");
    expect(tick(24000), "cop 2 300 m away (+0 m in 10 s, moved 0 m): not moving (stuck?)");
    place(1, 40.0f, 0.0f);
    expect(tick(34000), "cop 2 40 m away (-260 m in 10 s, moved 260 m): on you");

    /* More cops join the running pool; cop 2 is not reported as new. */
    pool(5);
    line = tick(35000);
    expect(line, "[MM3_POLICE] pool now 5 police cars (was 4)");
    assert(!strstr(line, "sent"));

    /* It gives up the chase. */
    MEM32(actor(1) + 0x2950) = 0;
    expect(tick(36000), "[MM3_POLICE] cop 2 called off after 33 s, 40 m away");

    /* The minute summary since the pool appeared. */
    expect(tick(62000), "[MM3_POLICE] last 60 s: 1 sent, 1 called off; 0 of 5 out");

    /* An empty pool (Cops none) is still reported. */
    MEM32(0x3C5780) = 0;
    expect(tick(63000), "[MM3_POLICE] police gone (race over)");
    assert(!*tick(64000));
    MEM32(0x3C5780) = k_module; MEM32(0x407C68) = 0; MEM32(k_module + 0xE66C) = 0; pool(0);
    expect(tick(65000), "0 police cars in the pool of 0 wanted (Cops setting 0");

    /* A broken list is never followed. */
    MEM32(0x3C5780) = 0; tick(66000);
    MEM32(0x3C5780) = k_module; pool(2); MEM32(actor(1)) = 0x12345678;
    assert(!*tick(67000));

    /* The game thread's errno and last error survive a sample. */
    MEM32(actor(1)) = 0x0037EE10u;
    errno = 33; SetLastError(1234);
    tick(68000);
    assert(errno == 33 && GetLastError() == 1234);

    /* Why a pool stays empty: the AI setup's wanted count and player type. */
    MEM32(k_module + 0xE66C) = 4; MEM32(k_module + 0xE674) = 1; MEM32(0x407C20) = 1;
    pool(0); k_setup_result = 1;
    k_out[0] = 0; g_ecx = k_module; g_esp = 0x300000; sub_00158291();
    expect(k_out, "[MM3_POLICE] city AI setup done: 4 cops wanted, player type 1 (0x407C20 = 1), police cars in the pool 0");
    assert(g_esp == 0x300008);
    k_setup_result = 0;
    k_out[0] = 0; g_ecx = k_module; sub_00158291();
    expect(k_out, "city AI setup stopped early");

    /* The fill: a car not made is reported, a full pool kept quiet. */
    k_fill_made = 0;
    k_out[0] = 0; g_eax = 4; g_edi = k_manager; g_esp = 0x300000; MEM32(0x300000) = 0x00158499u;
    sub_001507DA();
    expect(k_out, "[MM3_POLICE] police fill from 158499: wanted 4, pool 0 -> 0 (a car could not be made)");
    k_fill_made = 4;
    k_out[0] = 0; g_eax = 4; g_edi = k_manager; MEM32(0x300000) = 0x00158499u; sub_001507DA();
    expect(k_out, "wanted 4, pool 0 -> 4 (done)");
    k_out[0] = 0; g_eax = 4; g_edi = k_manager; sub_001507DA();
    assert(!k_out[0]);

    /* Car creation: the step it stopped at. */
    strcpy((char *)XBOX_PTR(0x301000), "Washington PoliceCar");
    k_out[0] = 0; mm3_police_created(0, 0x301000);
    expect(k_out, "police car \"Washington PoliceCar\" not made: the police manager was not ready");
    k_out[0] = 0; mm3_police_step(0); mm3_police_step(1); mm3_police_created(0, 0x301000);
    expect(k_out, "not made: the police car type was not found (sub_0013D5C1)");
    k_out[0] = 0; for (int s = 0; s <= 4; ++s) mm3_police_step(s);
    mm3_police_created(0, 0x301000);
    expect(k_out, "not made: no memory for the cop (0x298C bytes)");
    k_out[0] = 0; for (int s = 0; s <= 5; ++s) mm3_police_step(s);
    errno = 33; SetLastError(1234);
    mm3_police_created(0x5000, 0x301000);
    expect(k_out, "[MM3_POLICE] police car \"Washington PoliceCar\" made");
    assert(errno == 33 && GetLastError() == 1234);
    /* A few lines a race at most, whatever the game does. */
    for (int i = 0; i < 100; ++i) mm3_police_created(0x5000, 0x301000);
    k_out[0] = 0; mm3_police_created(0x5000, 0x301000);
    assert(!k_out[0] && s_diagnostic_lines == POLICE_DIAGNOSTIC_LINES);
    k_out[0] = 0; g_ecx = k_module; sub_00158291();          /* the next race gets its own lines */
    expect(k_out, "city AI setup");

    /* Inside the factory: what it could not do and the model it was loading
     * (a std::string: text through +4 from 16 bytes of capacity, else inline). */
    {
        const char *body_path = "+Cars\\washingtonpolice_lod1\\body.cmp";
        const uint32_t heap_string = 0x302000, inline_string = 0x302200;
        strcpy((char *)XBOX_PTR(0x302100), body_path);
        MEM32(heap_string + 4) = 0x302100; MEM32(heap_string + 0x14) = (uint32_t)strlen(body_path);
        MEM32(heap_string + 0x18) = 47;
        strcpy((char *)XBOX_PTR(inline_string + 4), "DCcop");
        MEM32(inline_string + 0x14) = 5; MEM32(inline_string + 0x18) = 15;

        mm3_police_note(NOTE_BODY, 0);                       /* outside the factory: ignored */
        mm3_police_note_path(heap_string);
        k_out[0] = 0; for (int s = 0; s <= 4; ++s) mm3_police_step(s);
        mm3_police_factory_begin();
        mm3_police_note(NOTE_ALLOC, 0x400000); mm3_police_note(NOTE_CAR, 0x400000);
        errno = 33; SetLastError(1234);
        mm3_police_note_path(heap_string);
        assert(errno == 33 && GetLastError() == 1234);
        mm3_police_note(NOTE_BODY, 0); mm3_police_note(NOTE_BUILD, 0); mm3_police_note(NOTE_INIT, 0);
        mm3_police_step(5);
        mm3_police_note(NOTE_ATTACH, 0);                     /* after the factory: ignored */
        mm3_police_created(0, 0x301000);
        expect(k_out, "[MM3_POLICE] police car \"Washington PoliceCar\" not made: the police factory refused it "
                      "(sub_0013BB36): the car body model did not load (sub_000E42B1) "
                      "\"+Cars\\washingtonpolice_lod1\\body.cmp\"\n");

        /* No collision file: the car's model name. */
        k_out[0] = 0; for (int s = 0; s <= 4; ++s) mm3_police_step(s);
        mm3_police_factory_begin();
        mm3_police_note(NOTE_ALLOC, 0x400000); mm3_police_note(NOTE_CAR, 0x400000);
        mm3_police_note_path(inline_string); mm3_police_note(NOTE_COLLISION, 0); mm3_police_note(NOTE_INIT, 0);
        mm3_police_step(5); mm3_police_created(0, 0x301000);
        expect(k_out, "the car model has no collision file (Cars\\<model>[_lod1]\\data.ccol), model \"DCcop\"");

        /* The body loaded but the physics did not. */
        k_out[0] = 0; for (int s = 0; s <= 4; ++s) mm3_police_step(s);
        mm3_police_factory_begin();
        mm3_police_note(NOTE_ALLOC, 0x400000); mm3_police_note(NOTE_CAR, 0x400000);
        mm3_police_note_path(heap_string); mm3_police_note(NOTE_BODY, 0x410000);
        mm3_police_note(NOTE_SETUP, 1); mm3_police_note(NOTE_PHYSICS, 0);
        mm3_police_note(NOTE_BUILD, 0); mm3_police_note(NOTE_INIT, 0);
        mm3_police_step(5); mm3_police_created(0, 0x301000);
        expect(k_out, "the car physics did not load (sub_0021ED64) for \"+Cars\\washingtonpolice_lod1\\body.cmp\"");

        /* The car was built but the cop did not take it; no memory; nothing reached. */
        k_out[0] = 0; for (int s = 0; s <= 4; ++s) mm3_police_step(s);
        mm3_police_factory_begin();
        mm3_police_note(NOTE_ALLOC, 0x400000); mm3_police_note(NOTE_CAR, 0x400000);
        mm3_police_note(NOTE_BUILD, 1); mm3_police_note(NOTE_INIT, 1); mm3_police_note(NOTE_ATTACH, 0);
        mm3_police_step(5); mm3_police_created(0, 0x301000);
        expect(k_out, "refused it (sub_0013BB36): the cop did not take the car");
        k_out[0] = 0; for (int s = 0; s <= 4; ++s) mm3_police_step(s);
        mm3_police_factory_begin(); mm3_police_note(NOTE_ALLOC, 0);
        mm3_police_step(5); mm3_police_created(0, 0x301000);
        expect(k_out, "refused it (sub_0013BB36): no memory for the car (0x82C bytes)\n");
        k_out[0] = 0; for (int s = 0; s <= 4; ++s) mm3_police_step(s);
        mm3_police_factory_begin(); mm3_police_step(5); mm3_police_created(0, 0x301000);
        expect(k_out, "refused it (sub_0013BB36): it stopped before making the car\n");

        /* A car made says only that; the next car starts with no notes. */
        k_out[0] = 0; for (int s = 0; s <= 4; ++s) mm3_police_step(s);
        mm3_police_factory_begin(); mm3_police_note(NOTE_ALLOC, 0x400000);
        mm3_police_step(5); mm3_police_created(0x5000, 0x301000);
        assert(!strcmp(k_out, "[MM3_POLICE] police car \"Washington PoliceCar\" made\n"));
        assert(!s_in_factory);
    }

    /* MM3_POLICE_LOG=0 turns it off. */
    _putenv("MM3_POLICE_LOG=0"); s_enabled = -1;
    MEM32(0x3C5780) = 0; tick(69000);
    MEM32(0x3C5780) = k_module;
    assert(!*tick(70000));
    s_diagnostic_lines = 0;
    k_out[0] = 0; mm3_police_created(0, 0x301000); g_ecx = k_module; sub_00158291();
    assert(!k_out[0]);
    mm3_police_factory_begin(); mm3_police_note(NOTE_ALLOC, 0);
    assert(!s_in_factory && !s_noted);

    printf("PASS police log: pool (of wanted, player type), sent once placed, closing/stuck/on you, pool growth, "
           "called off, minute summary, empty pool, broken list, host state, AI setup and fill result, "
           "car creation step, factory sub-step and model path, line cap, switch\n");
    return 0;
}
