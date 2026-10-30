/* The native best-times writer (sub_001E8A7A, size helper sub_001E8BDF, name
 * writer sub_001E4FC4) and loader (sub_001E9060 from its size check, name
 * reader sub_001E50F7), original and seamed, extracted by
 * mm3_score_tables_regression.py. Only the byte streams, the map iterator
 * and string housekeeping are mocked. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;

static unsigned s_prints;
#define MM3_SCORES_PRINT(...) (++s_prints)
#include "mm3_score_tables.c"

#define G(address) ((void *)XBOX_PTR(address))
void sub_001E4FC4(void);   /* native, defined after the writers that call it */

static unsigned char memory[0x800000];
enum {
    SCORES = 0x10000, HEADER = 0x10100, NODES = 0x20000, ENTRIES = 0x40000, HEAP = 0x60000,
    STACK = 0xFE000, TARGET = 0x110000, OUT = 0x200000, MEASURE = 0x400000, DATA = 0x600000,
    WRITER_VTABLE = 0x383E00, READER_VTABLE = 0x385318, ADAPTOR_VTABLE = 0x36E540,
    VERSION = 0x36E1FC, EMPTY = 0x3C5CD8, RUNAWAY = 0x100000
};
enum { W_FLOAT = 0xF001, W_INT, W_BYTES, R_SIZE, R_FLOAT, R_INT, R_BYTES, R_SKIP };

static uint32_t heap, order[256];
static unsigned order_count;
static jmp_buf runaway;

typedef struct { uint32_t key, value; float time; char name[17]; } Record;
static Record loaded[300 * 8];
static unsigned loaded_count;

static uint32_t grab(uint32_t bytes)
{
    uint32_t at = heap;
    heap += (bytes + 15u) & ~15u;
    assert(heap < 0x100000);
    return at;
}

/* Vector-like streams: +8 begin, +0xC end, +0x10 capacity end. */
static void append(uint32_t target, uint32_t from, uint32_t bytes)
{
    uint32_t end = MEM32(target + 0xC);
    assert(end + bytes <= MEM32(target + 0x10));
    memmove(G(end), G(from), bytes);
    MEM32(target + 0xC) = end + bytes;
    if (MEM32(target + 0xC) - MEM32(target + 8) > RUNAWAY) longjmp(runaway, 1);
}

static void consume(uint32_t adaptor, uint32_t to, uint32_t bytes)
{
    uint32_t reader = MEM32(adaptor + 4), cursor = MEM32(reader + 0xC);
    assert(cursor + bytes <= MEM32(reader + 8));
    if (to) memcpy(G(to), G(cursor), bytes);
    MEM32(reader + 0xC) = cursor + bytes;
}

static void mock_indirect(uint32_t target)
{
    switch (target) {
    case W_FLOAT: case W_INT:
        append(MEM32(g_ecx + 4), g_esp + 4, 4); g_esp += 8; break;
    case W_BYTES:
        append(MEM32(g_ecx + 4), MEM32(g_esp + 4), MEM32(g_esp + 8)); g_esp += 12; break;
    case R_SIZE:
        g_eax = MEM32(g_ecx + 8) - MEM32(g_ecx + 4); g_esp += 4; break;
    case R_FLOAT: case R_INT:
        consume(g_ecx, MEM32(g_esp + 4), 4); g_esp += 8; break;
    case R_BYTES:
        consume(g_ecx, MEM32(g_esp + 4), MEM32(g_esp + 8)); g_esp += 12; break;
    case R_SKIP:
        consume(g_ecx, 0, MEM32(g_esp + 4)); g_esp += 8; break;
    default:
        assert(!"unexpected indirect call");
    }
}
#undef RECOMP_ICALL_SAFE_AT
#define RECOMP_ICALL_SAFE_AT(target, saved, site) mock_indirect(target)

static void empty_string(uint32_t at)
{
    memset(G(at), 0, 0x1C);
    MEM32(at + 0x18) = 7;
}

static void sub_00016148(void) { assert(MEM32(g_esp + 4) == EMPTY); empty_string(g_eax); g_esp += 8; }
static void sub_0001734E(void) { memmove(G(g_eax), G(MEM32(g_esp + 4)), 0x1C); g_esp += 8; }
static void sub_00016403(void) { g_esp += 8; }
static void sub_00023049(void) { g_esp += 8; }
static void sub_000127A9(void) { g_esp += 8; }
static void sub_00170EC0(void) { g_esp += 4; }
static void sub_001BE59D(void) { g_esp += 4; }
static void sub_0004E6ED(void) { g_esp += 4; }
static void sub_000230B2(void)
{
    MEM32(g_ebx) = 0x383BE8;
    MEM32(g_ebx + 4) = 0;
    MEM32(g_ebx + 8) = MEM32(g_ebx + 0xC) = MEASURE;
    MEM32(g_ebx + 0x10) = MEASURE + 0x200000;
    g_eax = g_ebx;
    g_esp += 4;
}
static void sub_0007B4F9(void)
{
    unsigned i;
    for (i = 0; i < order_count && order[i] != MEM32(g_edx); ++i) {}
    assert(i < order_count);
    MEM32(g_edx) = i + 1 < order_count ? order[i + 1] : HEADER;
    g_esp += 4;
}
static void sub_00025C34(void)
{
    uint32_t text = g_ecx, length = g_eax;
    MEM32(text + 0x14) = length;
    if (length < 8) {
        MEM32(text + 0x18) = 7;
        memset(G(text + 4), 0, 16);
    } else {
        MEM32(text + 0x18) = length;
        MEM32(text + 4) = grab(length * 2 + 2);
    }
    g_esp += 8;
}
static void read_name(uint32_t text, char *out)
{
    uint32_t length = MEM32(text + 0x14), chars = MEM32(text + 0x18) < 8 ? text + 4 : MEM32(text + 4), i;
    assert(length <= 16);
    for (i = 0; i < length; ++i) out[i] = (char)MEM16(chars + i * 2);
    out[length] = 0;
}
static void sub_001E8C19(void)
{
    uint32_t entry = MEM32(g_esp + 0x10);
    Record *record = &loaded[loaded_count++];
    assert(MEM32(g_esp + 4) == SCORES && loaded_count <= sizeof loaded / sizeof *loaded);
    record->key = MEM32(g_esp + 0xC);
    read_name(entry, record->name);
    record->time = MEMF(entry + 0x1C);
    record->value = MEM32(entry + 0x20);
    MEM8(MEM32(g_esp + 8)) = 1;
    g_esp += 20;
}

#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
#include "native_scores.inc"

static void reset(unsigned entries)
{
    memset(memory, 0, sizeof memory);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    heap = HEAP;
    order_count = loaded_count = 0;
    g_fp_top = 0;
    MEMF(VERSION) = 1.2f;
    MEM32(WRITER_VTABLE + 0xC) = W_BYTES;
    MEM32(WRITER_VTABLE + 0x28) = W_FLOAT;
    MEM32(WRITER_VTABLE + 0x30) = W_INT;
    MEM32(READER_VTABLE + 8) = R_SIZE;
    MEM32(ADAPTOR_VTABLE + 0xC) = R_BYTES;
    MEM32(ADAPTOR_VTABLE + 0x14) = R_SKIP;
    MEM32(ADAPTOR_VTABLE + 0x24) = R_FLOAT;
    MEM32(ADAPTOR_VTABLE + 0x28) = R_INT;
    empty_string(EMPTY);
    MEM32(SCORES + 4) = entries;
    MEM32(SCORES + 0x28) = HEADER;
    MEM32(HEADER) = HEADER;
    MEM32(SCORES + 0x2C) = 0;
}

static void entry_name(unsigned table, unsigned entry, char *out)
{
    unsigned length = (table * 3 + entry) % 14, i;   /* 0-13 characters: inline and heap */
    for (i = 0; i < length; ++i) out[i] = (char)('A' + (table + entry + i) % 26);
    out[length] = 0;
}

/* `tables` best-times tables with real race keys, ascending like the map. */
static void fill(unsigned tables, unsigned entries)
{
    static uint32_t keys[256];
    unsigned count = 0, city, race, t, e, i;
    for (city = 0; city < 2; ++city) {
        for (race = 0; race < 20; ++race) keys[count++] = race + city * 0x80;           /* Blitz 00-19 */
        for (race = 0; race < 30; ++race) keys[count++] = race + 0x40 + city * 0x80;    /* Checkpoint 00-29 */
    }
    for (i = 0; count < 256; ++i) {   /* beyond the 100 real races: any other byte */
        uint32_t key = i, used = 0, k;
        for (k = 0; k < count; ++k) used |= keys[k] == key;
        if (!used) keys[count++] = key;
    }
    for (i = 1; i < count; ++i) {   /* sort */
        uint32_t key = keys[i], k = i;
        while (k && keys[k - 1] > key) { keys[k] = keys[k - 1]; --k; }
        keys[k] = key;
    }
    if (tables < 100) {   /* a spread of real races */
        for (t = 0; t < tables; ++t) keys[t] = keys[(t * 100u) / tables];
    }
    for (t = 0; t < tables; ++t) {
        uint32_t node = NODES + t * 0x40, list = ENTRIES + t * entries * 0x24;
        MEM32(node + 0xC) = keys[t];
        MEM32(node + 0x14) = list;
        MEM32(node + 0x18) = list + entries * 0x24;
        for (e = 0; e < entries; ++e) {
            uint32_t at = list + e * 0x24, chars;
            char name[17];
            entry_name(t, e, name);
            MEM32(at + 0x14) = (uint32_t)strlen(name);
            MEM32(at + 0x18) = strlen(name) < 8 ? 7 : (uint32_t)strlen(name);
            chars = strlen(name) < 8 ? at + 4 : (MEM32(at + 4) = grab(34));
            for (i = 0; name[i]; ++i) MEM16(chars + i * 2) = (uint16_t)name[i];
            MEMF(at + 0x1C) = (float)t * 10.0f + (float)e + 0.5f;
            MEM32(at + 0x20) = t * 100 + e;
        }
        order[order_count++] = node;
    }
    MEM32(HEADER) = tables ? order[0] : HEADER;
    MEM32(SCORES + 0x2C) = tables;
}

/* Bytes written, or 0 when the writer ran away. */
static uint32_t write_scores(void (*writer)(void), unsigned tables, unsigned entries, unsigned char *out)
{
    uint32_t size;
    reset(entries);
    fill(tables, entries);
    MEM32(TARGET + 8) = MEM32(TARGET + 0xC) = OUT;
    MEM32(TARGET + 0x10) = OUT + 0x200000;
    g_esp = STACK;
    PUSH32(g_esp, 0xDEAD0001u);
    g_eax = TARGET; g_edi = SCORES; g_ebx = 0xB0B0; g_esi = 0x5151;
    if (setjmp(runaway)) return 0;
    writer();
    assert(g_esp == STACK && g_edi == SCORES && g_ebx == 0xB0B0 && g_esi == 0x5151);
    assert(MEM32(SCORES + 0x2C) == tables);
    size = MEM32(TARGET + 0xC) - OUT;
    memcpy(out, G(OUT), size);
    return size;
}

static uint32_t table_bytes(unsigned entries) { return 4 + entries * 44; }

/* The file the Xbox format describes: tables, then empty ones up to `total`. */
static void check_file(const unsigned char *file, uint32_t size, unsigned tables, unsigned total, unsigned entries)
{
    const unsigned char *at = file + 8;
    unsigned t, e, i;
    float version;
    memcpy(&version, file, 4);
    assert(version == 1.2f && *(const uint32_t *)(file + 4) == entries);
    assert(size == 8 + total * table_bytes(entries));
    for (t = 0; t < total; ++t) {
        uint32_t key = *(const uint32_t *)at;
        at += 4;
        assert(key == (t < tables ? MEM32(order[t] + 0xC) : 0));
        for (e = 0; e < entries; ++e) {
            char name[17] = "";
            uint32_t length = *(const uint32_t *)at, value;
            float time;
            if (t < tables) entry_name(t, e, name);
            assert(length == strlen(name));
            for (i = 0; i < 16; ++i) assert(*(const uint16_t *)(at + 4 + i * 2) == (i < length ? (uint16_t)name[i] : 0));
            memcpy(&time, at + 36, 4);
            value = *(const uint32_t *)(at + 40);
            assert(time == (t < tables ? (float)t * 10.0f + (float)e + 0.5f : 0.0f));
            assert(value == (t < tables ? t * 100 + e : 0));
            at += 44;
        }
    }
}

/* Load `file`; returns the loader's result (al) and fills loaded[]. */
static int load_scores(void (*loader)(void), const unsigned char *file, uint32_t size, unsigned entries)
{
    const uint32_t entry_esp = STACK, frame = entry_esp - 4 - 116, body_esp = entry_esp - 4 - 0x1C0 - 12;
    reset(entries);
    memcpy(G(DATA), file, size);
    MEM32(entry_esp) = 0xDEAD0002u;
    MEM32(entry_esp + 4) = SCORES;
    MEM32(entry_esp - 4) = 0xEB0EB0;   /* caller's ebp */
    MEM32(body_esp) = 0xED1;            /* saved edi, esi, ebx */
    MEM32(body_esp + 4) = 0xE51;
    MEM32(body_esp + 8) = 0xEB1;
    g_esp = body_esp; g_ebp = frame;
    g_eax = DATA + size; g_ebx = DATA; g_esi = 0; g_edi = 0;
    if (setjmp(runaway)) assert(!"the size check ran away");
    loader();
    assert(g_esp == entry_esp + 8 && g_edi == 0xED1 && g_esi == 0xE51 && g_ebx == 0xEB1);
    assert(g_fp_top == 0);
    return (int)(g_eax & 0xFF);
}

static void check_loaded(unsigned tables, unsigned total, unsigned entries)
{
    unsigned t, e, n = 0;
    assert(loaded_count == total * entries);
    for (t = 0; t < total; ++t) {
        for (e = 0; e < entries; ++e, ++n) {
            char name[17] = "";
            if (t < tables) entry_name(t, e, name);
            assert(!strcmp(loaded[n].name, name));
            assert(loaded[n].time == (t < tables ? (float)t * 10.0f + (float)e + 0.5f : 0.0f));
            assert(loaded[n].value == (t < tables ? t * 100 + e : 0));
            if (t >= tables) assert(loaded[n].key == 0);
        }
    }
}

static unsigned char original_file[0x40000], modified_file[0x40000];

int main(void)
{
    static const unsigned counts[] = {0, 1, 40, 79, 80, 81, 90, 100, 101, 180, 256};
    static const unsigned entry_counts[] = {1, 5, 8};
    unsigned c, n;
    for (n = 0; n < sizeof entry_counts / sizeof *entry_counts; ++n) {
        const unsigned entries = entry_counts[n];
        for (c = 0; c < sizeof counts / sizeof *counts; ++c) {
            const unsigned tables = counts[c], total = tables > 80 ? tables : 80;
            uint32_t original, modified;
            unsigned t, e, k;
            s_prints = 0;
            modified = write_scores(sub_001E8A7A, tables, entries, modified_file);
            assert(s_prints == (tables == 81 && n == 0));   /* the first unpadded save of the session logs once */
            /* Keys the tables were written with, for check_loaded. */
            static uint32_t keys[256];
            for (t = 0; t < tables; ++t) keys[t] = MEM32(order[t] + 0xC);
            check_file(modified_file, modified, tables, total, entries);
            original = write_scores(original_writer, tables, entries, original_file);
            if (tables <= 80) {
                assert(original == modified && !memcmp(original_file, modified_file, modified));
            } else {
                assert(original == 0);   /* the Xbox writer never finishes: the crash */
            }

            /* Load what the PC wrote. */
            assert(load_scores(modified_loader, modified_file, modified, entries) == 1);
            check_loaded(tables, total, entries);
            for (t = 0, k = 0; t < total; ++t)
                for (e = 0; e < entries; ++e, ++k) assert(loaded[k].key == (t < tables ? keys[t] : 0));
            assert(mm3_score_file_table_count() == total);
            if (tables <= 80) {
                static Record first[sizeof loaded / sizeof *loaded];
                unsigned first_count = loaded_count;
                memcpy(first, loaded, sizeof loaded);
                assert(load_scores(original_loader, modified_file, modified, entries) == 1);
                assert(loaded_count == first_count && !memcmp(first, loaded, loaded_count * sizeof *loaded));
            } else {
                assert(load_scores(original_loader, modified_file, modified, entries) == 0 && loaded_count == 0);
                /* Damaged or foreign sizes are still refused. */
                assert(load_scores(modified_loader, modified_file, modified - 4, entries) == 0 && !loaded_count);
                assert(load_scores(modified_loader, modified_file, modified + 4, entries) == 0 && !loaded_count);
                *(uint32_t *)(modified_file + 4) = entries + 1;
                assert(load_scores(modified_loader, modified_file, modified, entries) == 0 && !loaded_count);
                *(uint32_t *)(modified_file + 4) = entries;
                assert(load_scores(modified_loader, modified_file, modified, entries + 1) == 0 && !loaded_count);
                assert(mm3_score_file_table_count() == 80);
            }
        }
        /* More tables than one-byte keys allow. */
        {
            uint32_t size = 8 + 257 * table_bytes(entries);
            memset(modified_file, 0, size);
            *(float *)modified_file = 1.2f;
            *(uint32_t *)(modified_file + 4) = entries;
            assert(load_scores(modified_loader, modified_file, size, entries) == 0 && !loaded_count);
        }
    }
    puts("PASS native score writer/loader: 0-80 tables identical to the Xbox, 81-256 saved unpadded and "
         "loaded in full (Xbox writer runs away, its loader refuses), damaged sizes refused");
    return 0;
}
