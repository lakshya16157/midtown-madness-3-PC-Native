#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mm3_launcher.h"
#include "mm3_launcher_config.h"
#include "mm3_display_config.h"

#ifndef MM3_LAUNCHER_AUDIO
#define MM3_LAUNCHER_AUDIO 0
#endif
#ifndef MM3_LAUNCHER_NETWORK
#define MM3_LAUNCHER_NETWORK 0
#endif

enum {
    ID_TABS = 100, ID_DEFAULTS, ID_STATUS, ID_KEYBOARD, ID_GROUP, ID_BINDINGS,
    ID_CAPTURE, ID_UNBIND, ID_SOURCE, ID_BIND_LABEL, ID_PAD_STATUS, ID_PAD_PORT, ID_PAD_COUNT,
    ID_RESOLUTION, ID_MODE, ID_FPS, ID_VSYNC, ID_SCALE, ID_ASPECT, ID_MSAA,
    ID_ANISO, ID_DRAW, ID_VIEW, ID_SHOW_FPS, ID_MINIMAP, ID_FOV, ID_PORT, ID_PEERS,
    ID_OPTION = 300
};
typedef struct LauncherControl { HWND window; int page; } LauncherControl;
typedef struct Launcher {
    HWND window, tabs;
    HFONT font, heading;
    LauncherControl controls[120];
    int control_count, page, result, done, smoke, capture, capture_armed, group;
    WORD previous_pad[4];
    int pad_seen[4];
    float scale;
    MM3LauncherConfig config;
    MM3DisplaySettings display;
} Launcher;

static const char *const drive_labels[MM3_LAUNCHER_DRIVE_KEYS] = {
    "Accelerate", "Brake / reverse", "Steer left", "Steer right", "Handbrake",
    "Manual gear up", "Manual gear down", "Change camera"
};
static const char *const key_labels[MM3_LAUNCHER_KEYS] = {
    "Xbox A", "Xbox B", "Xbox X", "Xbox Y", "White", "Black", "Left trigger", "Right trigger",
    "Start / pause", "Back", "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Left stick click", "Right stick click", "Left stick left", "Left stick right",
    "Left stick down", "Left stick up", "Right stick left", "Right stick right",
    "Right stick down", "Right stick up"
};

static HWND control(Launcher *ui, int id) { return GetDlgItem(ui->window, id); }
static HWND add(Launcher *ui, int page, const char *kind, const char *text,
                DWORD style, int id, int x, int y, int width, int height)
{
    if (page >= 0) y += 27; /* Leave the tab headers above page content. */
    x = (int)lroundf((float)x * ui->scale); y = (int)lroundf((float)y * ui->scale);
    width = (int)lroundf((float)width * ui->scale); height = (int)lroundf((float)height * ui->scale);
    HWND child = CreateWindowExA(!strcmp(kind, "EDIT") ? WS_EX_CLIENTEDGE : 0,
        kind, text, WS_CHILD | (page == 0 || page < 0 ? WS_VISIBLE : 0) | style,
        x, y, width, height, ui->window, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    if (child) {
        SendMessageA(child, WM_SETFONT, (WPARAM)ui->font, TRUE);
        if (ui->control_count < (int)(sizeof ui->controls / sizeof ui->controls[0])) {
            ui->controls[ui->control_count].window = child;
            ui->controls[ui->control_count++].page = page;
        }
    }
    return child;
}
static void label(Launcher *ui, int page, const char *text, int x, int y, int width)
{ add(ui, page, "STATIC", text, 0, -1, x, y, width, 24); }
static void combo_item(HWND combo, const char *text, int value)
{
    LRESULT at = SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)text);
    if (at >= 0) SendMessageA(combo, CB_SETITEMDATA, (WPARAM)at, value);
}
static void combo_select(HWND combo, int value)
{
    int count = (int)SendMessageA(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i)
        if ((int)SendMessageA(combo, CB_GETITEMDATA, i, 0) == value) {
            SendMessageA(combo, CB_SETCURSEL, i, 0); return;
        }
}
static int combo_value(HWND combo)
{
    LRESULT at = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    return at == CB_ERR ? -1 : (int)SendMessageA(combo, CB_GETITEMDATA, (WPARAM)at, 0);
}
static HWND combo(Launcher *ui, int page, int id, const char *caption, int x, int y,
                  int width)
{
    label(ui, page, caption, x, y, width);
    return add(ui, page, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
               id, x, y + 24, width, 240);
}
static LRESULT CALLBACK checkbox_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                      UINT_PTR subclass, DWORD_PTR reference)
{
    (void)subclass; (void)reference;
    if (message == BM_SETCHECK) {
        SetWindowLongPtrA(window, GWLP_USERDATA, wparam == BST_CHECKED);
        InvalidateRect(window, NULL, TRUE); return 0;
    }
    if (message == BM_GETCHECK) return GetWindowLongPtrA(window, GWLP_USERDATA) ? BST_CHECKED : BST_UNCHECKED;
    return DefSubclassProc(window, message, wparam, lparam);
}
static HWND check(Launcher *ui, int page, int id, const char *caption, int x, int y, int width)
{
    HWND window = add(ui, page, "BUTTON", caption, BS_OWNERDRAW | WS_TABSTOP, id, x, y, width, 24);
    SetWindowSubclass(window, checkbox_proc, 1, 0);
    return window;
}
static int checked(Launcher *ui, int id)
{ return SendMessageA(control(ui, id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
static void set_checked(Launcher *ui, int id, int on)
{ SendMessageA(control(ui, id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }
static void key_name(int vk, char *text, size_t capacity)
{
    char key[80];
    UINT scan;
    if (!vk) { snprintf(text, capacity, "Unbound"); return; }
    switch (vk) {
    case VK_LBUTTON: snprintf(text, capacity, "Left mouse"); return;
    case VK_RBUTTON: snprintf(text, capacity, "Right mouse"); return;
    case VK_MBUTTON: snprintf(text, capacity, "Middle mouse"); return;
    case VK_XBUTTON1: snprintf(text, capacity, "Mouse side 1"); return;
    case VK_XBUTTON2: snprintf(text, capacity, "Mouse side 2"); return;
    }
    scan = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
        vk == VK_PRIOR || vk == VK_NEXT || vk == VK_DIVIDE) scan |= 0x100;
    if (GetKeyNameTextA((LONG)(scan << 16), key, sizeof key)) snprintf(text, capacity, "%s", key);
    else snprintf(text, capacity, "VK %02X", vk);
}

static void page_show(Launcher *ui, int page)
{
    ui->page = page;
    ui->capture = 0;
    for (int i = 0; i < ui->control_count; ++i)
        if (ui->controls[i].page >= 0)
            ShowWindow(ui->controls[i].window, ui->controls[i].page == page ? SW_SHOW : SW_HIDE);
    TabCtrl_SetCurSel(ui->tabs, page);
    InvalidateRect(ui->window, NULL, TRUE);
}
static void bindings_refresh(Launcher *ui);
static void binding_detail(Launcher *ui)
{
    int selected = (int)SendMessageA(control(ui, ID_BINDINGS), LB_GETCURSEL, 0, 0);
    char text[256], key[80];
    if (selected < 0) return;
    if (ui->group == 0) {
        snprintf(text, sizeof text, "Xbox %s is controlled by %s.", mm3_launcher_pad_names[selected],
                  mm3_launcher_pad_sources[ui->config.pad[selected]]);
        combo_select(control(ui, ID_SOURCE), ui->config.pad[selected]);
    } else {
        int vk = ui->group == 1 ? ui->config.keys[selected] : ui->config.drive_keys[selected];
        key_name(vk, key, sizeof key);
        snprintf(text, sizeof text, "%s: %s", ui->group == 1 ? key_labels[selected] : drive_labels[selected], key);
    }
    SetWindowTextA(control(ui, ID_BIND_LABEL), text);
    EnableWindow(control(ui, ID_SOURCE), ui->group == 0);
    EnableWindow(control(ui, ID_PAD_PORT), ui->group == 0);
    SetWindowTextA(control(ui, ID_CAPTURE), ui->group == 0 ? "Capture controller button" : "Capture key or mouse button");
    SetWindowTextA(control(ui, ID_UNBIND), "Clear selected binding");
}
static void bindings_refresh(Launcher *ui)
{
    HWND list = control(ui, ID_BINDINGS);
    int old = (int)SendMessageA(list, LB_GETCURSEL, 0, 0);
    int count = ui->group == 0 ? MM3_LAUNCHER_PAD_INPUTS : ui->group == 1 ? MM3_LAUNCHER_KEYS : MM3_LAUNCHER_DRIVE_KEYS;
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; ++i) {
        char text[160], key[80];
        const char *name;
        if (ui->group == 0) {
            name = mm3_launcher_pad_names[i];
            snprintf(key, sizeof key, "%s", mm3_launcher_pad_sources[ui->config.pad[i]]);
        } else {
            name = ui->group == 1 ? key_labels[i] : drive_labels[i];
            key_name(ui->group == 1 ? ui->config.keys[i] : ui->config.drive_keys[i], key, sizeof key);
        }
        snprintf(text, sizeof text, "%s     :     %s", name, key);
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)text);
    }
    SendMessageA(list, LB_SETCURSEL, old >= 0 && old < count ? old : 0, 0);
    binding_detail(ui);
}
static void binding_set(Launcher *ui, int value)
{
    int selected = (int)SendMessageA(control(ui, ID_BINDINGS), LB_GETCURSEL, 0, 0);
    if (selected < 0) return;
    if (ui->group != 0 && (value == VK_F10 || value == VK_F11)) {
        SetWindowTextA(control(ui, ID_STATUS), "F10 and F11 are reserved for Graphics Settings and fullscreen. Choose another key, or Escape to cancel.");
        return;
    }
    if (ui->group == 0) ui->config.pad[selected] = value;
    else if (ui->group == 1) ui->config.keys[selected] = value;
    else ui->config.drive_keys[selected] = value;
    ui->capture = 0;
    SetWindowTextA(control(ui, ID_STATUS), "Binding updated. Play saves your preferences.");
    bindings_refresh(ui);
}

static void populate(Launcher *ui)
{
    char text[64];
    MM3DisplaySettings *s = &ui->display;
    int resolution = s->window_width * 10000 + s->window_height;
    combo_select(control(ui, ID_RESOLUTION), resolution);
    if (combo_value(control(ui, ID_RESOLUTION)) < 0) {
        snprintf(text, sizeof text, "%d x %d", s->window_width, s->window_height);
        combo_item(control(ui, ID_RESOLUTION), text, resolution);
        combo_select(control(ui, ID_RESOLUTION), resolution);
    }
    combo_select(control(ui, ID_MODE), s->fullscreen);
    snprintf(text, sizeof text, "%d", s->fps_limit); SetWindowTextA(control(ui, ID_FPS), text);
    set_checked(ui, ID_VSYNC, s->vsync); set_checked(ui, ID_SHOW_FPS, s->show_fps);
    combo_select(control(ui, ID_SCALE), (int)lroundf(s->render_scale * 1000.f));
    if (combo_value(control(ui, ID_SCALE)) < 0) {
        snprintf(text, sizeof text, "Custom %.2fx", (double)s->render_scale);
        combo_item(control(ui, ID_SCALE), text, (int)lroundf(s->render_scale * 1000.f));
        combo_select(control(ui, ID_SCALE), (int)lroundf(s->render_scale * 1000.f));
    }
    { HWND aspect = control(ui, ID_ASPECT);
      LRESULT at = SendMessageA(aspect, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)s->aspect);
      if (at == CB_ERR) at = SendMessageA(aspect, CB_ADDSTRING, 0, (LPARAM)s->aspect);
      SendMessageA(aspect, CB_SETCURSEL, (WPARAM)at, 0); }
    combo_select(control(ui, ID_MSAA), s->msaa); combo_select(control(ui, ID_ANISO), s->aniso);
    combo_select(control(ui, ID_DRAW), (int)lroundf(s->draw_distance * 1000.f));
    combo_select(control(ui, ID_VIEW), (int)lroundf(s->view_distance * 1000.f));
    if (combo_value(control(ui, ID_DRAW)) < 0) {
        snprintf(text, sizeof text, "Custom %.2fx", (double)s->draw_distance);
        combo_item(control(ui, ID_DRAW), text, (int)lroundf(s->draw_distance * 1000.f));
        combo_select(control(ui, ID_DRAW), (int)lroundf(s->draw_distance * 1000.f));
    }
    if (combo_value(control(ui, ID_VIEW)) < 0) {
        snprintf(text, sizeof text, "Custom %.2fx", (double)s->view_distance);
        combo_item(control(ui, ID_VIEW), text, (int)lroundf(s->view_distance * 1000.f));
        combo_select(control(ui, ID_VIEW), (int)lroundf(s->view_distance * 1000.f));
    }
    combo_select(control(ui, ID_MINIMAP), s->minimap_improved);
    set_checked(ui, ID_KEYBOARD, ui->config.option[MM3_LAUNCH_KEYBOARD]);
    combo_select(control(ui, ID_PAD_COUNT), ui->config.controller_count);
    for (int i = 1; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        set_checked(ui, ID_OPTION + i, ui->config.option[i]);
    snprintf(text, sizeof text, "%d", ui->config.fov_degrees); SetWindowTextA(control(ui, ID_FOV), text);
    snprintf(text, sizeof text, "%d", ui->config.network_port); SetWindowTextA(control(ui, ID_PORT), text);
    SetWindowTextA(control(ui, ID_PEERS), ui->config.peers);
    bindings_refresh(ui);
}

static int read_integer(Launcher *ui, int id, int low, int high, int *value)
{
    char text[64], *end;
    long number;
    GetWindowTextA(control(ui, id), text, sizeof text);
    number = strtol(text, &end, 10);
    if (end == text || *end || number < low || number > high) return 0;
    *value = (int)number;
    return 1;
}
static int read_controls(Launcher *ui, char *error, size_t capacity)
{
    int resolution = combo_value(control(ui, ID_RESOLUTION));
    MM3DisplaySettings *s = &ui->display;
    if (!read_integer(ui, ID_FPS, 1, 60, &s->fps_limit)) {
        snprintf(error, capacity, "Enter a maximum FPS between 1 and 60."); return 0;
    }
    if (!read_integer(ui, ID_FOV, 0, 110, &ui->config.fov_degrees) ||
        (ui->config.fov_degrees && ui->config.fov_degrees < 60)) {
        snprintf(error, capacity, "Field of view must be 0 (retail) or 60 to 110 degrees."); return 0;
    }
    if (!read_integer(ui, ID_PORT, 1, 65535, &ui->config.network_port)) {
        snprintf(error, capacity, "Enter a network port between 1 and 65535."); return 0;
    }
    s->window_width = resolution / 10000; s->window_height = resolution % 10000;
    s->fullscreen = combo_value(control(ui, ID_MODE));
    s->vsync = checked(ui, ID_VSYNC); s->show_fps = checked(ui, ID_SHOW_FPS);
    s->render_scale = (float)combo_value(control(ui, ID_SCALE)) / 1000.f;
    GetWindowTextA(control(ui, ID_ASPECT), s->aspect, sizeof s->aspect);
    s->msaa = combo_value(control(ui, ID_MSAA)); s->aniso = combo_value(control(ui, ID_ANISO));
    s->draw_distance = (float)combo_value(control(ui, ID_DRAW)) / 1000.f;
    s->view_distance = (float)combo_value(control(ui, ID_VIEW)) / 1000.f;
    s->minimap_improved = combo_value(control(ui, ID_MINIMAP));
    ui->config.option[MM3_LAUNCH_KEYBOARD] = checked(ui, ID_KEYBOARD);
    ui->config.controller_count = combo_value(control(ui, ID_PAD_COUNT));
    for (int i = 1; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        ui->config.option[i] = checked(ui, ID_OPTION + i);
    GetWindowTextA(control(ui, ID_PEERS), ui->config.peers, sizeof ui->config.peers);
    if (!mm3_launcher_config_valid(&ui->config)) {
        snprintf(error, capacity, "Check the controller bindings and peer addresses (host:port, comma separated; up to eight).");
        return 0;
    }
    return 1;
}

static int apply(Launcher *ui)
{
    char error[512];
    const char *failed;
    if (!read_controls(ui, error, sizeof error)) goto fail;
    if (!mm3_display_settings_save(&ui->display, error, sizeof error)) goto fail;
    if (!mm3_launcher_config_save(&ui->config, error, sizeof error)) {
        char detail[512];
        snprintf(detail, sizeof detail, "Display preferences were saved. Launcher preferences could not be saved. %.350s", error);
        snprintf(error, sizeof error, "%s", detail); goto fail;
    }
    failed = mm3_display_settings_apply_environment(&ui->display, 1);
    if (failed) { snprintf(error, sizeof error, "Cannot apply display setting %s.", failed); goto fail; }
    if (!mm3_launcher_config_apply(&ui->config, error, sizeof error)) goto fail;
    return 1;
fail:
    SetWindowTextA(control(ui, ID_STATUS), error);
    MessageBoxA(ui->window, error, "Preferences could not be applied", MB_OK | MB_ICONERROR);
    return 0;
}

static void settings_controls(Launcher *ui)
{
    HWND c;
    static const int sizes[][2] = {{960,540},{1280,720},{1600,900},{1920,1080},{2560,1440},{3840,2160}};
    c = combo(ui, 0, ID_RESOLUTION, "Window size", 42, 98, 238);
    for (int i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); ++i) {
        char name[48]; snprintf(name, sizeof name, "%d x %d", sizes[i][0], sizes[i][1]);
        combo_item(c, name, sizes[i][0] * 10000 + sizes[i][1]);
    }
    c = combo(ui, 0, ID_MODE, "Window mode", 310, 98, 238);
    combo_item(c, "Windowed", 0); combo_item(c, "Fullscreen", 1);
    c = combo(ui, 0, ID_ASPECT, "Aspect ratio", 578, 98, 238);
    for (int i = 0; i < 5; ++i) {
        static const char *const ratios[] = {"auto", "4:3", "16:9", "16:10", "21:9"};
        SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)ratios[i]);
    }
    label(ui, 0, "Maximum FPS (1-60)", 42, 166, 238);
    c = add(ui, 0, "COMBOBOX", "", CBS_DROPDOWN | WS_TABSTOP, ID_FPS, 42, 190, 110, 120);
    combo_item(c, "30", 30); combo_item(c, "60", 60);
    check(ui, 0, ID_VSYNC, "VSync", 172, 190, 100);
    c = combo(ui, 0, ID_SCALE, "Internal resolution", 310, 166, 238);
    combo_item(c, "Automatic (window size)", 0); combo_item(c, "480p", 1000);
    combo_item(c, "720p", 1500); combo_item(c, "960p", 2000); combo_item(c, "1440p", 3000);
    combo_item(c, "1920p", 4000); combo_item(c, "3840p", 8000);
    c = combo(ui, 0, ID_MSAA, "Anti-aliasing", 578, 166, 238);
    combo_item(c, "Off", 1); combo_item(c, "2x MSAA", 2); combo_item(c, "4x MSAA", 4); combo_item(c, "8x MSAA", 8);
    c = combo(ui, 0, ID_ANISO, "Texture filtering", 42, 234, 238);
    combo_item(c, "1x", 1); combo_item(c, "2x", 2); combo_item(c, "4x", 4);
    combo_item(c, "8x", 8); combo_item(c, "16x", 16);
    c = combo(ui, 0, ID_DRAW, "Object detail distance", 310, 234, 238);
    for (int i = 1; i <= 4; ++i) { char name[24]; snprintf(name, sizeof name, "%dx%s", i, i == 1 ? " (Xbox)" : ""); combo_item(c, name, i * 1000); }
    c = combo(ui, 0, ID_VIEW, "Building view distance", 578, 234, 238);
    for (int i = 1; i <= 4; ++i) { char name[24]; snprintf(name, sizeof name, "%dx%s", i, i == 1 ? " (Xbox)" : ""); combo_item(c, name, i * 1000); }
    check(ui, 0, ID_SHOW_FPS, "Show FPS in the window title", 42, 312, 350);
    c = combo(ui, 0, ID_MINIMAP, "Minimap style", 445, 302, 371);
    combo_item(c, "Retail", 0); combo_item(c, "Improved perspective roads", 1);
    check(ui, 0, ID_OPTION + MM3_LAUNCH_DSP, mm3_launcher_options[MM3_LAUNCH_DSP].label, 42, 379, 390);
    EnableWindow(control(ui, ID_OPTION + MM3_LAUNCH_DSP), MM3_LAUNCHER_AUDIO);
    label(ui, 0, MM3_LAUNCHER_AUDIO ? "Console DSP effects and mixing." : "Sound-processor controls will be enabled when the audio update is installed.", 42, 407, 774);
    check(ui, 0, ID_OPTION + MM3_LAUNCH_NETWORK, "Enable System Link", 42, 450, 230);
    EnableWindow(control(ui, ID_OPTION + MM3_LAUNCH_NETWORK), MM3_LAUNCHER_NETWORK);
    label(ui, 0, "Local UDP port", 310, 450, 145);
    add(ui, 0, "EDIT", "3075", WS_BORDER | ES_NUMBER | WS_TABSTOP, ID_PORT, 455, 447, 100, 25);
    label(ui, 0, "Peers (host:port, comma separated)", 42, 492, 774);
    add(ui, 0, "EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, ID_PEERS, 42, 518, 774, 26);
    SendMessageA(control(ui, ID_PEERS), EM_SETLIMITTEXT, 511, 0);
    EnableWindow(control(ui, ID_PORT), MM3_LAUNCHER_NETWORK);
    EnableWindow(control(ui, ID_PEERS), MM3_LAUNCHER_NETWORK);
    label(ui, 0, MM3_LAUNCHER_NETWORK ? "Each copy needs its own port and save directory. Xbox Live sign-in is separate." : "System Link controls will be enabled when networking is installed.", 42, 552, 774);
}

static void controls_controls(Launcher *ui)
{
    HWND c;
    check(ui, 1, ID_KEYBOARD, "Enable keyboard controls (player 1, alongside a controller)", 42, 94, 774);
    c = combo(ui, 1, ID_PAD_COUNT, "Controllers in game", 42, 130, 345);
    for (int i = 1; i <= 4; ++i) { char text[16]; snprintf(text, sizeof text, "%d", i); combo_item(c, text, i); }
    add(ui, 1, "STATIC", "Detecting controllers...", 0, ID_PAD_STATUS, 417, 130, 399, 64);
    c = combo(ui, 1, ID_GROUP, "Bindings", 42, 207, 345);
    combo_item(c, "Controller buttons (all four players)", 0);
    combo_item(c, "Keyboard Xbox buttons", 1); combo_item(c, "Keyboard driving shortcuts", 2);
    combo_select(c, 0);
    add(ui, 1, "LISTBOX", "", LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_TABSTOP,
         ID_BINDINGS, 42, 266, 345, 292);
    add(ui, 1, "STATIC", "", 0, ID_BIND_LABEL, 417, 213, 399, 48);
    c = combo(ui, 1, ID_SOURCE, "Controller source", 417, 274, 399);
    for (int i = 0; i < MM3_LAUNCHER_PAD_SOURCES; ++i) combo_item(c, mm3_launcher_pad_sources[i], i);
    c = combo(ui, 1, ID_PAD_PORT, "Controller to use for capture", 417, 340, 399);
    for (int i = 0; i < 4; ++i) { char text[32]; snprintf(text, sizeof text, "Controller %d", i + 1); combo_item(c, text, i); }
    combo_select(c, 0);
    add(ui, 1, "BUTTON", "Capture controller button", BS_PUSHBUTTON | WS_TABSTOP,
         ID_CAPTURE, 417, 414, 399, 32);
    add(ui, 1, "BUTTON", "Clear selected binding", BS_PUSHBUTTON | WS_TABSTOP,
         ID_UNBIND, 417, 458, 399, 30);
    add(ui, 1, "STATIC", "Select an action, then capture a key/button or select a controller source. Escape cancels capture. Stick axes stay analog. Controller bindings apply equally to players 1-4.",
         0, -1, 417, 504, 399, 80);
}
static void mods_controls(Launcher *ui)
{
    static const int options[] = {MM3_LAUNCH_WIDESCREEN, MM3_LAUNCH_DISTANCE, MM3_LAUNCH_SKY, MM3_LAUNCH_LOADING, MM3_LAUNCH_NATIVE_FOG,
        MM3_LAUNCH_CAR_LIGHT, MM3_LAUNCH_LAMP_LIGHT, MM3_LAUNCH_WORLD_LIGHT,
        MM3_LAUNCH_FLARE_DEPTH, MM3_LAUNCH_UNLOCK, MM3_LAUNCH_DEV_TOOLS,
        MM3_LAUNCH_DEV_HUD, MM3_LAUNCH_DEBUG_MENU, MM3_LAUNCH_CAMERAS, MM3_LAUNCH_E3};
    for (int i = 0; i < (int)(sizeof options / sizeof options[0]); ++i) {
        int x = i < 8 ? 42 : 445, row = i < 8 ? i : i - 8;
        check(ui, 2, ID_OPTION + options[i], mm3_launcher_options[options[i]].label, x, 104 + row * 38, 371);
    }
    label(ui, 2, "Horizontal field of view", 42, 435, 260);
    add(ui, 2, "EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_FOV, 310, 431, 77, 26);
    label(ui, 2, "0 = retail, or 60-110 degrees", 42, 466, 345);
    add(ui, 2, "STATIC", "Developer tools need the prepared scripts from tools\\debug-features\\prepare.py. These options invoke the game's own developer commands.",
        0, -1, 445, 370, 371, 60);
    HWND fixed = check(ui, 2, -1, "Collision-cell safety repair (required)", 42, 521, 774);
    SendMessageA(fixed, BM_SETCHECK, BST_CHECKED, 0); EnableWindow(fixed, FALSE);
    label(ui, 2, "Preferences apply when Play starts the game. Graphics Settings remain available in game with F10.", 42, 560, 774);
}

static int pad_source(const XINPUT_GAMEPAD *pad)
{
    static const WORD bits[14] = {XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X,
        XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_LEFT_THUMB,
        XINPUT_GAMEPAD_RIGHT_THUMB, XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT};
    for (int i = 0; i < 14; ++i) if (pad->wButtons & bits[i]) return i;
    if (pad->bLeftTrigger > 85) return 14;
    if (pad->bRightTrigger > 85) return 15;
    return -1;
}
static void pad_navigation(Launcher *ui, WORD pressed)
{
    HWND focus = GetFocus();
    char kind[32];
    GetClassNameA(focus, kind, sizeof kind);
    if (pressed & XINPUT_GAMEPAD_START) { SendMessageA(ui->window, WM_COMMAND, IDOK, 0); return; }
    if (pressed & XINPUT_GAMEPAD_B) { SendMessageA(ui->window, WM_COMMAND, IDCANCEL, 0); return; }
    if (pressed & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
        int direction = pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER ? 1 : -1;
        page_show(ui, (ui->page + direction + 3) % 3); SetFocus(ui->tabs); return;
    }
    if (pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) {
        if (!_stricmp(kind, "COMBOBOX")) {
            int count = (int)SendMessageA(focus, CB_GETCOUNT, 0, 0);
            int at = (int)SendMessageA(focus, CB_GETCURSEL, 0, 0);
            at += pressed & XINPUT_GAMEPAD_DPAD_RIGHT ? 1 : -1;
            if (count > 0) SendMessageA(focus, CB_SETCURSEL, (at + count) % count, 0);
            SendMessageA(ui->window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(focus), CBN_SELCHANGE), (LPARAM)focus);
        }
    }
    if (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)) {
        int previous = (pressed & XINPUT_GAMEPAD_DPAD_UP) != 0;
        if (!_stricmp(kind, "LISTBOX")) {
            int count = (int)SendMessageA(focus, LB_GETCOUNT, 0, 0);
            int at = (int)SendMessageA(focus, LB_GETCURSEL, 0, 0) + (previous ? -1 : 1);
            if (at >= 0 && at < count) {
                SendMessageA(focus, LB_SETCURSEL, at, 0); binding_detail(ui);
            } else SetFocus(GetNextDlgTabItem(ui->window, focus, previous));
        } else SetFocus(GetNextDlgTabItem(ui->window, focus, previous));
    }
    if (pressed & XINPUT_GAMEPAD_A) {
        if (!_stricmp(kind, "BUTTON")) SendMessageA(focus, BM_CLICK, 0, 0);
        else if (!_stricmp(kind, "COMBOBOX")) SendMessageA(focus, CB_SHOWDROPDOWN, !SendMessageA(focus, CB_GETDROPPEDSTATE, 0, 0), 0);
    }
}
static void poll_pads(Launcher *ui)
{
    char text[512] = "";
    size_t at = 0;
    int capture_port = combo_value(control(ui, ID_PAD_PORT));
    for (int i = 0; i < 4; ++i) {
        XINPUT_STATE state;
        int connected = XInputGetState((DWORD)i, &state) == ERROR_SUCCESS;
        char buttons[48] = "";
        WORD pressed = connected && ui->pad_seen[i] ? state.Gamepad.wButtons & (WORD)~ui->previous_pad[i] : 0;
        if (connected) {
            int source = pad_source(&state.Gamepad);
            snprintf(buttons, sizeof buttons, "%s%s%s", source >= 0 ? " (" : "",
                source >= 0 ? mm3_launcher_pad_sources[source] : "", source >= 0 ? ")" : "");
            if (ui->capture && ui->group == 0 && i == capture_port && GetForegroundWindow() == ui->window) {
                if (source < 0) ui->capture_armed = 1;
                else if (ui->capture_armed) binding_set(ui, source);
            } else if (!ui->capture && GetForegroundWindow() == ui->window)
                pad_navigation(ui, pressed);
        }
        ui->previous_pad[i] = connected ? state.Gamepad.wButtons : 0;
        ui->pad_seen[i] = connected;
        at += (size_t)snprintf(text + at, sizeof text - at, "P%d: %s%s%s", i + 1,
            connected ? "connected" : "not connected", buttons, i == 1 ? "\r\n" : i == 3 ? "" : "       ");
    }
    SetWindowTextA(control(ui, ID_PAD_STATUS), text);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    Launcher *ui = (Launcher *)GetWindowLongPtrA(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        ui = (Launcher *)((CREATESTRUCTA *)lparam)->lpCreateParams;
        ui->window = window; SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)ui);
    }
    if (!ui) return DefWindowProcA(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        TCITEMA tab = {0};
        ui->font = CreateFontA(-(int)lroundf(16.f * ui->scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        ui->heading = CreateFontA(-(int)lroundf(23.f * ui->scale), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        HWND title = add(ui, -1, "STATIC", "Midtown Madness 3", 0, -1, 25, 17, 570, 35);
        SendMessageA(title, WM_SETFONT, (WPARAM)ui->heading, TRUE);
        label(ui, -1, "Set up your game, then Play. F10 opens Graphics Settings in game.", 25, 55, 800);
        ui->tabs = add(ui, -1, WC_TABCONTROLA, "", WS_TABSTOP, ID_TABS, 22, 85, 820, 550);
        tab.mask = TCIF_TEXT;
        tab.pszText = "Settings"; TabCtrl_InsertItem(ui->tabs, 0, &tab);
        tab.pszText = "Controls"; TabCtrl_InsertItem(ui->tabs, 1, &tab);
        tab.pszText = "Mods"; TabCtrl_InsertItem(ui->tabs, 2, &tab);
        /* The tab is a sibling; put it behind its page controls. */
        SetWindowPos(ui->tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        settings_controls(ui); controls_controls(ui); mods_controls(ui);
        add(ui, -1, "STATIC", "Keyboard: Tab to move, Enter to activate. Controller: D-pad moves, A selects, LB/RB changes tab, Start plays.",
            0, ID_STATUS, 25, 644, 815, 45);
        add(ui, -1, "BUTTON", "Restore defaults", BS_PUSHBUTTON | WS_TABSTOP, ID_DEFAULTS, 25, 699, 155, 34);
        add(ui, -1, "BUTTON", "Play", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 583, 699, 122, 34);
        add(ui, -1, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 718, 699, 122, 34);
        populate(ui); page_show(ui, 0);
        SetTimer(window, 1, 70, NULL);
        if (ui->smoke) SetTimer(window, 2, 150, NULL);
        return 0;
    }
    case WM_NOTIFY:
        if (((NMHDR *)lparam)->idFrom == ID_TABS && ((NMHDR *)lparam)->code == TCN_SELCHANGE)
            page_show(ui, TabCtrl_GetCurSel(ui->tabs));
        return 0;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED && (LOWORD(wparam) == ID_KEYBOARD ||
            LOWORD(wparam) == ID_VSYNC || LOWORD(wparam) == ID_SHOW_FPS ||
            (LOWORD(wparam) >= ID_OPTION && LOWORD(wparam) < ID_OPTION + MM3_LAUNCH_OPTION_COUNT))) {
            int id = LOWORD(wparam);
            set_checked(ui, id, !checked(ui, id));
            return 0;
        }
        switch (LOWORD(wparam)) {
        case IDOK:
            if (!ui->capture && apply(ui)) { ui->result = 1; DestroyWindow(window); }
            return 0;
        case IDCANCEL:
            if (ui->capture) { ui->capture = 0; SetWindowTextA(control(ui, ID_STATUS), "Capture cancelled."); }
            else DestroyWindow(window);
            return 0;
        case ID_DEFAULTS:
            mm3_launcher_config_defaults(&ui->config);
            mm3_display_settings_defaults(&ui->display, getenv("MM3_PERFORMANCE_PROFILE"));
            populate(ui); SetWindowTextA(control(ui, ID_STATUS), "Defaults restored in this window. Play saves them; Cancel discards them.");
            return 0;
        case ID_GROUP:
            if (HIWORD(wparam) == CBN_SELCHANGE) { ui->capture = 0; ui->group = combo_value(control(ui, ID_GROUP)); bindings_refresh(ui); }
            return 0;
        case ID_BINDINGS:
            if (HIWORD(wparam) == LBN_SELCHANGE) { ui->capture = 0; binding_detail(ui); }
            return 0;
        case ID_SOURCE:
            if (HIWORD(wparam) == CBN_SELCHANGE && ui->group == 0) binding_set(ui, combo_value(control(ui, ID_SOURCE)));
            return 0;
        case ID_CAPTURE:
            ui->capture = 1; ui->capture_armed = 0;
            SetWindowTextA(control(ui, ID_STATUS), ui->group == 0 ?
                "Release the controller buttons, then press the desired button or trigger. Escape cancels." :
                "Press a key or mouse button now. Escape cancels; Clear selected binding unbinds it.");
            return 0;
        case ID_UNBIND: binding_set(ui, ui->group == 0 ? 16 : 0); return 0;
        }
        break;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lparam;
        char caption[256];
        RECT box = draw->rcItem, text = draw->rcItem;
        int size = (int)lroundf(16.f * ui->scale);
        UINT flags = DFCS_BUTTONCHECK;
        if (draw->CtlType != ODT_BUTTON) break;
        FillRect(draw->hDC, &draw->rcItem, (HBRUSH)(COLOR_BTNFACE + 1));
        box.left += 2; box.right = box.left + size;
        box.top += ((box.bottom - box.top) - size) / 2; box.bottom = box.top + size;
        if (SendMessageA(draw->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED) flags |= DFCS_CHECKED;
        if (!IsWindowEnabled(draw->hwndItem)) flags |= DFCS_INACTIVE;
        DrawFrameControl(draw->hDC, &box, DFC_BUTTON, flags);
        text.left = box.right + (int)lroundf(6.f * ui->scale);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, GetSysColor(IsWindowEnabled(draw->hwndItem) ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
        HFONT old = SelectObject(draw->hDC, ui->font);
        GetWindowTextA(draw->hwndItem, caption, sizeof caption);
        DrawTextA(draw->hDC, caption, -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(draw->hDC, old);
        if ((draw->itemState & ODS_FOCUS) && !(draw->itemState & ODS_NOFOCUSRECT)) DrawFocusRect(draw->hDC, &text);
        return TRUE;
    }
    case WM_TIMER:
        if (wparam == 1) { poll_pads(ui); return 0; }
        if (wparam == 2) {
            static const int important[] = {ID_MODE, ID_FPS, ID_MINIMAP, ID_KEYBOARD,
                ID_GROUP, ID_BINDINGS, ID_SOURCE, ID_CAPTURE, ID_PAD_COUNT, ID_FOV, IDOK, IDCANCEL};
            int valid = 1;
            for (int i = 0; i < (int)(sizeof important / sizeof important[0]); ++i)
                if (!control(ui, important[i])) valid = 0;
            page_show(ui, 1); ui->group = 1; combo_select(control(ui, ID_GROUP), 1); bindings_refresh(ui);
            valid = valid && SendMessageA(control(ui, ID_BINDINGS), LB_GETCOUNT, 0, 0) == MM3_LAUNCHER_KEYS;
            page_show(ui, 2); valid = valid && IsWindowVisible(control(ui, ID_FOV));
            page_show(ui, 0);
            fprintf(stderr, "[MM3_LAUNCHER_SMOKE] controls=%d tabs=3 keyboard_bindings=24 pad_ports=4 result=%s\n", ui->control_count, valid ? "PASS" : "FAIL");
            if (!valid) ui->smoke = -1;
            DestroyWindow(window); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        KillTimer(window, 1); KillTimer(window, 2);
        ui->done = 1;
        /* A synchronous cross-thread Close can be dispatched from inside
         * GetMessage; wake that wait without leaving WM_QUIT for the game. */
        PostThreadMessageA(GetCurrentThreadId(), WM_NULL, 0, 0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static int bootstrap(char *error, size_t capacity)
{
    static const char *const defaults[][2] = {
        {"RECOMP_PB_EXEC", "1"}, {"RECOMP_FB_WINDOW", "1"}, {"RECOMP_USB", "1"},
        {"RECOMP_APU_DSP_ACK", "0x80458810"}, {"RECOMP_KEYBOARD_LAYOUT", "mm3"}
    };
    for (int i = 0; i < (int)(sizeof defaults / sizeof defaults[0]); ++i) {
        const char *current = getenv(defaults[i][0]);
        if ((!current || !*current) && _putenv_s(defaults[i][0], defaults[i][1])) {
            snprintf(error, capacity, "Cannot initialize %s.", defaults[i][0]); return 0;
        }
    }
    return 1;
}

static float layout_scale(const RECT *work, float desired)
{
    RECT frame = {0};
    AdjustWindowRectEx(&frame, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                      FALSE, WS_EX_CONTROLPARENT);
    float width = (float)(work->right - work->left - (frame.right - frame.left) - 20) / 865.f;
    float height = (float)(work->bottom - work->top - (frame.bottom - frame.top) - 20) / 754.f;
    if (desired > width) desired = width;
    if (desired > height) desired = height;
    return desired < 0.4f ? 0.4f : desired;
}

int mm3_launcher_run(void)
{
    const char *mode = getenv("MM3_LAUNCHER"), *script = getenv("RECOMP_PB_EXEC");
    int show = mode && !strcmp(mode, "1") ? 1 : mode && !strcmp(mode, "0") ? 0 : !script;
    Launcher ui;
    char warning[512] = "", display_warning[512] = "";
    const char *smoke = getenv("MM3_LAUNCHER_SMOKE");
    memset(&ui, 0, sizeof ui);
    ui.smoke = smoke && !strcmp(smoke, "1");
    if (!mm3_launcher_config_startup(&ui.config, warning, sizeof warning) || *warning)
        fprintf(stderr, "[MM3_LAUNCHER] %s\n", warning);
    if (!mm3_display_settings_startup(&ui.display, display_warning, sizeof display_warning) || *display_warning)
        fprintf(stderr, "[MM3_LAUNCHER] %s\n", display_warning);
    if (show || ui.smoke) {
        INITCOMMONCONTROLSEX common = {sizeof common, ICC_TAB_CLASSES};
        WNDCLASSA wc = {0};
        RECT bounds, work;
        MSG message;
        SetProcessDPIAware();
        InitCommonControlsEx(&common);
        wc.lpfnWndProc = window_proc; wc.hInstance = GetModuleHandleA(NULL);
        wc.hIcon = LoadIconA(wc.hInstance, MAKEINTRESOURCEA(1));
        wc.hCursor = LoadCursorA(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "MM3NativeLauncher";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
        HDC screen = GetDC(NULL);
        ui.scale = layout_scale(&work, screen ? (float)GetDeviceCaps(screen, LOGPIXELSY) / 96.f : 1.f);
        if (screen) ReleaseDC(NULL, screen);
        bounds.left = bounds.top = 0;
        bounds.right = (LONG)lroundf(865.f * ui.scale); bounds.bottom = (LONG)lroundf(754.f * ui.scale);
        AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_CONTROLPARENT);
        HWND window = CreateWindowExA(WS_EX_CONTROLPARENT, wc.lpszClassName, "Midtown Madness 3 - Play",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            work.left + ((work.right - work.left) - (bounds.right - bounds.left)) / 2,
            work.top + ((work.bottom - work.top) - (bounds.bottom - bounds.top)) / 2,
            bounds.right - bounds.left, bounds.bottom - bounds.top,
            NULL, NULL, wc.hInstance, &ui);
        if (!window) return 0;
        ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window);
        if (*warning || *display_warning) SetWindowTextA(control(&ui, ID_STATUS), *warning ? warning : display_warning);
        SetFocus(control(&ui, IDOK));
        while (!ui.done && GetMessageA(&message, NULL, 0, 0) > 0) {
            if (ui.capture && ui.group != 0) {
                int vk = 0;
                if (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) {
                    if (message.wParam == VK_ESCAPE) {
                        ui.capture = 0; SetWindowTextA(control(&ui, ID_STATUS), "Capture cancelled."); continue;
                    }
                    vk = (int)message.wParam;
                }
                if (message.message == WM_LBUTTONDOWN) vk = VK_LBUTTON;
                if (message.message == WM_RBUTTONDOWN) vk = VK_RBUTTON;
                if (message.message == WM_MBUTTONDOWN) vk = VK_MBUTTON;
                if (message.message == WM_XBUTTONDOWN) vk = HIWORD(message.wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
                if (vk > 0 && vk <= 255) { binding_set(&ui, vk); continue; }
            } else if (ui.capture && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                ui.capture = 0; SetWindowTextA(control(&ui, ID_STATUS), "Capture cancelled."); continue;
            }
            if (!IsDialogMessageA(window, &message)) { TranslateMessage(&message); DispatchMessageA(&message); }
        }
        if (ui.font) DeleteObject(ui.font); if (ui.heading) DeleteObject(ui.heading);
        if (ui.smoke || !ui.result) return 0;
    } else if (!mm3_launcher_config_apply(&ui.config, warning, sizeof warning)) {
        fprintf(stderr, "[MM3_LAUNCHER] %s\n", warning); return 0;
    }
    if (!bootstrap(warning, sizeof warning)) { fprintf(stderr, "[MM3_LAUNCHER] %s\n", warning); return 0; }
    return 1;
}
