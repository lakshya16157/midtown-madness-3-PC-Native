#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "kernel.h"
#include "mm3_voice_fixes.h"

/* The retail Spanish Paris voice bank carries the Italian recordings of the
 * Paris police's lines (the Spanish release was never dubbed for them).
 * tools/car-mods/voice_fixes.py writes a copy with the French recordings of
 * those 42 lines, Paris_mm3.xwb beside the untouched Paris.xwb; open it in
 * the original's place. The sound manager builds the path as
 * "D:\Data\Shared\Audio\Voice\" + language + "\" + city + ".xwb" (0x3B62C4).
 * MM3_VOICE_FIXES=0 keeps the retail bank. */
#define SPANISH_PARIS "D:\\Data\\Shared\\Audio\\Voice\\Spanish\\Paris.xwb"
#define SPANISH_PARIS_FIXED "D:\\Data\\Shared\\Audio\\Voice\\Spanish\\Paris_mm3.xwb"

static int s_fixed_present = -1;

static int same_path(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        char x = *a == '/' ? '\\' : *a, y = *b == '/' ? '\\' : *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return !*a && !*b;
}

static const char *voice_rewrite(const char *xbox_path)
{
    if (!same_path(xbox_path, SPANISH_PARIS)) return NULL;
    if (s_fixed_present < 0) {
        WCHAR host[MAX_PATH];
        DWORD attributes;
        s_fixed_present = 0;
        if (xbox_translate_path(SPANISH_PARIS_FIXED, host, MAX_PATH)) {
            attributes = GetFileAttributesW(host);
            s_fixed_present = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        }
        fprintf(stderr, "[MM3_LANGUAGE] Spanish Paris police voices: %s\n",
                s_fixed_present ? "French recordings (Paris_mm3.xwb)" : "retail bank (Italian recordings)");
    }
    return s_fixed_present ? SPANISH_PARIS_FIXED : NULL;
}

void mm3_voice_fixes_install(void)
{
    const char *setting = getenv("MM3_VOICE_FIXES");
    if (setting && !strcmp(setting, "0")) return;
    g_xbox_path_rewrite = voice_rewrite;
}
