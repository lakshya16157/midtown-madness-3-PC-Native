/* Downloadable races 11-20 get names, descriptions, route pictures and
 * loading screens (src/mm3_dlc_races.c). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include "recomp_types.h"
#include "mm3_dlc_races.h"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_esp, g_ebp;
static unsigned char memory[0x400000];
static char appended[32];
static uint32_t called_return, called_string;
enum { STACK = 0x40000, FRAME = 0x41000, NAME = 0x20000, DEST = 0x21000, STRING = 0x22000, HEAP = 0x23000,
       TGA = 0x39CA6C, LOADING_TGA = 0x3AFFB4 };
/* The string builder's append (RET 8): copy what it was given. */
void sub_002430FD_original(void)
{
    uint32_t text = MEM32(g_esp + 8), i;
    called_return = MEM32(g_esp);
    called_string = MEM32(g_esp + 4);
    for (i = 0; i < sizeof appended - 1 && (appended[i] = (char)MEM8(text + i)) != 0; ++i) {}
    appended[i] = 0;
    g_esp += 12;
}
void sub_002430FD(void);
/* The race list finishing "Races/<City>/<Mode>/<NN>" with ".tga". */
static const char *finish_icon(const char *name)
{
    size_t i;
    for (i = 0; i <= strlen(name); ++i) MEM8(NAME + i) = (uint8_t)name[i];
    memcpy(memory + TGA, ".tga", 5);
    MEM32(FRAME - 0x2C) = NAME;
    MEM32(STACK) = 0x00103F59u; MEM32(STACK + 4) = DEST; MEM32(STACK + 8) = TGA;
    g_esp = STACK; g_ebp = FRAME; appended[0] = 0;
    sub_002430FD();
    assert(g_esp == STACK + 12 && g_ebp == FRAME && called_return == 0x00103F59u && called_string == DEST);
    return appended;
}

/* The loading screen's name written to an output stream: the stream points
 * to its buffer (vtable 0x384FD8), which holds an MSVC std::string at +4
 * (text in place below 16 characters, else on the heap); then ".tga". */
static const char *finish_loading_with(const char *name, uint32_t vtable)
{
    const uint32_t buffer = STRING + 0x14, text = buffer + 4;
    size_t length = strlen(name), i;
    memset(memory + STRING, 0xCC, 0x40);
    MEM32(STRING) = buffer;
    MEM32(buffer) = vtable;
    MEM32(text + 0x14) = (uint32_t)length;
    MEM32(text + 0x18) = length < 16 ? 15u : (uint32_t)length | 15u;
    if (length >= 16) MEM32(text + 4) = HEAP;
    for (i = 0; i <= length; ++i) MEM8((length < 16 ? text + 4 : HEAP) + i) = (uint8_t)name[i];
    memcpy(memory + LOADING_TGA, ".tga", 5);
    MEM32(STACK) = 0x001E82ACu; MEM32(STACK + 4) = STRING; MEM32(STACK + 8) = LOADING_TGA;
    g_esp = STACK; g_ebp = FRAME; appended[0] = 0;
    sub_002430FD();
    assert(g_esp == STACK + 12 && g_ebp == FRAME && called_return == 0x001E82ACu && called_string == STRING);
    return appended;
}

static const char *finish_loading(const char *name)
{
    return finish_loading_with(name, 0x00384FD8u);
}

int main(int argc, char **argv)
{
    const char *text;
    FILE *ini;
    g_xbox_mem_offset = (ptrdiff_t)memory;
    /* The installer lists the downloadable races it drew a route for. */
    _mkdir("game_files"); _mkdir("game_files/Data");
    ini = fopen("game_files/Data/mm3_dlc_icons.ini", "wb"); assert(ini);
    fputs("# test\r\nParis/Blitz/10\r\nWashington/Checkpoint/19\r\n", ini); fclose(ini);
    ini = fopen("game_files/Data/mm3_dlc_loading.ini", "wb"); assert(ini);
    fputs("# test\nEnglish/Paris/Blitz/10\nFrench/Washington/Checkpoint/19\n", ini); fclose(ini);
    if (argc > 1 && !strcmp(argv[1], "off")) {
        _putenv_s("MM3_DLC_RACE_INFO", "0");
        assert(!mm3_dlc_race_label("SID_BLITZ_1_11", 0) && !mm3_dlc_race_label("SID_B1R11QB", 0));
        assert(!strcmp(finish_icon("Races/Paris/Blitz/10"), ".tga"));
        assert(!strcmp(finish_loading("+LoadingScreens/English/Paris/Blitz/Loading10"), ".tga"));
        puts("PASS DLC race info off: MM3_DLC_RACE_INFO=0 keeps the retail placeholders, pictures and loading screens");
        return 0;
    }
    /* Own route pictures for listed downloadable races; everything else as retail. */
    assert(!strcmp(finish_icon("Races/Paris/Blitz/10"), "_mm3.tga"));
    assert(!strcmp(finish_icon("Races/Washington/Checkpoint/19"), "_mm3.tga"));
    assert(!strcmp(finish_icon("Races/Paris/Blitz/11"), ".tga"));          /* not drawn: DLC's copy */
    assert(!strcmp(finish_icon("Races/Paris/Blitz/00"), ".tga"));          /* retail race */
    assert(!strcmp(finish_icon("Races/Paris/Checkpoint/20"), ".tga"));     /* circuit */
    assert(!strcmp(finish_icon("Cars/Paris/Blitz/10"), ".tga"));
    /* Loading screens the installer made; every other screen as retail. */
    assert(!strcmp(finish_loading("+LoadingScreens/English/Paris/Blitz/Loading10"), "_mm3.tga"));
    assert(!strcmp(finish_loading("+LoadingScreens/French/Washington/Checkpoint/Loading19"), "_mm3.tga"));
    assert(!strcmp(finish_loading("+LoadingScreens/German/Paris/Blitz/Loading10"), ".tga"));   /* not made */
    assert(!strcmp(finish_loading("+LoadingScreens/English/Paris/Blitz/Loading00"), ".tga"));  /* retail */
    assert(!strcmp(finish_loading("+LoadingScreens/English/Paris/Career/02/Loading01"), ".tga"));
    assert(!strcmp(finish_loading("+LoadingScreens/English/Paris/Blitz/Loading100"), ".tga"));
    assert(!strcmp(finish_loading("+LoadingScreens"), ".tga"));                               /* in place */
    assert(!strcmp(finish_loading("LoadingScreens/English/Paris/Blitz/Loading10"), ".tga"));
    assert(!strcmp(finish_loading_with("+LoadingScreens/English/Paris/Blitz/Loading10", 0x00384FD4u), ".tga"));
    assert(!strcmp(mm3_dlc_race_label("SID_BLITZ_1_11", 0), "Summer Storm Dash"));
    assert(!strcmp(mm3_dlc_race_label("SID_BLITZ_2_20", 4), "Monument Marathon"));
    assert(!strcmp(mm3_dlc_race_label("SID_CHKPT_1_11", 2), "Going Downtown"));
    assert(!strcmp(mm3_dlc_race_label("SID_CHKPT_2_20", 0), "Long Spring Evening"));
    /* Retail races, circuits and other keys stay with their own lookups. */
    assert(!mm3_dlc_race_label("SID_BLITZ_1_10", 0) && !mm3_dlc_race_label("SID_BLITZ_1_21", 0));
    assert(!mm3_dlc_race_label("SID_CHKPT_1_21", 0) && !mm3_dlc_race_label("SID_CHKPT_3_11", 0));
    assert(!mm3_dlc_race_label("SID_B1R10QB", 0) && !mm3_dlc_race_label("SID_C1R21QB", 0));
    assert(!mm3_dlc_race_label("SID_BLITZ_1_11X", 0) && !mm3_dlc_race_label("SID_B1R11Q", 0));
    assert(!mm3_dlc_race_label("SID_CAR_MF_9014", 0) && !mm3_dlc_race_label(NULL, 0));
    text = mm3_dlc_race_label("SID_B1R11QB", 0);
    assert(!strcmp(text, "Downloaded blitz: reach all 14 checkpoints within 2:05. Summer, evening, rain."));
    text = mm3_dlc_race_label("SID_B2R20QB", 0);
    assert(!strcmp(text, "Downloaded blitz: reach all 54 checkpoints within 12:40. Summer, midday, clear skies."));
    text = mm3_dlc_race_label("SID_C2R16QB", 0);
    assert(!strcmp(text, "Downloaded checkpoint race: 8 checkpoints against rival drivers. Winter, morning, clear skies."));
    /* Each language, accents as Latin-1; unknown languages fall back to English. */
    text = mm3_dlc_race_label("SID_B1R12QB", 4);
    assert(!strcmp(text, "Carrera rel\xE1mpago descargada: alcanza los 15 puntos de control en 1:25. "
                         "Primavera, mediod\xED\x61, cielo despejado."));
    text = mm3_dlc_race_label("SID_C1R16QB", 1);
    assert(!strcmp(text, "Course t\xE9l\xE9\x63harg\xE9\x65 : 16 points de contr\xF4le contre des pilotes rivaux. "
                         "Printemps, nuit, pluie."));
    assert(strstr(mm3_dlc_race_label("SID_B1R13QB", 2), "Schnee"));
    assert(strstr(mm3_dlc_race_label("SID_B1R13QB", 3), "neve"));
    assert(strstr(mm3_dlc_race_label("SID_B1R13QB", 6), "snow"));
    assert(strstr(mm3_dlc_race_label("SID_B1R18QB", 4), "ma\xF1\x61na"));
    puts("PASS DLC races: names for races 11-20 of both cities and modes, descriptions from each race's "
         "checkpoints, time limit, season, time and weather in five languages, own route pictures and loading screens for the listed ones; other keys untouched");
    return 0;
}
