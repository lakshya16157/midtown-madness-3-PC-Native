#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "recomp_types.h"
#include "mm3_display_config.h"

/* The game's language choice, kept across restarts.
 *
 * MM3 holds its current language in the int at 0x3C5648 (0 English,
 * 1 French, 2 German, 3 Italian, 4 Spanish, 6 Japanese: sub_001DDB23's map of
 * the console language). At start-up sub_001193A6 sets it from the console's
 * language and region; the Game Setup menu writes it through varLanguage and
 * runs the updateLanguage command (sub_00124822), which loads that language's
 * strings. Nothing ever saves the choice, so every restart fell back to the
 * console default and players had to pick their language again.
 *
 * The choice made in the menu is stored as language.ini beside display.ini
 * (%LOCALAPPDATA%\MM3Recomp, or MM3_DISPLAY_CONFIG's folder for named
 * instances) and restored after the start-up default is set. */
extern void sub_001193A6_original(void);
extern void sub_00124822_original(void);
extern void sub_000190E0(void);
extern void sub_0022231C(void);

#define LANGUAGE_ADDRESS 0x003C5648u
#define LANGUAGE_MAX 6
/* Voices: the sound manager (0x3C5D48) speaks the language named in its
 * +0xB8 string, "English" from its constructor. Only updateLanguage sets it
 * (0x12484B: the name from the table at 0x407C94 via sub_000190E0, then the
 * setter sub_0022231C, which also points the "LANG/" alias at that folder),
 * so after a restart with a saved language the text followed the choice and
 * every voice stayed English (user, 10-25: Angelina in English in Spanish). */
#define SOUND_MANAGER 0x003C5D48u
#define LANGUAGE_NAMES 0x00407C94u
#define VOICE_LANGUAGES 4           /* French, German, Italian, Spanish folders */

static int language_path(char *path, size_t capacity)
{
    char settings[1024], error[128];
    char *slash;
    if (!mm3_display_settings_path(settings, sizeof settings, error, sizeof error)) return 0;
    slash = strrchr(settings, '\\');
    if (!slash || (strrchr(settings, '/') && strrchr(settings, '/') > slash)) slash = strrchr(settings, '/');
    if (slash) slash[1] = '\0';
    else settings[0] = '\0';
    return snprintf(path, capacity, "%slanguage.ini", settings) < (int)capacity;
}

int mm3_language_saved(void)
{
    char path[1100], line[64];
    FILE *file;
    int language = -1;
    if (!language_path(path, sizeof path) || !(file = fopen(path, "r"))) return -1;
    while (fgets(line, sizeof line, file))
        if (!strncmp(line, "language=", 9)) {
            char *end;
            long value = strtol(line + 9, &end, 10);
            if (end != line + 9 && value >= 0 && value <= LANGUAGE_MAX) language = (int)value;
        }
    fclose(file);
    return language;
}

void mm3_language_save(int language)
{
    char path[1100], folder[1100];
    char *slash;
    FILE *file;
    if (language < 0 || language > LANGUAGE_MAX || !language_path(path, sizeof path)) return;
    if (mm3_language_saved() == language) return;
    snprintf(folder, sizeof folder, "%s", path);
    slash = strrchr(folder, '\\');
    if (!slash || (strrchr(folder, '/') && strrchr(folder, '/') > slash)) slash = strrchr(folder, '/');
    if (slash) {
        *slash = '\0';
#ifdef _WIN32
        _mkdir(folder);
#endif
    }
    if (!(file = fopen(path, "w"))) {
        fprintf(stderr, "[MM3_LANGUAGE] could not save the language to %s\n", path);
        return;
    }
    fprintf(file, "; Midtown Madness 3: the language chosen in Options > Game Setup\nlanguage=%d\n", language);
    fclose(file);
    fprintf(stderr, "[MM3_LANGUAGE] saved language %d\n", language);
}

/* Which sound manager speaks which language: a new manager speaks English. */
static uint32_t s_voice_manager;
static int s_voice_language;

static void voice_language_apply(int language)
{
    uint32_t manager = MEM32(SOUND_MANAGER), name;
    uint32_t saved[] = {g_eax, g_ecx, g_edx, g_ebp, g_seh_ebp}, stack_top = g_esp;
    if (manager < 0x10000u || manager >= 0x08000000u) return;
    if (manager != s_voice_manager) { s_voice_manager = manager; s_voice_language = 0; }
    if (language == s_voice_language || language < 0 || language > VOICE_LANGUAGES) return;
    if (MEM32(LANGUAGE_NAMES + 8u) < 5u) return;            /* the name table is not built */
    g_eax = (uint32_t)language;
    PUSH32(g_esp, 0);                                       /* host-owned call, no return address */
    sub_000190E0();
    name = g_eax;
    if (name >= 0x10000u && name < 0x08000000u) {
        PUSH32(g_esp, name);
        PUSH32(g_esp, 0);
        g_ecx = manager;
        sub_0022231C();
        s_voice_language = language;
        fprintf(stderr, "[MM3_LANGUAGE] voices follow language %d\n", language);
    }
    g_eax = saved[0]; g_ecx = saved[1]; g_edx = saved[2]; g_ebp = saved[3]; g_seh_ebp = saved[4];
    g_esp = stack_top;
}

/* Start-up and every menu (this is the menu base constructor, which re-reads
 * the console language): the player's saved choice, voices included. */
void sub_001193A6(void)
{
    int saved;
    sub_001193A6_original();
    saved = mm3_language_saved();
    if (saved >= 0 && (int)MEM32(LANGUAGE_ADDRESS) != saved) {
        static int reported;
        if (!reported) {
            reported = 1;
            fprintf(stderr, "[MM3_LANGUAGE] restored language %d (console default %d)\n",
                    saved, (int)MEM32(LANGUAGE_ADDRESS));
        }
        MEM32(LANGUAGE_ADDRESS) = (uint32_t)saved;
    }
    voice_language_apply((int)MEM32(LANGUAGE_ADDRESS));
}

/* Game Setup > Language applied (the game sets the voices itself). */
void sub_00124822(void)
{
    sub_00124822_original();
    s_voice_manager = MEM32(SOUND_MANAGER);
    s_voice_language = (int)MEM32(LANGUAGE_ADDRESS);
    mm3_language_save((int)MEM32(LANGUAGE_ADDRESS));
}
