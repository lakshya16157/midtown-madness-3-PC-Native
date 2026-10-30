/* Real launcher, isolated preferences, controlled messages instead of global
 * keyboard automation. No renderer, game process or physical pad required. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

static SRWLOCK s_pad_lock = SRWLOCK_INIT;
static XINPUT_STATE s_pad_states[4];
static unsigned s_pad_mask;
static UINT WINAPI fake_raw_input_device_list(PRAWINPUTDEVICELIST devices, PUINT count, UINT size);
static UINT WINAPI fake_raw_input_device_info(HANDLE device, UINT command, LPVOID data, PUINT size);

static DWORD WINAPI fake_xinput_get_state(DWORD port, XINPUT_STATE *state)
{
    DWORD result = ERROR_DEVICE_NOT_CONNECTED;
    AcquireSRWLockShared(&s_pad_lock);
    if (port < 4 && (s_pad_mask & (1u << port))) {
        *state = s_pad_states[port];
        result = ERROR_SUCCESS;
    }
    ReleaseSRWLockShared(&s_pad_lock);
    return result;
}

/* Redirect after the platform declaration: production still calls real XInput,
 * while this translation unit never depends on attached hardware. */
#define XInputGetState fake_xinput_get_state
#define GetRawInputDeviceList fake_raw_input_device_list
#define GetRawInputDeviceInfoA fake_raw_input_device_info
/* The update button, aimed at a local stand-in for GitHub; the question is
 * answered by the test and Setup is only recorded, never started. */
static char s_update_api[256];
static int s_update_answer, s_update_asked, s_update_opened;
static wchar_t s_update_setup[600];
#define MM3_VERSION "2.1"
#define MM3_UPDATE_API_URL s_update_api
#define MM3_UPDATE_TRUSTED ""
#define MM3_UPDATE_ASK(window, text) (++s_update_asked, s_update_answer)
#define MM3_UPDATE_OPEN(setup, folder) (++s_update_opened, wcsncpy(s_update_setup, (setup), 599), 1)
#include "../src/mm3_launcher.c"
#undef XInputGetState
#undef GetRawInputDeviceList
#undef GetRawInputDeviceInfoA

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL launcher UI line %d: %s\n", __LINE__, #test); ExitProcess(1); } } while (0)
static int s_play;
static void capture_page(HWND window, const char *name);
static RID_DEVICE_INFO s_raw_info[8];
static unsigned s_raw_count, s_raw_info_errors;
static int s_raw_count_error, s_raw_list_error, s_raw_resize_attempts;

static UINT WINAPI fake_raw_input_device_list(PRAWINPUTDEVICELIST devices, PUINT count, UINT size)
{
    UINT result = (UINT)-1;
    AcquireSRWLockExclusive(&s_pad_lock);
    if (size != sizeof *devices || s_raw_count_error) SetLastError(ERROR_INVALID_PARAMETER);
    else if (!devices) { *count = s_raw_count; result = 0; }
    else if (s_raw_list_error) SetLastError(ERROR_INVALID_PARAMETER);
    else if (s_raw_resize_attempts || *count < s_raw_count) {
        if (s_raw_resize_attempts) --s_raw_resize_attempts;
        *count = s_raw_count + 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
    } else {
        for (unsigned i = 0; i < s_raw_count; ++i) {
            devices[i].hDevice = (HANDLE)(UINT_PTR)(i + 1);
            devices[i].dwType = s_raw_info[i].dwType;
        }
        result = *count = s_raw_count;
    }
    ReleaseSRWLockExclusive(&s_pad_lock);
    return result;
}

static UINT WINAPI fake_raw_input_device_info(HANDLE device, UINT command, LPVOID data, PUINT size)
{
    UINT result = (UINT)-1;
    UINT_PTR port = (UINT_PTR)device;
    AcquireSRWLockShared(&s_pad_lock);
    if (command != RIDI_DEVICEINFO || !port || port > s_raw_count ||
        (s_raw_info_errors & (1u << (port - 1))) || *size < sizeof(RID_DEVICE_INFO))
        SetLastError(ERROR_INVALID_PARAMETER);
    else {
        *(RID_DEVICE_INFO *)data = s_raw_info[port - 1];
        result = *size = sizeof(RID_DEVICE_INFO);
    }
    ReleaseSRWLockShared(&s_pad_lock);
    return result;
}

static void fake_raw_reset(void)
{
    AcquireSRWLockExclusive(&s_pad_lock);
    memset(s_raw_info, 0, sizeof s_raw_info);
    s_raw_count = s_raw_info_errors = 0;
    s_raw_count_error = s_raw_list_error = s_raw_resize_attempts = 0;
    ReleaseSRWLockExclusive(&s_pad_lock);
}

static void fake_raw_device(DWORD type, DWORD vendor, DWORD product, USHORT page, USHORT usage)
{
    AcquireSRWLockExclusive(&s_pad_lock);
    CHECK(s_raw_count < 8);
    RID_DEVICE_INFO *info = &s_raw_info[s_raw_count++];
    info->cbSize = sizeof *info;
    info->dwType = type;
    info->hid.dwVendorId = vendor; info->hid.dwProductId = product;
    info->hid.usUsagePage = page; info->hid.usUsage = usage;
    ReleaseSRWLockExclusive(&s_pad_lock);
}

static void dualsense_detection_contract(void)
{
    fake_raw_reset();
    CHECK(mm3_dualsense_connected() == 0);
    fake_raw_device(RIM_TYPEHID, 0x054c, 0x0ce6, 1, 5);
    CHECK(mm3_dualsense_connected() == 1);
    fake_raw_device(RIM_TYPEHID, 0x054c, 0x0df2, 1, 4);
    CHECK(mm3_dualsense_connected() == 2);
    fake_raw_device(RIM_TYPEHID, 0x1234, 0x0ce6, 1, 5);
    fake_raw_device(RIM_TYPEHID, 0x054c, 0x9999, 1, 5);
    fake_raw_device(RIM_TYPEHID, 0x054c, 0x0ce6, 2, 5);
    fake_raw_device(RIM_TYPEHID, 0x054c, 0x0ce6, 1, 6);
    fake_raw_device(RIM_TYPEKEYBOARD, 0x054c, 0x0ce6, 1, 5);
    CHECK(mm3_dualsense_connected() == 2);
    s_raw_info_errors = 1;
    CHECK(mm3_dualsense_connected() == 1);
    s_raw_info_errors = 0;
    s_raw_resize_attempts = 1;
    CHECK(mm3_dualsense_connected() == 2);
    s_raw_resize_attempts = 3;
    CHECK(mm3_dualsense_connected() == 0);
    s_raw_list_error = 1;
    CHECK(mm3_dualsense_connected() == 0);
    s_raw_list_error = 0; s_raw_count_error = 1;
    CHECK(mm3_dualsense_connected() == 0);
    fake_raw_reset();
}

static void fake_ports(unsigned mask, int active_port, const XINPUT_GAMEPAD *active)
{
    AcquireSRWLockExclusive(&s_pad_lock);
    memset(s_pad_states, 0, sizeof s_pad_states);
    s_pad_mask = mask;
    if (active && active_port >= 0 && active_port < 4)
        s_pad_states[active_port].Gamepad = *active;
    ReleaseSRWLockExclusive(&s_pad_lock);
}

typedef struct HelpText {
    char title[256], value[512], body[2048], effect[2048];
} HelpText;

static void help_text(HWND window, HelpText *text)
{
    GetDlgItemTextA(window, ID_HELP_TITLE, text->title, sizeof text->title);
    GetDlgItemTextA(window, ID_HELP_VALUE, text->value, sizeof text->value);
    GetDlgItemTextA(window, ID_HELP_BODY, text->body, sizeof text->body);
    GetDlgItemTextA(window, ID_HELP_EFFECT, text->effect, sizeof text->effect);
    CHECK(strlen(text->title) >= 3);
    CHECK(strlen(text->body) >= 24 && strlen(text->effect) >= 12);
}

static int contains(const char *text, const char *part)
{
    size_t length = strlen(part);
    for (; *text; ++text) if (!_strnicmp(text, part, length)) return 1;
    return 0;
}

static int help_contains(HWND window, const char *part)
{
    HelpText text;
    help_text(window, &text);
    return contains(text.title, part) || contains(text.value, part) ||
        contains(text.body, part) || contains(text.effect, part);
}

static RECT child_bounds(HWND window, int id)
{
    RECT bounds;
    CHECK(GetWindowRect(GetDlgItem(window, id), &bounds));
    MapWindowPoints(NULL, window, (POINT *)&bounds, 2);
    return bounds;
}

/* The blue line under the tab headers: no control of any tab may cover it.
 * The mods tabs' first row (Original/Improved, Xbox/Modded) began on it and,
 * drawn over it, broke the line under the open tab (user, 10-29). */
static void select_page(HWND window, int page);
static void tab_bar_clear(HWND window)
{
    HWND bar = GetDlgItem(window, ID_TAB_BAR), tabs = GetDlgItem(window, ID_TABS);
    RECT line = child_bounds(window, ID_TAB_BAR), other, overlap;
    CHECK(bar && IsWindowVisible(bar) && line.right - line.left > 400 && line.bottom > line.top);
    for (int page = 0; page < 4; ++page) {
        select_page(window, page);
        for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (child == bar || child == tabs || !IsWindowVisible(child)) continue;
            CHECK(GetWindowRect(child, &other));
            MapWindowPoints(NULL, window, (POINT *)&other, 2);
            if (IntersectRect(&overlap, &line, &other))
                fprintf(stderr, "tab %d: control %d covers the tab bar (%ld-%ld, %ld-%ld)\n", page,
                        GetDlgCtrlID(child), other.left, other.right, other.top, other.bottom);
            CHECK(!IntersectRect(&overlap, &line, &other));
        }
    }
    select_page(window, 0);
}

static void text_fits(HWND window, int id)
{
    HWND child = GetDlgItem(window, id);
    RECT bounds = child_bounds(window, id), measured = {0};
    char text[1024];
    GetWindowTextA(child, text, sizeof text);
    measured.right = bounds.right - bounds.left;
    HDC dc = GetDC(child);
    HFONT previous = SelectObject(dc, (HFONT)SendMessageA(child, WM_GETFONT, 0, 0));
    DrawTextA(dc, text, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, previous); ReleaseDC(child, dc);
    if (measured.bottom > bounds.bottom - bounds.top + 2)
        fprintf(stderr, "Controller text id %d needs %ld px in %ld x %ld bounds: %s\n", id,
            measured.bottom, bounds.right - bounds.left, bounds.bottom - bounds.top, text);
    CHECK(measured.bottom <= bounds.bottom - bounds.top + 2);
}

static void controller_summary_layout(HWND window)
{
    RECT client, summary = child_bounds(window, ID_PAD_SUMMARY), tabs = child_bounds(window, ID_TABS);
    GetClientRect(window, &client);
    CHECK(IsWindowVisible(GetDlgItem(window, ID_PAD_SUMMARY)));
    CHECK(summary.left >= client.left && summary.right <= client.right);
    CHECK(summary.top >= client.top && summary.bottom <= tabs.top);
    text_fits(window, ID_PAD_SUMMARY);
}

static void help_layout(HWND window)
{
    static const int ids[] = {ID_HELP_TITLE, ID_HELP_VALUE, ID_HELP_BODY, ID_HELP_EFFECT};
    RECT client, tabs = child_bounds(window, ID_TABS), footer = child_bounds(window, ID_STATUS);
    LONG previous_bottom = tabs.top;
    GetClientRect(window, &client);
    for (int i = 0; i < (int)(sizeof ids / sizeof ids[0]); ++i) {
        HWND child = GetDlgItem(window, ids[i]);
        RECT bounds = child_bounds(window, ids[i]), measured;
        char text[2048];
        CHECK(IsWindowVisible(child));
        CHECK(bounds.left > tabs.right && bounds.right <= client.right);
        CHECK(bounds.top >= previous_bottom && bounds.bottom <= footer.top);
        CHECK(bounds.bottom <= client.bottom && bounds.top >= client.top);
        previous_bottom = bounds.bottom;
        GetWindowTextA(child, text, sizeof text);
        measured.left = measured.top = 0;
        measured.right = bounds.right - bounds.left; measured.bottom = 0;
        HDC dc = GetDC(child);
        HFONT previous = SelectObject(dc, (HFONT)SendMessageA(child, WM_GETFONT, 0, 0));
        DrawTextA(dc, text, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, previous); ReleaseDC(child, dc);
        CHECK(measured.bottom <= bounds.bottom - bounds.top + 2);
    }
}

static void focus_control(HWND window, int id)
{
    static HWND previous;
    HWND child = GetDlgItem(window, id);
    CHECK(child);
    /* Deliver to the real child subclass, on its owner thread, without global
     * keyboard/mouse input or dependence on the desktop's foreground window. */
    if (previous != child && IsWindow(previous)) SendMessageA(previous, WM_KILLFOCUS, (WPARAM)child, 0);
    SendMessageA(child, WM_SETFOCUS, 0, 0);
    previous = child;
}

static void choose(HWND window, int id, int value)
{
    HWND child = GetDlgItem(window, id);
    combo_select(child, value);
    CHECK(combo_value(child) == value);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), (LPARAM)child);
}

static void edit_value(HWND window, int id, const char *value)
{
    HWND child = GetDlgItem(window, id);
    CHECK(SetWindowTextA(child, value));
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(id, id == ID_FPS ? CBN_EDITCHANGE : EN_CHANGE), (LPARAM)child);
}

static void controllers_layout(HWND window)
{
    Launcher *ui = (Launcher *)GetWindowLongPtrA(window, GWLP_USERDATA);
    RECT client, count, status, bindings, hint = child_bounds(window, ID_PAD_HINT);
    GetClientRect(window, &client);
    GetWindowRect(GetDlgItem(window, ID_PAD_COUNT), &count);
    GetWindowRect(GetDlgItem(window, ID_PAD_STATUS), &status);
    GetWindowRect(GetDlgItem(window, ID_GROUP), &bindings);
    MapWindowPoints(NULL, window, (POINT *)&count, 2);
    MapWindowPoints(NULL, window, (POINT *)&status, 2);
    MapWindowPoints(NULL, window, (POINT *)&bindings, 2);
    CHECK(IsWindowVisible(GetDlgItem(window, ID_PAD_COUNT)));
    CHECK(IsWindowVisible(GetDlgItem(window, ID_PAD_STATUS)));
    CHECK(IsWindowVisible(GetDlgItem(window, ID_PAD_HINT)));
    CHECK(count.left >= client.left && count.right <= status.left && count.top >= client.top);
    CHECK(status.right <= client.right && status.bottom <= bindings.top);
    CHECK(count.bottom <= bindings.top);
    CHECK(hint.top >= count.bottom && hint.right <= status.left);
    CHECK(hint.bottom <= bindings.top - (LONG)lroundf(24.f * ui->scale));
    CHECK(status.bottom <= child_bounds(window, ID_BIND_LABEL).top);
    text_fits(window, ID_PAD_STATUS); text_fits(window, ID_PAD_HINT);
    controller_summary_layout(window);
}

static HWND wait_launcher(void)
{
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 10000) {
        HWND window = NULL;
        while ((window = FindWindowExA(NULL, window, "MM3NativeLauncher", NULL)) != NULL) {
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (pid == GetCurrentProcessId() && IsWindowVisible(window) && GetDlgItem(window, IDOK)) return window;
        }
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

static void expect_ports(HWND window, unsigned mask)
{
    char status[1024], summary[256], expected[256];
    int count = 0, rows = 1;
    GetDlgItemTextA(window, ID_PAD_STATUS, status, sizeof status);
    GetDlgItemTextA(window, ID_PAD_SUMMARY, summary, sizeof summary);
    for (int i = 0; i < 4; ++i) {
        int connected = (mask & (1u << i)) != 0;
        snprintf(expected, sizeof expected, "P%d: %s", i + 1, connected ? "connected" : "not connected");
        CHECK(strstr(status, expected));
        count += connected;
    }
    for (const char *at = status; *at; ++at) if (*at == '\n') ++rows;
    CHECK(rows == 4);
    if (count) snprintf(expected, sizeof expected, "%s detected: %d connected. Test sticks/triggers in Controls.",
        count == 1 ? "Controller" : "Controllers", count);
    else snprintf(expected, sizeof expected, "No controller detected (XInput). Check Controls.");
    CHECK(!strcmp(summary, expected));
    controller_summary_layout(window);
}

static void timer_ports(HWND window, unsigned mask, int active_port, const XINPUT_GAMEPAD *active)
{
    fake_ports(mask, active_port, active);
    SendMessageA(window, WM_TIMER, 1, 0);
    expect_ports(window, mask);
}

static void controller_detection(HWND window, const char *capture_name)
{
    char text[1024];
    XINPUT_GAMEPAD active = {0};
    choose(window, ID_PAD_COUNT, 1);
    select_page(window, 1);
    timer_ports(window, 0, -1, NULL);
    GetDlgItemTextA(window, ID_PAD_HINT, text, sizeof text);
    CHECK(strstr(text, "No XInput controller detected") && strstr(text, "XInput mode"));
    controllers_layout(window);

    /* A newly connected held button must be visible without navigating the UI. */
    active.wButtons = XINPUT_GAMEPAD_A;
    timer_ports(window, 1, 0, &active);
    GetDlgItemTextA(window, ID_PAD_STATUS, text, sizeof text);
    CHECK(strstr(text, "P1: connected (A)"));
    for (int page = 0; page < 3; ++page) {
        select_page(window, page);
        expect_ports(window, 1);
        CHECK((IsWindowVisible(GetDlgItem(window, ID_PAD_STATUS)) != 0) == (page == 1));
    }
    select_page(window, 1);

    active.wButtons = 0;
    active.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    active.bRightTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    active.sThumbLX = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE - 1;
    active.sThumbRY = -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE + 1;
    timer_ports(window, 1, 0, &active);
    GetDlgItemTextA(window, ID_PAD_STATUS, text, sizeof text);
    CHECK(!strstr(text, "LT") && !strstr(text, "RT") && !strstr(text, "stick"));
    active.bLeftTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD + 1;
    active.bRightTrigger = XINPUT_GAMEPAD_TRIGGER_THRESHOLD + 1;
    active.sThumbLX = -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE - 1;
    active.sThumbRY = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE + 1;
    timer_ports(window, 1, 0, &active);
    GetDlgItemTextA(window, ID_PAD_STATUS, text, sizeof text);
    CHECK(strstr(text, "LT") && strstr(text, "RT"));
    CHECK(strstr(text, "Left stick") && strstr(text, "Right stick"));
    controllers_layout(window);

    timer_ports(window, 15, 0, &active);
    GetDlgItemTextA(window, ID_PAD_HINT, text, sizeof text);
    CHECK(strstr(text, "Controllers in game") && strstr(text, "4"));
    controllers_layout(window);
    choose(window, ID_PAD_COUNT, 4);
    SendMessageA(window, WM_TIMER, 1, 0);
    GetDlgItemTextA(window, ID_PAD_HINT, text, sizeof text);
    CHECK(strstr(text, "4 connected") && !strstr(text, "outside"));
    controllers_layout(window);
    capture_page(window, capture_name);

    choose(window, ID_PAD_COUNT, 1);
    timer_ports(window, 8, 3, &active);
    GetDlgItemTextA(window, ID_PAD_STATUS, text, sizeof text);
    CHECK(strstr(text, "P4: connected (LT, RT, Left stick, Right stick)"));
    GetDlgItemTextA(window, ID_PAD_HINT, text, sizeof text);
    CHECK(strstr(text, "P4 detected outside") && strstr(text, "Controllers in game to 4"));
    controllers_layout(window);
    choose(window, ID_PAD_COUNT, 4);
    SendMessageA(window, WM_TIMER, 1, 0);
    GetDlgItemTextA(window, ID_PAD_HINT, text, sizeof text);
    CHECK(strstr(text, "1 connected") && !strstr(text, "outside"));

    /* Simulate unplugging and reconnecting while the Settings page is open. */
    select_page(window, 0);
    timer_ports(window, 0, -1, NULL);
    timer_ports(window, 1, -1, NULL);
    select_page(window, 1);
    expect_ports(window, 1);
    controllers_layout(window);
    timer_ports(window, 0, -1, NULL);
    fake_raw_device(RIM_TYPEHID, 0x054c, 0x0ce6, 1, 5);
    SendMessageA(window, WM_DEVICECHANGE, 0, 0);
    GetDlgItemTextA(window, ID_PAD_SUMMARY, text, sizeof text);
    CHECK(!strcmp(text, "DualSense connected. XInput needed for play (see Controls)."));
    GetDlgItemTextA(window, ID_PAD_HINT, text, sizeof text);
    CHECK(strstr(text, "DualSense found") && strstr(text, "Steam Input"));
    controllers_layout(window);
    /* A real HID may remain present after an input wrapper exposes XInput. */
    timer_ports(window, 1, -1, NULL);
    fake_raw_reset();
    SendMessageA(window, WM_DEVICECHANGE, 0, 0);
    expect_ports(window, 1);
    timer_ports(window, 0, -1, NULL);
    choose(window, ID_PAD_COUNT, 1);
    select_page(window, 0);
}

static LRESULT CALLBACK count_text_updates(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR subclass, DWORD_PTR reference)
{
    (void)subclass;
    if (message == WM_SETTEXT) ++*(int *)reference;
    return DefSubclassProc(window, message, wparam, lparam);
}

static void stable_controller_text(HWND window)
{
    static const int ids[] = {ID_PAD_SUMMARY, ID_PAD_STATUS, ID_PAD_HINT};
    int writes[3] = {0};
    /* This helper runs on the window owner thread so the native subclass and
     * its stack counters have a controlled lifetime. */
    CHECK(GetWindowThreadProcessId(window, NULL) == GetCurrentThreadId());
    timer_ports(window, 0, -1, NULL);
    for (int i = 0; i < 3; ++i)
        CHECK(SetWindowSubclass(GetDlgItem(window, ids[i]), count_text_updates, 47, (DWORD_PTR)&writes[i]));
    SendMessageA(window, WM_TIMER, 1, 0);
    SendMessageA(window, WM_TIMER, 1, 0);
    for (int i = 0; i < 3; ++i) CHECK(writes[i] == 0);
    timer_ports(window, 1, -1, NULL);
    for (int i = 0; i < 3; ++i) CHECK(writes[i] == 1);
    SendMessageA(window, WM_TIMER, 1, 0);
    for (int i = 0; i < 3; ++i) {
        CHECK(writes[i] == 1);
        CHECK(RemoveWindowSubclass(GetDlgItem(window, ids[i]), count_text_updates, 47));
    }
    timer_ports(window, 0, -1, NULL);
}

static void contextual_help(HWND window)
{
    static const int settings[] = {ID_RESOLUTION, ID_MODE, ID_SCREEN, ID_FPS, ID_VSYNC, ID_SCALE,
        ID_ASPECT, ID_MSAA, ID_ANISO, ID_DRAW, ID_VIEW, ID_SHOW_FPS, ID_MINIMAP,
        ID_PORT, ID_PEERS};
    static const int choices[] = {ID_RESOLUTION, ID_MODE, ID_SCREEN, ID_SCALE, ID_ASPECT,
        ID_MSAA, ID_ANISO, ID_DRAW, ID_VIEW, ID_MINIMAP};
    static const int controls[] = {ID_KEYBOARD, ID_PAD_COUNT, ID_GROUP, ID_BINDINGS,
        ID_SOURCE, ID_PAD_PORT, ID_CAPTURE, ID_UNBIND};
    static const int buttons[] = {ID_DEFAULTS, ID_UPDATE, IDOK, IDCANCEL};
    HelpText text, previous;
    char page_titles[LAUNCHER_PAGES][256];
    for (int page = 0; page < LAUNCHER_PAGES; ++page) {
        select_page(window, page);
        help_text(window, &text); help_layout(window);
        snprintf(page_titles[page], sizeof page_titles[page], "%s", text.title);
        for (int other = 0; other < page; ++other) CHECK(strcmp(page_titles[other], page_titles[page]));
    }

    select_page(window, 0);
    for (int i = 0; i < (int)(sizeof settings / sizeof settings[0]); ++i) {
        focus_control(window, settings[i]); help_text(window, &text); help_layout(window);
        CHECK(*text.value);
        CHECK(strcmp(text.title, page_titles[0]));
    }
    /* A keyboard focus change and pointer hover both update the same panel. */
    focus_control(window, ID_ANISO); help_text(window, &previous);
    SendMessageA(GetDlgItem(window, ID_RESOLUTION), WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    help_text(window, &text); CHECK(strcmp(previous.title, text.title));
    focus_control(window, ID_ANISO); help_text(window, &text);
    CHECK(!strcmp(previous.title, text.title));

    for (int i = 0; i < (int)(sizeof choices / sizeof choices[0]); ++i) {
        HWND child = GetDlgItem(window, choices[i]);
        int count = (int)SendMessageA(child, CB_GETCOUNT, 0, 0);
        previous.value[0] = 0;
        CHECK(count >= 2);
        for (int at = 0; at < count; ++at) {
            SendMessageA(child, CB_SETCURSEL, at, 0);
            SendMessageA(window, WM_COMMAND, MAKEWPARAM(choices[i], CBN_SELCHANGE), (LPARAM)child);
            help_text(window, &text); help_layout(window);
            CHECK(strcmp(previous.value, text.value));
            snprintf(previous.value, sizeof previous.value, "%s", text.value);
        }
    }
    choose(window, ID_RESOLUTION, 2560 * 10000 + 1440);
    CHECK(help_contains(window, "2560") && help_contains(window, "1440"));
    choose(window, ID_MSAA, 4);
    CHECK(help_contains(window, "4x"));
    CHECK(help_contains(window, "edge") || help_contains(window, "jagged") || help_contains(window, "smooth"));
    choose(window, ID_SCALE, 3000);
    CHECK(help_contains(window, "1440p"));
    CHECK(help_contains(window, "pixel") || help_contains(window, "resolution"));
    edit_value(window, ID_FPS, "30"); help_text(window, &previous);
    CHECK(help_contains(window, "30"));
    edit_value(window, ID_FPS, "abc"); help_text(window, &text);
    CHECK(strcmp(previous.value, text.value));
    CHECK(help_contains(window, "1") && help_contains(window, "60"));
    CHECK(help_contains(window, "invalid") || help_contains(window, "enter") || help_contains(window, "between"));
    edit_value(window, ID_FPS, "60"); CHECK(help_contains(window, "60"));
    edit_value(window, ID_PORT, "3076"); CHECK(help_contains(window, "3076"));
    edit_value(window, ID_PORT, "0");
    CHECK(help_contains(window, "65535"));
    edit_value(window, ID_PEERS, "localhost:3075"); help_text(window, &text);
    CHECK(contains(text.value, "1") && contains(text.value, "peer"));
    const char *peers = "192.168.1.11:3075,192.168.1.12:3075,192.168.1.13:3075,192.168.1.14:3075,"
        "192.168.1.15:3075,192.168.1.16:3075,192.168.1.17:3075,192.168.1.18:3075";
    edit_value(window, ID_PEERS, peers); help_text(window, &text); help_layout(window);
    CHECK(contains(text.value, "8") && contains(text.value, "peer"));
    char full_peers[512]; GetDlgItemTextA(window, ID_PEERS, full_peers, sizeof full_peers);
    CHECK(!strcmp(full_peers, peers));

    HWND vsync = GetDlgItem(window, ID_VSYNC);
    for (int on = 0; on < 2; ++on) {
        SendMessageA(vsync, BM_SETCHECK, on ? BST_UNCHECKED : BST_CHECKED, 0);
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_VSYNC, BN_CLICKED), (LPARAM)vsync);
        help_text(window, &text);
        CHECK(contains(text.value, on ? "On" : "Off"));
    }

    select_page(window, 1);
    for (int i = 0; i < (int)(sizeof controls / sizeof controls[0]); ++i) {
        focus_control(window, controls[i]); help_text(window, &text); help_layout(window);
        CHECK(*text.value);
        CHECK(strcmp(text.title, page_titles[1]));
    }
    choose(window, ID_PAD_COUNT, 4); CHECK(help_contains(window, "4"));
    choose(window, ID_GROUP, 2);
    HWND list = GetDlgItem(window, ID_BINDINGS);
    SendMessageA(list, LB_SETCURSEL, 1, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_BINDINGS, LBN_SELCHANGE), (LPARAM)list);
    CHECK(help_contains(window, "Brake"));
    choose(window, ID_GROUP, 0);

    select_page(window, 2);
    edit_value(window, ID_FOV, "95"); help_text(window, &previous);
    CHECK(help_contains(window, "95"));
    CHECK(help_contains(window, "wide") || help_contains(window, "view"));
    edit_value(window, ID_FOV, "25"); help_text(window, &text);
    CHECK(strcmp(previous.value, text.value));
    CHECK(help_contains(window, "60") && help_contains(window, "110"));
    CHECK(help_contains(window, "invalid") || help_contains(window, "enter") || help_contains(window, "must"));
    edit_value(window, ID_FOV, "0");
    CHECK(help_contains(window, "retail") || help_contains(window, "original"));
    select_page(window, 3);
    focus_control(window, ID_REQUIRED); help_text(window, &text); help_layout(window);
    CHECK(!IsWindowEnabled(GetDlgItem(window, ID_REQUIRED)) && IsWindowVisible(GetDlgItem(window, ID_REQUIRED)));
    CHECK(help_contains(window, "required") || help_contains(window, "always"));

    /* Original or Improved look: a pair of choices, the visual mods greyed
     * out in the Original look and every choice there kept. */
    select_page(window, 2);
    {
        HWND original = GetDlgItem(window, ID_ORIGINAL), improved = GetDlgItem(window, ID_OPTION + MM3_LAUNCH_IMPROVED);
        HWND lights = GetDlgItem(window, ID_OPTION + MM3_LAUNCH_CAR_LIGHT), sky = GetDlgItem(window, ID_OPTION + MM3_LAUNCH_SKY);
        HWND trees = GetDlgItem(window, ID_OPTION + MM3_LAUNCH_TREES);
        int lights_on = SendMessageA(lights, BM_GETCHECK, 0, 0) == BST_CHECKED;
        CHECK(original && improved && IsWindowVisible(original) && IsWindowVisible(improved));
        CHECK(SendMessageA(improved, BM_GETCHECK, 0, 0) == BST_CHECKED && SendMessageA(original, BM_GETCHECK, 0, 0) != BST_CHECKED);
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_ORIGINAL, BN_CLICKED), (LPARAM)original);
        CHECK(SendMessageA(improved, BM_GETCHECK, 0, 0) != BST_CHECKED && SendMessageA(original, BM_GETCHECK, 0, 0) == BST_CHECKED);
        CHECK(help_contains(window, "Original") && help_contains(window, "On"));
        CHECK(!IsWindowEnabled(lights) && IsWindowEnabled(sky) && IsWindowEnabled(GetDlgItem(window, ID_FOV)));
        /* Settings the Original look fixes at the Xbox's are greyed out too. */
        CHECK(!IsWindowEnabled(GetDlgItem(window, ID_DRAW)) && !IsWindowEnabled(GetDlgItem(window, ID_VIEW)) &&
              !IsWindowEnabled(GetDlgItem(window, ID_MINIMAP)) && IsWindowEnabled(GetDlgItem(window, ID_MSAA)));
        CHECK((SendMessageA(lights, BM_GETCHECK, 0, 0) == BST_CHECKED) == lights_on);
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_ORIGINAL, BN_CLICKED), (LPARAM)original);
        CHECK(SendMessageA(original, BM_GETCHECK, 0, 0) == BST_CHECKED); /* A choice, not a toggle. */
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_OPTION + MM3_LAUNCH_IMPROVED, BN_CLICKED), (LPARAM)improved);
        CHECK(SendMessageA(improved, BM_GETCHECK, 0, 0) == BST_CHECKED && SendMessageA(original, BM_GETCHECK, 0, 0) != BST_CHECKED);
        CHECK(IsWindowEnabled(lights) && help_contains(window, "Improved"));
        CHECK(IsWindowEnabled(GetDlgItem(window, ID_DRAW)));
        /* The look never touches the gameplay mods. */
        CHECK(!IsWindowVisible(trees));
    }
    /* Xbox or Modded gameplay: Xbox gameplay by default, the gameplay mods
     * greyed out with it and the minimap fixed at the Xbox's. */
    select_page(window, 3);
    {
        HWND xbox = GetDlgItem(window, ID_XBOX_GAMEPLAY), modded = GetDlgItem(window, ID_OPTION + MM3_LAUNCH_GAMEPLAY);
        HWND trees = GetDlgItem(window, ID_OPTION + MM3_LAUNCH_TREES), arrow = GetDlgItem(window, ID_OPTION + MM3_LAUNCH_RACE_ARROW);
        int trees_on = SendMessageA(trees, BM_GETCHECK, 0, 0) == BST_CHECKED;
        CHECK(xbox && modded && IsWindowVisible(xbox) && IsWindowVisible(modded) && IsWindowVisible(trees));
        CHECK(SendMessageA(xbox, BM_GETCHECK, 0, 0) == BST_CHECKED && SendMessageA(modded, BM_GETCHECK, 0, 0) != BST_CHECKED);
        CHECK(!IsWindowEnabled(trees) && !IsWindowEnabled(arrow) && !IsWindowEnabled(GetDlgItem(window, ID_MINIMAP)));
        CHECK(IsWindowEnabled(GetDlgItem(window, ID_DRAW)));
        focus_control(window, ID_OPTION + MM3_LAUNCH_RACE_ARROW); help_text(window, &text);
        CHECK(contains(text.title, "Hide") && help_contains(window, "shows it"));
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_OPTION + MM3_LAUNCH_GAMEPLAY, BN_CLICKED), (LPARAM)modded);
        CHECK(SendMessageA(modded, BM_GETCHECK, 0, 0) == BST_CHECKED && SendMessageA(xbox, BM_GETCHECK, 0, 0) != BST_CHECKED);
        CHECK(help_contains(window, "Modded") && help_contains(window, "On"));
        CHECK(IsWindowEnabled(trees) && IsWindowEnabled(arrow) && IsWindowEnabled(GetDlgItem(window, ID_MINIMAP)));
        CHECK((SendMessageA(trees, BM_GETCHECK, 0, 0) == BST_CHECKED) == trees_on);
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_OPTION + MM3_LAUNCH_GAMEPLAY, BN_CLICKED), (LPARAM)modded);
        CHECK(SendMessageA(modded, BM_GETCHECK, 0, 0) == BST_CHECKED); /* A choice, not a toggle. */
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_XBOX_GAMEPLAY, BN_CLICKED), (LPARAM)xbox);
        CHECK(SendMessageA(xbox, BM_GETCHECK, 0, 0) == BST_CHECKED && !IsWindowEnabled(trees));
        CHECK(help_contains(window, "Xbox") && help_contains(window, "On"));
    }
    for (int i = 1; i < MM3_LAUNCH_OPTION_COUNT; ++i) {
        int id = ID_OPTION + i;
        HWND child = GetDlgItem(window, id);
        if (i == MM3_LAUNCH_IMPROVED || i == MM3_LAUNCH_GAMEPLAY) continue;
        select_page(window, i == MM3_LAUNCH_DSP || i == MM3_LAUNCH_NETWORK ? 0 :
                    mm3_launcher_option_master(i) == MM3_LAUNCH_GAMEPLAY ? 3 : 2);
        CHECK(child && IsWindowVisible(child));
        focus_control(window, id); help_text(window, &previous); help_layout(window);
        CHECK(*previous.value);
        CHECK(strstr(previous.title, option_caption(i)));
        int before = SendMessageA(child, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SendMessageA(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)child);
        help_text(window, &text); help_layout(window);
        CHECK(strcmp(previous.value, text.value));
        CHECK(contains(text.value, before ? "Off" : "On"));
    }
    for (int i = 0; i < (int)(sizeof buttons / sizeof buttons[0]); ++i) {
        focus_control(window, buttons[i]); help_text(window, &text); help_layout(window);
    }
    SendMessageA(window, WM_COMMAND, ID_DEFAULTS, 0);
    select_page(window, 0);
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
/* The desktop's real pointer, wherever it rests over the window, makes Windows
 * send WM_MOUSEMOVE as controls show and hide, and the launcher then shows the
 * help of the control under it: the help panel changed between a check's
 * delivered message and its read, at a different check each run. Let the
 * pointer pass through the window; hover checks deliver their own moves. */
static void ignore_pointer(HWND window)
{
    LONG_PTR style = GetWindowLongPtrA(window, GWL_EXSTYLE);
    SetWindowLongPtrA(window, GWL_EXSTYLE, style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    CHECK(SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA));
}

static DWORD WINAPI exercise(void *unused)
{
    (void)unused;
    HWND window = wait_launcher();
    RECT bounds, work;
    ignore_pointer(window);
    GetWindowRect(window, &bounds); SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    CHECK(bounds.left >= work.left && bounds.top >= work.top && bounds.right <= work.right && bounds.bottom <= work.bottom);
    HWND play = GetDlgItem(window, IDOK); RECT footer;
    GetWindowRect(play, &footer);
    CHECK(footer.bottom <= bounds.bottom && footer.top >= bounds.top && IsWindowVisible(play));
    tab_bar_clear(window);
    controller_detection(window, "controls-detected");
    contextual_help(window);
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
    HelpText hint; help_text(window, &hint); CHECK(strstr(hint.value, "<- W"));
    capture_page(window, "controls");
    SendMessageA(group, CB_SETCURSEL, 0, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_GROUP, CBN_SELCHANGE), (LPARAM)group);
    SendMessageA(list, LB_SETCURSEL, 0, 0);
    HWND source = GetDlgItem(window, ID_SOURCE);
    SendMessageA(source, CB_SETCURSEL, 15, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_SOURCE, CBN_SELCHANGE), (LPARAM)source);
    GetDlgItemTextA(window, ID_BIND_LABEL, status, sizeof status); CHECK(strstr(status, "RT"));
    help_text(window, &hint); CHECK(strstr(hint.value, "<- RT"));
    select_page(window, 2);
    CHECK(IsWindowVisible(GetDlgItem(window, ID_FOV)));
    SetDlgItemTextA(window, ID_FOV, "95");
    SendMessageA(GetDlgItem(window, ID_OPTION + MM3_LAUNCH_WIDESCREEN), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(window, ID_OPTION + MM3_LAUNCH_DISTANCE), BM_SETCHECK, BST_UNCHECKED, 0);
    focus_control(window, ID_OPTION + MM3_LAUNCH_CAR_LIGHT);
    capture_page(window, "mods");
    select_page(window, 0);
    SetDlgItemTextA(window, ID_FPS, "30");
    SendMessageA(GetDlgItem(window, ID_VSYNC), BM_SETCHECK, BST_CHECKED, 0);
    choose(window, ID_MSAA, 4);
    choose(window, ID_SCREEN, 1);
    CHECK(help_contains(window, "Screen") && help_contains(window, "Selected"));
    capture_page(window, "settings");
    SendMessageA(window, WM_COMMAND, s_play ? IDOK : IDCANCEL, 0);
    return 0;
}
static int s_update_run;   /* 0: answers; 1: Cancel during a slow check; 2: download and open Setup */
static const char *s_update_port;

static void update_api_for(const char *scenario)
{
    snprintf(s_update_api, sizeof s_update_api, "http://127.0.0.1:%s/%s/latest", s_update_port, scenario);
}

static void update_wait_status(HWND window, const char *wanted)
{
    DWORD start = GetTickCount();
    char status[512];
    for (;;) {
        GetDlgItemTextA(window, ID_STATUS, status, sizeof status);
        if (strstr(status, wanted)) return;
        if (GetTickCount() - start > 15000) { fprintf(stderr, "update status was \"%s\", wanted \"%s\"\n", status, wanted); CHECK(0); }
        Sleep(20);
    }
}

static void update_click(HWND window)
{
    CHECK(IsWindowEnabled(GetDlgItem(window, ID_UPDATE)));
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_UPDATE, BN_CLICKED), (LPARAM)GetDlgItem(window, ID_UPDATE));
}

static DWORD WINAPI update_exercise(void *unused)
{
    (void)unused;
    HWND window = wait_launcher();
    ignore_pointer(window);
    HWND button = GetDlgItem(window, ID_UPDATE), play = GetDlgItem(window, IDOK), defaults = GetDlgItem(window, ID_DEFAULTS);
    RECT b, p, d;
    GetWindowRect(button, &b); GetWindowRect(play, &p); GetWindowRect(defaults, &d);
    CHECK(IsWindowVisible(button) && b.left >= d.right && b.right <= p.left && b.top == p.top && b.bottom == p.bottom);
    if (s_update_run == 0) {
        update_api_for("same");
        update_click(window);
        update_wait_status(window, "latest version (2.1)");
        CHECK(s_update_asked == 0);
        /* No connection: a plain message, and the button works again. */
        snprintf(s_update_api, sizeof s_update_api, "http://127.0.0.1:1/newer/latest");
        update_click(window);
        update_wait_status(window, "Could not reach GitHub");
        update_api_for("nosum");
        update_click(window);
        update_wait_status(window, "not attached");
        /* A newer release, declined. */
        s_update_answer = 0;
        update_api_for("newer");
        update_click(window);
        update_wait_status(window, "Version 99.0 is available");
        CHECK(s_update_asked == 1 && s_update_opened == 0);
        /* A newer release whose download does not match its checksum: nothing opens. */
        s_update_answer = 1;
        update_api_for("badsum");
        update_click(window);
        update_wait_status(window, "does not match the release's checksum");
        CHECK(s_update_asked == 2 && s_update_opened == 0 && IsWindowEnabled(button));
        /* The button's explanation. */
        focus_control(window, ID_UPDATE);
        CHECK(help_contains(window, "Check for updates") && help_contains(window, "GitHub"));
        SendMessageA(window, WM_COMMAND, IDCANCEL, 0);
    } else if (s_update_run == 1) {
        char status[512];
        update_api_for("slow");
        update_click(window);
        Sleep(200);
        CHECK(!IsWindowEnabled(button));
        SendMessageA(window, WM_COMMAND, IDOK, 0);       /* Play waits for the update step */
        GetDlgItemTextA(window, ID_STATUS, status, sizeof status);
        CHECK(strstr(status, "Wait for the update"));
        SendMessageA(window, WM_COMMAND, IDCANCEL, 0);   /* Cancel stops it, then closes */
        GetDlgItemTextA(window, ID_STATUS, status, sizeof status);
        CHECK(strstr(status, "Stopping"));
        CHECK(IsWindow(window));
    } else {
        s_update_answer = 1;
        update_api_for("newer");
        update_click(window);   /* asks, downloads, opens Setup and closes: nothing more to do here */
    }
    return 0;
}

static void update_runs(const char *port, const char *folder)
{
    wchar_t expected[700];
    HANDLE thread, file;
    LARGE_INTEGER size;
    s_update_port = port;
    CHECK(!_putenv_s("MM3_LAUNCHER", "1")); CHECK(!_putenv_s("MM3_LAUNCHER_SMOKE", ""));
    for (s_update_run = 0; s_update_run < 3; ++s_update_run) {
        s_update_opened = 0; s_update_setup[0] = 0;
        thread = CreateThread(NULL, 0, update_exercise, NULL, 0, NULL); CHECK(thread);
        CHECK(!mm3_launcher_run());
        CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0); CloseHandle(thread);
        if (s_update_run < 2) CHECK(s_update_opened == 0);
    }
    /* The last run: Setup was downloaded to updates\ beside this program and then opened. */
    swprintf(expected, sizeof expected / sizeof expected[0], L"%hs\\updates\\MM3-Setup.exe", folder);
    CHECK(s_update_opened == 1 && !_wcsicmp(s_update_setup, expected));
    file = CreateFileW(expected, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    CHECK(file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && size.QuadPart == 3000000);
    CloseHandle(file);
}
static void smaller_layout(void)
{
    Launcher ui;
    RECT work = {0, 0, 1280, 680}, bounds, footer;
    memset(&ui, 0, sizeof ui);
    mm3_launcher_config_defaults(&ui.config);
    mm3_display_settings_defaults(&ui.display, NULL);
    ui.display.aniso = 3; /* Valid saved values need not be predefined choices. */
    strcpy(ui.display.monitor, "\\\\.\\DISPLAY99"); /* Saved, but not connected now. */
    ui.scale = layout_scale(&work, 1.5f); /* High DPI, 720p with a taskbar. */
    bounds.left = bounds.top = 0;
    bounds.right = (LONG)lroundf((float)LAUNCHER_WIDTH * ui.scale);
    bounds.bottom = (LONG)lroundf((float)LAUNCHER_HEIGHT * ui.scale);
    AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_CONTROLPARENT);
    fake_ports(8, -1, NULL);
    HWND window = CreateWindowExA(WS_EX_CONTROLPARENT, "MM3NativeLauncher", "Midtown Madness 3 - 720p layout check",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0,
        bounds.right - bounds.left, bounds.bottom - bounds.top, NULL, NULL, GetModuleHandleA(NULL), &ui);
    CHECK(window); ignore_pointer(window); ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window);
    /* No messages have been pumped: connection discovery must happen during
     * creation instead of waiting for the first 70 ms timer. */
    expect_ports(window, 8);
    stable_controller_text(window);
    controller_detection(window, "controls-detected-720p");
    GetWindowRect(window, &bounds); GetWindowRect(GetDlgItem(window, IDOK), &footer);
    CHECK(bounds.bottom <= work.bottom && bounds.right <= work.right);
    CHECK(footer.bottom < bounds.bottom && IsWindowVisible(GetDlgItem(window, IDOK)));
    CHECK(combo_value(GetDlgItem(window, ID_ANISO)) == 3);
    /* A saved screen that is not connected stays chosen and is kept. */
    CHECK(combo_value(GetDlgItem(window, ID_SCREEN)) == SCREEN_MISSING);
    focus_control(window, ID_SCREEN);
    CHECK(help_contains(window, "DISPLAY99") && help_contains(window, "not connected"));
    { char read_error[256]; CHECK(read_controls(&ui, read_error, sizeof read_error));
      CHECK(!strcmp(ui.display.monitor, "\\\\.\\DISPLAY99")); }
    choose(window, ID_SCREEN, SCREEN_MAIN);
    { char read_error[256]; CHECK(read_controls(&ui, read_error, sizeof read_error));
      CHECK(!strcmp(ui.display.monitor, "primary")); }
    CHECK(help_contains(window, "Now: Screen"));
    focus_control(window, ID_ANISO);
    HelpText hint; help_text(window, &hint); CHECK(contains(hint.value, "3x"));
    contextual_help(window);
    help_layout(window);
    focus_control(window, ID_MSAA);
    capture_page(window, "settings-720p");
    page_show(&ui, 1); controllers_layout(window); help_layout(window);
    CHECK(combo_value(GetDlgItem(window, ID_PAD_COUNT)) == 1);
    choose(window, ID_GROUP, 2);
    HWND list = GetDlgItem(window, ID_BINDINGS);
    SendMessageA(list, LB_SETCURSEL, 0, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(ID_BINDINGS, LBN_SELCHANGE), (LPARAM)list);
    SetFocus(list); CHECK(GetFocus() == list);
    pad_navigation(&ui, XINPUT_GAMEPAD_DPAD_DOWN);
    CHECK(SendMessageA(list, LB_GETCURSEL, 0, 0) == 1);
    help_text(window, &hint); CHECK(contains(hint.value, "Brake"));
    pad_navigation(&ui, XINPUT_GAMEPAD_DPAD_UP);
    CHECK(SendMessageA(list, LB_GETCURSEL, 0, 0) == 0);
    help_text(window, &hint); CHECK(contains(hint.value, "Accelerate"));
    choose(window, ID_GROUP, 0);
    focus_control(window, ID_BINDINGS);
    capture_page(window, "controls-720p");
    page_show(&ui, 2); help_layout(window); capture_page(window, "mods-720p");
    page_show(&ui, 3); help_layout(window); capture_page(window, "gameplay-720p");
    DestroyWindow(window); DeleteObject(ui.font); DeleteObject(ui.heading);
}
int main(int argc, char **argv)
{
    char display_path[32768], launcher_path[32768], error[512];
    HANDLE thread;
    MM3LauncherConfig config;
    MM3DisplaySettings display;
    CHECK(argc == 3);   /* the folder, and the update stand-in's port */
    dualsense_detection_contract();
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
    /* Xbox gameplay by default: the game sees every gameplay mod off. */
    CHECK(!config.option[MM3_LAUNCH_GAMEPLAY] && !strcmp(getenv("MM3_GAMEPLAY_MODS"), "0"));
    CHECK(!strcmp(getenv("MM3_EXTRA_CIRCUITS"), "0") && !strcmp(getenv("MM3_HUD_RACE_ARROW"), "1"));
    mm3_display_settings_defaults(&display, NULL);
    CHECK(mm3_display_settings_load(&display, error, sizeof error));
    CHECK(display.fps_limit == 30 && display.vsync);
    /* Screen 1 of this PC, by its Windows device name, reaches the game. */
    CHECK(!strncmp(display.monitor, "\\\\.\\", 4) && !strcmp(getenv("RECOMP_MONITOR"), display.monitor));
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
    update_runs(argv[2], argv[1]);
    puts("PASS launcher UI: deterministic XInput detection (zero/one/four and sparse ports), DualSense/Edge HID filtering and hotplug, live buttons/triggers/sticks, unplug/reconnect, persistent summary, stable idle text, normal/720p controller/help layout, contextual explanations and live values, keyboard capture/reserved keys, pad mapping, Cancel isolation, Play persistence, defaults, explicit USB overrides and bypass bootstrap, Check for updates (latest, offline, declined, bad checksum, Cancel during a check, download then Setup opened)");
    return 0;
}
