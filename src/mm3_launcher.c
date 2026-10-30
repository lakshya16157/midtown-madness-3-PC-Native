#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <xinput.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mm3_launcher.h"
#include "mm3_launcher_config.h"
#include "mm3_display_config.h"
#include "mm3_controller_detection.h"
#include "mm3_update.h"

/* The update button's outside world, replaceable by the UI contract test. */
#ifndef MM3_UPDATE_API_URL
#define MM3_UPDATE_API_URL MM3_UPDATE_API
#endif
#ifndef MM3_UPDATE_TRUSTED
#define MM3_UPDATE_TRUSTED MM3_UPDATE_TRUSTED_PREFIX
#endif
#ifndef MM3_UPDATE_ASK
#define MM3_UPDATE_ASK(window, text) \
    (MessageBoxA(window, text, "Midtown Madness 3", MB_YESNO | MB_ICONQUESTION) == IDYES)
#endif
#ifndef MM3_UPDATE_OPEN
#define MM3_UPDATE_OPEN(setup, folder) update_open_setup(setup, folder)
#endif

#ifndef MM3_LAUNCHER_AUDIO
#define MM3_LAUNCHER_AUDIO 0
#endif
#ifndef MM3_LAUNCHER_NETWORK
#define MM3_LAUNCHER_NETWORK 0
#endif
#ifndef MM3_VERSION
#define MM3_VERSION "development build"
#endif

enum {
    ID_TABS = 100, ID_DEFAULTS, ID_STATUS, ID_KEYBOARD, ID_GROUP, ID_BINDINGS,
    ID_CAPTURE, ID_UNBIND, ID_SOURCE, ID_BIND_LABEL, ID_PAD_STATUS, ID_PAD_PORT, ID_PAD_COUNT,
    ID_RESOLUTION, ID_MODE, ID_FPS, ID_VSYNC, ID_SCALE, ID_ASPECT, ID_MSAA,
    ID_ANISO, ID_DRAW, ID_VIEW, ID_SHOW_FPS, ID_MINIMAP, ID_FOV, ID_PORT, ID_PEERS,
    ID_SCREEN, ID_ORIGINAL, ID_XBOX_GAMEPLAY, ID_DAMAGE, ID_TIME, ID_NIGHT, ID_UPDATE,
    ID_HELP_TITLE, ID_HELP_VALUE, ID_HELP_BODY, ID_HELP_EFFECT, ID_REQUIRED,
    ID_PAD_SUMMARY, ID_PAD_HINT, ID_TAB_BAR, ID_OWN_DAMAGE, ID_AI_LEVEL, ID_CAMERA_KEY,
    ID_OPTION = 300,
    LAUNCHER_WIDTH = 1215, LAUNCHER_HEIGHT = 754,
    /* Settings, Controls, Visual + audio mods, Gameplay mods. */
    LAUNCHER_PAGES = 4
};
/* Messages from the update worker thread to the window. */
enum { WM_UPDATE_DONE = WM_APP + 1, WM_UPDATE_PROGRESS };
/* One "Check for updates" step on its own thread: the check, then (if the
 * player agrees) the download. The thread touches nothing but this block and
 * posts it back; the window frees it. */
typedef struct UpdateJob {
    HWND window;
    volatile LONG cancel;
    int download, result;       /* result: what mm3_update_check/download returned */
    unsigned long long reported;
    MM3Update update;
    wchar_t target[2 * MAX_PATH];
    char message[512];
} UpdateJob;
typedef struct LauncherControl { HWND window; int page; } LauncherControl;
/* A connected display, for the Screen choice. number is the N of \\.\DISPLAYN,
 * which is usually the number Windows shows when it identifies displays. */
typedef struct LauncherScreen {
    char device[32], name[128];
    int number, width, height, hertz, primary;
} LauncherScreen;
enum { SCREEN_MAIN = 0, SCREEN_MISSING = 99, LAUNCHER_SCREENS = 16 };
typedef struct Launcher {
    HWND window, tabs;
    HFONT font, heading, section;
    HBRUSH accent, tab_idle;
    LauncherControl controls[160];
    int control_count, page, result, done, smoke, capture, capture_armed, group;
    int page_shift;                 /* pixels page content moves down to clear the tab bar */
    WORD previous_pad[4];
    int pad_seen[4];
    int dualsense_count;
    ULONGLONG device_poll_time;
    float scale;
    int help_id;
    MM3LauncherConfig config;
    MM3DisplaySettings display;
    LauncherScreen screens[LAUNCHER_SCREENS];
    int screen_count;
    UpdateJob *update_job;          /* the step in flight, if any */
    int close_after_update;         /* Cancel or Close was pressed while it ran */
    MM3Update update_found;
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
/* Two options switch off a retail HUD element. Their boxes offer hiding it,
 * so on both mods tabs a tick always means a change from the Xbox game. */
static int option_inverted(int option)
{ return option == MM3_LAUNCH_NORTH_MARKER || option == MM3_LAUNCH_RACE_ARROW; }
static const char *option_caption(int option)
{
    if (option == MM3_LAUNCH_NORTH_MARKER) return "Hide the minimap compass pins";
    if (option == MM3_LAUNCH_RACE_ARROW) return "Hide the race direction arrow";
    return mm3_launcher_options[option].label;
}
/* The two choices at the top of the mods tabs. */
static int radio_choice(int id)
{
    return id == ID_ORIGINAL || id == ID_OPTION + MM3_LAUNCH_IMPROVED ||
           id == ID_XBOX_GAMEPLAY || id == ID_OPTION + MM3_LAUNCH_GAMEPLAY;
}
static void help_show(Launcher *ui, int id);
static LRESULT CALLBACK help_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                  UINT_PTR subclass, DWORD_PTR reference)
{
    Launcher *ui = (Launcher *)reference;
    HWND field = window;
    (void)subclass;
    while (GetParent(field) && GetParent(field) != ui->window) field = GetParent(field);
    if (message == WM_SETFOCUS || (message == WM_MOUSEMOVE && ui->help_id != GetDlgCtrlID(field)))
        help_show(ui, GetDlgCtrlID(field));
    return DefSubclassProc(window, message, wparam, lparam);
}
static HWND add(Launcher *ui, int page, const char *kind, const char *text,
                DWORD style, int id, int x, int y, int width, int height)
{
    if (page >= 0) y += 27; /* Leave the tab headers above page content. */
    x = (int)lroundf((float)x * ui->scale); y = (int)lroundf((float)y * ui->scale);
    if (page >= 0) y += ui->page_shift;
    width = (int)lroundf((float)width * ui->scale); height = (int)lroundf((float)height * ui->scale);
    HWND child = CreateWindowExA(!strcmp(kind, "EDIT") ? WS_EX_CLIENTEDGE : 0,
        kind, text, WS_CHILD | (page == 0 || page < 0 ? WS_VISIBLE : 0) | style,
        x, y, width, height, ui->window, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    if (child) {
        SendMessageA(child, WM_SETFONT, (WPARAM)ui->font, TRUE);
        if (id > 0 && id < ID_HELP_TITLE) {
            SetWindowSubclass(child, help_proc, 2, (DWORD_PTR)ui);
            if (!strcmp(kind, "COMBOBOX")) {
                COMBOBOXINFO info = {sizeof info};
                if (GetComboBoxInfo(child, &info) && info.hwndItem != child)
                    SetWindowSubclass(info.hwndItem, help_proc, 2, (DWORD_PTR)ui);
            }
        } else if (id == ID_REQUIRED || (id >= ID_OPTION && id < ID_OPTION + MM3_LAUNCH_OPTION_COUNT))
            SetWindowSubclass(child, help_proc, 2, (DWORD_PTR)ui);
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

static BOOL CALLBACK screen_found(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data)
{
    Launcher *ui = (Launcher *)data;
    LauncherScreen *screen;
    MONITORINFOEXA info;
    DISPLAY_DEVICEA device;
    DEVMODEA mode;
    const char *digits;
    (void)dc; (void)rect;
    if (ui->screen_count >= LAUNCHER_SCREENS) return FALSE;
    memset(&info, 0, sizeof info);
    info.cbSize = sizeof info;
    if (!GetMonitorInfoA(monitor, (MONITORINFO *)&info) || !mm3_display_monitor_valid(info.szDevice))
        return TRUE;
    screen = &ui->screens[ui->screen_count];
    memset(screen, 0, sizeof *screen);
    snprintf(screen->device, sizeof screen->device, "%s", info.szDevice);
    screen->primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    screen->width = info.rcMonitor.right - info.rcMonitor.left;
    screen->height = info.rcMonitor.bottom - info.rcMonitor.top;
    memset(&mode, 0, sizeof mode);
    mode.dmSize = sizeof mode;
    if (EnumDisplaySettingsA(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        screen->width = (int)mode.dmPelsWidth;
        screen->height = (int)mode.dmPelsHeight;
        screen->hertz = mode.dmDisplayFrequency > 1 ? (int)mode.dmDisplayFrequency : 0;
    }
    memset(&device, 0, sizeof device);
    device.cb = sizeof device;
    snprintf(screen->name, sizeof screen->name, "%s",
             EnumDisplayDevicesA(info.szDevice, 0, &device, 0) && device.DeviceString[0]
             ? device.DeviceString : "A display");
    for (digits = info.szDevice + strlen(info.szDevice);
         digits > info.szDevice && isdigit((unsigned char)digits[-1]); --digits) {}
    screen->number = *digits ? atoi(digits) : ui->screen_count + 1;
    ui->screen_count++;
    return TRUE;
}

/* The connected displays, in the order Windows numbers them. */
static void screens_find(Launcher *ui)
{
    ui->screen_count = 0;
    EnumDisplayMonitors(NULL, NULL, screen_found, (LPARAM)ui);
    for (int i = 1; i < ui->screen_count; ++i)
        for (int j = i; j > 0 && ui->screens[j].number < ui->screens[j - 1].number; --j) {
            LauncherScreen swap = ui->screens[j];
            ui->screens[j] = ui->screens[j - 1];
            ui->screens[j - 1] = swap;
        }
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

/* Read the controls, not the saved configuration: the guide follows edits
 * immediately, before Play validates and commits the user's preferences. */
static void help_show(Launcher *ui, int id)
{
    const char *title = NULL, *body = NULL, *effect = NULL;
    char value[640] = "", detail[640] = "", consequence[512] = "";
    int selected;
    if (!control(ui, ID_HELP_TITLE)) return;
    if (id >= ID_OPTION && id < ID_OPTION + MM3_LAUNCH_OPTION_COUNT) {
        static const char *const descriptions[MM3_LAUNCH_OPTION_COUNT] = {
            "Use keyboard and mouse bindings for player 1 alongside a controller. Inputs work while the game window has focus.",
            "Fit the driving view and HUD to the selected aspect ratio. Off uses the original 4:3 presentation, regardless of the aspect-ratio choice.",
            "Enable the object-detail and building-distance multipliers on Settings. Off uses the original Xbox distances, even if those values are higher than 1x.",
            "Smooth the enlarged panorama of distant buildings to reduce blocky skyline edges. This affects the horizon artwork, separately from distance fog.",
            "Add illumination around vehicle lamps and on the road. Front lights activate in evening/night; morning/afternoon rear-light spill is small and faint. Off uses retail vehicle lighting.",
            "Add warm, local illumination around street lamps, with source-sized reach. Keeps the lamps' placement, activation and breakage. Off uses retail lamp lighting.",
            "Adjust world and local-light composition. Daylight keeps its authored colors; default night fill is darker so lamps stand out. Softens overlapping light fields. Off uses retail composition.",
            "Use scene depth to hide lamp flares behind cars and buildings. Off can leave bright flares visible through solid objects.",
            "Changes the gameplay. Make all installed vehicles, including hidden bonus cars, selectable without earning them. This adds no downloads and does not complete career missions.",
            "Allow the game's developer scripts and commands. The developer HUD, menu, camera modes and E3 preset below depend on this switch and on the scripts prepared by tools\\debug-features\\prepare.py; they invoke the game's own developer commands.",
            "Show the game's diagnostic HUD at race start, including position and vehicle data. Requires developer tools and prepared scripts.",
            "Open the game's developer menu at race start. Its experimental commands can change the current scene and do not pause the mission timer. Requires developer tools and prepared scripts.",
            "Enable additional camera modes through the game's developer commands. Requires developer tools and prepared scripts.",
            "Run the original E3 demonstration settings, including camera, LOD and streaming-sound changes. It can replace other visual choices. Requires developer tools and prepared scripts.",
            "Show an animated spinner when new game frames stop briefly during loading. It is an activity indicator, not a percentage or estimate of remaining load time. Hidden during movies.",
            "Use scene depth for distance haze and a smoother horizon transition. Off restores the game's original fog and horizon path.",
            "Run the game's sound-processor programs for effects such as reverb, spatial processing and final mixing. Off uses a simpler voice mix.",
            "Enable System Link networking for local multiplayer and configured UDP peers. This does not provide Xbox Live sign-in. Leave off for ordinary offline play.",
            "Hide the compass pins on the edge of the round minimap: the N letter and the east, south and west pins, which turn with the map. Off shows them, as on the Xbox.",
            "Changes the gameplay. Hide the game's yellow arrow at the top of the screen, which points toward the next checkpoint in races. Off shows it, as on the Xbox. With the route line on, the minimap still shows the way.",
            "Changes the gameplay. Draw the route to the next checkpoint or job target on the race map, like a navigation system, along the roads the course takes. Off shows the map without a drawn route, as on the Xbox.",
            "Changes the gameplay. A car close behind another pulls a little harder on the throttle: up to about 6 km/h more each second at high speed. Never while touching the car in front or after a crash. You and the AI alike. Off (default): no slipstream, as in the original game.",
            "Changes the gameplay. Some original cars could roll over in hard corners. Once a car on its wheels leans past 20 degrees, its roll is damped and eased back; crashes, jumps and upside-down cars are left alone. Off (default): original handling.",
            "In circuit races the opponents may drive any installed car, DLC and extra cars included, and the rival takes the best car of a faster group than yours. Off keeps the opponents to the original cars. The original races always give the opponents the Xbox cars.",
            "Play with the visual and audio mods on this tab: better lighting, longer view distance, exhaust smoke, the in-game Graphics menu and more. The closest chase camera also sits a little farther back. Original look shows the Xbox visuals, camera, menus and HUD, with only the PC graphics and audio upgrades. Nothing on this tab changes the gameplay.",
            "Add Graphics Settings to the game's own main and pause menus, to change resolution, quality and the HUD without leaving the game. Off: the menus are as on the Xbox; F10 still opens the settings window.",
            "Changes the gameplay. Add Circuit mode to Single Player: lap races through Washington DC and Paris, with AI opponents. Off: the original modes only.",
            "Changes the gameplay. Show the cars you added with the installer's Car Workshop in the vehicle menu, to drive them yourself. Off shows only the Xbox cars and the official downloadable ones.",
            "Give the downloadable Blitz and Checkpoint races their own names, descriptions and course pictures, in the game's language. Off: they appear as the Xbox showed them.",
            "Changes the gameplay. Small trees and lamp posts tip over and fall when a car hits them hard. Off: they react as on the Xbox.",
            "Cars breathe soft exhaust smoke from their own exhaust pipes: a light haze standing still, more of it and leaving the pipe faster the faster they go and the harder they accelerate. It drifts in the air, spreads and fades away.",
            "Play with the gameplay mods ticked on this tab. Xbox gameplay drives, races and progresses exactly as the Xbox game: no driving assists, falling trees, extra races or cars, unlocks, developer commands or route line, and the game's own race arrow and minimap.",
            "Put Quit in the main menu, in place of Sign In (Xbox Live, which no longer exists). Quit closes the game, as closing its window does. Off shows Sign In, as on the Xbox.",
            "Changes the gameplay. In Checkpoint and circuit races every opponent drives the same car as you, whichever car you pick. Career jobs keep their own cars. Off: the opponents get the game's own cars.",
            "In a race, the Free camera key flies the view away from your car: W/A/S/D or the left stick move, Q/E or the triggers go down and up, the arrows, right stick or mouse with its right button look. Shift/RB faster, Ctrl/LB slower. Your car gets no input meanwhile.",
            "Changes the gameplay. While playing, type howhardcanitbe and a digit, 0 to 9, to change the Opponents' difficulty until you quit; a chime confirms it. Off: typing does nothing more than press the keys' controller buttons."
        };
        int option = id - ID_OPTION, on = checked(ui, id), master = mm3_launcher_option_master(option);
        title = option_caption(option);
        body = descriptions[option];
        snprintf(value, sizeof value, "%s", on ? "On" : "Off");
        effect = "Applies when you press Play. Your choice is saved for the next launch.";
        if (option == MM3_LAUNCH_IMPROVED || option == MM3_LAUNCH_GAMEPLAY)
            effect = "Every option on this tab can still be turned on or off one by one.";
        else if (master == MM3_LAUNCH_IMPROVED && !checked(ui, ID_OPTION + master))
            effect = option_inverted(option) ? "The Original look is selected, so the game shows it, as on the Xbox. Choose Improved look to hide it." :
                     "The Original look is selected, so this mod stays off. Choose Improved look to use it.";
        else if (master == MM3_LAUNCH_GAMEPLAY && !checked(ui, ID_OPTION + master))
            effect = option_inverted(option) ? "Xbox gameplay is selected, so the game shows it, as on the Xbox. Choose Modded gameplay to hide it." :
                     "Xbox gameplay is selected, so this stays off, as on the Xbox. Choose Modded gameplay to use it.";
        else if (option == MM3_LAUNCH_DSP)
            effect = MM3_LAUNCHER_AUDIO ? (on ? "More accurate effects and mixing, with higher CPU use. Turn off if audio processing limits performance." : "Uses less CPU, with simpler audio effects and mixing.") : "This build does not include the sound-processor option.";
        else if (option >= MM3_LAUNCH_DEV_HUD && option <= MM3_LAUNCH_E3)
            effect = checked(ui, ID_OPTION + MM3_LAUNCH_DEV_TOOLS) ? "Needs the optional prepared developer scripts. These are experimental game commands." : "Enable retail developer tools first. Prepared developer scripts are also required.";
        else if (option == MM3_LAUNCH_NETWORK)
            effect = MM3_LAUNCHER_NETWORK ? "For two copies on one PC, use separate instances, saves and local UDP ports. Other machines can use the same port." : "This build does not include networking.";
    } else {
        GetWindowTextA(control(ui, id), value, sizeof value);
        switch (id) {
        case ID_RESOLUTION:
            title = "Window size";
            body = "Size of the game area in windowed mode. Fullscreen fills the current monitor at its native size. Automatic internal resolution also follows the fitted game area.";
            effect = combo_value(control(ui, ID_MODE)) ? "Fullscreen is selected, so this size is used when you return to windowed mode." : "A larger window takes more screen space. With Automatic internal resolution it also increases GPU work.";
            break;
        case ID_MODE:
            title = "Window mode";
            body = combo_value(control(ui, id)) ? "Borderless fullscreen fills the current monitor at its native size. The Window size setting is retained for windowed mode." : "Play in a movable desktop window at the selected Window size.";
            effect = "F11 or Alt+Enter switches fullscreen on and off while playing.";
            break;
        case ID_SCREEN: {
            int at = combo_value(control(ui, id));
            const LauncherScreen *screen = NULL;
            title = "Screen";
            body = "The display the game opens on. Fullscreen fills that screen at its native size; a window opens centred on it. Windows main display follows whichever display Windows uses as its main one.";
            if (at >= 1 && at <= ui->screen_count) screen = &ui->screens[at - 1];
            for (int i = 0; !screen && at == SCREEN_MAIN && i < ui->screen_count; ++i)
                if (ui->screens[i].primary) screen = &ui->screens[i];
            if (at == SCREEN_MISSING)
                effect = "That screen is not connected now, so the game opens on the main display until it is.";
            else if (screen) {
                char rate[24] = "";
                if (screen->hertz) snprintf(rate, sizeof rate, " at %d Hz", screen->hertz);
                snprintf(consequence, sizeof consequence, "%s: Screen %d, %s, %d x %d%s.",
                         at == SCREEN_MAIN ? "Now" : "Selected", screen->number, screen->name,
                         screen->width, screen->height, rate);
                effect = consequence;
            } else
                effect = "F11 or Alt+Enter still switches fullscreen on the screen the window is on.";
            break;
        }
        case ID_ASPECT:
            title = "Aspect ratio";
            body = !strcmp(value, "auto") ? "Auto matches the game's view to the actual window or fullscreen area. Resizing the window updates the view's shape." : "Choose the shape of the game view. 4:3 is the original Xbox shape; 16:9 is widescreen, 16:10 is slightly taller, and 21:9 is ultrawide. Unused space is letterboxed when shapes differ.";
            effect = checked(ui, ID_OPTION + MM3_LAUNCH_WIDESCREEN) ? "Wider ratios show more at the sides. The widescreen mod keeps the HUD correctly placed." : "Widescreen view and HUD is off, so the game will use 4:3. Enable that mod to use this choice.";
            break;
        case ID_FPS: {
            char *end;
            long fps = strtol(value, &end, 10);
            title = "Maximum FPS";
            body = "Limit gameplay frame presentation to 1-60 frames per second. This is a ceiling, not a guarantee that the PC can reach that rate. Movies keep their own frame rate.";
            if (end == value || *end || fps < 1 || fps > 60)
                effect = "Enter a whole number from 1 to 60 before pressing Play.";
            else {
                snprintf(consequence, sizeof consequence, "%ld FPS allows about %.1f ms per frame. Higher values look smoother but need more CPU/GPU work; lower values reduce demand.", fps, 1000.0 / (double)fps);
                effect = consequence;
            }
            break;
        }
        case ID_VSYNC:
            title = "VSync"; snprintf(value, sizeof value, "%s", checked(ui, id) ? "On" : "Off");
            body = "Synchronize presentation with the monitor to reduce screen tearing. The Maximum FPS limit still applies.";
            effect = checked(ui, id) ? "Can add input delay or reduce the achieved frame rate when rendering misses a refresh." : "May respond sooner, but moving scenes can show tearing.";
            break;
        case ID_SCALE: {
            int scale = combo_value(control(ui, id));
            title = "Internal resolution";
            body = "Resolution used to render the 3D scene before fitting it into the window. Higher settings sharpen edges and distant detail; they do not enlarge the window.";
            if (scale == 0) {
                const char *setting = getenv("RECOMP_SUPERSAMPLE");
                int samples = setting ? atoi(setting) : 2;
                if (samples < 1) samples = 1; if (samples > 4) samples = 4;
                snprintf(consequence, sizeof consequence, "Automatic currently uses %dx the fitted window width and height, then downsamples (480-3840 vertical lines). At 2x this is about four times the pixels.", samples);
            } else {
                snprintf(consequence, sizeof consequence, "Renders about %d vertical lines. Doubling this resolution draws roughly four times the pixels and needs more GPU time and memory.", (int)lroundf((float)scale * .48f));
            }
            effect = consequence;
            break;
        }
        case ID_MSAA:
            title = "Anti-aliasing";
            body = "Smooth polygon edges such as car outlines and building silhouettes. This is separate from internal resolution and texture filtering.";
            selected = combo_value(control(ui, id));
            if (selected == 1) effect = "Off uses less GPU memory and work, with more visible jagged edges.";
            else { snprintf(consequence, sizeof consequence, "%dx MSAA uses more edge samples. Higher values smooth edges further and increase GPU memory and rendering cost.", selected); effect = consequence; }
            break;
        case ID_ANISO:
            title = "Texture filtering";
            body = "Keep textures sharper when viewed at a shallow angle, especially road markings and surfaces stretching into the distance.";
            selected = combo_value(control(ui, id));
            if (selected == 1) effect = "1x uses basic filtering. Angled road textures can look blurrier.";
            else { snprintf(consequence, sizeof consequence, "%dx anisotropic filtering preserves more detail at angles. Higher values may cost some GPU time.", selected); effect = consequence; }
            break;
        case ID_DRAW: case ID_VIEW:
            title = id == ID_DRAW ? "Object detail distance" : "Building view distance";
            body = id == ID_DRAW ? "Keep detailed objects, pedestrian models and blob shadows visible farther away. 1x uses the Xbox distances; 2x-4x extend them. This does not set the city's building visibility range." : "Extend city/building visibility and the camera's far range. 1x uses the Xbox range. Streamed city cells limit how much extra geometry is available; facade detail extends only up to 2x.";
            effect = !checked(ui, ID_OPTION + MM3_LAUNCH_IMPROVED) ? "The Original look is selected (Visual + audio mods tab), so the Xbox distances (1x) are used." :
                     checked(ui, ID_OPTION + MM3_LAUNCH_DISTANCE) ? (id == ID_DRAW ? "Higher values increase scene and CPU/GPU work. Lower this if dense streets reduce performance." : "Higher values can increase rendering work; 3x/4x may not reveal more buildings where the streamed-city limit is reached.") : "Extended detail and building distance is off. Both distances will use 1x until you enable that mod.";
            break;
        case ID_SHOW_FPS:
            title = "Show FPS"; snprintf(value, sizeof value, "%s", checked(ui, id) ? "On" : "Off");
            body = "Display the current frame rate in the game window's title bar. This is a readout and does not change the Maximum FPS limit.";
            effect = "Useful for comparing settings. The title bar is normally hidden in fullscreen.";
            break;
        case ID_MINIMAP:
            title = "Minimap style";
            body = combo_value(control(ui, id)) ? "Draw transparent roads in perspective, with framing that widens with driving speed, so more of the map shows at speed." : "Use the original Xbox minimap appearance and framing.";
            effect = !checked(ui, ID_OPTION + MM3_LAUNCH_IMPROVED) ? "The Original look is selected (Visual + audio mods tab), so the original Xbox minimap is used." :
                     !checked(ui, ID_OPTION + MM3_LAUNCH_GAMEPLAY) ? "Xbox gameplay is selected (Gameplay mods tab). The improved minimap shows more of the map at speed, so the original Xbox minimap is used." :
                     "Choose the style you find easier to read. Mission goals and the actual road layout are unchanged.";
            break;
        case ID_FOV: {
            char *end;
            long degrees = strtol(value, &end, 10);
            title = "Horizontal field of view";
            body = "Set the horizontal angle seen by the driving camera. 0 keeps the original camera framing with the selected aspect-ratio correction. Custom values must be 60-110 degrees.";
            if (end == value || *end || degrees < 0 || degrees > 110 || (degrees && degrees < 60))
                effect = "Enter 0 for retail framing, or a whole number from 60 to 110.";
            else effect = degrees ? "Higher angles show more at the sides, make objects appear smaller and increase edge distortion. Lower angles zoom in. Menus and mirrors keep their own cameras." : "Uses the game's own camera framing. Set a value from 60 to 110 to choose a fixed horizontal angle.";
            break;
        }
        case ID_PORT:
            title = "Local UDP port";
            body = "Network port used by this copy for System Link peer traffic. The default is 3075. Other players use this port when adding this copy as a peer.";
            effect = "Used with System Link enabled. Enter 1-65535; two simultaneous copies on one PC need different ports.";
            break;
        case ID_PEERS:
            title = "System Link peers";
            if (!*value) snprintf(value, sizeof value, "Empty (no configured peer destinations)");
            else {
                int peers = 1;
                for (const char *at = value; *at; ++at) if (*at == ',') ++peers;
                snprintf(value, sizeof value, "%d configured peer destination%s", peers, peers == 1 ? "" : "s");
            }
            body = "Add up to eight remote host:port destinations, separated by commas. Example: 192.168.1.20:3075,192.168.1.21:3075. An empty field leaves the peer-destination list empty.";
            effect = "Used with System Link enabled. Each port must match that remote copy's local UDP port. These are game-network destinations, not Xbox Live accounts.";
            break;
        case ID_KEYBOARD:
            title = "Keyboard controls"; snprintf(value, sizeof value, "%s", checked(ui, id) ? "On" : "Off");
            body = "Use keyboard and mouse bindings for player 1 while the game window has focus. A controller can still be used alongside them.";
            effect = checked(ui, id) ? "Edit Xbox-button bindings or driving shortcuts below. F10 and F11 remain reserved for graphics settings and fullscreen." : "Keyboard gameplay bindings are disabled. Controllers and reserved window shortcuts remain available.";
            break;
        case ID_PAD_COUNT:
            title = "Controllers in game";
            body = "Expose 1-4 Xbox controller ports to the game. This is separate from the connected-controller status: choosing 4 does not create four physical controllers.";
            effect = "Physical controller 1 feeds player 1, controller 2 feeds player 2, and so on. Keyboard input belongs to player 1.";
            break;
        case ID_GROUP:
            title = "Binding group";
            body = ui->group == 0 ? "Map physical controller buttons and triggers to Xbox inputs. The same mapping is used by all four players; stick axes keep their analog movement." : ui->group == 1 ? "Map keyboard keys or mouse buttons to Xbox buttons and stick directions. These generic Xbox inputs are interpreted by the game's current control layout." : "Map direct driving shortcuts for acceleration, braking, steering, gears, handbrake and camera. They take priority over colliding generic keyboard bindings while driving.";
            effect = "Select an action in the list, then capture an input or clear that action. Changes are saved only when you press Play.";
            break;
        case ID_BINDINGS: case ID_SOURCE: case ID_CAPTURE: case ID_UNBIND: case ID_PAD_PORT: {
            int action = (int)SendMessageA(control(ui, ID_BINDINGS), LB_GETCURSEL, 0, 0);
            char binding[80];
            if (action < 0) action = 0;
            title = id == ID_PAD_PORT ? "Controller used for capture" : id == ID_CAPTURE ? "Capture a binding" : id == ID_UNBIND ? "Clear selected binding" : "Selected binding";
            if (ui->group == 0) {
                snprintf(value, sizeof value, "Xbox %s <- %s", mm3_launcher_pad_names[action], mm3_launcher_pad_sources[ui->config.pad[action]]);
                body = "Choose the physical button or trigger that will produce this Xbox input. NONE unbinds it. The mapping applies equally to all players; stick axes remain analog.";
                effect = "Triggers keep analog pressure for analog inputs. For digital actions they activate after about one-third travel.";
            } else {
                key_name(ui->group == 1 ? ui->config.keys[action] : ui->config.drive_keys[action], binding, sizeof binding);
                snprintf(value, sizeof value, "%s <- %s", ui->group == 1 ? key_labels[action] : drive_labels[action], binding);
                body = ui->group == 1 ? "This key or mouse button produces the selected Xbox input. Its game action follows the in-game control layout." : "This key or mouse button activates the selected driving shortcut. It takes priority over a generic Xbox-key binding on the same key while driving.";
                effect = "Capture accepts a key or mouse button. Escape cancels capture; F10 and F11 are reserved.";
            }
            if (id == ID_PAD_PORT) {
                GetWindowTextA(control(ui, id), value, sizeof value);
                body = "Choose which physical controller to listen to when capturing a button or trigger. This affects capture only, not which player uses that controller.";
                effect = "Release held buttons, choose Capture, then press a button on this controller. The resulting mapping is shared by all players.";
            } else if (id == ID_UNBIND)
                effect = "Clears only this action. Other bindings stay as they are; Play saves the edit and Cancel discards it.";
            else if (id == ID_CAPTURE)
                effect = ui->capture ? "Listening now. Press the desired input, or Escape to cancel." : "Start capture, then press the desired input. Release controller buttons before capturing. Escape cancels.";
            break;
        }
        case ID_ORIGINAL:
            title = "Original look"; snprintf(value, sizeof value, "%s", checked(ui, id) ? "On" : "Off");
            body = "The Xbox visuals, camera, menus and HUD, at your PC's resolution and frame rate, with the PC graphics and audio upgrades: widescreen, sharper rendering, native fog and the accurate sound processor. No new lighting, effects or menus.";
            effect = "The Improved visual mods stay off. Your choices there are kept for when you choose Improved look again. The gameplay is set on the Gameplay mods tab.";
            break;
        case ID_XBOX_GAMEPLAY:
            title = "Xbox gameplay"; snprintf(value, sizeof value, "%s", checked(ui, id) ? "On" : "Off");
            body = "Drive, race and progress exactly as in the Xbox game: its physics and damage, its cars for you and the opponents, its races, its unlocks, its race arrow and its minimap. The PC port's fixes that restore Xbox behaviour stay on.";
            effect = "Every gameplay mod stays off. Your choices there are kept for when you choose Modded gameplay again.";
            break;
        case ID_DAMAGE:
            title = "Car damage";
            selected = combo_value(control(ui, id));
            body = "How much damage each crash does, to every car: yours, the opponents', the traffic's and the cars a job asks you to wreck. 100% is the Xbox game.";
            effect = !checked(ui, ID_OPTION + MM3_LAUNCH_GAMEPLAY) ? "Xbox gameplay is selected, so crashes do the Xbox damage (100%). Choose Modded gameplay to change it." :
                     selected == 0 ? "No car is ever damaged or wrecked, so jobs where you must wreck a car cannot be won." :
                     selected < 100 ? "Cars take less damage: yours survives more crashes, and a car you must wreck needs more hits." :
                     selected > 100 ? "Cars take more damage: yours wrecks sooner, and a car you must wreck goes down faster." :
                     "Crashes do the Xbox damage.";
            break;
        case ID_OWN_DAMAGE:
            title = "Your car's damage";
            selected = combo_value(control(ui, id));
            body = "How much damage your own car takes, on top of Car damage. Only your car: the opponents, traffic and the cars a job asks you to wreck keep Car damage. 100% is the Xbox game.";
            effect = !checked(ui, ID_OPTION + MM3_LAUNCH_GAMEPLAY) ? "Xbox gameplay is selected, so your car takes the Xbox damage (100%). Choose Modded gameplay to change it." :
                     selected == 0 ? "Your car is never damaged or wrecked, whatever it hits." :
                     selected < 100 ? "Your car takes less damage and survives more crashes." :
                     selected > 100 ? "Your car takes more damage and wrecks sooner." :
                     "Your car takes the same damage as every other car.";
            break;
        case ID_AI_LEVEL:
            title = "Opponents' difficulty";
            selected = combo_value(control(ui, id));
            body = "How hard the race opponents drive, 0 to 9; 5 is the Xbox game. Higher: they accelerate and brake harder, brake later, corner a little faster and no longer wait for you in the first races. Lower: slower and more careful. Police and traffic are unchanged.";
            effect = !checked(ui, ID_OPTION + MM3_LAUNCH_GAMEPLAY) ? "Xbox gameplay is selected, so the opponents drive as on the Xbox (5). Choose Modded gameplay to change it." :
                     selected > 5 ? "Harder races: opponents are quicker everywhere. At 8 and 9 they may overshoot some corners." :
                     selected < 5 ? "Easier races: opponents are slower and give you more room." :
                     "The opponents drive as on the Xbox.";
            break;
        case ID_CAMERA_KEY:
            title = "Free camera key";
            body = "The key that turns the free camera on and off during a race. The camera stays within 400 m of your car, where the city is loaded.";
            effect = !checked(ui, ID_OPTION + MM3_LAUNCH_GAMEPLAY) ? "Xbox gameplay is selected, so there is no free camera. Choose Modded gameplay to use it." :
                     !checked(ui, ID_OPTION + MM3_LAUNCH_FREE_CAMERA) ? "The free camera is off. Tick Free camera to use this key." :
                     "Choose a key you have not bound to a controller button on the Controls tab.";
            break;
        case ID_NIGHT:
            title = "Night brightness";
            selected = combo_value(control(ui, id));
            body = "How bright the city is at night where no lamp, headlight or window lights it: the share of the game's own night light. Street lamps, headlights and lit windows stay as they are. F10 changes it while you play.";
            effect = !checked(ui, ID_OPTION + MM3_LAUNCH_IMPROVED) ? "The Original look is selected, so nights keep the game's own light." :
                     !checked(ui, ID_OPTION + MM3_LAUNCH_WORLD_LIGHT) ? "Improved world lighting is off, so nights keep the game's own light. Tick it to use this choice." :
                     selected >= 100 ? "Nights as bright as on the Xbox: lamps and headlights stand out less." :
                     selected < MM3_NIGHT_LIGHT_DEFAULT ? "Darker nights: streets away from lamps are hard to see, lamps and headlights stand out more." :
                     selected > MM3_NIGHT_LIGHT_DEFAULT ? "Brighter nights: more of the city shows away from lamps." :
                     "The default: dark nights lit mostly by lamps, headlights and windows.";
            break;
        case ID_TIME:
            title = "Time limits";
            selected = combo_value(control(ui, id));
            body = "Scale the time you race against: the time limit of Blitz races and timed Career jobs, and the time each delivery or checkpoint adds. 100% is the Xbox game. The race clock, time penalties and goals such as surviving a set time are unchanged.";
            effect = !checked(ui, ID_OPTION + MM3_LAUNCH_GAMEPLAY) ? "Xbox gameplay is selected, so the Xbox time limits (100%) are used. Choose Modded gameplay to change them." :
                     selected > 100 ? "More time: Blitz races and timed jobs are easier." :
                     selected < 100 ? "Less time: Blitz races and timed jobs are harder, and some may not be possible." :
                     "The Xbox time limits.";
            break;
        case ID_REQUIRED:
            title = "Collision-cell safety repair"; snprintf(value, sizeof value, "Always on");
            body = "Repair decoded collision cells before the game's collision queries use them. This compatibility fix is required for reliable native gameplay.";
            effect = "Always enabled. It is a compatibility repair, not an optional visual effect.";
            break;
        case ID_DEFAULTS:
            title = "Restore defaults"; value[0] = 0;
            body = "Reset the choices in this launcher to the current performance-profile defaults, including graphics, mods and input bindings.";
            effect = "Changes stay in this window until Play saves them. Cancel keeps your previously saved preferences.";
            break;
        case IDOK:
            title = "Ready to play"; value[0] = 0;
            body = "Play validates and saves every tab, then launches the game with these preferences. Changing a control here does not alter a game that is already running.";
            effect = "Hover over, focus or change any setting to see its current value and what it does. In-game Graphics Settings are available with F10.";
            break;
        case ID_UPDATE:
            title = "Check for updates"; value[0] = 0;
            body = "Ask GitHub whether a newer version of Midtown Madness 3 PC has been published. If there is one, the launcher offers to download its Setup, checks the download against the release's checksum, closes and opens Setup so you can install it.";
            effect = "Nothing is changed until you choose to download and then install in Setup. Your saved games, DLC and settings stay. It needs an internet connection.";
            break;
        case IDCANCEL:
            title = "Cancel"; value[0] = 0;
            body = "Close the launcher without saving the edits in this window or starting the game. During input capture, Cancel stops capture first.";
            effect = "Your previously saved preferences remain available for the next launch.";
            break;
        case ID_TABS:
            title = ui->page == 0 ? "Game settings" : ui->page == 1 ? "Controls" : ui->page == 2 ? "Visual and audio mods" : "Gameplay mods"; value[0] = 0;
            body = ui->page == 0 ? "Set the display, rendering quality, minimap, audio and System Link options. Select any setting to read an explanation and its performance tradeoffs." :
                   ui->page == 1 ? "Set up keyboard, mouse and controller inputs. Select a binding to see what it currently uses and how capture works." :
                   ui->page == 2 ? "Choose the Original look or the Improved look, then pick the visual and audio mods you want. None of them changes the gameplay." :
                   "Choose Xbox gameplay or Modded gameplay, then pick the gameplay mods you want. With Xbox gameplay the game plays as on the Xbox.";
            effect = "Every choice is saved when you press Play. Cancel discards this launcher's edits.";
            break;
        default: return;
        }
    }
    ui->help_id = id;
    if (*value) snprintf(detail, sizeof detail, "Selected: %s", value);
    SetWindowTextA(control(ui, ID_HELP_TITLE), title);
    SetWindowTextA(control(ui, ID_HELP_VALUE), detail);
    SetWindowTextA(control(ui, ID_HELP_BODY), body);
    SetWindowTextA(control(ui, ID_HELP_EFFECT), effect);
}

static void page_show(Launcher *ui, int page)
{
    ui->page = page;
    ui->capture = 0;
    for (int i = 0; i < ui->control_count; ++i)
        if (ui->controls[i].page >= 0)
            ShowWindow(ui->controls[i].window, ui->controls[i].page == page ? SW_SHOW : SW_HIDE);
    TabCtrl_SetCurSel(ui->tabs, page);
    help_show(ui, ID_TABS);
    InvalidateRect(ui->window, NULL, TRUE);
}
static void bindings_refresh(Launcher *ui);
static void mods_enable(Launcher *ui);
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
    help_show(ui, ID_BINDINGS);
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
    { /* A saved screen that is not connected stays chosen, named as such. */
      HWND screen = control(ui, ID_SCREEN);
      int at = SCREEN_MAIN;
      for (int i = (int)SendMessageA(screen, CB_GETCOUNT, 0, 0) - 1; i >= 0; --i)
          if ((int)SendMessageA(screen, CB_GETITEMDATA, i, 0) == SCREEN_MISSING)
              SendMessageA(screen, CB_DELETESTRING, i, 0);
      for (int i = 0; i < ui->screen_count; ++i)
          if (!_stricmp(ui->screens[i].device, s->monitor)) at = i + 1;
      if (at == SCREEN_MAIN && strcmp(s->monitor, "primary")) {
          snprintf(text, sizeof text, "%s (not connected)", s->monitor + 4);
          combo_item(screen, text, SCREEN_MISSING);
          at = SCREEN_MISSING;
      }
      combo_select(screen, at); }
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
    if (combo_value(control(ui, ID_ANISO)) < 0) {
        snprintf(text, sizeof text, "%dx", s->aniso);
        combo_item(control(ui, ID_ANISO), text, s->aniso);
        combo_select(control(ui, ID_ANISO), s->aniso);
    }
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
    combo_select(control(ui, ID_NIGHT), s->night_light);
    if (combo_value(control(ui, ID_NIGHT)) != s->night_light) { /* a saved value not in the list stays chosen */
        snprintf(text, sizeof text, "%d%%", s->night_light);
        combo_item(control(ui, ID_NIGHT), text, s->night_light);
        combo_select(control(ui, ID_NIGHT), s->night_light);
    }
    set_checked(ui, ID_KEYBOARD, ui->config.option[MM3_LAUNCH_KEYBOARD]);
    combo_select(control(ui, ID_PAD_COUNT), ui->config.controller_count);
    for (int i = 1; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        set_checked(ui, ID_OPTION + i, option_inverted(i) ? !ui->config.option[i] : ui->config.option[i]);
    set_checked(ui, ID_ORIGINAL, !ui->config.option[MM3_LAUNCH_IMPROVED]);
    set_checked(ui, ID_XBOX_GAMEPLAY, !ui->config.option[MM3_LAUNCH_GAMEPLAY]);
    mods_enable(ui);
    snprintf(text, sizeof text, "%d", ui->config.fov_degrees); SetWindowTextA(control(ui, ID_FOV), text);
    combo_select(control(ui, ID_AI_LEVEL), ui->config.ai_level);
    combo_select(control(ui, ID_CAMERA_KEY), ui->config.free_camera_key);
    for (int i = 0; i < 3; ++i) { /* a saved value not in the list stays chosen */
        HWND percent = control(ui, i == 2 ? ID_TIME : i ? ID_OWN_DAMAGE : ID_DAMAGE);
        int value = i == 2 ? ui->config.time_percent : i ? ui->config.own_damage_percent : ui->config.damage_percent;
        combo_select(percent, value);
        if (combo_value(percent) != value) {
            snprintf(text, sizeof text, "%d%%", value);
            combo_item(percent, text, value);
            combo_select(percent, value);
        }
    }
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
    { int at = combo_value(control(ui, ID_SCREEN));
      if (at >= 1 && at <= ui->screen_count)
          snprintf(s->monitor, sizeof s->monitor, "%s", ui->screens[at - 1].device);
      else if (at != SCREEN_MISSING) strcpy(s->monitor, "primary"); }
    s->vsync = checked(ui, ID_VSYNC); s->show_fps = checked(ui, ID_SHOW_FPS);
    s->render_scale = (float)combo_value(control(ui, ID_SCALE)) / 1000.f;
    GetWindowTextA(control(ui, ID_ASPECT), s->aspect, sizeof s->aspect);
    s->msaa = combo_value(control(ui, ID_MSAA)); s->aniso = combo_value(control(ui, ID_ANISO));
    s->draw_distance = (float)combo_value(control(ui, ID_DRAW)) / 1000.f;
    s->view_distance = (float)combo_value(control(ui, ID_VIEW)) / 1000.f;
    s->minimap_improved = combo_value(control(ui, ID_MINIMAP));
    s->night_light = combo_value(control(ui, ID_NIGHT));
    ui->config.option[MM3_LAUNCH_KEYBOARD] = checked(ui, ID_KEYBOARD);
    ui->config.controller_count = combo_value(control(ui, ID_PAD_COUNT));
    ui->config.damage_percent = combo_value(control(ui, ID_DAMAGE));
    ui->config.own_damage_percent = combo_value(control(ui, ID_OWN_DAMAGE));
    ui->config.time_percent = combo_value(control(ui, ID_TIME));
    ui->config.ai_level = combo_value(control(ui, ID_AI_LEVEL));
    ui->config.free_camera_key = combo_value(control(ui, ID_CAMERA_KEY));
    for (int i = 1; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        ui->config.option[i] = option_inverted(i) ? !checked(ui, ID_OPTION + i) : checked(ui, ID_OPTION + i);
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
    c = combo(ui, 0, ID_RESOLUTION, "Window size", 42, 98, 150);
    for (int i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); ++i) {
        char name[48]; snprintf(name, sizeof name, "%d x %d", sizes[i][0], sizes[i][1]);
        combo_item(c, name, sizes[i][0] * 10000 + sizes[i][1]);
    }
    c = combo(ui, 0, ID_MODE, "Window mode", 212, 98, 150);
    combo_item(c, "Windowed", 0); combo_item(c, "Fullscreen", 1);
    c = combo(ui, 0, ID_SCREEN, "Screen", 382, 98, 264);
    combo_item(c, "Windows main display", SCREEN_MAIN);
    screens_find(ui);
    for (int i = 0; i < ui->screen_count; ++i) {
        char name[64];
        snprintf(name, sizeof name, "Screen %d: %d x %d%s", ui->screens[i].number,
                 ui->screens[i].width, ui->screens[i].height, ui->screens[i].primary ? " (main)" : "");
        combo_item(c, name, i + 1);
    }
    c = combo(ui, 0, ID_ASPECT, "Aspect ratio", 666, 98, 150);
    for (int i = 0; i < 5; ++i) {
        static const char *const ratios[] = {"auto", "4:3", "16:9", "16:10", "21:9"};
        SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)ratios[i]);
    }
    label(ui, 0, "Maximum FPS (1-60)", 42, 166, 238);
    c = add(ui, 0, "COMBOBOX", "", CBS_DROPDOWN | WS_TABSTOP, ID_FPS, 42, 190, 110, 120);
    combo_item(c, "30", 30); combo_item(c, "60", 60);
    check(ui, 0, ID_VSYNC, "VSync", 172, 190, 100);
    c = combo(ui, 0, ID_SCALE, "Internal resolution", 310, 166, 238);
    combo_item(c, "Automatic + supersampling", 0); combo_item(c, "480p", 1000);
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
    label(ui, 0, MM3_LAUNCHER_NETWORK ? "Copies on one PC need separate ports and saves. Xbox Live sign-in is separate." : "System Link controls will be enabled when networking is installed.", 42, 552, 774);
}

static void controls_controls(Launcher *ui)
{
    HWND c;
    check(ui, 1, ID_KEYBOARD, "Enable keyboard controls (player 1, alongside a controller)", 42, 94, 774);
    c = combo(ui, 1, ID_PAD_COUNT, "Controllers in game", 42, 130, 345);
    for (int i = 1; i <= 4; ++i) { char text[16]; snprintf(text, sizeof text, "%d", i); combo_item(c, text, i); }
    add(ui, 1, "STATIC", "Detecting controllers...", 0, ID_PAD_STATUS, 417, 130, 399, 92);
    add(ui, 1, "STATIC", "", 0, ID_PAD_HINT, 42, 190, 345, 48);
    c = combo(ui, 1, ID_GROUP, "Bindings", 42, 247, 345);
    combo_item(c, "Controller buttons (all four players)", 0);
    combo_item(c, "Keyboard Xbox buttons", 1); combo_item(c, "Keyboard driving shortcuts", 2);
    combo_select(c, 0);
    add(ui, 1, "LISTBOX", "", LBS_NOTIFY | WS_BORDER | WS_VSCROLL | WS_TABSTOP,
         ID_BINDINGS, 42, 307, 345, 251);
    add(ui, 1, "STATIC", "", 0, ID_BIND_LABEL, 417, 229, 399, 48);
    c = combo(ui, 1, ID_SOURCE, "Controller source", 417, 285, 399);
    for (int i = 0; i < MM3_LAUNCHER_PAD_SOURCES; ++i) combo_item(c, mm3_launcher_pad_sources[i], i);
    c = combo(ui, 1, ID_PAD_PORT, "Controller to use for capture", 417, 350, 399);
    for (int i = 0; i < 4; ++i) { char text[32]; snprintf(text, sizeof text, "Controller %d", i + 1); combo_item(c, text, i); }
    combo_select(c, 0);
    add(ui, 1, "BUTTON", "Capture controller button", BS_PUSHBUTTON | WS_TABSTOP,
         ID_CAPTURE, 417, 414, 399, 32);
    add(ui, 1, "BUTTON", "Clear selected binding", BS_PUSHBUTTON | WS_TABSTOP,
         ID_UNBIND, 417, 458, 399, 30);
    add(ui, 1, "STATIC", "Select an action, then capture a key/button or select a controller source. Escape cancels capture. Stick axes stay analog. Controller bindings apply equally to players 1-4.",
         0, -1, 417, 504, 399, 80);
}
typedef struct OptionColumn { int x, width; const char *title, *subtitle; int options[11]; } OptionColumn;
/* A mods tab's columns of options (0 ends a column, -1 starts its subtitle). */
static void option_columns(Launcher *ui, int page, const OptionColumn *columns, int count)
{
    for (int c = 0; c < count; ++c) {
        int y = 164;
        HWND title = add(ui, page, "STATIC", columns[c].title, 0, -1, columns[c].x, y, columns[c].width, 26);
        SendMessageA(title, WM_SETFONT, (WPARAM)ui->section, TRUE);
        for (int i = 0; i < 11 && columns[c].options[i]; ++i, y += 34) {
            int option = columns[c].options[i];
            if (option < 0) {
                title = add(ui, page, "STATIC", columns[c].subtitle, 0, -1, columns[c].x, y + 40, columns[c].width, 26);
                SendMessageA(title, WM_SETFONT, (WPARAM)ui->section, TRUE);
                y += 6;
                continue;
            }
            check(ui, page, ID_OPTION + option, option_caption(option), columns[c].x, y + 32, columns[c].width);
        }
    }
}

/* The Visual + audio mods tab: the Original or the Improved look first, then
 * three columns. The first keeps the player's choices in both looks; the
 * others are greyed out in the Original look (and as on the Xbox in game,
 * mm3_launcher_config_apply). Nothing on this tab changes the gameplay. */
static void look_controls(Launcher *ui)
{
    static const OptionColumn columns[] = {
        {42, 240, "Graphics and audio", NULL, {MM3_LAUNCH_WIDESCREEN, MM3_LAUNCH_SKY, MM3_LAUNCH_NATIVE_FOG, 0}},
        {300, 250, "Improved visuals", NULL, {MM3_LAUNCH_DISTANCE, MM3_LAUNCH_CAR_LIGHT, MM3_LAUNCH_LAMP_LIGHT,
            MM3_LAUNCH_WORLD_LIGHT, MM3_LAUNCH_FLARE_DEPTH, MM3_LAUNCH_EXHAUST, 0}},
        {560, 260, "Menus and HUD", NULL, {MM3_LAUNCH_LOADING, MM3_LAUNCH_GRAPHICS_MENU, MM3_LAUNCH_QUIT,
            MM3_LAUNCH_DLC_INFO, MM3_LAUNCH_NORTH_MARKER, 0}}
    };
    static const int nights[] = {10, 15, 20, MM3_NIGHT_LIGHT_DEFAULT, 35, 50, 75, 100};
    HWND c;
    check(ui, 2, ID_ORIGINAL, "Original look", 42, 92, 240);
    check(ui, 2, ID_OPTION + MM3_LAUNCH_IMPROVED, mm3_launcher_options[MM3_LAUNCH_IMPROVED].label, 300, 92, 250);
    label(ui, 2, "Both looks keep the PC's graphics and audio upgrades. Nothing here changes gameplay.", 42, 122, 774);
    option_columns(ui, 2, columns, (int)(sizeof columns / sizeof columns[0]));
    c = combo(ui, 2, ID_NIGHT, "Night brightness", 300, 404, 250);
    for (int i = 0; i < (int)(sizeof nights / sizeof nights[0]); ++i) {
        char name[32];
        snprintf(name, sizeof name, "%d%%%s", nights[i], nights[i] == MM3_NIGHT_LIGHT_DEFAULT ? " (default)" :
                 nights[i] == 100 ? " (Xbox night)" : "");
        combo_item(c, name, nights[i]);
    }
    label(ui, 2, "Field of view: 0 = retail, or 60-110", 42, 546, 240);
    add(ui, 2, "EDIT", "0", ES_NUMBER | WS_TABSTOP, ID_FOV, 42, 570, 77, 26);
    label(ui, 2, "Preferences apply when Play starts the game. F10 opens display settings in game.", 145, 573, 680);
}

/* The Gameplay mods tab: Xbox or Modded gameplay first, then every option
 * that changes how the game plays, greyed out with Xbox gameplay (and as on
 * the Xbox in game, mm3_launcher_config_apply). */
static void gameplay_controls(Launcher *ui)
{
    static const OptionColumn columns[] = {
        {42, 240, "Driving", "Developer tools", {MM3_LAUNCH_DRAFTING, MM3_LAUNCH_ROLL_GUARD, MM3_LAUNCH_TREES, -1,
            MM3_LAUNCH_DEV_TOOLS, MM3_LAUNCH_DEV_HUD, MM3_LAUNCH_DEBUG_MENU, MM3_LAUNCH_CAMERAS, MM3_LAUNCH_E3, 0}},
        {300, 250, "Races and cars", NULL, {MM3_LAUNCH_CIRCUITS, MM3_LAUNCH_EXTRA_CARS, MM3_LAUNCH_AI_ALL_CARS,
            MM3_LAUNCH_SAME_CAR, MM3_LAUNCH_UNLOCK, 0}},
        {560, 260, "Navigation", NULL, {MM3_LAUNCH_ROUTE_NAVIGATION, MM3_LAUNCH_RACE_ARROW, 0}}
    };
    static const int damage[] = {0, 25, 50, 75, 100, 150, 200, 300}, time[] = {50, 75, 100, 125, 150, 200, 300};
    HWND fixed, c;
    check(ui, 3, ID_XBOX_GAMEPLAY, "Xbox gameplay", 42, 92, 240);
    check(ui, 3, ID_OPTION + MM3_LAUNCH_GAMEPLAY, mm3_launcher_options[MM3_LAUNCH_GAMEPLAY].label, 300, 92, 250);
    label(ui, 3, "Xbox gameplay plays as the Xbox game did. Modded gameplay uses the mods ticked below.", 42, 122, 774);
    option_columns(ui, 3, columns, (int)(sizeof columns / sizeof columns[0]));
    fixed = add(ui, 3, "STATIC", "Free camera and codes", 0, -1, 300, 384, 250, 26);
    SendMessageA(fixed, WM_SETFONT, (WPARAM)ui->section, TRUE);
    check(ui, 3, ID_OPTION + MM3_LAUNCH_FREE_CAMERA, option_caption(MM3_LAUNCH_FREE_CAMERA), 300, 416, 250);
    c = combo(ui, 3, ID_CAMERA_KEY, "Free camera key", 300, 448, 250);
    for (int i = 0; i < MM3_LAUNCHER_CAMERA_KEYS; ++i) {
        char name[32];
        snprintf(name, sizeof name, "%s%s", mm3_launcher_camera_key_names[i],
                 mm3_launcher_camera_keys[i] == 0x77 ? " (default)" : "");
        combo_item(c, name, mm3_launcher_camera_keys[i]);
    }
    check(ui, 3, ID_OPTION + MM3_LAUNCH_CHEAT_CODES, option_caption(MM3_LAUNCH_CHEAT_CODES), 300, 512, 250);
    fixed = add(ui, 3, "STATIC", "Difficulty", 0, -1, 560, 270, 260, 26);
    SendMessageA(fixed, WM_SETFONT, (WPARAM)ui->section, TRUE);
    c = combo(ui, 3, ID_DAMAGE, "Car damage", 560, 300, 260);
    for (int i = 0; i < (int)(sizeof damage / sizeof damage[0]); ++i) {
        char name[32];
        if (damage[i]) snprintf(name, sizeof name, "%d%%%s", damage[i], damage[i] == 100 ? " (Xbox)" : "");
        else snprintf(name, sizeof name, "No damage");
        combo_item(c, name, damage[i]);
    }
    c = combo(ui, 3, ID_OWN_DAMAGE, "Your car's damage", 560, 356, 260);
    for (int i = 0; i < (int)(sizeof damage / sizeof damage[0]); ++i) {
        char name[32];
        if (damage[i]) snprintf(name, sizeof name, "%d%%%s", damage[i], damage[i] == 100 ? " (Xbox)" : "");
        else snprintf(name, sizeof name, "Invulnerable");
        combo_item(c, name, damage[i]);
    }
    c = combo(ui, 3, ID_TIME, "Time limits", 560, 412, 260);
    for (int i = 0; i < (int)(sizeof time / sizeof time[0]); ++i) {
        char name[32];
        snprintf(name, sizeof name, "%d%%%s", time[i], time[i] == 100 ? " (Xbox)" : "");
        combo_item(c, name, time[i]);
    }
    c = combo(ui, 3, ID_AI_LEVEL, "Opponents' difficulty", 560, 468, 260);
    for (int i = 0; i <= 9; ++i) {
        char name[32];
        snprintf(name, sizeof name, "%d%s", i, i == 0 ? " (easiest)" : i == 5 ? " (Xbox)" : i == 9 ? " (hardest)" : "");
        combo_item(c, name, i);
    }
    fixed = check(ui, 3, ID_REQUIRED, "Collision repair (always on)", 560, 532, 260);
    SendMessageA(fixed, BM_SETCHECK, BST_CHECKED, 0); EnableWindow(fixed, FALSE);
    label(ui, 3, "The port's fixes that restore Xbox behaviour, such as car damage at 60 fps, are always on.", 42, 573, 774);
}

/* Grey out each mod while its tab's Xbox choice is selected (Original look,
 * Xbox gameplay), and the Settings that choice fixes at the Xbox's:
 * distances with the Original look, the minimap (a new look that also shows
 * more of the map at speed) with either. */
static void mods_enable(Launcher *ui)
{
    int improved = checked(ui, ID_OPTION + MM3_LAUNCH_IMPROVED);
    int modded = checked(ui, ID_OPTION + MM3_LAUNCH_GAMEPLAY);
    for (int i = 1; i < MM3_LAUNCH_OPTION_COUNT; ++i) {
        int master = mm3_launcher_option_master(i);
        if (master >= 0) EnableWindow(control(ui, ID_OPTION + i), checked(ui, ID_OPTION + master));
    }
    EnableWindow(control(ui, ID_DRAW), improved);
    EnableWindow(control(ui, ID_VIEW), improved);
    EnableWindow(control(ui, ID_MINIMAP), improved && modded);
    EnableWindow(control(ui, ID_NIGHT), improved && checked(ui, ID_OPTION + MM3_LAUNCH_WORLD_LIGHT));
    EnableWindow(control(ui, ID_DAMAGE), modded);
    EnableWindow(control(ui, ID_OWN_DAMAGE), modded);
    EnableWindow(control(ui, ID_TIME), modded);
    EnableWindow(control(ui, ID_AI_LEVEL), modded);
    EnableWindow(control(ui, ID_CAMERA_KEY), modded && checked(ui, ID_OPTION + MM3_LAUNCH_FREE_CAMERA));
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
        page_show(ui, (ui->page + direction + LAUNCHER_PAGES) % LAUNCHER_PAGES); SetFocus(ui->tabs); return;
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
                SendMessageA(focus, LB_SETCURSEL, at, 0); binding_detail(ui); help_show(ui, ID_BINDINGS);
            } else SetFocus(GetNextDlgTabItem(ui->window, focus, previous));
        } else SetFocus(GetNextDlgTabItem(ui->window, focus, previous));
    }
    if (pressed & XINPUT_GAMEPAD_A) {
        if (!_stricmp(kind, "BUTTON")) SendMessageA(focus, BM_CLICK, 0, 0);
        else if (!_stricmp(kind, "COMBOBOX")) SendMessageA(focus, CB_SHOWDROPDOWN, !SendMessageA(focus, CB_GETDROPPEDSTATE, 0, 0), 0);
    }
}
static void pad_activity(const XINPUT_GAMEPAD *pad, char *text, size_t capacity)
{
    const char *parts[5];
    int count = 0, source = pad_source(pad);
    size_t at = 0;
    if (source >= 0 && source < 14) parts[count++] = mm3_launcher_pad_sources[source];
    if (pad->bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) parts[count++] = "LT";
    if (pad->bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) parts[count++] = "RT";
    if (abs((int)pad->sThumbLX) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
        abs((int)pad->sThumbLY) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
        parts[count++] = "Left stick";
    if (abs((int)pad->sThumbRX) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ||
        abs((int)pad->sThumbRY) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
        parts[count++] = "Right stick";
    text[0] = 0;
    for (int i = 0; i < count; ++i)
        at += (size_t)snprintf(text + at, capacity - at, "%s%s%s",
            i ? ", " : " (", parts[i], i == count - 1 ? ")" : "");
}

static void status_text(HWND window, const char *text)
{
    char previous[512];
    GetWindowTextA(window, previous, sizeof previous);
    if (strcmp(previous, text)) SetWindowTextA(window, text);
}

/* Windows lets a window come to the front only for the foreground process
 * or the one that had the last keyboard or mouse input. Play pressed with a
 * controller leaves no such input, and the launcher's window closes before
 * the game opens its own, so the game window could open without the focus -
 * and Steam Input keeps a controller on its desktop setup until the game
 * window has it. While the launcher is still in front, a thread waits for the
 * game window and brings it forward. */
static BOOL CALLBACK game_window_found(HWND window, LPARAM param)
{
    DWORD pid;
    char name[80];
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && GetClassNameA(window, name, sizeof name) &&
        !strcmp(name, "XboxRecompFramebuffer") && IsWindowVisible(window)) {
        *(HWND *)param = window;
        return FALSE;
    }
    return TRUE;
}

static DWORD WINAPI game_window_forward(LPVOID unused)
{
    HWND game = NULL;
    (void)unused;
    for (int wait = 0; !game && wait < 1200; ++wait) {   /* up to two minutes */
        EnumWindows(game_window_found, (LPARAM)&game);
        if (!game) Sleep(100);
    }
    for (int attempt = 0; game && attempt < 20 && GetForegroundWindow() != game; ++attempt) {
        HWND front = GetForegroundWindow();
        DWORD front_thread = front ? GetWindowThreadProcessId(front, NULL) : 0;
        DWORD self = GetCurrentThreadId();
        BOOL attached = front_thread && front_thread != self && AttachThreadInput(self, front_thread, TRUE);
        if (IsIconic(game)) ShowWindow(game, SW_RESTORE);
        BringWindowToTop(game);
        SetForegroundWindow(game);
        if (attached) AttachThreadInput(self, front_thread, FALSE);
        if (GetForegroundWindow() != game) Sleep(100);
        else fprintf(stderr, "[MM3_LAUNCHER] game window brought to the front (attempt %d)\n", attempt + 1);
    }
    if (game && GetForegroundWindow() != game)
        fprintf(stderr, "[MM3_LAUNCHER] could not bring the game window to the front\n");
    return 0;
}

static void game_window_follow(void)
{
    HANDLE thread;
    AllowSetForegroundWindow(GetCurrentProcessId());
    thread = CreateThread(NULL, 0, game_window_forward, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

static void poll_pads(Launcher *ui)
{
    char text[512] = "", summary[128], hint[160];
    size_t at = 0;
    int count = 0, highest_port = 0;
    int capture_port = combo_value(control(ui, ID_PAD_PORT));
    ULONGLONG now = GetTickCount64();
    if (!ui->device_poll_time || now - ui->device_poll_time >= 1000) {
        ui->dualsense_count = mm3_dualsense_connected();
        ui->device_poll_time = now;
    }
    for (int i = 0; i < 4; ++i) {
        XINPUT_STATE state;
        int connected = XInputGetState((DWORD)i, &state) == ERROR_SUCCESS;
        char buttons[96] = "";
        WORD pressed = connected && ui->pad_seen[i] ? state.Gamepad.wButtons & (WORD)~ui->previous_pad[i] : 0;
        if (connected) {
            int source = pad_source(&state.Gamepad);
            ++count; highest_port = i + 1;
            pad_activity(&state.Gamepad, buttons, sizeof buttons);
            if (ui->capture && ui->group == 0 && i == capture_port && GetForegroundWindow() == ui->window) {
                if (source < 0) ui->capture_armed = 1;
                else if (ui->capture_armed) binding_set(ui, source);
            } else if (!ui->capture && GetForegroundWindow() == ui->window)
                pad_navigation(ui, pressed);
        }
        ui->previous_pad[i] = connected ? state.Gamepad.wButtons : 0;
        ui->pad_seen[i] = connected;
        at += (size_t)snprintf(text + at, sizeof text - at, "P%d: %s%s%s", i + 1,
            connected ? "connected" : "not connected", buttons, i == 3 ? "" : "\r\n");
    }
    if (!count) {
        if (ui->dualsense_count) {
            snprintf(summary, sizeof summary, "DualSense connected. XInput needed for play (see Controls).");
            snprintf(hint, sizeof hint, "DualSense found. Enable Steam Input\r\nfor this game (see Play_game.txt).");
        } else {
            snprintf(summary, sizeof summary, "No controller detected (XInput). Check Controls.");
            snprintf(hint, sizeof hint, "No XInput controller detected.\r\nConnect/pair it or use XInput mode.");
        }
    } else {
        snprintf(summary, sizeof summary, "%s detected: %d connected. Test sticks/triggers in Controls.",
            count == 1 ? "Controller" : "Controllers", count);
        if (highest_port > combo_value(control(ui, ID_PAD_COUNT)))
            snprintf(hint, sizeof hint, "P%d detected outside enabled game ports.\r\nSet Controllers in game to %d to use it.", highest_port, highest_port);
        else snprintf(hint, sizeof hint, "%d connected. Move a stick or squeeze a trigger to test.", count);
    }
    status_text(control(ui, ID_PAD_STATUS), text);
    status_text(control(ui, ID_PAD_SUMMARY), summary);
    status_text(control(ui, ID_PAD_HINT), hint);
}

/* ---- Check for updates ------------------------------------------------- */

static int update_progress(void *context, unsigned long long done, unsigned long long total)
{
    UpdateJob *job = (UpdateJob *)context;
    if (job->cancel) return 0;
    if (done - job->reported >= (256u << 10) || (total && done >= total)) {
        job->reported = done;
        PostMessageA(job->window, WM_UPDATE_PROGRESS, (WPARAM)(done >> 10), (LPARAM)(total >> 10));
    }
    return 1;
}

static DWORD WINAPI update_thread(LPVOID parameter)
{
    UpdateJob *job = (UpdateJob *)parameter;
    job->message[0] = 0;
    if (job->download)
        job->result = mm3_update_download(&job->update, job->target, update_progress, job,
                                          job->message, sizeof job->message);
    else
        job->result = mm3_update_check(MM3_UPDATE_API_URL, MM3_VERSION, MM3_UPDATE_TRUSTED, &job->update,
                                       job->message, sizeof job->message);
    if (!PostMessageA(job->window, WM_UPDATE_DONE, 0, (LPARAM)job)) free(job);   /* the window is gone */
    return 0;
}

/* Where the downloaded Setup goes (updates\ beside the game) and the folder it
 * should install into: the one this game runs from. */
static int update_paths(wchar_t *setup, size_t setup_size, wchar_t *folder, size_t folder_size)
{
    wchar_t *slash;
    DWORD length = GetModuleFileNameW(NULL, folder, (DWORD)folder_size);
    if (!length || length >= folder_size) return 0;
    slash = wcsrchr(folder, L'\\');
    if (!slash) return 0;
    *slash = 0;
    return _snwprintf(setup, setup_size, L"%ls\\updates\\MM3-Setup.exe", folder) > 0;
}

static int update_open_setup(const wchar_t *setup, const wchar_t *folder)
{
    wchar_t arguments[2 * MAX_PATH + 16];
    _snwprintf(arguments, sizeof arguments / sizeof arguments[0], L"--dest \"%ls\"", folder);
    arguments[sizeof arguments / sizeof arguments[0] - 1] = 0;
    return (INT_PTR)ShellExecuteW(NULL, L"open", setup, arguments, folder, SW_SHOWNORMAL) > 32;
}

static void update_status(Launcher *ui, const char *text)
{ SetWindowTextA(control(ui, ID_STATUS), text); }

static void update_start(Launcher *ui, int download)
{
    UpdateJob *job = (UpdateJob *)calloc(1, sizeof *job);
    wchar_t folder[2 * MAX_PATH];
    if (!job) { update_status(ui, "Not enough memory to check for updates."); return; }
    job->window = ui->window;
    job->download = download;
    if (download) {
        job->update = ui->update_found;
        if (!update_paths(job->target, sizeof job->target / sizeof job->target[0], folder, sizeof folder / sizeof folder[0])) {
            free(job);
            update_status(ui, "Cannot tell where the game is installed, so the update cannot be saved.");
            return;
        }
    }
    ui->update_job = job;
    EnableWindow(control(ui, ID_UPDATE), FALSE);
    update_status(ui, download ? "Downloading the new Setup..." : "Checking for updates...");
    {
        HANDLE thread = CreateThread(NULL, 0, update_thread, job, 0, NULL);
        if (!thread) {
            ui->update_job = NULL; free(job);
            EnableWindow(control(ui, ID_UPDATE), TRUE);
            update_status(ui, "Could not start the update check.");
            return;
        }
        CloseHandle(thread);
    }
}

/* The step finished (or was stopped): the check's answer, or the downloaded Setup. */
static void update_done(Launcher *ui, UpdateJob *job)
{
    char text[640];
    wchar_t setup[2 * MAX_PATH], folder[2 * MAX_PATH];
    if (ui->update_job == job) ui->update_job = NULL;
    EnableWindow(control(ui, ID_UPDATE), TRUE);
    if (ui->close_after_update) { free(job); DestroyWindow(ui->window); return; }
    if (job->download) {
        if (job->result != 1) update_status(ui, job->message);
        else if (!update_paths(setup, sizeof setup / sizeof setup[0], folder, sizeof folder / sizeof folder[0]) ||
                 !MM3_UPDATE_OPEN(setup, folder))
            update_status(ui, "Setup was downloaded to the updates folder beside the game, but could not be opened. "
                              "Open MM3-Setup.exe there yourself.");
        else {
            /* Setup replaces this game's files, so the game must not start or stay open. */
            ui->result = 0;
            DestroyWindow(ui->window);
        }
    } else if (job->result < 0) {
        update_status(ui, job->message);
    } else if (job->result == 0) {
        snprintf(text, sizeof text, "You have the latest version (%s).", MM3_VERSION);
        update_status(ui, text);
    } else {
        ui->update_found = job->update;
        snprintf(text, sizeof text,
                 "Version %s is available (this is %s).\n\nDownload its Setup now? When it is ready, this window "
                 "closes and Setup opens: choose your game image there and press Install to update. "
                 "Your saved games and DLC stay.", job->update.version, MM3_VERSION);
        if (MM3_UPDATE_ASK(ui->window, text)) update_start(ui, 1);
        else {
            snprintf(text, sizeof text, "Version %s is available. Press Check for updates to download it.",
                     job->update.version);
            update_status(ui, text);
        }
    }
    free(job);
}

/* Cancel and Close while an update step runs: ask it to stop, close when it has. */
static int update_defer_close(Launcher *ui)
{
    if (!ui->update_job) return 0;
    InterlockedExchange(&ui->update_job->cancel, 1);
    ui->close_after_update = 1;
    update_status(ui, "Stopping the update...");
    return 1;
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
        ui->section = CreateFontA(-(int)lroundf(17.f * ui->scale), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        HWND title = add(ui, -1, "STATIC", "Midtown Madness 3  " MM3_VERSION, 0, -1, 25, 17, 570, 35);
        SendMessageA(title, WM_SETFONT, (WPARAM)ui->heading, TRUE);
        add(ui, -1, "STATIC", "Detecting controllers...", 0, ID_PAD_SUMMARY, 620, 23, 570, 24);
        label(ui, -1, "Choose your settings. The guide explains each value as you select or change it.", 25, 55, 1150);
        /* The classic tab control barely marks the open tab, so WM_DRAWITEM
         * draws the headers: the open one solid blue with white bold text,
         * the others grey, and a blue line joins it to its page. */
        ui->accent = CreateSolidBrush(RGB(0, 103, 192));
        ui->tab_idle = CreateSolidBrush(RGB(222, 225, 230));
        ui->tabs = add(ui, -1, WC_TABCONTROLA, "", WS_TABSTOP | WS_CLIPSIBLINGS | TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH,
                       ID_TABS, 22, 85, 820, 550);
        TabCtrl_SetItemSize(ui->tabs, lroundf(196.f * ui->scale), lroundf(32.f * ui->scale));
        tab.mask = TCIF_TEXT;
        tab.pszText = "Settings"; TabCtrl_InsertItem(ui->tabs, 0, &tab);
        tab.pszText = "Controls"; TabCtrl_InsertItem(ui->tabs, 1, &tab);
        tab.pszText = "Visual + audio mods"; TabCtrl_InsertItem(ui->tabs, 2, &tab);
        tab.pszText = "Gameplay mods"; TabCtrl_InsertItem(ui->tabs, 3, &tab);
        /* The tab is a sibling; put it behind its page controls. */
        SetWindowPos(ui->tabs, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        {
            RECT header, pane;
            HWND bar = add(ui, -1, "STATIC", "", 0, ID_TAB_BAR, 22, 85, 820, 3);
            int thickness = lroundf(3.f * ui->scale) < 2 ? 2 : (int)lroundf(3.f * ui->scale);
            TabCtrl_GetItemRect(ui->tabs, 1, &header);
            GetWindowRect(ui->tabs, &pane);
            MapWindowPoints(NULL, window, (POINT *)&pane, 2);
            if (bar) MoveWindow(bar, pane.left, pane.top + header.bottom, pane.right - pane.left, thickness, FALSE);
            /* Page content starts below the bar. The mods tabs' first row
             * (y 92) began on it, and those controls, drawn over it, broke
             * the line under the open tab (user, 10-29). */
            ui->page_shift = pane.top + header.bottom + thickness - (int)lroundf(119.f * ui->scale);
            if (ui->page_shift < 0) ui->page_shift = 0;
        }
        settings_controls(ui); controls_controls(ui); look_controls(ui); gameplay_controls(ui);
        add(ui, -1, "STATIC", "", SS_ETCHEDVERT, -1, 855, 85, 2, 550);
        label(ui, -1, "SETTING GUIDE", 880, 103, 295);
        HWND help_title = add(ui, -1, "STATIC", "", SS_NOPREFIX, ID_HELP_TITLE, 880, 139, 295, 76);
        SendMessageA(help_title, WM_SETFONT, (WPARAM)ui->heading, TRUE);
        add(ui, -1, "STATIC", "", SS_NOPREFIX, ID_HELP_VALUE, 880, 219, 295, 52);
        add(ui, -1, "STATIC", "", SS_NOPREFIX, ID_HELP_BODY, 880, 285, 295, 190);
        label(ui, -1, "WHAT TO EXPECT", 880, 489, 295);
        add(ui, -1, "STATIC", "", SS_NOPREFIX, ID_HELP_EFFECT, 880, 521, 295, 100);
        add(ui, -1, "STATIC", "Keyboard: Tab to move, Enter to activate. Controller: D-pad moves, A selects, LB/RB changes tab, Start plays.",
            0, ID_STATUS, 25, 644, 1165, 45);
        add(ui, -1, "BUTTON", "Restore defaults", BS_PUSHBUTTON | WS_TABSTOP, ID_DEFAULTS, 25, 699, 155, 34);
        add(ui, -1, "BUTTON", "Check for updates", BS_PUSHBUTTON | WS_TABSTOP, ID_UPDATE, 195, 699, 175, 34);
        add(ui, -1, "BUTTON", "Play", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 933, 699, 122, 34);
        add(ui, -1, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 1068, 699, 122, 34);
        populate(ui); page_show(ui, 0);
        poll_pads(ui);
        SetTimer(window, 1, 70, NULL);
        if (ui->smoke) SetTimer(window, 2, 150, NULL);
        return 0;
    }
    case WM_NOTIFY:
        if (((NMHDR *)lparam)->idFrom == ID_TABS && ((NMHDR *)lparam)->code == TCN_SELCHANGE)
            page_show(ui, TabCtrl_GetCurSel(ui->tabs));
        return 0;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED && radio_choice(LOWORD(wparam))) {
            int look = LOWORD(wparam) == ID_ORIGINAL || LOWORD(wparam) == ID_OPTION + MM3_LAUNCH_IMPROVED;
            int option = look ? MM3_LAUNCH_IMPROVED : MM3_LAUNCH_GAMEPLAY;
            int on = LOWORD(wparam) == ID_OPTION + option;
            set_checked(ui, ID_OPTION + option, on);
            set_checked(ui, look ? ID_ORIGINAL : ID_XBOX_GAMEPLAY, !on);
            mods_enable(ui);
            help_show(ui, LOWORD(wparam));
            return 0;
        }
        if (HIWORD(wparam) == BN_CLICKED && (LOWORD(wparam) == ID_KEYBOARD ||
            LOWORD(wparam) == ID_VSYNC || LOWORD(wparam) == ID_SHOW_FPS ||
            (LOWORD(wparam) >= ID_OPTION && LOWORD(wparam) < ID_OPTION + MM3_LAUNCH_OPTION_COUNT))) {
            int id = LOWORD(wparam);
            set_checked(ui, id, !checked(ui, id));
            if (id == ID_OPTION + MM3_LAUNCH_WORLD_LIGHT || id == ID_OPTION + MM3_LAUNCH_FREE_CAMERA)
                mods_enable(ui);   /* Night brightness, the free camera key */
            help_show(ui, id);
            return 0;
        }
        if (HIWORD(wparam) == CBN_SELCHANGE || HIWORD(wparam) == CBN_EDITCHANGE ||
            HIWORD(wparam) == EN_CHANGE || HIWORD(wparam) == LBN_SELCHANGE)
            help_show(ui, LOWORD(wparam));
        switch (LOWORD(wparam)) {
        case IDOK:
            if (ui->update_job) { update_status(ui, "Wait for the update to finish, or press Cancel."); return 0; }
            if (!ui->capture && apply(ui)) { ui->result = 1; game_window_follow(); DestroyWindow(window); }
            return 0;
        case IDCANCEL:
            if (ui->capture) { ui->capture = 0; SetWindowTextA(control(ui, ID_STATUS), "Capture cancelled."); help_show(ui, ID_CAPTURE); }
            else if (!update_defer_close(ui)) DestroyWindow(window);
            return 0;
        case ID_UPDATE:
            if (!ui->update_job) update_start(ui, 0);
            return 0;
        case ID_DEFAULTS:
            mm3_launcher_config_defaults(&ui->config);
            mm3_display_settings_defaults(&ui->display, getenv("MM3_PERFORMANCE_PROFILE"));
            populate(ui); SetWindowTextA(control(ui, ID_STATUS), "Defaults restored in this window. Play saves them; Cancel discards them.");
            help_show(ui, ID_DEFAULTS);
            return 0;
        case ID_GROUP:
            if (HIWORD(wparam) == CBN_SELCHANGE) { ui->capture = 0; ui->group = combo_value(control(ui, ID_GROUP)); bindings_refresh(ui); help_show(ui, ID_GROUP); }
            return 0;
        case ID_BINDINGS:
            if (HIWORD(wparam) == LBN_SELCHANGE) { ui->capture = 0; binding_detail(ui); help_show(ui, ID_BINDINGS); }
            return 0;
        case ID_SOURCE:
            if (HIWORD(wparam) == CBN_SELCHANGE && ui->group == 0) binding_set(ui, combo_value(control(ui, ID_SOURCE)));
            return 0;
        case ID_CAPTURE:
            ui->capture = 1; ui->capture_armed = 0;
            SetWindowTextA(control(ui, ID_STATUS), ui->group == 0 ?
                "Release the controller buttons, then press the desired button or trigger. Escape cancels." :
                "Press a key or mouse button now. Escape cancels; Clear selected binding unbinds it.");
            help_show(ui, ID_CAPTURE);
            return 0;
        case ID_UNBIND: binding_set(ui, ui->group == 0 ? 16 : 0); return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        /* Disabled controls cannot receive focus or mouse messages. Let their
         * parent provide explanations too, including the required repair. */
        POINT point = {(short)LOWORD(lparam), (short)HIWORD(lparam)};
        HWND child = ChildWindowFromPointEx(window, point, CWP_SKIPINVISIBLE);
        if (child && child != window && ui->help_id != GetDlgCtrlID(child))
            help_show(ui, GetDlgCtrlID(child));
        break;
    }
    case WM_CTLCOLORSTATIC:
        if ((HWND)lparam == control(ui, ID_TAB_BAR) && ui->accent) return (LRESULT)ui->accent;
        if ((HWND)lparam == control(ui, ID_HELP_VALUE)) {
            SetTextColor((HDC)wparam, RGB(0, 90, 150));
            SetBkColor((HDC)wparam, GetSysColor(COLOR_BTNFACE));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lparam;
        char caption[256];
        RECT box = draw->rcItem, text = draw->rcItem;
        int size = (int)lroundf(16.f * ui->scale);
        UINT flags = radio_choice((int)draw->CtlID) ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK;
        if (draw->CtlType == ODT_TAB) {
            int open = (int)draw->itemID == TabCtrl_GetCurSel(draw->hwndItem);
            TCITEMA item = {0};
            HFONT previous;
            item.mask = TCIF_TEXT; item.pszText = caption; item.cchTextMax = sizeof caption;
            caption[0] = 0;
            TabCtrl_GetItem(draw->hwndItem, (int)draw->itemID, &item);
            FillRect(draw->hDC, &draw->rcItem, open ? ui->accent : ui->tab_idle);
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, open ? RGB(255, 255, 255) : RGB(70, 70, 70));
            previous = SelectObject(draw->hDC, open ? ui->section : ui->font);
            DrawTextA(draw->hDC, caption, -1, &text, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(draw->hDC, previous);
            if (open && GetFocus() == draw->hwndItem && !(draw->itemState & ODS_NOFOCUSRECT)) {
                InflateRect(&text, -(int)lroundf(4.f * ui->scale), -(int)lroundf(4.f * ui->scale));
                DrawFocusRect(draw->hDC, &text);
            }
            return TRUE;
        }
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
                ID_GROUP, ID_BINDINGS, ID_SOURCE, ID_CAPTURE, ID_PAD_COUNT, ID_PAD_STATUS,
                ID_PAD_SUMMARY, ID_PAD_HINT, ID_FOV, IDOK, IDCANCEL};
            int valid = 1;
            for (int i = 0; i < (int)(sizeof important / sizeof important[0]); ++i)
                if (!control(ui, important[i])) valid = 0;
            page_show(ui, 1); ui->group = 1; combo_select(control(ui, ID_GROUP), 1); bindings_refresh(ui);
            valid = valid && SendMessageA(control(ui, ID_BINDINGS), LB_GETCOUNT, 0, 0) == MM3_LAUNCHER_KEYS;
            page_show(ui, 2); valid = valid && IsWindowVisible(control(ui, ID_FOV));
            page_show(ui, 3); valid = valid && IsWindowVisible(control(ui, ID_XBOX_GAMEPLAY)) &&
                                    IsWindowVisible(control(ui, ID_OPTION + MM3_LAUNCH_DRAFTING));
            page_show(ui, 0);
            fprintf(stderr, "[MM3_LAUNCHER_SMOKE] controls=%d tabs=4 keyboard_bindings=24 pad_ports=4 dualsense=%d xinput_connected=%d result=%s\n",
                ui->control_count, ui->dualsense_count,
                ui->pad_seen[0] + ui->pad_seen[1] + ui->pad_seen[2] + ui->pad_seen[3], valid ? "PASS" : "FAIL");
            if (!valid) ui->smoke = -1;
            DestroyWindow(window); return 0;
        }
        break;
    case WM_DEVICECHANGE:
        ui->device_poll_time = 0;
        poll_pads(ui);
        return 0;
    case WM_UPDATE_PROGRESS:
        if (ui->update_job && ui->update_job->download && !ui->close_after_update) {
            char text[160];
            snprintf(text, sizeof text, "Downloading Setup %s... %u%s", ui->update_found.version,
                     (unsigned)((wparam + 512) >> 10), lparam ? "" : " MB");
            if (lparam) snprintf(text + strlen(text), sizeof text - strlen(text), " / %u MB", (unsigned)((lparam + 512) >> 10));
            update_status(ui, text);
        }
        return 0;
    case WM_UPDATE_DONE:
        update_done(ui, (UpdateJob *)lparam);
        return 0;
    case WM_CLOSE:
        if (!update_defer_close(ui)) DestroyWindow(window);
        return 0;
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
    float width = (float)(work->right - work->left - (frame.right - frame.left) - 20) / (float)LAUNCHER_WIDTH;
    float height = (float)(work->bottom - work->top - (frame.bottom - frame.top) - 20) / (float)LAUNCHER_HEIGHT;
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
        bounds.right = (LONG)lroundf((float)LAUNCHER_WIDTH * ui.scale); bounds.bottom = (LONG)lroundf((float)LAUNCHER_HEIGHT * ui.scale);
        AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_CONTROLPARENT);
        HWND window = CreateWindowExA(WS_EX_CONTROLPARENT, wc.lpszClassName, "Midtown Madness 3 " MM3_VERSION " - Play",
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
                        ui.capture = 0; SetWindowTextA(control(&ui, ID_STATUS), "Capture cancelled."); help_show(&ui, ID_CAPTURE); continue;
                    }
                    vk = (int)message.wParam;
                }
                if (message.message == WM_LBUTTONDOWN) vk = VK_LBUTTON;
                if (message.message == WM_RBUTTONDOWN) vk = VK_RBUTTON;
                if (message.message == WM_MBUTTONDOWN) vk = VK_MBUTTON;
                if (message.message == WM_XBUTTONDOWN) vk = HIWORD(message.wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
                if (vk > 0 && vk <= 255) { binding_set(&ui, vk); continue; }
            } else if (ui.capture && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                ui.capture = 0; SetWindowTextA(control(&ui, ID_STATUS), "Capture cancelled."); help_show(&ui, ID_CAPTURE); continue;
            }
            if (!IsDialogMessageA(window, &message)) { TranslateMessage(&message); DispatchMessageA(&message); }
        }
        if (ui.font) DeleteObject(ui.font); if (ui.heading) DeleteObject(ui.heading); if (ui.section) DeleteObject(ui.section);
        if (ui.accent) DeleteObject(ui.accent); if (ui.tab_idle) DeleteObject(ui.tab_idle);
        if (ui.smoke || !ui.result) return 0;
    } else if (!mm3_launcher_config_apply(&ui.config, warning, sizeof warning)) {
        fprintf(stderr, "[MM3_LAUNCHER] %s\n", warning); return 0;
    }
    if (!bootstrap(warning, sizeof warning)) { fprintf(stderr, "[MM3_LAUNCHER] %s\n", warning); return 0; }
    return 1;
}
