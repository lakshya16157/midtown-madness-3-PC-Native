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

#define LANGUAGE_ADDRESS 0x003C5648u
#define LANGUAGE_MAX 6

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

/* Start-up: the console default, then the player's saved choice. */
void sub_001193A6(void)
{
    int saved;
    sub_001193A6_original();
    saved = mm3_language_saved();
    if (saved >= 0 && (int)MEM32(LANGUAGE_ADDRESS) != saved) {
        fprintf(stderr, "[MM3_LANGUAGE] restored language %d (console default %d)\n",
                saved, (int)MEM32(LANGUAGE_ADDRESS));
        MEM32(LANGUAGE_ADDRESS) = (uint32_t)saved;
    }
}

/* Game Setup > Language applied. */
void sub_00124822(void)
{
    sub_00124822_original();
    mm3_language_save((int)MEM32(LANGUAGE_ADDRESS));
}
