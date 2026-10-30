#include <stdio.h>
#include "recomp_types.h"

/* The best-times file ("score" in each profile's save). Every race with a
 * time has a table, keyed race + 0x40 for Checkpoint + 0x80 for Washington
 * (sub_001E882B). The Xbox wrote exactly 80 tables, padding with empty ones:
 * two cities x two modes x races 00-19 (retail and downloadable) is 80 keys.
 * The circuit races (Checkpoint 20-29) add 20. Once a player had times in
 * more than 80 races, the writer's padding count 80 - tables wrapped and it
 * wrote until memory ran out, after a race's results (sub_001E8A7A, saved by
 * sub_001E8E32). Now it pads only up to 80 and saves every table, and the
 * loader (sub_001E9060) reads as many tables as the file holds. Files with
 * 80 tables or fewer stay the Xbox's, byte for byte.
 * Applied by scripts/mm3_score_tables_seam.cmake. */

#ifndef MM3_SCORES_PRINT
#define MM3_SCORES_PRINT(...) (fprintf(stderr, __VA_ARGS__), fflush(stderr))
#endif

#define SCORE_TABLES 0x50u    /* the Xbox's fixed count */
#define SCORE_KEYS 0x100u     /* keys are one byte */
#define SCORE_HEADER 8u       /* version 1.2, entries per table */
#define SCORE_ENTRY 44u       /* name (length + 16 UTF-16 units), time, value */

static uint32_t s_tables = SCORE_TABLES;

/* Writer: nonzero skips the padding (more tables than the Xbox's 80). */
int mm3_score_tables_unpadded(uint32_t tables)
{
    static int logged;
    if (tables <= SCORE_TABLES) return 0;
    if (!logged) {
        logged = 1;
        MM3_SCORES_PRINT("[MM3_SCORES] saving best times for %u races, more than the Xbox's 80 "
                         "(the circuit races add 20): the score file grows instead of being padded\n", tables);
    }
    return 1;
}

/* Loader: how many tables to read from a file of `size` bytes at `data`
 * (after the size check's `expected`, the Xbox's own count), or 0 to reject
 * it as the Xbox did. `scores` is the best-times object (+4 entries a table). */
uint32_t mm3_score_file_tables(uint32_t data, uint32_t size, uint32_t expected, uint32_t scores)
{
    uint32_t entries, table, tables;
    s_tables = SCORE_TABLES;
    if (size == expected) return s_tables;
    entries = MEM32(scores + 4u);
    if (entries == 0 || entries > 64u || size <= SCORE_HEADER || MEM32(data + 4u) != entries) return 0;
    table = 4u + entries * SCORE_ENTRY;
    tables = (size - SCORE_HEADER) / table;
    if ((size - SCORE_HEADER) % table || tables <= SCORE_TABLES || tables > SCORE_KEYS) return 0;
    MM3_SCORES_PRINT("[MM3_SCORES] score file holds best times for %u races (the Xbox's fixed 80 + circuit races)\n",
                     tables);
    return s_tables = tables;
}

uint32_t mm3_score_file_table_count(void)
{
    return s_tables;
}
