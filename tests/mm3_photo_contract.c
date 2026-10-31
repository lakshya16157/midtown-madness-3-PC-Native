/* Photo mode: F2 holds the race still (src/mm3_pause_clock.c), F3 and the
 * controller's A save a picture of the window (src/mm3_photo.c), keys and
 * buttons from src/mm3_cheat_keys.c and src/mm3_free_camera.c. The key thread
 * runs for real against pretend keys; the renderer is a stub that hands over
 * a frame when asked.
 *
 *   contract.exe        everything on
 *   contract.exe off    MM3_PHOTO_MODE=0 (the driver sets it)
 *   contract.exe net    RECOMP_NET=nat: System Link is on (the driver sets it) */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <xinput.h>
#include "recomp_types.h"
#include "../src/mm3_cheat_keys.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
static unsigned char memory[0x430000];
static const uint32_t k_manager = 0x31000, k_camera = 0x32000, k_out = 0x33000, k_stack = 0xFF00;
static int held[256], blocked, pad_connected;
static XINPUT_GAMEPAD pad_state;
static uint64_t counter;
static void (*requested)(const void *, uint32_t, uint32_t, uint32_t);
static int requests;
static volatile LONG renderer_hud_hidden;

int xbox_FramebufferKeyDown(int vk) { return vk >= 0 && vk < 256 && held[vk]; }
void nv2a_d3d_set_hud_hidden(int hidden) { InterlockedExchange(&renderer_hud_hidden, hidden); }
void xbox_InputSetBlocked(int value) { blocked = value; }
DWORD WINAPI test_XInputGetState(DWORD port, XINPUT_STATE *s)
{
    if (!pad_connected || port) return ERROR_DEVICE_NOT_CONNECTED;
    memset(s, 0, sizeof *s);
    s->Gamepad = pad_state;
    return ERROR_SUCCESS;
}
void nv2a_d3d_request_screenshot(void (*take)(const void *, uint32_t, uint32_t, uint32_t)) { requested = take; ++requests; }
void sub_001EA16C_original(void) { g_esp += 8; }              /* the game's matrix: left alone */
void sub_0016FC89_original(void)                              /* the game's counter, ret 4 */
{
    g_eax = (uint32_t)counter;
    g_edx = (uint32_t)(counter >> 32);
    g_esp += 8;
}

#include "../src/mm3_free_camera.c"
#include "../src/mm3_pause_clock.c"
#include "../src/mm3_photo.c"

static const uint64_t k_second = 733333333ull;

static void draw(void)                                        /* a race frame's view */
{
    g_esp = k_stack;
    MEM32(g_esp + 4) = k_camera;
    g_ebx = k_out;
    sub_001EA16C();
    assert(g_esp == k_stack + 8);
}

static uint64_t read_clock(uint64_t real)
{
    counter = real;
    g_esp = k_stack;
    sub_0016FC89();
    assert(g_esp == k_stack + 8);
    return ((uint64_t)g_edx << 32) | g_eax;
}

static void tap(int vk)
{
    held[vk] = 1; Sleep(40);
    held[vk] = 0; Sleep(40);
}

static void keys_contract(void)
{
    _putenv_s("MM3_PAUSE_KEY", "");
    _putenv_s("MM3_PHOTO_KEY", "");
    assert(photo_key("MM3_PAUSE_KEY", 0x71, 0x77, 0) == 0x71);             /* F2 */
    assert(photo_key("MM3_PHOTO_KEY", 0x72, 0x77, 0x71) == 0x72);          /* F3 */
    assert(photo_key("MM3_PAUSE_KEY", 0x71, 0x71, 0) == 0x72);             /* the free camera on F2: pause moves on */
    assert(photo_key("MM3_PHOTO_KEY", 0x72, 0x71, 0x72) == 0x73);
    _putenv_s("MM3_PAUSE_KEY", "120");                                      /* F9 is the black frame capture */
    assert(photo_key("MM3_PAUSE_KEY", 0x71, 0x77, 0) == 0x71);
    _putenv_s("MM3_PAUSE_KEY", "122");                                      /* F11 is full screen */
    assert(photo_key("MM3_PAUSE_KEY", 0x71, 0x77, 0) == 0x71);
    _putenv_s("MM3_PAUSE_KEY", "80");                                       /* P */
    assert(photo_key("MM3_PAUSE_KEY", 0x71, 0x77, 0) == 80);
    _putenv_s("MM3_PAUSE_KEY", "119");                                      /* the free camera's own key */
    assert(photo_key("MM3_PAUSE_KEY", 0x71, 0x77, 0) == 0x71);
    _putenv_s("MM3_PAUSE_KEY", "nonsense");
    assert(photo_key("MM3_PAUSE_KEY", 0x71, 0x77, 0) == 0x71);
    _putenv_s("MM3_PAUSE_KEY", "");
    puts("PASS photo keys: F2 pauses, F3 pictures; F9-F11 and the free camera's key are never taken; a taken default moves on");
}

static void clock_contract(int network)
{
    uint64_t t = 5000ull * k_second, hold, a, b, c;
    memset(&memory[0x429DB0], 0, 8);
    MEM32(0x3C5CDC) = k_manager; MEM32(k_manager + 0x54) = k_camera;
    draw();
    assert(mm3_race_view_recent());
    assert(read_clock(t) == t && !mm3_pause_frozen());                      /* untouched while running */
    assert(read_clock(t + k_second) == t + k_second);

    /* F2 in a race: the clock holds where it was. */
    tap(VK_F2);
    t += 2 * k_second;
    draw();
    hold = read_clock(t);
    if (network) {                                                           /* System Link: no pause key at all */
        assert(!mm3_pause_frozen() && hold == t && read_clock(t + k_second) == t + k_second);
        puts("PASS System Link: F2 does nothing (the other players' clocks would run on)");
        return;
    }
    assert(mm3_pause_frozen() && hold == t);
    a = read_clock(t + k_second);
    assert(a - hold == k_second / RATE);                                     /* a hundred-thousandth */
    draw();
    assert(InterlockedCompareExchange(&s_active, 0, 0) && blocked);         /* the held race: the camera flies */
    assert(read_clock(t + 11 * k_second) - hold == 11 * k_second / RATE);
    assert(a > hold && read_clock(t + 11 * k_second) > a);                  /* never backwards, never zero steps of time */

    /* F2 again: continues from where the game's clock is, then runs 1:1. */
    tap(VK_F2);
    draw();
    b = read_clock(t + 21 * k_second);
    assert(!mm3_pause_frozen());
    assert(b - hold == 21 * k_second / RATE);                                /* the held time is not in the game's clock */
    c = read_clock(t + 22 * k_second);
    assert(c - b == k_second);
    assert(read_clock(t + 22 * k_second + 1000) - c == 1000);
    draw();
    assert(!InterlockedCompareExchange(&s_active, 0, 0) && !blocked);       /* and lands with the race */
    printf("PASS pause clock: F2 holds at %g of the speed, F2 again resumes with no jump and the held 21 s are not in the game's time\n",
           1.0 / (double)RATE);

    /* A press away from a race belongs to no race. */
    s_drawn = (LONG)(GetTickCount() - 5000u);
    assert(!mm3_race_view_recent());
    tap(VK_F2);
    t += 40 * k_second;
    read_clock(t);                                                           /* takes the press and drops it */
    draw();
    a = read_clock(t + k_second);
    assert(!mm3_pause_frozen() && read_clock(t + 2 * k_second) - a == k_second);
    puts("PASS pause clock: a press made while no race is drawn is dropped");

    /* Held, then the race stops drawing: the clock lets go. */
    tap(VK_F2);
    draw();
    a = read_clock(t + 50 * k_second);
    assert(mm3_pause_frozen());
    s_drawn = (LONG)(GetTickCount() - 5000u);
    b = read_clock(t + 51 * k_second);
    assert(!mm3_pause_frozen() && b - a == k_second / RATE);
    assert(read_clock(t + 52 * k_second) - b == k_second);
    puts("PASS pause clock: held while the race stops drawing (menus, loading) lets go");

    /* The game's own pause (Start, with its menu) needs a running clock. */
    draw();
    tap(VK_F2);
    draw();
    a = read_clock(t + 60 * k_second);
    assert(mm3_pause_frozen());
    MEM8(0x429DB2) = 1;
    b = read_clock(t + 61 * k_second);
    assert(!mm3_pause_frozen() && b - a == k_second / RATE);
    assert(read_clock(t + 62 * k_second) - b == k_second);
    tap(VK_F2);                                                              /* F2 does not start a hold over the open menu */
    draw();
    read_clock(t + 63 * k_second);
    assert(!mm3_pause_frozen());
    MEM8(0x429DB2) = 0;
    puts("PASS pause clock: the game's own pause menu takes over; F2 does not hold over it");
}

static int wait_for_file(const wchar_t *dir, wchar_t *found, size_t count)
{
    wchar_t pattern[MAX_PATH * 2];
    WIN32_FIND_DATAW data;
    int tries;
    swprintf_s(pattern, MAX_PATH * 2, L"%ls\\MM3-*.png", dir);
    for (tries = 0; tries < 200; ++tries) {
        HANDLE h = FindFirstFileW(pattern, &data);
        if (h != INVALID_HANDLE_VALUE) {
            FindClose(h);
            if (data.nFileSizeLow > 100) {
                swprintf_s(found, count, L"%ls\\%ls", dir, data.cFileName);
                return 1;
            }
        }
        Sleep(50);
    }
    return 0;
}

static int count_files(const wchar_t *dir)
{
    wchar_t pattern[MAX_PATH * 2];
    WIN32_FIND_DATAW data;
    HANDLE h;
    int n = 0;
    swprintf_s(pattern, MAX_PATH * 2, L"%ls\\MM3-*.png", dir);
    h = FindFirstFileW(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do ++n; while (FindNextFileW(h, &data));
    FindClose(h);
    return n;
}

/* The picture the driver checks pixel by pixel: B from x, G from y, R from both. */
static void frame(uint32_t w, uint32_t h, uint32_t pitch, uint8_t *out)
{
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t *p = out + (size_t)y * pitch + x * 4;
            p[0] = (uint8_t)(x * 4); p[1] = (uint8_t)(y * 8); p[2] = (uint8_t)((x + y) * 2); p[3] = 0;   /* alpha ignored */
        }
}

static void photo_contract(void)
{
    static uint8_t pixels[(64 * 4 + 16) * 32];
    wchar_t dir[MAX_PATH * 2], found[MAX_PATH * 2];
    const wchar_t *env = _wgetenv(L"MM3_PHOTO_DIR");
    FILE *file;
    unsigned char head[32];
    assert(env && *env);
    wcscpy_s(dir, MAX_PATH * 2, env);
    assert(count_files(dir) == 0);
    frame(64, 32, 64 * 4 + 16, pixels);

    /* F3 asks the renderer; the frame it hands over is saved off-thread. */
    requests = 0; requested = NULL;
    tap(VK_F3);
    assert(requests == 1 && requested);
    requested(pixels, 64, 32, 64 * 4 + 16);
    assert(wait_for_file(dir, found, MAX_PATH * 2));
    file = _wfopen(found, L"rb");
    assert(file && fread(head, 1, 24, file) == 24);
    fclose(file);
    assert(!memcmp(head, "\x89PNG\r\n\x1a\n", 8) && !memcmp(head + 12, "IHDR", 4));
    assert(head[16] == 0 && head[19] == 64 && head[20] == 0 && head[23] == 32);   /* 64 x 32 */
    puts("PASS photo: F3 asks the renderer, the frame is saved as a PNG of its size under MM3_PHOTO_DIR");

    /* A double press is one picture; a later one is another. */
    requests = 0;
    Sleep(600);
    held[VK_F3] = 1; Sleep(40); held[VK_F3] = 0; Sleep(40);
    held[VK_F3] = 1; Sleep(40); held[VK_F3] = 0; Sleep(40);
    assert(requests == 1);
    requested(pixels, 64, 32, 64 * 4 + 16);
    Sleep(1500);
    assert(count_files(dir) == 2);
    puts("PASS photo: presses within half a second are one picture, a later one is a second file");

    /* The renderer cannot give an image (no D3D11 present): nothing is written and the slot frees. */
    Sleep(600);
    requests = 0;
    tap(VK_F3);
    assert(requests == 1);
    requested(NULL, 0, 0, 0);
    Sleep(300);
    assert(count_files(dir) == 2);
    Sleep(300);
    requests = 0;
    tap(VK_F3);
    assert(requests == 1);
    requested(pixels, 64, 32, 64 * 4 + 16);
    Sleep(1500);
    assert(count_files(dir) == 3);
    puts("PASS photo: a frame the renderer cannot give writes nothing and the next picture still works");

    /* Free camera on the controller: Y holds the race, A takes the picture. */
    pad_connected = 1;
    memset(&pad_state, 0, sizeof pad_state);
    draw();                                                                  /* a race view first: presses away from one are dropped */
    InterlockedIncrement(&s_camera_presses);
    draw();
    assert(InterlockedCompareExchange(&s_active, 0, 0) && blocked);
    mm3_cheat_pause_presses();
    Sleep(600);
    requests = 0;
    pad_state.wButtons = XINPUT_GAMEPAD_Y;
    draw();
    assert(mm3_cheat_pause_presses() == 1 && requests == 0);
    draw();                                                                  /* still held: not a second press */
    assert(mm3_cheat_pause_presses() == 0);
    pad_state.wButtons = XINPUT_GAMEPAD_A;
    draw();
    assert(requests == 1 && mm3_cheat_pause_presses() == 0);
    pad_state.wButtons = 0;
    draw();
    InterlockedIncrement(&s_camera_presses);
    draw();
    assert(!InterlockedCompareExchange(&s_active, 0, 0));
    pad_connected = 0;
    puts("PASS free camera controller: Y holds the race, A takes the picture, once per press");
}

static int renderer_hides(void) { return InterlockedCompareExchange(&renderer_hud_hidden, 0, 0) != 0; }

static void hud_contract(void)
{
    MEM32(0x3C5CDC) = k_manager; MEM32(k_manager + 0x54) = k_camera;
    draw();
    assert(!mm3_hud_hidden() && !renderer_hides());
    tap(VK_F4);
    assert(mm3_hud_hidden() && renderer_hides());
    tap(VK_F4);
    assert(!mm3_hud_hidden() && !renderer_hides());
    puts("PASS hide HUD: F4 hides the race HUD in the renderer and F4 again shows it");

    /* The race ends with the HUD hidden: menus and the next race get it back. */
    tap(VK_F4);
    assert(mm3_hud_hidden());
    s_drawn = (LONG)(GetTickCount() - 5000u);
    Sleep(60);
    assert(!mm3_hud_hidden() && !renderer_hides());
    tap(VK_F4);                                                              /* away from a race: nothing */
    assert(!mm3_hud_hidden() && !renderer_hides());
    puts("PASS hide HUD: it comes back by itself when races stop drawing, and F4 away from a race does nothing");

    /* The free camera's X on the controller, once per press. */
    pad_connected = 1;
    memset(&pad_state, 0, sizeof pad_state);
    draw();
    InterlockedIncrement(&s_camera_presses);
    draw();
    assert(InterlockedCompareExchange(&s_active, 0, 0));
    pad_state.wButtons = XINPUT_GAMEPAD_X;
    draw();
    draw();                                                                  /* still held: not a second press */
    Sleep(60);
    assert(mm3_hud_hidden() && renderer_hides());
    pad_state.wButtons = 0;
    draw();
    pad_state.wButtons = XINPUT_GAMEPAD_X;
    draw();
    Sleep(60);
    assert(!mm3_hud_hidden() && !renderer_hides());
    pad_state.wButtons = 0;
    draw();
    InterlockedIncrement(&s_camera_presses);
    draw();
    pad_connected = 0;
    puts("PASS hide HUD: the free camera's X hides and shows it, once per press");
}

static void off_contract(void)
{
    wchar_t dir[MAX_PATH * 2];
    wcscpy_s(dir, MAX_PATH * 2, _wgetenv(L"MM3_PHOTO_DIR"));
    requests = 0;
    MEM32(0x3C5CDC) = k_manager; MEM32(k_manager + 0x54) = k_camera;
    draw();
    tap(VK_F3);
    tap(VK_F2);
    draw();
    assert(requests == 0 && mm3_cheat_pause_presses() == 0 && count_files(dir) == 0);
    pad_connected = 1;
    InterlockedIncrement(&s_camera_presses);
    draw();
    assert(InterlockedCompareExchange(&s_active, 0, 0));                     /* the camera flies: the buttons are heard */
    pad_state.wButtons = XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X;
    draw();
    tap(VK_F4);
    Sleep(60);
    assert(requests == 0 && mm3_cheat_pause_presses() == 0 && !mm3_hud_hidden() && !renderer_hides());
    puts("PASS MM3_PHOTO_MODE=0: F2, F3, F4 and the controller buttons do nothing");
}

int main(int argc, char **argv)
{
    g_xbox_mem_offset = (ptrdiff_t)memory;
    mm3_cheat_keys_start();
    Sleep(100);
    if (argc > 1 && !strcmp(argv[1], "off")) { off_contract(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "net")) { clock_contract(1); return 0; }
    keys_contract();
    clock_contract(0);
    photo_contract();
    hud_contract();
    return 0;
}
