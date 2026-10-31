#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_cheat_keys.h"
#include "mm3_photo.h"

/* Keys the game window gives us, each switched on the launcher's Gameplay
 * mods tab:
 *
 *   F8               the free camera on or off (src/mm3_free_camera.c). Free
 *                    camera, MM3_FREE_CAMERA (only with Modded gameplay); the
 *                    key is MM3_FREE_CAMERA_KEY (a Win32 virtual key, F8 = 119
 *                    by default).
 *   F2               holds the race still and flies the camera around it, and
 *                    again lets the race run (src/mm3_pause_clock.c,
 *                    src/mm3_free_camera.c); F3 saves a picture of the window
 *                    (src/mm3_photo.c). Photo mode, MM3_PHOTO_MODE; the keys
 *                    are MM3_PAUSE_KEY and MM3_PHOTO_KEY (F2 = 113, F3 = 114),
 *                    never F9-F11 or the free camera's key.
 *   F4               hides the race HUD and shows it again (the renderer skips
 *                    it, nv2a_d3d_set_hud_hidden, and the checkpoint arrow
 *                    stays away, src/mm3_hud_options.c); it comes back by
 *                    itself when the race ends. Photo mode too, MM3_HUD_KEY.
 *
 * The window records which keys are held (xbox_FramebufferKeyDown, written
 * by the window thread, only while it has the focus), so a thread of our own
 * watches it every few milliseconds and counts presses: a key is held for
 * 30 ms or more even when tapped. Nothing here touches guest memory: the game
 * thread reads the results.
 *
 * The opponents' difficulty is the launcher's Opponents' difficulty,
 * MM3_AI_LEVEL; the typed codes that once changed it in game are gone. */
extern int xbox_FramebufferKeyDown(int vk);
extern void nv2a_d3d_set_hud_hidden(int hidden);

static volatile LONG s_camera_presses;
static volatile LONG s_pause_presses;
static volatile LONG s_hud_presses;     /* from the free camera's controller button */
static volatile LONG s_hud_hidden;

int mm3_hud_hidden(void)
{
    return InterlockedCompareExchange(&s_hud_hidden, 0, 0) != 0;
}

static void hide_hud(int hidden, const char *why)
{
    InterlockedExchange(&s_hud_hidden, hidden != 0);
    nv2a_d3d_set_hud_hidden(hidden);
    fprintf(stderr, "[MM3_PHOTO] HUD %s (%s)\n", hidden ? "hidden" : "shown", why);
    fflush(stderr);
}

/* An on/off switch from the launcher; on unless it says 0. */
static int switched_on(const char *name)
{
    const char *v = getenv(name);
    return !v || !*v || strcmp(v, "0") != 0;
}

static int camera_key(void)
{
    const char *v = getenv("MM3_FREE_CAMERA_KEY");
    int vk = v && *v ? atoi(v) : 0;
    return vk > 0 && vk < 256 && vk != 0x79 && vk != 0x7A ? vk : 0x77;   /* not F10/F11 */
}

/* A key from the launcher or the environment, else the default. The keys the
 * game window keeps for itself (F9 black frame capture, F10 settings, F11 full
 * screen) and the ones already taken fall back to the default, and a default
 * that is taken moves on to the next key. */
static int photo_key(const char *name, int fallback, int taken, int taken_too)
{
    const char *v = getenv(name);
    int vk = v && *v ? atoi(v) : 0;
    if (vk < 1 || vk > 255 || (vk >= 0x78 && vk <= 0x7A) || vk == taken || vk == taken_too) vk = fallback;
    while (vk == taken || vk == taken_too) ++vk;
    return vk;
}

static DWORD WINAPI watch(void *unused)
{
    static unsigned char camera_was, pause_was, photo_was, hud_was;
    int camera = switched_on("MM3_FREE_CAMERA"), key = camera_key();
    int photos = switched_on("MM3_PHOTO_MODE");
    int pause_key = photo_key("MM3_PAUSE_KEY", 0x71, camera ? key : 0, 0);
    int picture_key = photo_key("MM3_PHOTO_KEY", 0x72, camera ? key : 0, pause_key);
    int hud_key = photo_key("MM3_HUD_KEY", 0x73, camera ? key : 0, pause_key);
    while (hud_key == picture_key || hud_key == pause_key || (camera && hud_key == key) ||
           (hud_key >= 0x78 && hud_key <= 0x7A)) ++hud_key;
    (void)unused;
    fprintf(stderr, "[MM3_CHEAT] free camera %s (key 0x%02X), opponents' difficulty %d\n",
            camera ? "on" : "off", key, mm3_cheat_ai_level());
    fprintf(stderr, "[MM3_CHEAT] photo mode %s (pause and fly key 0x%02X, picture key 0x%02X, hide HUD key 0x%02X)\n",
            photos ? "on" : "off", pause_key, picture_key, hud_key);
    for (;;) {
        int down = camera && xbox_FramebufferKeyDown(key);
        if (down && !camera_was) InterlockedIncrement(&s_camera_presses);
        camera_was = (unsigned char)down;
        down = photos && xbox_FramebufferKeyDown(pause_key);
        if (down && !pause_was) InterlockedIncrement(&s_pause_presses);
        pause_was = (unsigned char)down;
        down = photos && xbox_FramebufferKeyDown(picture_key);
        if (down && !photo_was) mm3_photo_take();
        photo_was = (unsigned char)down;
        /* The HUD key only in a race; the HUD returns when the race ends. */
        down = photos && xbox_FramebufferKeyDown(hud_key);
        if (down && !hud_was && mm3_race_view_recent()) hide_hud(!mm3_hud_hidden(), "key");
        hud_was = (unsigned char)down;
        if (InterlockedExchange(&s_hud_presses, 0) && photos && mm3_race_view_recent())
            hide_hud(!mm3_hud_hidden(), "controller");
        if (mm3_hud_hidden() && !mm3_race_view_recent()) hide_hud(0, "no race view");
        mm3_free_camera_watchdog();
        Sleep(4);
    }
}

void mm3_cheat_keys_start(void)
{
    HANDLE thread;
    if (!switched_on("MM3_FREE_CAMERA") && !switched_on("MM3_PHOTO_MODE")) {
        fprintf(stderr, "[MM3_CHEAT] free camera and photo mode off, opponents' difficulty %d\n",
                mm3_cheat_ai_level());
        return;
    }
    thread = CreateThread(NULL, 0, watch, NULL, 0, NULL);
    if (thread) {
        SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(thread);
    }
}

int mm3_cheat_ai_level(void)
{
    const char *v = getenv("MM3_AI_LEVEL");
    return v && *v >= '0' && *v <= '9' && !v[1] ? *v - '0' : 5;
}

int mm3_cheat_camera_presses(void)
{
    return (int)InterlockedExchange(&s_camera_presses, 0);
}

int mm3_cheat_pause_presses(void)
{
    return (int)InterlockedExchange(&s_pause_presses, 0);
}

void mm3_cheat_pause_press(void)
{
    InterlockedIncrement(&s_pause_presses);
}

void mm3_cheat_hud_press(void)
{
    InterlockedIncrement(&s_hud_presses);
}
