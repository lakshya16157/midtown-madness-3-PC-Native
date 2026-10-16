/* Drive the actual dialog resource and handlers without a second game or GPU.
 * The runtime publication APIs are spies; persistence uses the real model. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mm3_display_settings.c"

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); ExitProcess(1); } } while (0)
static void (*settings_handler)(HWND);
static int blocked, publishes, published_fps, published_vsync, published_width;
static float published_scale;
static float published_aspect;

void xbox_FramebufferSetSettingsHandler(void (*handler)(HWND)) { settings_handler = handler; }
HWND xbox_FramebufferWindowHandle(void) { return NULL; }
int xbox_FramebufferSetDisplayMode(int width, int height, int borderless)
{ (void)height; (void)borderless; published_width = width; return 1; }
void xbox_FramebufferSetShowFPS(int show) { (void)show; }
int nv2a_d3d_set_present_options(int vsync, int fps)
{ ++publishes; published_fps = fps; published_vsync = vsync; return 1; }
void nv2a_d3d_set_render_scale(float scale) { published_scale = scale; }
void nv2a_d3d_set_display_aspect(float aspect) { published_aspect = aspect; }
void xbox_InputSetBlocked(int value) { InterlockedExchange((LONG *)&blocked, value); }

static HWND wait_dialog(void)
{
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 10000) {
        HWND window = NULL;
        while ((window = FindWindowExA(NULL, window, "#32770", "Display Settings")) != NULL) {
            DWORD pid;
            GetWindowThreadProcessId(window, &pid);
            if (pid == GetCurrentProcessId() && IsWindowVisible(window)) {
                char status[384];
                /* Windows may expose the HWND during WM_INITDIALOG. Wait for
                 * population to finish before sending cross-thread commands. */
                if (GetDlgItemTextA(window, IDC_DISPLAY_STATUS, status, sizeof status))
                    return window;
            }
        }
        Sleep(10);
    }
    fprintf(stderr, "FAIL: dialog timeout\n");
    ExitProcess(1);
}

static void capture(HWND dialog)
{
    const char *path = getenv("MM3_UI_CAPTURE");
    RECT bounds;
    HDC screen, memory;
    HBITMAP bitmap, old;
    BITMAPINFO info = {0};
    BITMAPFILEHEADER header = {0};
    void *pixels;
    FILE *file;
    if (!path || !*path) return;
    GetWindowRect(dialog, &bounds);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = bounds.right - bounds.left;
    info.bmiHeader.biHeight = -(bounds.bottom - bounds.top);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    screen = GetDC(NULL);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    old = SelectObject(memory, bitmap);
    CHECK(PrintWindow(dialog, memory, 0));
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof header + sizeof info.bmiHeader;
    header.bfSize = header.bfOffBits + info.bmiHeader.biWidth * (-info.bmiHeader.biHeight) * 4;
    file = fopen(path, "wb");
    CHECK(file);
    CHECK(fwrite(&header, sizeof header, 1, file) == 1);
    CHECK(fwrite(&info.bmiHeader, sizeof info.bmiHeader, 1, file) == 1);
    CHECK(fwrite(pixels, header.bfSize - header.bfOffBits, 1, file) == 1);
    fclose(file);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
}

static void notify(HWND dialog, int id, int notification)
{
    SendMessageA(dialog, WM_COMMAND, MAKEWPARAM(id, notification), (LPARAM)GetDlgItem(dialog, id));
}

static DWORD WINAPI drive_dialog(LPVOID argument)
{
    int stage = (int)(INT_PTR)argument;
    HWND dialog = wait_dialog();
    char text[512];
    CHECK(blocked);
    if (stage == 0) {
        capture(dialog);
        CHECK(combo_value(dialog, IDC_DISPLAY_MINIMAP) == 1);
        CHECK(mm3_minimap_improved() == 1);
        CHECK(combo_value(dialog, IDC_DISPLAY_ANISO) == 3);
        CHECK(combo_value(dialog, IDC_DISPLAY_ASPECT) == 5);
        CHECK(combo_float(dialog, IDC_DISPLAY_SCALE) == 5.f);
        CHECK(combo_float(dialog, IDC_DISPLAY_DISTANCE) == 1.25f);
        CHECK(combo_float(dialog, IDC_DISPLAY_VIEW_DISTANCE) == 3.25f);
        SetDlgItemTextA(dialog, IDC_DISPLAY_FPS, "45");
        combo_select(dialog, IDC_DISPLAY_ASPECT, 1); /* Live 4:3 selection. */
        combo_select(dialog, IDC_DISPLAY_MSAA, 2);
        combo_select(dialog, IDC_DISPLAY_SCALE, float_value(2.5f));
        combo_select(dialog, IDC_DISPLAY_MINIMAP, 0);
        combo_select(dialog, IDC_DISPLAY_DISTANCE, float_value(1.5f));
        combo_select(dialog, IDC_DISPLAY_VIEW_DISTANCE, float_value(4.f));
        CheckDlgButton(dialog, IDC_DISPLAY_VSYNC, BST_CHECKED);
        combo_select(dialog, IDC_DISPLAY_MODE, 1);
        notify(dialog, IDC_DISPLAY_MODE, CBN_SELCHANGE);
        CHECK(!IsWindowEnabled(GetDlgItem(dialog, IDC_DISPLAY_RESOLUTION)));
        combo_select(dialog, IDC_DISPLAY_MODE, 0);
        notify(dialog, IDC_DISPLAY_MODE, CBN_SELCHANGE);
        CHECK(IsWindowEnabled(GetDlgItem(dialog, IDC_DISPLAY_RESOLUTION)));
        notify(dialog, IDC_DISPLAY_APPLY, BN_CLICKED);
        CHECK(published_fps == 45 && published_vsync == 1 && published_scale == 2.5f);
        CHECK(fabsf(published_aspect - 4.f / 3.f) < 0.0001f);
        CHECK(published_width == 1280);
        CHECK(mm3_minimap_improved() == 0);
        GetDlgItemTextA(dialog, IDC_DISPLAY_STATUS, text, sizeof text);
        CHECK(strstr(text, "Saved.") && strstr(text, "Restart") && strstr(text, "anti-aliasing"));
        CHECK(strstr(text, "object detail distance") && strstr(text, "building view distance"));
        CHECK(!strstr(text, "aspect ratio"));
    } else if (stage == 1) {
        GetDlgItemTextA(dialog, IDC_DISPLAY_FPS, text, sizeof text);
        CHECK(strcmp(text, "45") == 0);
        CHECK(combo_value(dialog, IDC_DISPLAY_MSAA) == 2);
        CHECK(combo_value(dialog, IDC_DISPLAY_ASPECT) == 1);
        CHECK(combo_value(dialog, IDC_DISPLAY_MINIMAP) == 0);
        CHECK(combo_float(dialog, IDC_DISPLAY_DISTANCE) == 1.5f);
        CHECK(combo_float(dialog, IDC_DISPLAY_VIEW_DISTANCE) == 4.f);
        SetDlgItemTextA(dialog, IDC_DISPLAY_FPS, "999");
        notify(dialog, IDC_DISPLAY_FPS, CBN_EDITCHANGE);
        GetDlgItemTextA(dialog, IDC_DISPLAY_STATUS, text, sizeof text);
        CHECK(strstr(text, "1 to 60"));
        SetDlgItemTextA(dialog, IDC_DISPLAY_FPS, "60");
    } else {
        notify(dialog, IDC_DISPLAY_DEFAULTS, BN_CLICKED);
        CHECK(combo_value(dialog, IDC_DISPLAY_MSAA) == 4);
        CHECK(combo_float(dialog, IDC_DISPLAY_SCALE) == 2.f);
        CHECK(combo_value(dialog, IDC_DISPLAY_MINIMAP) == 1);
        CHECK(combo_float(dialog, IDC_DISPLAY_DISTANCE) == 2.f);
        CHECK(combo_float(dialog, IDC_DISPLAY_VIEW_DISTANCE) == 2.f);
        CHECK(mm3_minimap_improved() == 0); /* Restore stays pending until Apply. */
    }
    PostMessageA(dialog, WM_COMMAND, IDCANCEL, 0);
    return 0;
}

int main(int argc, char **argv)
{
    HWND owner;
    MM3DisplaySettings loaded;
    char error[512];
    int stage;
    CHECK(argc == 2);
    CHECK(_putenv_s("MM3_DISPLAY_CONFIG", argv[1]) == 0);
    CHECK(_putenv_s("RECOMP_ASPECT", "1.77778") == 0);
    CHECK(_putenv_s("RECOMP_ANISO", "3") == 0);
    CHECK(_putenv_s("RECOMP_RENDER_SCALE", "5") == 0);
    CHECK(_putenv_s("RECOMP_DRAW_DISTANCE", "1.25") == 0);
    CHECK(_putenv_s("RECOMP_VIEW_DISTANCE", "3.25") == 0);
    mm3_display_settings_init();
    CHECK(settings_handler);
    CHECK(fabsf(published_aspect - 1.77778f) < 0.0001f);
    owner = CreateWindowExA(0, "STATIC", "Display contract owner", WS_OVERLAPPEDWINDOW,
                            100, 100, 1296, 839, NULL, NULL, GetModuleHandleA(NULL), NULL);
    CHECK(owner);
    for (stage = 0; stage < 3; ++stage) {
        HANDLE thread = CreateThread(NULL, 0, drive_dialog, (LPVOID)(INT_PTR)stage, 0, NULL);
        CHECK(thread);
        settings_handler(owner);
        CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
        CloseHandle(thread);
        CHECK(!blocked && !s_dialog);
    }
    CHECK(publishes == 2); /* Startup + Apply; Cancel and Restore don't publish. */
    mm3_display_settings_defaults(&loaded, NULL);
    CHECK(mm3_display_settings_load(&loaded, error, sizeof error));
    CHECK(loaded.fps_limit == 45 && loaded.msaa == 2 && loaded.vsync == 1);
    CHECK(loaded.render_scale == 2.5f && loaded.aniso == 3);
    CHECK(loaded.draw_distance == 1.5f && loaded.view_distance == 4.f);
    CHECK(loaded.minimap_improved == 0);
    CHECK(strcmp(loaded.aspect, "4:3") == 0);
    strcpy(loaded.aspect, "auto");
    CHECK(mm3_display_settings_commit(&loaded, error, sizeof error));
    CHECK(published_aspect == 0.f && !strstr(error, "aspect ratio"));
    DestroyWindow(owner);
    puts("PASS: real Display Settings resource, custom values, Apply, restart notice, fullscreen controls, invalid FPS, Cancel, defaults, persistence and input capture");
    return 0;
}
