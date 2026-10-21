/* Exercise the real hidden window, settings shortcut and input suppression. */
#include <windows.h>
#include <xinput.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static BOOL fixture_connected = TRUE;
static DWORD WINAPI fixture_get_state(DWORD port, XINPUT_STATE *state)
{
    if (!fixture_connected || port != 0) return ERROR_DEVICE_NOT_CONNECTED;
    memset(state, 0, sizeof *state);
    state->dwPacketNumber = 71;
    state->Gamepad.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_UP;
    state->Gamepad.bRightTrigger = 255;
    return ERROR_SUCCESS;
}
static DWORD WINAPI fixture_set_state(DWORD port, XINPUT_VIBRATION *state)
{ (void)port; (void)state; return ERROR_SUCCESS; }
static DWORD WINAPI fixture_get_caps(DWORD port, DWORD flags, XINPUT_CAPABILITIES *caps)
{ (void)port; (void)flags; memset(caps, 0, sizeof *caps); return ERROR_SUCCESS; }

static HWND WINAPI create_hidden_window(DWORD extended, LPCSTR name, LPCSTR title,
    DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu,
    HINSTANCE instance, LPVOID parameter)
{
    return CreateWindowExA(extended, name, title, style & ~WS_VISIBLE, x, y,
        width, height, parent, menu, instance, parameter);
}

#define XInputGetState fixture_get_state
#define XInputSetState fixture_set_state
#define XInputGetCapabilities fixture_get_caps
#include "../tools/xboxrecomp/src/input/xinput_device.c"
#define CreateWindowExA create_hidden_window
#include "../tools/xboxrecomp/src/video/fb_present.c"
#undef CreateWindowExA
#include "../tools/xboxrecomp/src/usb/usb_gamepad.c"

ptrdiff_t xbox_GetMemoryOffset(void) { return 0; }
bool nv2a_hook_pvideo_snapshot(uint32_t regs[0x1000 / 4]) { (void)regs; return false; }
bool nv2a_hook_pvideo_scanout_snapshot(Nv2aPvideoSnapshot *snapshot) { (void)snapshot; return false; }
void nv2a_hook_pvideo_consume(unsigned bank) { (void)bank; }
float nv2a_d3d_display_aspect(void) { return 4.0f / 3.0f; }

static volatile LONG opened, closed;
static DWORD dialog_thread;
static void on_settings(HWND owner)
{
    CHECK(owner == xbox_FramebufferWindowHandle());
    CHECK(!xbox_FramebufferKeyDown('Z'));
    dialog_thread = GetCurrentThreadId();
    InterlockedIncrement(&opened);
}
static void on_close(void) { InterlockedIncrement(&closed); }

static void wait_size(HWND window, int width, int height, int borderless)
{
    ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        RECT rectangle;
        CHECK(GetClientRect(window, &rectangle));
        if (rectangle.right == width && rectangle.bottom == height &&
            ((GetWindowLongA(window, GWL_STYLE) & WS_OVERLAPPEDWINDOW) == 0) == borderless)
            return;
        Sleep(10);
    } while (GetTickCount64() < deadline);
    CHECK(0);
}

static void wait_title(HWND window, int has_fps)
{
    ULONGLONG deadline = GetTickCount64() + 3000;
    do {
        char title[256];
        GetWindowTextA(window, title, sizeof title);
        if (!!strstr(title, "FPS:") == has_fps && strstr(title, "F10: Settings")) return;
        Sleep(10);
    } while (GetTickCount64() < deadline);
    CHECK(0);
}

int main(int argc, char **argv)
{
    HANDLE thread;
    DWORD thread_id;
    HWND window = NULL;
    ULONGLONG deadline;
    MONITORINFO monitor = { sizeof monitor };
    XBOX_INPUT_STATE first, second;
    uint8_t report[20];
    DWORD active_packet;
    unsigned i;
    int preconfigured = argc == 2 && !strcmp(argv[1], "preconfigured");
    CHECK(argc == 1 || preconfigured);
    CHECK(_putenv_s("RECOMP_FULLSCREEN", preconfigured ? "1" : "0") == 0);
    CHECK(_putenv_s("RECOMP_WINDOW_SIZE", "800x600") == 0);
    CHECK(_putenv_s("RECOMP_KEYBOARD", "1") == 0);
    CHECK(_putenv_s("RECOMP_PAD_PRESS", "16,100000,50000") == 0);
    CHECK(_putenv_s("RECOMP_PAD_SCRIPT", "") == 0);
    CHECK(_putenv_s("RECOMP_PAD_LIVE", "") == 0);
    xbox_FramebufferSetCloseHandler(on_close);
    xbox_FramebufferSetSettingsHandler(on_settings);
    CHECK(!xbox_FramebufferSetDisplayMode(639, 720, 0));
    CHECK(!xbox_FramebufferSetDisplayMode(1280, 479, 0));
    CHECK(!xbox_FramebufferSetDisplayMode(7681, 720, 0));
    CHECK(!xbox_FramebufferSetDisplayMode(1280, 4321, 0));
    CHECK(!xbox_FramebufferSetDisplayMode(1280, 720, 2));
    if (preconfigured) CHECK(xbox_FramebufferSetDisplayMode(1024, 768, 0));
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InterlockedExchange(&s_fb_running, 1);
    thread = CreateThread(NULL, 0, fb_thread, NULL, 0, &thread_id);
    CHECK(thread);
    deadline = GetTickCount64() + 5000;
    while (!(window = xbox_FramebufferWindowHandle()) && GetTickCount64() < deadline)
        CHECK(WaitForSingleObject(thread, 10) == WAIT_TIMEOUT);
    CHECK(window && !IsWindowVisible(window));
    wait_size(window, preconfigured ? 1024 : 800, preconfigured ? 768 : 600, 0);
    SendMessageA(window, WM_NULL, 0, 0); /* Finish startup before querying its system menu. */
    CHECK(GetMenuState(GetSystemMenu(window, FALSE), FB_SETTINGS_COMMAND, MF_BYCOMMAND) != (UINT)-1);
    SendMessageA(window, WM_KEYDOWN, 'Z', 0);
    SendMessageA(window, WM_KEYDOWN, VK_F10, 0);
    CHECK(opened == 1 && dialog_thread == thread_id);
    SendMessageA(window, WM_KEYDOWN, VK_F10, (LPARAM)1 << 30);
    SendMessageA(window, WM_SYSKEYUP, VK_F10, 0);
    CHECK(opened == 1 && !xbox_FramebufferKeyDown(VK_F10));
    SendMessageA(window, WM_SYSCOMMAND, FB_SETTINGS_COMMAND, 0);
    CHECK(opened == 2);
    CHECK(xbox_FramebufferSetDisplayMode(1280, 720, 0));
    wait_size(window, 1280, 720, 0);
    CHECK(GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor));
    CHECK(xbox_FramebufferSetDisplayMode(1024, 768, 1));
    wait_size(window, monitor.rcMonitor.right - monitor.rcMonitor.left,
        monitor.rcMonitor.bottom - monitor.rcMonitor.top, 1);
    CHECK(xbox_FramebufferSetDisplayMode(1024, 768, 0));
    wait_size(window, 1024, 768, 0);
    SendMessageA(window, WM_KEYDOWN, VK_F11, 0);
    wait_size(window, monitor.rcMonitor.right - monitor.rcMonitor.left,
        monitor.rcMonitor.bottom - monitor.rcMonitor.top, 1);
    SendMessageA(window, WM_KEYDOWN, VK_F11, 0);
    wait_size(window, 1024, 768, 0);
    xbox_FramebufferSetShowFPS(0);
    wait_title(window, 0);
    xbox_FramebufferSetShowFPS(1);
    wait_title(window, 1);
    CHECK(xbox_InputGetState(0, &first) == ERROR_SUCCESS && first.Gamepad.bAnalogButtons[0] == 255);
    active_packet = first.dwPacketNumber;
    CHECK(usb_gamepad_report(0, report, 20) == 20 && (report[2] & XBOX_GAMEPAD_START));
    xbox_InputSetBlocked(1);
    CHECK(xbox_InputIsBlocked());
    CHECK(xbox_InputGetState(0, &first) == ERROR_SUCCESS);
    CHECK(first.dwPacketNumber > active_packet);
    CHECK(xbox_InputGetState(0, &second) == ERROR_SUCCESS && first.dwPacketNumber != second.dwPacketNumber);
    for (i = 0; i < sizeof first.Gamepad; ++i) CHECK(((uint8_t *)&first.Gamepad)[i] == 0);
    CHECK(usb_gamepad_report(0, report, 20) == 20);
    for (i = 2; i < 20; ++i) CHECK(report[i] == 0);
    for (i = 0; i < 100; ++i) CHECK(xbox_InputGetState(0, &second) == ERROR_SUCCESS);
    CHECK(xbox_InputIsConnected(0));
    CHECK(xbox_InputGetState(1, &first) == ERROR_DEVICE_NOT_CONNECTED);
    CHECK(!xbox_InputIsConnected(1));
    fixture_connected = FALSE;
    CHECK(xbox_InputGetState(0, &second) == ERROR_SUCCESS); /* Keyboard fallback stays present. */
    CHECK(!xbox_InputIsConnected(0));
    for (i = 0; i < sizeof second.Gamepad; ++i) CHECK(((uint8_t *)&second.Gamepad)[i] == 0);
    fixture_connected = TRUE;
    xbox_InputSetBlocked(0);
    CHECK(!xbox_InputIsBlocked());
    CHECK(xbox_InputGetState(0, &first) == ERROR_SUCCESS && first.Gamepad.bAnalogButtons[0] == 255);
    CHECK(first.dwPacketNumber > second.dwPacketNumber);
    CHECK(PostMessageA(window, WM_CLOSE, 0, 0));
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0 && closed == 1);
    CHECK(xbox_FramebufferWindowHandle() == NULL);
    CloseHandle(thread);
    CHECK(UnregisterClassA("XboxRecompFramebuffer", GetModuleHandleA(NULL)));
    puts("PASS: live window size/mode, startup override, F10/menu, FPS visibility, modal input and clean close");
    return 0;
}
