/* Create the production framebuffer window with its executable's icon resource. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

/* Keep the real creation and message loop, but never display the test window. */
static HWND WINAPI create_hidden_window(DWORD extended_style, LPCSTR class_name,
    LPCSTR title, DWORD style, int x, int y, int width, int height,
    HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter)
{
    return CreateWindowExA(extended_style, class_name, title,
        style & ~WS_VISIBLE, x, y, width, height, parent, menu, instance, parameter);
}

#define CreateWindowExA create_hidden_window
#include "../tools/xboxrecomp/src/video/fb_present.c"
#undef CreateWindowExA

ptrdiff_t xbox_GetMemoryOffset(void) { return 0; }
bool nv2a_hook_pvideo_snapshot(uint32_t regs[0x1000 / 4])
{ (void)regs; return false; }
void nv2a_hook_pvideo_consume(unsigned bank) { (void)bank; }
float nv2a_d3d_display_aspect(void) { return 1.6f; }

static volatile LONG close_count;
static void on_close(void) { InterlockedIncrement(&close_count); }

static void check_icon_size(HICON icon, int width, int height)
{
    ICONINFO info;
    BITMAP bitmap;
    assert(GetIconInfo(icon, &info));
    assert(info.hbmColor);
    assert(GetObjectA(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap));
    assert(bitmap.bmWidth == width && bitmap.bmHeight == height);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
}

int main(int argc, char **argv)
{
    int embedded = argc == 1;
    HINSTANCE instance = GetModuleHandleA(NULL);
    HICON large_icon, small_icon;
    HANDLE thread;
    HWND window = NULL;
    WNDCLASSEXA window_class;
    ULONGLONG deadline;
    assert(embedded || (argc == 2 && strcmp(argv[1], "without-resource") == 0));
    assert((FindResourceA(instance, MAKEINTRESOURCEA(1), RT_GROUP_ICON) != NULL)
        == embedded);
    /* The executable runs in a scratch directory containing no loose icon. */
    assert(GetFileAttributesA("mm3.ico") == INVALID_FILE_ATTRIBUTES);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    large_icon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    small_icon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    assert((large_icon != NULL) == embedded && (small_icon != NULL) == embedded);
    if (embedded) {
        check_icon_size(large_icon, GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON));
        check_icon_size(small_icon, GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON));
    }

    assert(_putenv_s("RECOMP_FULLSCREEN", "") == 0);
    assert(_putenv_s("RECOMP_WINDOW_SIZE", "160x100") == 0);
    xbox_FramebufferSetCloseHandler(on_close);
    InterlockedExchange(&s_fb_running, 1);
    thread = CreateThread(NULL, 0, fb_thread, NULL, 0, NULL);
    assert(thread);
    deadline = GetTickCount64() + 5000;
    while (!(window = xbox_FramebufferWindowHandle()) && GetTickCount64() < deadline)
        assert(WaitForSingleObject(thread, 10) == WAIT_TIMEOUT);
    assert(window && !IsWindowVisible(window));
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    assert(GetClassInfoExA(instance, "XboxRecompFramebuffer", &window_class));
    assert(window_class.hIcon == large_icon && window_class.hIconSm == small_icon);
    assert((HICON)GetClassLongPtrA(window, GCLP_HICON) == large_icon);
    assert((HICON)GetClassLongPtrA(window, GCLP_HICONSM) == small_icon);

    assert(PostMessageA(window, WM_CLOSE, 0, 0));
    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    assert(close_count == 1 && s_fb_running == 0);
    assert(xbox_FramebufferWindowHandle() == NULL);
    CloseHandle(thread);
    assert(UnregisterClassA("XboxRecompFramebuffer", instance));
    puts(embedded
        ? "PASS window icon: embedded resource, correct large/small class icons, clean close"
        : "PASS window icon: optional resource absent, hidden window and clean close");
    return 0;
}
