/* The language chosen in Game Setup survives a restart (src/mm3_language.c). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static unsigned char memory[0x400000];
static unsigned applied;

void sub_001193A6_original(void) { MEM32(0x3C5648) = 0; }      /* console default: English */
void sub_00124822_original(void) { ++applied; }
void sub_001193A6(void);
void sub_00124822(void);
int mm3_language_saved(void);

int main(int argc, char **argv)
{
    char ini[1024], path[1100];
    FILE *file;
    assert(argc == 2);
    g_xbox_mem_offset = (ptrdiff_t)memory;
    snprintf(ini, sizeof ini, "%s\\display.ini", argv[1]);
    snprintf(path, sizeof path, "%s\\language.ini", argv[1]);
    _putenv_s("MM3_DISPLAY_CONFIG", ini);
    remove(path);
    sub_001193A6(); assert(MEM32(0x3C5648) == 0);              /* nothing saved: the game's default */
    MEM32(0x3C5648) = 2; sub_00124822(); assert(applied == 1);  /* German picked in Game Setup */
    assert(mm3_language_saved() == 2);
    sub_001193A6(); assert(MEM32(0x3C5648) == 2);              /* restart: German again */
    MEM32(0x3C5648) = 0; sub_00124822(); assert(mm3_language_saved() == 0);
    sub_001193A6(); assert(MEM32(0x3C5648) == 0);              /* back to English, kept */
    MEM32(0x3C5648) = 9; sub_00124822(); assert(mm3_language_saved() == 0);   /* out of range: not saved */
    file = fopen(path, "w"); assert(file); fputs("language=banana\n", file); fclose(file);
    MEM32(0x3C5648) = 4; sub_001193A6(); assert(MEM32(0x3C5648) == 0);       /* unreadable file: default */
    remove(path);
    puts("PASS language: Game Setup choice saved beside display.ini and restored after the start-up default; "
         "out-of-range and unreadable values ignored");
    return 0;
}
