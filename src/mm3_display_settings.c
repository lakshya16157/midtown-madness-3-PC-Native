#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <video/fb_present.h>
#include <video/nv2a_d3d11.h>
#include <xinput_xbox.h>
#include "mm3_display_config.h"
#include "mm3_display_settings.h"
#include "mm3_display_settings_ids.h"
#include "mm3_frame_limit.h"

/* Only the framebuffer message thread edits these after startup. The renderer
 * receives live changes through its own synchronized publication APIs. */
static MM3DisplaySettings s_current, s_boot;
static HWND s_dialog;

static void combo_add(HWND dialog, int id, const char *label, LPARAM value)
{
    HWND control = GetDlgItem(dialog, id);
    LRESULT index = SendMessageA(control, CB_ADDSTRING, 0, (LPARAM)label);
    if (index >= 0) SendMessageA(control, CB_SETITEMDATA, index, value);
}

static void combo_select(HWND dialog, int id, LPARAM value)
{
    HWND control = GetDlgItem(dialog, id);
    int count = (int)SendMessageA(control, CB_GETCOUNT, 0, 0), i;
    for (i = 0; i < count; ++i)
        if (SendMessageA(control, CB_GETITEMDATA, i, 0) == value) {
            SendMessageA(control, CB_SETCURSEL, i, 0);
            return;
        }
}

static LPARAM combo_value(HWND dialog, int id)
{
    HWND control = GetDlgItem(dialog, id);
    LRESULT index = SendMessageA(control, CB_GETCURSEL, 0, 0);
    return index == CB_ERR ? CB_ERR : SendMessageA(control, CB_GETITEMDATA, index, 0);
}

static LPARAM float_value(float value)
{
    unsigned long bits;
    memcpy(&bits, &value, sizeof value);
    return (LPARAM)bits;
}

static float combo_float(HWND dialog, int id)
{
    unsigned long bits = (unsigned long)combo_value(dialog, id);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static void float_choice(HWND dialog, int id, float value, const char *label)
{
    combo_add(dialog, id, label, float_value(value));
}

static void select_float(HWND dialog, int id, float value, const char *suffix)
{
    char text[80];
    combo_select(dialog, id, float_value(value));
    if (combo_value(dialog, id) != float_value(value)) {
        snprintf(text, sizeof text, "%.3gx%s", value, suffix);
        float_choice(dialog, id, value, text);
        combo_select(dialog, id, float_value(value));
    }
}

static void resolution_add(HWND dialog, int width, int height)
{
    char label[64];
    LPARAM packed = (LPARAM)MAKELONG(width, height);
    HWND control = GetDlgItem(dialog, IDC_DISPLAY_RESOLUTION);
    int count = (int)SendMessageA(control, CB_GETCOUNT, 0, 0), i;
    for (i = 0; i < count; ++i)
        if (SendMessageA(control, CB_GETITEMDATA, i, 0) == packed) return;
    snprintf(label, sizeof label, "%d x %d", width, height);
    combo_add(dialog, IDC_DISPLAY_RESOLUTION, label, packed);
}

static void populate(HWND dialog, const MM3DisplaySettings *settings)
{
    static const int ids[] = { IDC_DISPLAY_RESOLUTION, IDC_DISPLAY_MODE,
        IDC_DISPLAY_FPS, IDC_DISPLAY_SCALE, IDC_DISPLAY_ASPECT,
        IDC_DISPLAY_MSAA, IDC_DISPLAY_ANISO, IDC_DISPLAY_DISTANCE };
    static const int resolutions[][2] = { {800,600}, {1024,768}, {1280,720},
        {1280,800}, {1366,768}, {1536,960}, {1600,900}, {1920,1080},
        {1920,1200}, {2560,1440}, {2560,1600}, {3840,2160} };
    static const int rates[] = { 15, 24, 30, 40, 45, 50, 60 };
    static const float scales[] = { .5f, .75f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 4.f };
    static const char *aspects[] = { "auto", "4:3", "16:10", "16:9", "21:9" };
    static const float distances[] = { 1.f, 1.5f, 2.f, 3.f, 4.f };
    unsigned i;
    char label[80];
    DEVMODEA mode;
    for (i = 0; i < sizeof ids / sizeof ids[0]; ++i)
        SendDlgItemMessageA(dialog, ids[i], CB_RESETCONTENT, 0, 0);
    for (i = 0; i < sizeof resolutions / sizeof resolutions[0]; ++i)
        resolution_add(dialog, resolutions[i][0], resolutions[i][1]);
    memset(&mode, 0, sizeof mode);
    mode.dmSize = sizeof mode;
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmPelsWidth >= 640 && mode.dmPelsWidth <= 7680 &&
        mode.dmPelsHeight >= 480 && mode.dmPelsHeight <= 4320)
        resolution_add(dialog, mode.dmPelsWidth, mode.dmPelsHeight);
    resolution_add(dialog, settings->window_width, settings->window_height);
    combo_select(dialog, IDC_DISPLAY_RESOLUTION,
                 MAKELONG(settings->window_width, settings->window_height));
    combo_add(dialog, IDC_DISPLAY_MODE, "Windowed", 0);
    combo_add(dialog, IDC_DISPLAY_MODE, "Borderless fullscreen", 1);
    combo_select(dialog, IDC_DISPLAY_MODE, settings->fullscreen);
    for (i = 0; i < sizeof rates / sizeof rates[0]; ++i) {
        snprintf(label, sizeof label, "%d", rates[i]);
        combo_add(dialog, IDC_DISPLAY_FPS, label, rates[i]);
    }
    snprintf(label, sizeof label, "%d", settings->fps_limit);
    SetDlgItemTextA(dialog, IDC_DISPLAY_FPS, label);
    SendDlgItemMessageA(dialog, IDC_DISPLAY_FPS, CB_LIMITTEXT, 3, 0);
    float_choice(dialog, IDC_DISPLAY_SCALE, 0.f, "Automatic (window size)");
    for (i = 0; i < sizeof scales / sizeof scales[0]; ++i) {
        snprintf(label, sizeof label, "%.3gx (%d vertical pixels)",
                 scales[i], (int)(480 * scales[i]));
        float_choice(dialog, IDC_DISPLAY_SCALE, scales[i], label);
    }
    select_float(dialog, IDC_DISPLAY_SCALE, settings->render_scale, " (custom)");
    for (i = 0; i < sizeof aspects / sizeof aspects[0]; ++i) {
        combo_add(dialog, IDC_DISPLAY_ASPECT, i ? aspects[i] : "Automatic", i);
        if (strcmp(settings->aspect, aspects[i]) == 0)
            combo_select(dialog, IDC_DISPLAY_ASPECT, i);
    }
    if (combo_value(dialog, IDC_DISPLAY_ASPECT) == CB_ERR) {
        combo_add(dialog, IDC_DISPLAY_ASPECT, settings->aspect, 5);
        combo_select(dialog, IDC_DISPLAY_ASPECT, 5);
    }
    for (i = 1; i <= 16; i *= 2) {
        snprintf(label, sizeof label, i == 1 ? "Off" : "%ux", i);
        if (i <= 8) combo_add(dialog, IDC_DISPLAY_MSAA, label, i);
        combo_add(dialog, IDC_DISPLAY_ANISO, label, i);
    }
    combo_select(dialog, IDC_DISPLAY_MSAA, settings->msaa);
    combo_select(dialog, IDC_DISPLAY_ANISO, settings->aniso);
    if (combo_value(dialog, IDC_DISPLAY_ANISO) == CB_ERR) {
        snprintf(label, sizeof label, "%dx", settings->aniso);
        combo_add(dialog, IDC_DISPLAY_ANISO, label, settings->aniso);
        combo_select(dialog, IDC_DISPLAY_ANISO, settings->aniso);
    }
    for (i = 0; i < sizeof distances / sizeof distances[0]; ++i) {
        snprintf(label, sizeof label, distances[i] == 1.f ? "1x (stock)" : "%.3gx", distances[i]);
        float_choice(dialog, IDC_DISPLAY_DISTANCE, distances[i], label);
    }
    select_float(dialog, IDC_DISPLAY_DISTANCE, settings->draw_distance, " (custom)");
    CheckDlgButton(dialog, IDC_DISPLAY_VSYNC, settings->vsync ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog, IDC_DISPLAY_SHOW_FPS, settings->show_fps ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(dialog, IDC_DISPLAY_RESOLUTION), !settings->fullscreen);
}

static int read_controls(HWND dialog, MM3DisplaySettings *settings)
{
    static const char *aspects[] = { "auto", "4:3", "16:10", "16:9", "21:9" };
    char fps[16], *end;
    long limit;
    LPARAM resolution = combo_value(dialog, IDC_DISPLAY_RESOLUTION);
    LPARAM aspect = combo_value(dialog, IDC_DISPLAY_ASPECT);
    *settings = s_current;
    GetDlgItemTextA(dialog, IDC_DISPLAY_FPS, fps, sizeof fps);
    limit = strtol(fps, &end, 10);
    while (*end == ' ') ++end;
    if (end == fps || *end || limit < 1 || limit > 60) return 0;
    if (resolution == CB_ERR || aspect < 0 || aspect > 5) return 0;
    settings->window_width = LOWORD(resolution);
    settings->window_height = HIWORD(resolution);
    settings->fullscreen = (int)combo_value(dialog, IDC_DISPLAY_MODE);
    settings->fps_limit = (int)limit;
    settings->vsync = IsDlgButtonChecked(dialog, IDC_DISPLAY_VSYNC) == BST_CHECKED;
    settings->show_fps = IsDlgButtonChecked(dialog, IDC_DISPLAY_SHOW_FPS) == BST_CHECKED;
    settings->render_scale = combo_float(dialog, IDC_DISPLAY_SCALE);
    if (aspect < 5) strcpy(settings->aspect, aspects[aspect]);
    settings->msaa = (int)combo_value(dialog, IDC_DISPLAY_MSAA);
    settings->aniso = (int)combo_value(dialog, IDC_DISPLAY_ANISO);
    settings->draw_distance = combo_float(dialog, IDC_DISPLAY_DISTANCE);
    return 1;
}

static void update_status(HWND dialog, int saved)
{
    MM3DisplaySettings pending;
    char text[384] = "", changes[256] = "";
    if (!read_controls(dialog, &pending)) {
        SetDlgItemTextA(dialog, IDC_DISPLAY_STATUS, "Enter a maximum FPS from 1 to 60.");
        return;
    }
#define CHANGED(test, label) if (test) { if (*changes) strcat(changes, ", "); strcat(changes, label); }
    CHANGED(strcmp(pending.aspect, s_boot.aspect), "aspect ratio");
    CHANGED(pending.msaa != s_boot.msaa, "anti-aliasing");
    CHANGED(pending.aniso != s_boot.aniso, "texture filtering");
    CHANGED(fabsf(pending.draw_distance - s_boot.draw_distance) > 0.0001f, "draw distance");
#undef CHANGED
    if (*changes)
        snprintf(text, sizeof text, "%sRestart the game to apply: %s.", saved ? "Saved. " : "", changes);
    else
        snprintf(text, sizeof text, "%sNo restart required.", saved ? "Saved. Display changes applied. " : "");
    SetDlgItemTextA(dialog, IDC_DISPLAY_STATUS, text);
}

static int apply(HWND dialog)
{
    MM3DisplaySettings settings;
    char error[512];
    if (!read_controls(dialog, &settings)) {
        MessageBoxA(dialog, "Enter a maximum FPS from 1 to 60.", "Display Settings", MB_OK | MB_ICONINFORMATION);
        SetFocus(GetDlgItem(dialog, IDC_DISPLAY_FPS));
        return 0;
    }
    /* Commit the file before publishing live changes. If saving fails the
     * panel stays open and the user's current display is left intact. */
    if (!mm3_display_settings_save(&settings, error, sizeof error)) {
        MessageBoxA(dialog, error, "Could not save Display Settings", MB_OK | MB_ICONERROR);
        return 0;
    }
    s_current = settings;
    nv2a_d3d_set_present_options(settings.vsync, settings.fps_limit);
    nv2a_d3d_set_render_scale(settings.render_scale);
    xbox_FramebufferSetShowFPS(settings.show_fps);
    if (!xbox_FramebufferSetDisplayMode(settings.window_width, settings.window_height, settings.fullscreen)) {
        SetDlgItemTextA(dialog, IDC_DISPLAY_STATUS,
            "Settings saved. Restart the game to apply the window resolution and mode.");
        return 0;
    }
    update_status(dialog, 1);
    fprintf(stderr, "[MM3_DISPLAY] saved window=%dx%d fullscreen=%d fps=%d vsync=%d scale=%.3g\n",
            settings.window_width, settings.window_height, settings.fullscreen,
            settings.fps_limit, settings.vsync, settings.render_scale);
    return 1;
}

static INT_PTR CALLBACK display_dialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    switch (message) {
    case WM_INITDIALOG: {
        RECT parent, bounds;
        MONITORINFO monitor = { sizeof monitor };
        s_dialog = dialog;
        populate(dialog, &s_current);
        update_status(dialog, 0);
        if (GetWindowRect(GetParent(dialog), &parent) && GetWindowRect(dialog, &bounds)) {
            int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
            int x = parent.left + ((parent.right - parent.left) - width) / 2;
            int y = parent.top + ((parent.bottom - parent.top) - height) / 2;
            if (GetMonitorInfoA(MonitorFromWindow(GetParent(dialog), MONITOR_DEFAULTTONEAREST), &monitor)) {
                if (x + width > monitor.rcWork.right) x = monitor.rcWork.right - width;
                if (y + height > monitor.rcWork.bottom) y = monitor.rcWork.bottom - height;
                if (x < monitor.rcWork.left) x = monitor.rcWork.left;
                if (y < monitor.rcWork.top) y = monitor.rcWork.top;
            }
            SetWindowPos(dialog, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDOK:
            if (apply(dialog)) EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        case IDC_DISPLAY_APPLY:
            apply(dialog);
            return TRUE;
        case IDC_DISPLAY_DEFAULTS: {
            MM3DisplaySettings defaults;
            mm3_display_settings_defaults(&defaults, getenv("MM3_PERFORMANCE_PROFILE"));
            populate(dialog, &defaults);
            update_status(dialog, 0);
            return TRUE;
        }
        case IDC_DISPLAY_MODE:
            EnableWindow(GetDlgItem(dialog, IDC_DISPLAY_RESOLUTION),
                         combo_value(dialog, IDC_DISPLAY_MODE) == 0);
            break;
        }
        if (HIWORD(wparam) == CBN_SELCHANGE || HIWORD(wparam) == CBN_EDITCHANGE ||
            HIWORD(wparam) == BN_CLICKED) update_status(dialog, 0);
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void open_settings(HWND window)
{
    RECT client;
    INT_PTR result;
    if (s_dialog) {
        SetForegroundWindow(s_dialog);
        return;
    }
    /* Reflect changes made with F11/Alt+Enter or dragging the game window. */
    s_current.fullscreen = !(GetWindowLongA(window, GWL_STYLE) & WS_OVERLAPPEDWINDOW);
    if (!s_current.fullscreen && GetClientRect(window, &client) &&
        client.right >= 640 && client.right <= 7680 && client.bottom >= 480 && client.bottom <= 4320) {
        s_current.window_width = client.right;
        s_current.window_height = client.bottom;
    }
    xbox_InputSetBlocked(1);
    result = DialogBoxParamA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDD_DISPLAY_SETTINGS),
                            window, display_dialog, 0);
    s_dialog = NULL;
    xbox_InputSetBlocked(0);
    if (result == -1)
        MessageBoxA(window, "The Display Settings panel could not be opened.",
                    "Display Settings", MB_OK | MB_ICONERROR);
}

void mm3_display_settings_init(void)
{
    char warning[512] = "";
    int loaded = mm3_display_settings_startup(&s_current, warning, sizeof warning);
    if (!loaded || *warning) fprintf(stderr, "[MM3_DISPLAY] %s\n", warning);
    s_boot = s_current;
    xbox_FramebufferSetDisplayMode(s_current.window_width, s_current.window_height, s_current.fullscreen);
    xbox_FramebufferSetShowFPS(s_current.show_fps);
    nv2a_d3d_set_present_options(s_current.vsync, s_current.fps_limit);
    nv2a_d3d_set_render_scale(s_current.render_scale);
    xbox_FramebufferSetSettingsHandler(open_settings);
    puts("Display Settings: press F10 (F11 toggles fullscreen).");
}
