/* Steam overlay attach (issue #47): a game Steam started through a script gets the overlay loaded
 * from the Steam folder; a game Steam did not start, one that already has the overlay, and a run
 * with MM3_STEAM_OVERLAY=0 are left alone. The regression builds a stand-in GameOverlayRenderer64.dll
 * in a temporary "Steam folder" (SteamPath) and runs this once per scenario with a scrubbed
 * environment. Usage: contract <scenario> [stand-in dll path]. */
#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mm3_steam_overlay.c"

static int overlay_loaded(void)
{
    return GetModuleHandleW(L"GameOverlayRenderer64.dll") != NULL;
}

int main(int argc, char **argv)
{
    const char *scenario = argc > 1 ? argv[1] : "";

    /* Which environments are Steam's. */
    assert(mm3_steam_overlay_env_is_steam("123", NULL, NULL));
    assert(mm3_steam_overlay_env_is_steam(NULL, "123", NULL));
    assert(mm3_steam_overlay_env_is_steam(NULL, NULL, "1"));
    assert(!mm3_steam_overlay_env_is_steam(NULL, NULL, NULL));
    assert(!mm3_steam_overlay_env_is_steam("", "", ""));

    if (!strcmp(scenario, "not-steam") || !strcmp(scenario, "opt-out")) {
        /* No Steam environment (or switched off): nothing is loaded. */
        assert(mm3_steam_overlay_attach() == MM3_STEAM_OVERLAY_NOT_STEAM);
        assert(!overlay_loaded());
    } else if (!strcmp(scenario, "load")) {
        /* Started by Steam, no overlay yet: loaded from SteamPath (forward slashes, trailing slash). */
        assert(!overlay_loaded());
        assert(mm3_steam_overlay_attach() == MM3_STEAM_OVERLAY_LOADED);
        assert(overlay_loaded());
        assert(mm3_steam_overlay_attach() == MM3_STEAM_OVERLAY_ALREADY);   /* never loaded twice */
    } else if (!strcmp(scenario, "already")) {
        /* Steam hooked this process itself (a shortcut to the exe): left as it is. */
        assert(argc > 2 && LoadLibraryA(argv[2]));
        assert(overlay_loaded());
        assert(mm3_steam_overlay_attach() == MM3_STEAM_OVERLAY_ALREADY);
    } else if (!strcmp(scenario, "broken")) {
        /* The file is there but is not a library: reported, the game still starts. */
        assert(mm3_steam_overlay_attach() == MM3_STEAM_OVERLAY_FAILED);
        assert(!overlay_loaded());
    } else if (!strcmp(scenario, "number")) {
        /* SteamGameId of a non-Steam shortcut is (number << 32) | 0x02000000; a Steam game's is its small app id. */
        char id[40];
        snprintf(id, sizeof id, "%llu", (2735476592ull << 32) | 0x02000000ull);
        assert(mm3_steam_shortcut_number(id) == 2735476592u);
        assert(mm3_steam_shortcut_number("480") == 0 && mm3_steam_shortcut_number("") == 0);
        assert(mm3_steam_shortcut_number(NULL) == 0 && mm3_steam_shortcut_number("abc") == 0);
        snprintf(id, sizeof id, "%llu", (2735476592ull << 32) | 0x01000000ull);
        assert(mm3_steam_shortcut_number(id) == 0);                    /* the low half says it is no shortcut */
        snprintf(id, sizeof id, "%llu", 0x02000000ull);
        assert(mm3_steam_shortcut_number(id) == 0);                    /* no number in it */
    } else if (!strcmp(scenario, "art")) {
        /* argv[2]: steam-artwork with the five pictures; argv[3]: the grid folder, made or not yet.
         * The regression checks the files left behind. */
        wchar_t source[MAX_PATH], grid[MAX_PATH];
        assert(argc > 3);
        assert(MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, source, MAX_PATH) > 0);
        assert(MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, grid, MAX_PATH) > 0);
        assert(mm3_steam_art_copy(source, grid, 2735476592u) == 3);   /* the portrait and the hero were the user's */
        assert(mm3_steam_art_copy(source, grid, 2735476592u) == 0);   /* nothing new the second time */
        assert(mm3_steam_art_copy(source, grid, 7u) == 5);            /* another shortcut: everything */
    } else {
        fprintf(stderr, "unknown scenario '%s'\n", scenario);
        return 2;
    }
    printf("PASS: steam overlay %s\n", scenario);
    return 0;
}
