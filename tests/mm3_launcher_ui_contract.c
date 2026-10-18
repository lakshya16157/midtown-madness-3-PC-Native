/* Real launcher, isolated preferences, controlled messages instead of global
 * keyboard automation. No renderer, game process or physical pad required. */
#include "../src/mm3_launcher.c"

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL launcher UI line %d: %s\n", __LINE__, #test); ExitProcess(1); } } while (0)
static int s_play;

static void controllers_layout(HWND window)
{
    RECT client, count, status, bindings;
    GetClientRect(window, &client);
    GetWindowRect(GetDlgItem(window, ID_PAD_COUNT), &count);
    GetWindowRect(GetDlgItem(window, ID_PAD_STATUS), &status);
    GetWindowRect(GetDlgItem(window, ID_GROUP), &bindings);
    MapWindowPoints(NULL, window, (POINT *)&count, 2);
    MapWindowPoints(NULL, window, (POINT *)&status, 2);
    MapWindowPoints(NULL, window, (POINT *)&bindings, 2);
    CHECK(IsWindowVisible(GetDlgItem(window, ID_PAD_COUNT)));
    CHECK(count.left >= client.left && count.right <= status.left && count.top >= client.top);
    CHECK(status.right <= client.right && status.bottom <= bindings.top);
    CHECK(count.bottom <= bindings.top);
}

static HWND wait_launcher(void)
{
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 10000) {
        HWND window = FindWindowA("MM3NativeLauncher", NULL);
        DWORD pid = 0;
        if (window) GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(window) && GetDlgItem(window, IDOK)) return window;
        Sleep(10);
    }
    CHECK(0); return NULL;
}
static void select_page(HWND window, int page)
{
    HWND tabs = GetDlgItem(window, ID_TABS);
    NMHDR notification = {tabs, ID_TABS, TCN_SELCHANGE};
    SendMessageA(tabs, TCM_SETCURSEL, page, 0);
    SendMessageA(window, WM_NOTIFY, ID_TABS, (LPARAM)&notification);
}
static void capture_page(HWND window, const char *name)
{
    const char *directory = getenv("MM3_LAUNCHER_CAPTURE_DIR");
    BITMAPINFO info = {0}; BITMAPFILEHEADER header = {0};
    RECT bounds;
    HDC screen, memory;
    HBITMAP bitmap, previous;
    void *pixels;
    FILE *file;
    char path[32768];
    if (!directory || !*directory) return;
    UpdateWindow(window);
    GetWindowRect(window, &bounds);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = bounds.right - bounds.left;
    info.bmiHeader.biHeight = -(bounds.bottom - bounds.top);
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    screen = GetDC(NULL); memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    CHECK(bitmap); previous = SelectObject(memory, bitmap);
    CHECK(PrintWindow(window, memory, 0));
    header.bfType = 0x4D42; header.bfOffBits = sizeof header + sizeof info.bmiHeader;
    header.bfSize = header.bfOffBits + (DWORD)(info.bmiHeader.biWidth * -info.bmiHeader.biHeight * 4);
    snprintf(path, sizeof path, "%s/%s.bmp", directory, name);
    file = fopen(path, "wb"); CHECK(file);
    CHECK(fwrite(&header, 1, sizeof header, file) == sizeof header);
    CHECK(fwrite(&info.bmiHeader, 1, sizeof info.bmiHeader, file) == sizeof info.bmiHeader);
    CHECK(fwrite(pixels, 1, header.bfSize - header.bfOffBits, file) == header.bfSize - header.bfOffBits);
    CHECK(!fclose(file));
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(NULL, screen);
}
static DWORD WINAPI exercise(void *unused)
{
    (void)unused;
    HWND window = wait_launcher();
    RECT bounds, work;
    GetWindowRect(window, &bounds); SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    CHECK(bounds.left >= work.left && bounds.top >= work.top && bounds.right <= work.right && bounds.bottom <= work.bottom);
    HWND play = GetDlgItem(window, IDOK); RECT footer;
    GetWindowRect(play, &footer);
    CHECK(footer.bottom <= bounds.bottom && footer.top >= bounds.top && IsWindowVisible(play));
    select_page(window, 1);
    controllers_layout(window);
    HWND count = GetDlgItem(window, ID_PAD_COUNT);
    CHECK(SendMessageA(count, CB_GETCOUNT, 0, 0) == 4 && combo_value(count) == 1);
    combo_select(count, 4);
    SendMessageA(window, WM_COMMAND, ID_DEFAULTS, 0);
    CHECK(combo_value(count) == 1);
    combo_select(count, 4);
    HWND group = GetDlgItem(window, ID_GROUP), list = GetDlgItem(window, ID_BINDINGS);
    CHECK(IsWindowVisible(list));
    SendMessageA(group, CB_SETCURSEL, 1, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_GROUP, CBN_SELCHANGE), (LPARAM)group);
    CHECK(SendMessageA(list, LB_GETCOUNT, 0, 0) == MM3_LAUNCHER_KEYS);
    SendMessageA(group, CB_SETCURSEL, 2, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_GROUP, CBN_SELCHANGE), (LPARAM)group);
    CHECK(SendMessageA(list, LB_GETCOUNT, 0, 0) == MM3_LAUNCHER_DRIVE_KEYS);
    SendMessageA(list, LB_SETCURSEL, 0, 0);
    SendMessageA(window, WM_COMMAND, ID_CAPTURE, 0);
    PostMessageA(window, WM_KEYDOWN, VK_F10, 0);
    Sleep(80);
    char status[512]; GetDlgItemTextA(window, ID_STATUS, status, sizeof status);
    CHECK(strstr(status, "reserved"));
    PostMessageA(window, WM_KEYDOWN, 'W', 0);
    Sleep(80);
    GetDlgItemTextA(window, ID_BIND_LABEL, status, sizeof status); CHECK(strstr(status, "W"));
    capture_page(window, "controls");
    SendMessageA(group, CB_SETCURSEL, 0, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_GROUP, CBN_SELCHANGE), (LPARAM)group);
    SendMessageA(list, LB_SETCURSEL, 0, 0);
    HWND source = GetDlgItem(window, ID_SOURCE);
    SendMessageA(source, CB_SETCURSEL, 15, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_SOURCE, CBN_SELCHANGE), (LPARAM)source);
    GetDlgItemTextA(window, ID_BIND_LABEL, status, sizeof status); CHECK(strstr(status, "RT"));
    select_page(window, 2);
    CHECK(IsWindowVisible(GetDlgItem(window, ID_FOV)));
    SetDlgItemTextA(window, ID_FOV, "95");
    SendMessageA(GetDlgItem(window, ID_OPTION + MM3_LAUNCH_WIDESCREEN), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(window, ID_OPTION + MM3_LAUNCH_DISTANCE), BM_SETCHECK, BST_UNCHECKED, 0);
    capture_page(window, "mods");
    select_page(window, 0);
    SetDlgItemTextA(window, ID_FPS, "30");
    SendMessageA(GetDlgItem(window, ID_VSYNC), BM_SETCHECK, BST_CHECKED, 0);
    capture_page(window, "settings");
    SendMessageA(window, WM_COMMAND, s_play ? IDOK : IDCANCEL, 0);
    return 0;
}
static void smaller_layout(void)
{
    Launcher ui;
    RECT work = {0, 0, 1280, 680}, bounds, footer;
    memset(&ui, 0, sizeof ui);
    mm3_launcher_config_defaults(&ui.config);
    mm3_display_settings_defaults(&ui.display, NULL);
    ui.scale = layout_scale(&work, 1.5f); /* High DPI, 720p with a taskbar. */
    bounds.left = bounds.top = 0;
    bounds.right = (LONG)lroundf(865.f * ui.scale); bounds.bottom = (LONG)lroundf(754.f * ui.scale);
    AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_CONTROLPARENT);
    HWND window = CreateWindowExA(WS_EX_CONTROLPARENT, "MM3NativeLauncher", "Midtown Madness 3 - 720p layout check",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0,
        bounds.right - bounds.left, bounds.bottom - bounds.top, NULL, NULL, GetModuleHandleA(NULL), &ui);
    CHECK(window); ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window);
    GetWindowRect(window, &bounds); GetWindowRect(GetDlgItem(window, IDOK), &footer);
    CHECK(bounds.bottom <= work.bottom && bounds.right <= work.right);
    CHECK(footer.bottom < bounds.bottom && IsWindowVisible(GetDlgItem(window, IDOK)));
    capture_page(window, "settings-720p");
    page_show(&ui, 1); controllers_layout(window);
    CHECK(combo_value(GetDlgItem(window, ID_PAD_COUNT)) == 1);
    capture_page(window, "controls-720p");
    page_show(&ui, 2); capture_page(window, "mods-720p");
    DestroyWindow(window); DeleteObject(ui.font); DeleteObject(ui.heading);
}
int main(int argc, char **argv)
{
    char display_path[32768], launcher_path[32768], error[512];
    HANDLE thread;
    MM3LauncherConfig config;
    MM3DisplaySettings display;
    CHECK(argc == 2);
    snprintf(display_path, sizeof display_path, "%s/display.ini", argv[1]);
    snprintf(launcher_path, sizeof launcher_path, "%s/launcher.ini", argv[1]);
    CHECK(!_putenv_s("MM3_DISPLAY_CONFIG", display_path));
    CHECK(!_putenv_s("MM3_LAUNCHER_CONFIG", launcher_path));
    CHECK(!_putenv_s("MM3_LAUNCHER", "1"));
    CHECK(!_putenv_s("MM3_LAUNCHER_SMOKE", ""));
    thread = CreateThread(NULL, 0, exercise, NULL, 0, NULL); CHECK(thread);
    CHECK(!mm3_launcher_run());
    CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0); CloseHandle(thread);
    CHECK(GetFileAttributesA(display_path) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesA(launcher_path) == INVALID_FILE_ATTRIBUTES);
    s_play = 1;
    thread = CreateThread(NULL, 0, exercise, NULL, 0, NULL); CHECK(thread);
    CHECK(mm3_launcher_run());
    CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0); CloseHandle(thread);
    CHECK(!strcmp(getenv("RECOMP_FPS_LIMIT"), "30"));
    CHECK(!strcmp(getenv("RECOMP_ASPECT"), "4:3"));
    CHECK(!strcmp(getenv("RECOMP_VIEW_DISTANCE"), "1"));
    CHECK(!strcmp(getenv("RECOMP_PB_EXEC"), "1"));
    CHECK(!strcmp(getenv("RECOMP_FB_WINDOW"), "1"));
    CHECK(!strcmp(getenv("RECOMP_USB"), "1"));
    CHECK(!strcmp(getenv("RECOMP_USB_PADS"), "4"));
    CHECK(!strcmp(getenv("RECOMP_APU_DSP_ACK"), "0x80458810"));
    CHECK(!strcmp(getenv("RECOMP_KEYBOARD_LAYOUT"), "mm3"));
    CHECK(strstr(getenv("MM3_DRIVE_KEYMAP"), "THROTTLE:57"));
    CHECK(strstr(getenv("RECOMP_PADMAP"), "A:RT"));
    mm3_launcher_config_defaults(&config);
    CHECK(mm3_launcher_config_load(&config, error, sizeof error));
    CHECK(config.drive_keys[0] == 'W' && config.pad[0] == 15 && config.fov_degrees == 95);
    CHECK(config.controller_count == 4);
    mm3_display_settings_defaults(&display, NULL);
    CHECK(mm3_display_settings_load(&display, error, sizeof error));
    CHECK(display.fps_limit == 30 && display.vsync);
    CHECK(!_putenv_s("MM3_LAUNCHER", "0"));
    CHECK(!_putenv_s("MM3_FOV_DEGREES", "100"));
    CHECK(!_putenv_s("RECOMP_USB_PADS", "2"));
    CHECK(!_putenv_s("RECOMP_USB_NDP", "1"));
    CHECK(mm3_launcher_run()); CHECK(!strcmp(getenv("MM3_FOV_DEGREES"), "100"));
    CHECK(!strcmp(getenv("RECOMP_USB_PADS"), "2") && !strcmp(getenv("RECOMP_USB_NDP"), "1"));
    CHECK(!_putenv_s("MM3_LAUNCHER_SMOKE", "1"));
    CHECK(!mm3_launcher_run());
    smaller_layout();
    DeleteFileA(display_path); DeleteFileA(launcher_path);
    puts("PASS launcher UI: scaled controller-count choice, keyboard capture/reserved keys, pad mapping, Cancel isolation, Play persistence, defaults, explicit USB overrides and bypass bootstrap");
    return 0;
}
