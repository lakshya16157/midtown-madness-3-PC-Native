#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_cheat_keys.h"

/* Keyboard cheats typed into the game window, each switched on the
 * launcher's Gameplay mods tab (only with Modded gameplay):
 *
 *   howhardcanitbeN  N = 0..9: the race opponents' difficulty for the rest of
 *                    the session (src/mm3_ai_difficulty.c); 5 is the Xbox game.
 *                    Typed cheat codes, MM3_CHEAT_CODES. Until one is typed the
 *                    launcher's Opponents' difficulty, MM3_AI_LEVEL, holds.
 *   F8               the free camera on or off (src/mm3_free_camera.c). Free
 *                    camera, MM3_FREE_CAMERA; the key is MM3_FREE_CAMERA_KEY
 *                    (a Win32 virtual key, F8 = 119 by default).
 *
 * The window records which keys are held (xbox_FramebufferKeyDown, written
 * by the window thread, only while it has the focus), so a thread of our own
 * watches it every few milliseconds and turns presses into typed letters:
 * a key is held for 30 ms or more even in fast typing. The letters still
 * reach the game as the controller buttons they are bound to; the code is
 * long enough that nothing is ever typed by accident. Nothing here touches
 * guest memory: the game thread reads the results. */
extern int xbox_FramebufferKeyDown(int vk);

#define CODE "HOWHARDCANITBE"

static volatile LONG s_ai_level = -1;      /* -1 until typed: MM3_AI_LEVEL or 5 */
static volatile LONG s_camera_presses;

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

static void typed(char c)
{
    static char text[32];
    size_t length = strlen(text), code = sizeof CODE - 1;
    if (length == sizeof text - 1) {
        memmove(text, text + 1, length);
        --length;
    }
    text[length++] = c;
    text[length] = 0;
    if (c >= '0' && c <= '9' && length > code && !memcmp(text + length - 1 - code, CODE, code)) {
        InterlockedExchange(&s_ai_level, c - '0');
        fprintf(stderr, "[MM3_CHEAT] howhardcanitbe%c: opponents' difficulty %c (5 = Xbox)\n", c, c);
        fflush(stderr);
        MessageBeep(MB_ICONASTERISK);   /* the game shows nothing: a sound says it was taken */
        text[0] = 0;
    }
}

static DWORD WINAPI watch(void *unused)
{
    static unsigned char was[256], camera_was;
    int codes = switched_on("MM3_CHEAT_CODES"), camera = switched_on("MM3_FREE_CAMERA"), key = camera_key();
    (void)unused;
    fprintf(stderr, "[MM3_CHEAT] typed cheat codes %s, free camera %s (key 0x%02X), opponents' difficulty %d\n",
            codes ? "on" : "off", camera ? "on" : "off", key, mm3_cheat_ai_level());
    for (;;) {
        int vk, down;
        for (vk = '0'; codes && vk <= 'Z'; ++vk) {
            if (vk > '9' && vk < 'A') continue;
            down = xbox_FramebufferKeyDown(vk) || (vk <= '9' && xbox_FramebufferKeyDown(VK_NUMPAD0 + vk - '0'));
            if (down && !was[vk]) typed((char)vk);
            was[vk] = (unsigned char)down;
        }
        down = camera && xbox_FramebufferKeyDown(key);
        if (down && !camera_was) InterlockedIncrement(&s_camera_presses);
        camera_was = (unsigned char)down;
        mm3_free_camera_watchdog();
        Sleep(4);
    }
}

void mm3_cheat_keys_start(void)
{
    HANDLE thread;
    if (!switched_on("MM3_CHEAT_CODES") && !switched_on("MM3_FREE_CAMERA")) {
        fprintf(stderr, "[MM3_CHEAT] typed cheat codes and free camera off, opponents' difficulty %d\n",
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
    LONG level = InterlockedCompareExchange(&s_ai_level, -1, -1);
    if (level < 0) {
        const char *v = getenv("MM3_AI_LEVEL");
        level = v && *v >= '0' && *v <= '9' && !v[1] ? *v - '0' : 5;
    }
    return (int)level;
}

int mm3_cheat_camera_presses(void)
{
    return (int)InterlockedExchange(&s_camera_presses, 0);
}
