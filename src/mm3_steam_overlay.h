#ifndef MM3_STEAM_OVERLAY_H
#define MM3_STEAM_OVERLAY_H

/* Steam's overlay (FPS counter, game recording, screenshots) when Steam started this program but
 * did not hook it: issue #47, a Steam shortcut to "Play Midtown Madness 3.cmd". Steam hooks the
 * process it starts (cmd.exe); the game is cmd.exe's child and was left without the overlay, so
 * Steam could neither show its FPS counter nor find the game to record.
 *
 * Windows only. Call before the renderer creates its window. Does nothing unless Steam started the
 * game (a Steam environment variable, or Steam among its parent processes), does nothing when the
 * overlay is already loaded (a shortcut to the exe), and MM3_STEAM_OVERLAY=0 turns it off.
 * Returns what happened: one of the MM3_STEAM_OVERLAY_* codes (logged as [MM3_STEAM]). */
enum {
    MM3_STEAM_OVERLAY_NOT_STEAM = 0,   /* not started by Steam, or MM3_STEAM_OVERLAY=0 */
    MM3_STEAM_OVERLAY_ALREADY,         /* Steam's hook is in this process already */
    MM3_STEAM_OVERLAY_LOADED,          /* loaded from the Steam folder just now */
    MM3_STEAM_OVERLAY_MISSING,         /* no Steam folder or no GameOverlayRenderer64.dll in it */
    MM3_STEAM_OVERLAY_FAILED           /* the DLL is there but Windows would not load it */
};

#ifdef _WIN32
#include <wchar.h>

int mm3_steam_overlay_attach(void);

/* Pure helper (tested): is this environment one Steam made. */
int mm3_steam_overlay_env_is_steam(const char *game_id, const char *overlay_game_id, const char *client_launch);

/* Library pictures for a Steam shortcut (the grey tile of a game added by hand). Setup draws them into
 * <install>\steam-artwork and copies them for a shortcut that exists at install time; when Steam starts the
 * game for a shortcut made later, mm3_steam_art_place copies them into that Steam user's grid folder, named
 * by the shortcut's number. Nothing already there is replaced. Returns the number of files copied.
 * MM3_STEAM_ART=0 turns it off. mm3_steam_shortcut_number: the number in SteamGameId, or 0 when that is a
 * Steam game or unset; mm3_steam_art_copy: the copy itself (tested). */
unsigned mm3_steam_shortcut_number(const char *game_id);
int mm3_steam_art_copy(const wchar_t *source_dir, const wchar_t *grid_dir, unsigned number);
int mm3_steam_art_place(void);
#endif

#endif
