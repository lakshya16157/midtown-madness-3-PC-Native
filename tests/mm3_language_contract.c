/* The language chosen in Game Setup survives a restart, voices included
 * (src/mm3_language.c). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_types.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
static unsigned char memory[0x410000];     /* through the name table at 0x407C94 */
static unsigned applied, voiced;
static uint32_t voice_manager, voice_name;
#define STACK 0x30000u
#define MANAGER 0x60000u

void sub_001193A6(void);
void sub_00124822(void);
int mm3_language_saved(void);
void sub_001193A6_original(void) { MEM32(0x3C5648) = 0; }      /* console default: English */
void sub_00124822_original(void) { ++applied; }
/* The language name table: eax = language -> eax = its std::string. */
void sub_000190E0(void) { assert(g_esp == STACK - 4); g_eax = 0x50000u + g_eax * 0x20u; g_esp += 4; }
/* The sound manager's setter: thiscall, one argument, ret 4. */
void sub_0022231C(void)
{
    assert(g_esp == STACK - 8);
    ++voiced; voice_manager = g_ecx; voice_name = MEM32(g_esp + 4);
    g_ecx = g_edx = 0xDEAD; g_esp += 8;
}
static void menu(void)
{
    g_esp = STACK; g_ecx = 0x1234; g_edx = 0x5678; g_ebp = 0x9ABC;
    sub_001193A6();
    assert(g_esp == STACK && g_ecx == 0x1234 && g_edx == 0x5678 && g_ebp == 0x9ABC);
}

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
    /* Voices: a restart with Spanish saved points the sound manager at the
     * Spanish folder once, as Game Setup would; again for a new manager; not
     * without a manager or name table, not for English or a language with no
     * voices on the disc; never twice after Game Setup did it. */
    MEM32(0x407C94 + 8) = 7; MEM32(0x3C5D48) = MANAGER;
    MEM32(0x3C5648) = 4; sub_00124822(); voiced = 0;
    menu(); assert(MEM32(0x3C5648) == 4 && voiced == 0);       /* Game Setup already set it */
    MEM32(0x3C5D48) = MANAGER + 0x100;                          /* restart: a new sound manager */
    menu(); assert(voiced == 1 && voice_manager == MANAGER + 0x100 && voice_name == 0x50080u);
    menu(); menu(); assert(voiced == 1);
    MEM32(0x3C5D48) = 0; menu(); assert(voiced == 1);           /* no manager yet */
    MEM32(0x3C5D48) = MANAGER + 0x200; MEM32(0x407C94 + 8) = 0;
    menu(); assert(voiced == 1);                                /* no name table yet */
    MEM32(0x407C94 + 8) = 7; menu(); assert(voiced == 2 && voice_name == 0x50080u);
    MEM32(0x3C5648) = 0; sub_00124822(); MEM32(0x3C5D48) = MANAGER + 0x300;
    menu(); assert(voiced == 2);                                /* English: the manager's own */
    MEM32(0x3C5648) = 6; sub_00124822(); MEM32(0x3C5D48) = MANAGER + 0x400;
    menu(); assert(voiced == 2);                                /* Japanese: no voices on the disc */
    remove(path);
    puts("PASS language: Game Setup choice saved beside display.ini and restored after the start-up default, "
         "voices follow it after a restart; out-of-range and unreadable values ignored");
    return 0;
}
