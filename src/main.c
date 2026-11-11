#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#include "platform/xbox_winnt.h"

#include <xbox/xboxrecomp.h>

#include "recomp_types.h"
#include "recomp_funcs.h"
#include "crash_report.h"
#include "mods/teleport.h"
#include "mm3_asset_cache.h"
#include "nv2a_pb_recovery.h"
#include "mm3_performance_profile.h"
#include "mm3_display_settings.h"
#include "mm3_frame_limit.h"
#include "mm3_launcher.h"
#include "mm3_launcher_config.h"
#include "mm3_crash_diagnostics.h"
#include "mm3_world_lighting.h"
#include "mm3_collision_profile.h"
#include "mm3_direct_log.h"
#include "mm3_cheat_keys.h"
#include "mods/mods.h"
#include <xbox_host.h>
#include <xinput_xbox.h>
#include <video/fb_present.h>
#include <video/nv2a_d3d11.h>
#include <net/net.h>

#define MM3_XBE_PATH "game_files/default.xbe"
#define MM3_GAME_DIR "game_files"
#define MM3_KERNEL_THUNK_ADDRESS 0x00361F00u
#define MM3_KERNEL_THUNK_COUNT 151u
#define MM3_MEMORY_MAP_SIZE (128u * 1024u * 1024u)

/* Inherited MM3 guest identity is compatibility policy, not detected hardware.
 * Keep credential input in the port; the shared kernel copies export bytes. */
static void mm3_configure_kernel_identity(void)
{
    static char disk_model[]="XBOXRECOMP VIRTUAL HDD";
    static char disk_serial[]="XR0000000000";
    static char image_path[]="\\Device\\CdRom0\\default.xbe";
    xbox_HalDiskCachePartitionCount=3;
    xbox_HalDiskModelNumber=(XBOX_ANSI_STRING){sizeof(disk_model)-1,sizeof(disk_model),disk_model};
    xbox_HalDiskSerialNumber=(XBOX_ANSI_STRING){sizeof(disk_serial)-1,sizeof(disk_serial),disk_serial};
    xbox_XeImageFileName=(XBOX_ANSI_STRING){sizeof(image_path)-1,sizeof(image_path),image_path};
    static const struct { const char *name; UCHAR *bytes; } keys[] = {
        {"RECOMP_XBOX_HDKEY", xbox_HDKey},
        {"RECOMP_XBOX_SIGKEY", xbox_SignatureKey},
        {"RECOMP_XBOX_LANKEY", xbox_LANKey}
    };
    memset(&xbox_HardwareInfo,0,sizeof(xbox_HardwareInfo));
    xbox_HardwareInfo.GpuRevision=0xA1;
    xbox_HardwareInfo.McpRevision=0xB1;
    xbox_kernel_set_version(1,0,5849,0);
    for (unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++) {
        const char *text=getenv(keys[i].name);
        memset(keys[i].bytes,0,16);
        if (!text || !*text) continue;
        if (strlen(text)!=32 || strspn(text,"0123456789abcdefABCDEF")!=32) {
            fprintf(stderr,"ignoring invalid %s (expected 32 hex digits)\n",keys[i].name);
            continue;
        }
        for (unsigned n=0;n<16;n++) {
            unsigned byte;
            if (sscanf(text+2*n,"%2x",&byte)==1) keys[i].bytes[n]=(UCHAR)byte;
        }
    }
}

/* Inherited port compatibility: references expose one finished thread object,
 * not live per-handle KTHREAD state. Keep this approximation out of the runtime.
 * Reset its guest address before each host-owned memory generation. */
static uint32_t mm3_finished_thread_object;
static void mm3_reference_object_bridge(void)
{
    uint32_t handle=MEM32(g_esp),type=MEM32(g_esp+4),output=MEM32(g_esp+8);
    MEMORY_BASIC_INFORMATION region;
    uintptr_t pointer=(uintptr_t)output+g_xbox_mem_offset;
    if (output<0x1000 || (uint64_t)output+4>xbox_GetMappedSize() ||
        !VirtualQuery((void *)pointer,&region,sizeof(region)) ||
        region.State!=MEM_COMMIT || region.Protect!=PAGE_READWRITE ||
        pointer+4>(uintptr_t)region.BaseAddress+region.RegionSize) {
        g_eax=STATUS_INVALID_PARAMETER; return;
    }
    MEM32(output)=0;
    if (!handle || handle==UINT32_MAX) { g_eax=STATUS_INVALID_HANDLE; return; }
    if (type==XBOX_KERNEL_DATA_BASE+KDATA_THREAD_OBJ_TYPE) {
        if (!mm3_finished_thread_object) {
            mm3_finished_thread_object=xbox_HeapAlloc(0x124,16);
            if (mm3_finished_thread_object) {
                memset((void *)XBOX_PTR(mm3_finished_thread_object),0,0x124);
                MEM32(mm3_finished_thread_object+4)=1;
            }
        }
        MEM32(output)=mm3_finished_thread_object;
        g_eax=mm3_finished_thread_object?STATUS_SUCCESS:STATUS_NO_MEMORY;
    } else { MEM32(output)=handle; g_eax=STATUS_SUCCESS; }
}

/* Preserve the port's optional launch command-line interface. The identity
 * comes from this XBE's certificate, never from a different title's capture. */
static void mm3_configure_launch_data(const void *image, size_t size)
{
    static XBOX_LAUNCH_DATA_PAGE page;
    const char *cmdline = getenv("RECOMP_CMDLINE");
    uint32_t base, certificate, title_id;
    size_t offset, length;
    xbox_LaunchDataPage = NULL;
    if (!cmdline || !*cmdline || size < 0x11Cu) return;
    memcpy(&base, (const uint8_t *)image + 0x104, 4);
    memcpy(&certificate, (const uint8_t *)image + 0x118, 4);
    if (certificate < base) return;
    offset = (size_t)(certificate - base);
    if (offset > size || size - offset < 12) return;
    memcpy(&title_id, (const uint8_t *)image + offset + 8, 4);
    memset(&page, 0, sizeof(page));
    page.LaunchDataType = 3;
    page.TitleId = title_id;
    length = strlen(cmdline);
    if (length >= sizeof(page.LaunchData)) length = sizeof(page.LaunchData) - 1;
    memcpy(page.LaunchData, cmdline, length);
    xbox_LaunchDataPage = &page;
    fprintf(stderr, "[MM3_LAUNCH] title=0x%08X command-line bytes=%zu\n",
            (unsigned)title_id, length);
}

static HANDLE g_mm3_main_worker;
static HANDLE g_mm3_main_worker_ready;
static int g_mm3_inline_threads;
extern void sub_00083A6C_original(void);
extern void mm3_configure_diagnostics(void);
extern void mm3_host_video_init(void);

static void mm3_close_game(void)
{
    /* Xbox guest workers do not observe a host WM_CLOSE. Freeing their shared
     * memory while they run is unsafe; end the process as the title's existing
     * firmware-exit bridge does. Windows releases its file/device handles. */
    fprintf(stderr, "[MM3_EXIT] close requested\n");
    fflush(stderr);
    ExitProcess(0);
}

/* The main menu's Quit row (mm3_graphics_menu.c): the same as closing the window. */
void mm3_quit_game(void)
{
    mm3_close_game();
}

static BOOL WINAPI mm3_console_control(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT &&
        event != CTRL_CLOSE_EVENT)
        return FALSE;
    mm3_close_game();
    return TRUE;
}
int mm3_launcher(void);

#ifdef _WIN32
/* The launcher's renderer choice (MM3_RENDERER_VULKAN): the DirectX 11 and the
 * Vulkan game are two programs side by side (mm3_recomp.exe and
 * mm3_recomp_vulkan.exe; their renderers share function names). When the
 * choice is the other one, start it with the settings the launcher applied
 * (inherited environment, launcher skipped) and end this one. 1 if started.
 * A missing sibling keeps this renderer. */
static int mm3_renderer_handoff(void)
{
    const char *want = getenv("MM3_RENDERER_VULKAN");
    int vulkan = want && !strcmp(want, "1");
#ifdef MM3_RENDERER_SDL3
    const int built_vulkan = 1;
#else
    const int built_vulkan = 0;
#endif
    wchar_t path[MAX_PATH], self[MAX_PATH], *slash, *args;
    const char *handed = getenv("MM3_RENDERER_HANDOFF");
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (vulkan == built_vulkan) return 0;
    /* A program a hand-off started never hands off again: no chain of starts. */
    if (handed && *handed) return 0;
    if (!GetModuleFileNameW(NULL, self, MAX_PATH)) return 0;
    wcscpy_s(path, MAX_PATH, self);
    if (!(slash = wcsrchr(path, L'\\'))) return 0;
    wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), vulkan ? L"mm3_recomp_vulkan.exe" : L"mm3_recomp.exe");
    if (!_wcsicmp(path, self)) return 0;     /* a build folder's Vulkan program is itself mm3_recomp.exe */
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "[MM3_RENDERER] %s chosen but %ls is missing; playing with %s\n",
                vulkan ? "Vulkan" : "DirectX 11", path, built_vulkan ? "Vulkan" : "DirectX 11");
        return 0;
    }
    _putenv_s("MM3_LAUNCHER", "0");         /* the settings are already applied */
    _putenv_s("MM3_RENDERER_HANDOFF", "1");
    args = GetCommandLineW();               /* keep the arguments, swap the program */
    if (*args == L'"') { args = wcschr(args + 1, L'"'); args = args ? args + 1 : L""; }
    else { while (*args && *args != L' ') ++args; }
    {
        static wchar_t command[4096];
        swprintf(command, sizeof command / sizeof command[0], L"\"%ls\"%ls", path, args);
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        if (!CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "[MM3_RENDERER] could not start %ls (error %lu); playing with %s\n",
                    path, GetLastError(), built_vulkan ? "Vulkan" : "DirectX 11");
            return 0;
        }
    }
    fprintf(stderr, "[MM3_RENDERER] started %ls (%s)\n", path, vulkan ? "Vulkan" : "DirectX 11");
    fflush(stderr);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}
#endif

/* Register the first XAPI worker before it can create background workers.
 * The runtime's debug handle changes on every spawn and cannot determine
 * the lifetime of the game. This wrapper preserves the guest register ABI. */
void sub_00083A6C(void)
{
    if (!g_mm3_inline_threads &&
        !InterlockedCompareExchangePointer((PVOID volatile *)&g_mm3_main_worker,
                                           NULL, NULL)) {
        HANDLE current = NULL;
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                             GetCurrentProcess(), &current,
                             SYNCHRONIZE, FALSE, 0)) {
            fprintf(stderr, "cannot register MM3 main worker: %lu\n", GetLastError());
            ExitProcess(1);
        }
        if (InterlockedCompareExchangePointer((PVOID volatile *)&g_mm3_main_worker,
                                              current, NULL)) {
            CloseHandle(current);
        } else {
            fprintf(stderr, "[MM3_MAIN_THREAD] tid=%lu entry=00083A6C\n",
                    GetCurrentThreadId());
            SetEvent(g_mm3_main_worker_ready);
        }
    }
    sub_00083A6C_original();
}

static LONG WINAPI mm3_crash_report(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *record = info ? info->ExceptionRecord : NULL;
    SYSTEMTIME now;
    const char *access = "unavailable";
    MM3CrashWord seh_object, fs_thread, manager, race;
    uint8_t paused = 0;
    int pause_read;
    if (record && (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
                   record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        record->NumberParameters > 1) {
        switch (record->ExceptionInformation[0]) {
        case 0: access = "read"; break;
        case 1: access = "write"; break;
        case 8: access = "execute"; break;
        default: access = "unknown"; break;
        }
    }
    GetSystemTime(&now);
    fprintf(stderr, "[CRASH] utc=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ uptime_ms=%llu thread=%lu access=%s\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
            now.wSecond, now.wMilliseconds, (unsigned long long)GetTickCount64(),
            GetCurrentThreadId(), access);
    /* Generic host, native and guest register lines (and the minidump) come from the fork. */
    xbox_CrashReport(info);
    mm3_crash_guest_frames(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE, g_ebp);
    seh_object = mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                                     "seh_frame+8", (uint64_t)g_seh_ebp + 8u);
    mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                         "seh_frame+c", (uint64_t)g_seh_ebp + 0xCu);
    mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                         "seh_frame+10", (uint64_t)g_seh_ebp + 0x10u);
    mm3_crash_guest_indirect(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                             "seh_object+18", seh_object, 0x18u);
    mm3_crash_guest_indirect(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                             "seh_object+580", seh_object, 0x580u);
    mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                         "callback", 0x00362018u);
    fs_thread = mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                                    "fs+20", (uint64_t)g_fs_base + 0x20u);
    mm3_crash_guest_indirect(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                             "fs20+250", fs_thread, 0x250u);
    for (uint32_t i = 0; i < 12u; ++i) {
        char name[24];
        snprintf(name, sizeof name, "stack+%02X", i * 4u);
        mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                             name, (uint64_t)g_esp + i * 4u);
    }
    fprintf(stderr, "[CRASH] heap_candidate=0x%08X bucket=%u\n", g_esi, g_edi);
    for (uint32_t i = 0; i < 5u; ++i) {
        char name[24];
        snprintf(name, sizeof name, "heap_bin[%u].head", i);
        mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                             name, (uint64_t)g_esi + 0x180u + i * 8u);
        snprintf(name, sizeof name, "heap_bin[%u].tail", i);
        mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                             name, (uint64_t)g_esi + 0x184u + i * 8u);
    }
    /* Reuse the verified pause/engine globals and player-manager chain from
     * src/mods/teleport.c; snapshot them without calling into the game. */
    pause_read = mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                                     0x00429DB2u, &paused, sizeof paused);
    if (pause_read)
        fprintf(stderr, "[CRASH_CONTEXT] pause_active=%u\n", (unsigned)paused);
    else
        fprintf(stderr, "[CRASH_CONTEXT] pause_active=unreadable\n");
    mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                         "engine_mode", 0x00431610u);
    manager = mm3_crash_guest_word(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                                   "player_manager", 0x003C5CDCu);
    race.value = 0;
    race.readable = manager.readable && manager.value &&
        mm3_crash_guest_read(g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                             (uint64_t)manager.value + 0x54u,
                             &race.value, sizeof race.value);
    if (manager.readable && !manager.value)
        fprintf(stderr, "[CRASH_CONTEXT] race=absent: player_manager null\n");
    else if (race.readable && !race.value)
        fprintf(stderr, "[CRASH_CONTEXT] race=absent\n");
    else if (race.readable)
        fprintf(stderr, "[CRASH_CONTEXT] race=0x%08X\n", race.value);
    else
        fprintf(stderr, "[CRASH_CONTEXT] race=unreadable\n");
    mm3_crash_guest_indirect(stderr, g_xbox_mem_offset, MM3_MEMORY_MAP_SIZE,
                             "primary_player", race, 0xD8u);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static int load_file(const char *path, void **data, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;

    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0)
        goto fail;
    length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0)
        goto fail;

    *data = malloc((size_t)length);
    if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length) {
        free(*data);
        *data = NULL;
        goto fail;
    }

    *size = (size_t)length;
    fclose(file);
    return 1;

fail:
    fclose(file);
    return 0;
}

#ifdef _WIN32
static int mm3_is_file(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* The game reads game_files/ relative to the working directory. Steam starts a
 * non-Steam shortcut in the executable's own folder (build\RelWithDebInfo, two
 * levels below the repository), so look for the game beside the executable and
 * in its parent folders. A working directory that already holds the game, such
 * as the one set by play.ps1, is left alone. Returns the directory entered. */
static const wchar_t *mm3_enter_game_directory(void)
{
    static wchar_t directory[4096];
    wchar_t probe[4096];
    DWORD length;

    if (mm3_is_file(L"game_files\\default.xbe"))
        return NULL;
    length = GetModuleFileNameW(NULL, directory, 4096);
    if (length == 0 || length >= 4096)
        return NULL;
    for (int level = 0; level < 5; ++level) {
        wchar_t *slash = wcsrchr(directory, L'\\');
        if (!slash)
            return NULL;
        *slash = L'\0';
        if (_snwprintf_s(probe, 4096, _TRUNCATE, L"%ls\\game_files\\default.xbe",
                         directory) > 0 && mm3_is_file(probe))
            return SetCurrentDirectoryW(directory) ? directory : NULL;
    }
    return NULL;
}

#else
#include <unistd.h>
#include <sys/stat.h>
/* POSIX: the same search as below, from /proc/self/exe. */
static const wchar_t *mm3_enter_game_directory(void)
{
    static wchar_t wide[4096];
    char directory[4096], probe[4200];
    struct stat st;
    ssize_t length;
    if (!stat("game_files/default.xbe", &st) && S_ISREG(st.st_mode))
        return NULL;
    length = readlink("/proc/self/exe", directory, sizeof directory - 1);
    if (length <= 0)
        return NULL;
    directory[length] = 0;
    for (int level = 0; level < 5; ++level) {
        char *slash = strrchr(directory, '/');
        if (!slash || slash == directory)
            return NULL;
        *slash = 0;
        snprintf(probe, sizeof probe, "%s/game_files/default.xbe", directory);
        if (!stat(probe, &st) && S_ISREG(st.st_mode) && !chdir(directory)) {
            mbstowcs(wide, directory, 4095);
            return wide;
        }
    }
    return NULL;
}

#endif

/* Car paint mirrors the screen copy as sharply as glass, so lamps showed on
 * it as hard bright spots. A slightly rough finish (user, 10-29: "slightly
 * increasing car-paint roughness, so the reflections are softer not just a
 * bright blob") blurs that mirror over MM3_PAINT_SOFTNESS display pixels
 * (0-32; 0 keeps the Xbox's sharp mirror, as the launcher's Original look). */
#define MM3_PAINT_SOFTNESS 6.0f
static float mm3_paint_softness(void)
{
    const char *setting = getenv("MM3_PAINT_SOFTNESS");
    char *end = NULL;
    float value;
    if (!setting || !*setting) return MM3_PAINT_SOFTNESS;
    value = strtof(setting, &end);
    if (end == setting || *end || !(value >= 0.0f && value <= 32.0f)) {
        fprintf(stderr, "[MM3_PAINT] invalid MM3_PAINT_SOFTNESS; expected 0..32, using %.0f\n", MM3_PAINT_SOFTNESS);
        return MM3_PAINT_SOFTNESS;
    }
    return value;
}

/* Opt out of Windows power throttling (EcoQoS): on laptops Windows may run a
 * process's threads at efficiency clocks or on efficiency cores. The game
 * thread is the frame-rate limit in dense streets and must run flat out. */
static void mm3_full_speed(void)
{
#if defined(_WIN32) && defined(PROCESS_POWER_THROTTLING_CURRENT_VERSION)
    PROCESS_POWER_THROTTLING_STATE state = {0};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state);
#endif
}

int main(void)
{
    void *xbe_data = NULL;
    size_t xbe_size = 0;
    const wchar_t *entered_directory = mm3_enter_game_directory();

    mm3_direct_launch_log();
    mm3_full_speed();
    mm3_configure_diagnostics();
    mm3_collision_profile_init();
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Unbuffered redirected stderr made diagnostic formatting issue many
     * tiny Windows writes and stalled the renderer on the shared FILE lock.
     * Movie markers and crash reports explicitly flush when needed. */
    static char stderr_buffer[64 * 1024];
    const char *unbuffered = getenv("MM3_LOG_UNBUFFERED");
    if (unbuffered && *unbuffered && strcmp(unbuffered, "0") != 0)
        setvbuf(stderr, NULL, _IONBF, 0);
    else
        setvbuf(stderr, stderr_buffer, _IOFBF, sizeof stderr_buffer);
    SetUnhandledExceptionFilter(mm3_crash_report);
    if (entered_directory)
        fprintf(stderr, "[MM3_CWD] game_files not in the launch folder; working directory is now %ls\n",
                entered_directory);
#ifdef _WIN32
    if (!mm3_launcher_run()) return 0;
    if (mm3_renderer_handoff()) return 0;
#else
    {
        /* Linux: launcher.ini (written by the Settings window, mm3_launcher.py, or by hand)
         * holds the same mod choices as on Windows; explicit environment wins. */
        MM3LauncherConfig saved;
        char note[512] = "";
        if (!mm3_launcher_config_startup(&saved, note, sizeof note) || *note)
            fprintf(stderr, "[MM3_LAUNCHER] %s\n", note);
        if (!mm3_launcher_config_apply(&saved, note, sizeof note)) {
            fprintf(stderr, "[MM3_LAUNCHER] %s\n", note);
            return 0;
        }
    }
#endif
    /* Bootstrap capture uses the real audio engine without displaying a game
     * window or changing the user's device volume or saved preferences. */
    if (getenv("MM3_DSP_CAPTURE") && !strcmp(getenv("MM3_DSP_CAPTURE"), "1"))
        _putenv_s("RECOMP_FB_WINDOW", "");
    /* The audio DSP programs the game runs are saved here; the next build
     * recompiles them to C (CMakeLists.txt, XBOXRECOMP_DSP_PROGRAM_DIR). */
    if (!getenv("RECOMP_DSP_PROGRAM_DIR") &&
        _putenv_s("RECOMP_DSP_PROGRAM_DIR", MM3_GAME_DIR "/dsp") != 0) {
        fprintf(stderr, "cannot select MM3 DSP program directory\n");
        return 1;
    }
    nv2a_d3d_prepare_distance_fog(!getenv("MM3_NATIVE_FOG") || strcmp(getenv("MM3_NATIVE_FOG"), "0"));
    xbox_FramebufferSetCloseHandler(mm3_close_game);
    nv2a_d3d_set_night_glass(!getenv("MM3_NIGHT_GLASS") || strcmp(getenv("MM3_NIGHT_GLASS"), "0"));
    nv2a_d3d_set_paint_softness(mm3_paint_softness());
    mm3_world_lighting_apply_gain();
    if (!getenv("MM3_MOD_SKY") || strcmp(getenv("MM3_MOD_SKY"), "0"))
        nv2a_d3d_set_bspline_blend(0x304u, 0x305u);
    SetConsoleCtrlHandler(mm3_console_control, TRUE);
    /* MM3 instances share read-only assets for system-link testing. */
    xbox_SetReadOnlyFileSharing(TRUE);
#ifndef MM3_VERSION
#define MM3_VERSION "development build"
#endif
    puts("MM3 static recompilation " MM3_VERSION);
    fprintf(stderr, "[MM3_VERSION] Midtown Madness 3 PC %s, built %s %s\n", MM3_VERSION, __DATE__, __TIME__);
    mm3_power_watch();

    mm3_display_settings_init();
    mm3_cheat_keys_start();

    const char *profile_error = mm3_apply_performance_defaults(getenv("MM3_PERFORMANCE_PROFILE"));
    if (profile_error) {
        fprintf(stderr, "cannot select MM3 performance setting %s\n", profile_error);
        return 1;
    }

    /* Apply the same bounded rate before either vblank or presentation
     * caches its setting, including launches that bypass play.ps1. */
    char frame_limit[16];
    snprintf(frame_limit, sizeof frame_limit, "%d",
             mm3_frame_limit(getenv("RECOMP_FPS_LIMIT")));
    if (_putenv_s("RECOMP_FPS_LIMIT", frame_limit) != 0) {
        fprintf(stderr, "cannot set MM3 frame-rate limit\n");
        return 1;
    }

    /* The Win32 launcher above is the settings window. The shared port's RmlUi
     * launcher (mm3_launcher.cpp) is its only one off Windows, and on Windows
     * an opt-in with MM3_RMLUI_LAUNCHER=1. Scripts set RECOMP_PB_EXEC themselves. */
    const char *scripted = getenv("RECOMP_PB_EXEC");
#ifdef _WIN32
    const char *rmlui_setting = getenv("MM3_RMLUI_LAUNCHER");
    const int use_rmlui_launcher = rmlui_setting && *rmlui_setting && strcmp(rmlui_setting, "0") != 0;
#else
    const int use_rmlui_launcher = 1;
#endif
    if (use_rmlui_launcher && (!scripted || !*scripted) && !mm3_launcher())
        return 0;

    /* Preserve MM3's inherited completion of authored partial mip chains.
     * The renderer's generic default retains guest levels. Configure before
     * the first texture upload; an explicit caller override takes priority. */
    if (!getenv("RECOMP_COMPLETE_MIPS") &&
        _putenv_s("RECOMP_COMPLETE_MIPS", "1") != 0) {
        fprintf(stderr, "cannot configure MM3 mip completion\n");
        return 1;
    }

    /* MM3 releases large model buffers through NtFreeVirtualMemory when a
     * scene is destroyed. The shared runtime leaves NT heap reclamation
     * opt-in; without it each race/menu round trip retains those buffers
     * until the guest heap is exhausted. Enable before its first allocation,
     * including direct executable launches. */
    if (_putenv_s("RECOMP_HEAP_RECLAIM", "1") != 0) {
        fprintf(stderr, "cannot enable MM3 guest memory reclamation\n");
        return 1;
    }

    /* Keep the profile's render quality while fitting the gameplay view to
     * the window. Saved or explicit aspect settings take priority. */
    if (!getenv("RECOMP_ASPECT") &&
        _putenv_s("RECOMP_ASPECT", "auto") != 0) {
        fprintf(stderr, "cannot select MM3 display aspect\n");
        return 1;
    }

    fprintf(stderr, "[MM3_PERFORMANCE] profile=%s scale=%s msaa=%s object_distance=%s view_distance=%s aniso=%s fps=%s\n",
            mm3_performance_profile(getenv("MM3_PERFORMANCE_PROFILE"))->name,
            getenv("RECOMP_RENDER_SCALE"), getenv("RECOMP_MSAA"),
            getenv("RECOMP_DRAW_DISTANCE"), getenv("RECOMP_VIEW_DISTANCE"),
            getenv("RECOMP_ANISO"), getenv("RECOMP_FPS_LIMIT"));

    if (!load_file(MM3_XBE_PATH, &xbe_data, &xbe_size)) {
        fprintf(stderr, "cannot load %s\n", MM3_XBE_PATH);
        return 1;
    }

    /* MM3's DirectSoundCreate waits for the AC'97 codec-ready bit. Route the
     * request through the runtime APU/MMIO model so the codec and DSP handshake
     * are initialized together; otherwise DSERR_NODRIVER leaves its audio
     * engine object half-built and the title later dereferences a stale vtable. */
    if (_putenv_s("RECOMP_AC97_READY", "1") != 0) {
        fprintf(stderr, "cannot enable MM3 AC97/APU initialization\n");
        free(xbe_data);
        return 1;
    }

    /* MM3 keeps its inherited export-page address as explicit port policy. */
    if (!xbox_ConfigureKernelData(0x00740000u)) {
        fprintf(stderr, "cannot configure MM3 kernel data\n");
        free(xbe_data);
        return 1;
    }
    /* Preserve MM3's inherited worker headroom as an explicit minimum. */
    if (!xbox_ConfigureThreadStack(512u*1024u)) {
        fprintf(stderr,"cannot configure MM3 worker stack minimum\n");
        free(xbe_data); return 1;
    }
    /* MM3 retains its inherited main-stack placement/budget as port policy.
     * The shared runtime validates it and keeps callback slices separate. */
    if (!xbox_ConfigureMainStack(0x00780000u, 8u * 1024u * 1024u)) {
        fprintf(stderr, "cannot configure MM3 main stack\n");
        free(xbe_data);
        return 1;
    }
    /* Keep the emulated RAM at 64 MB, but leave virtual address space above
     * it for MM3's large MEM_RESERVE requests. */
    xbox_SetMapSize(MM3_MEMORY_MAP_SIZE);
    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        fprintf(stderr, "xbox_MemoryLayoutInit failed\n");
        free(xbe_data);
        return 1;
    }

    g_xbox_mem_offset = xbox_GetMemoryOffset();
    /* Preserve the inherited application scratch chain in the port, not the
     * generic loader. Its MM3 usage still needs a title-specific audit. */
    *(uint32_t *)((uintptr_t)g_xbox_mem_offset + XBOX_FS_BASE + 0x28u) = 0x00760000u;
    *(uint32_t *)((uintptr_t)g_xbox_mem_offset + 0x00760028u) = 0x00700000u;

    /* Preserve the inherited empty-section PE probe response in MM3 only. */
    void *kernel_probe = xbox_GetKernelMemoryBase();
    if (kernel_probe)
        *(uint32_t *)((uint8_t *)kernel_probe + 0x3Cu) = 0x80u;

    mm3_mods_init();
    /* XPP submits stack-backed USB requests whose callback signals an inline
     * KEVENT. Its explicit wait treats an invalid handle as completion, so
     * the request can outlive its stack frame. Permit only the verified XPP
     * setter, explicit wait/cancel wait and synchronous-submit wait sites. */
    {
        static const uint32_t usb_event_callers[] = {
            0x0035C348u, 0x0035D553u, 0x0035D56Cu, 0x0035C7F1u
        };
        if (xbox_kernel_set_inline_event_callers(usb_event_callers,
                sizeof(usb_event_callers) / sizeof(usb_event_callers[0])) != 0) {
            fprintf(stderr, "cannot register MM3 USB event bridges\n");
            xbox_MemoryLayoutShutdown();
            free(xbe_data);
            return 1;
        }
    }
    xbox_kernel_set_thunk_address(MM3_KERNEL_THUNK_ADDRESS,
                                   MM3_KERNEL_THUNK_COUNT);
    mm3_configure_kernel_identity();
    mm3_configure_launch_data(xbe_data, xbe_size);
    xbox_kernel_init();
    xbox_path_init(MM3_GAME_DIR, getenv("MM3_SAVE_DIR"));
    mm3_asset_cache_refresh();
    mm3_finished_thread_object=0;
    if (xbox_kernel_set_bridge_override(246,mm3_reference_object_bridge)!=0) {
        fprintf(stderr,"cannot register MM3 object-reference bridge\n");
        xbox_kernel_shutdown(); xbox_MemoryLayoutShutdown(); free(xbe_data); return 1;
    }
    xbox_kernel_bridge_init();
    /* MM3 reopens disc files while they are still open (streaming + loaders). */
    xbox_SetReadOnlyFileSharing(TRUE);
    nv2a_pb_set_stop_handler(nv2a_pb_recover_semaphore_stop);
    if (xbox_Nv2aMirrorFence(0x00351F48u, 0x2Cu, 0x30u) != 0 ||
        xbox_Nv2aCommandFence(0x00351F48u, 0xCu, 0u) != 0) {
        fprintf(stderr, "NV2A command fence registration failed\n");
        xbox_kernel_shutdown();
        xbox_MemoryLayoutShutdown();
        free(xbe_data);
        return 1;
    }
    /* MM3's statically linked D3D device/channel layout, for diagnostics only. */
    const XBOX_NV2A_CAPTURE_LAYOUT capture_layout = {
        0x17C0u, 0x40u, 0x44u, 0x4F8u, 0x24u, 0x28u
    };
    if (xbox_Nv2aCaptureLayout(0x00351F48u, &capture_layout) != 0) {
        fprintf(stderr, "NV2A capture layout registration failed\n");
        xbox_kernel_shutdown();
        xbox_MemoryLayoutShutdown();
        free(xbe_data);
        return 1;
    }
    /* D3D's GPU-notify KEVENT (device+0x196C, waited on in 0x344640),
     * raised by the NOP(5) it patches into the push buffer */
    xbox_Nv2aNotifyEvent(0x00351F48u, 0x196Cu, 5u);
    mm3_host_video_init();
    /* Preserve MM3's existing online-service routing outside the runtime. */
    static const XboxDnsAlias service_dns[] = {
        {"macs.xboxlive.com", "macs.insig.uk"},
        {"as.xboxlive.com", "as.insig.uk"},
        {"tgs.xboxlive.com", "tgs.insig.uk"},
        {"xds.xboxlive.com", "xexds.xboxlive.com"},
    };
    net_backend_set_dns_aliases(service_dns, sizeof service_dns / sizeof service_dns[0]);
    if (!xbox_HostHardwareInit()) {
        xbox_kernel_shutdown();
        xbox_MemoryLayoutShutdown();
        free(xbe_data);
        return 1;
    }
    if (!xbox_HostGraphicsInit("Midtown Madness 3 on PC improved and modded !!")) {
        xbox_HostHardwareShutdown();
        xbox_kernel_shutdown();
        xbox_MemoryLayoutShutdown();
        free(xbe_data);
        return 1;
    }
    /* Retail behavior is SPAWN. INLINE is a bounded single-thread diagnostic
     * that removes cross-thread trace interleaving while locating startup. */
    g_mm3_inline_threads = getenv("MM3_THREAD_MODE") &&
                          _stricmp(getenv("MM3_THREAD_MODE"), "inline") == 0;
    xbox_SetThreadMode(g_mm3_inline_threads ? XBOX_THREAD_MODE_INLINE : XBOX_THREAD_MODE_SPAWN);

    if (!recomp_dispatch_init())
        fprintf(stderr, "dispatch table allocation failed; using binary lookup\n");

    g_mm3_main_worker_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_mm3_main_worker_ready) {
        fprintf(stderr, "cannot create MM3 worker monitor\n");
        xbox_HostHardwareShutdown();
        xbox_kernel_shutdown();
        xbox_MemoryLayoutShutdown();
        free(xbe_data);
        return 1;
    }

    /* Optional RECOMP_WATCHDOG_SECS diagnostic; must run on the guest thread. */
    xbox_WatchdogStart();
    if (getenv("MM3_TRACE_BOOT_STATE")) {
        fprintf(stderr,
                "[BOOT_STATE] host_tid=%lu eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X fs=%08X stack=%08X,%08X,%08X fsdata=%08X,%08X,%08X,%08X kernel=%08X,%08X,%08X kdata=%08X\n",
                GetCurrentThreadId(), g_eax, g_ebx, g_ecx, g_edx, g_esi,
                g_edi, g_ebp, g_esp, g_fs_base, MEM32(g_esp),
                MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_fs_base),
                MEM32(g_fs_base + 4), MEM32(g_fs_base + 8),
                MEM32(g_fs_base + 0x1C), MEM32(0x10108), MEM32(0x10118),
                MEM32(0x10128), MEM32(MEM32(0x10118)));
    }
    puts("starting XBE entry point 0x00083C55");
    xbe_entry_point();
    if (!g_mm3_inline_threads) {
        WaitForSingleObject(g_mm3_main_worker_ready, INFINITE);
        WaitForSingleObject(g_mm3_main_worker, INFINITE);
        CloseHandle(g_mm3_main_worker);
    }
    CloseHandle(g_mm3_main_worker_ready);

    xbox_HostHardwareShutdown();
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);
    return 0;
}
