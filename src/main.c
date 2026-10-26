#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#include <windows.h>

#include <xbox/xboxrecomp.h>

#include "recomp_types.h"
#include "recomp_funcs.h"
#include "mm3_debug_teleport.h"
#include "mm3_asset_cache.h"
#include "mm3_frame_limit.h"
#include "mm3_performance_profile.h"
#include "mm3_display_settings.h"
#include "mm3_launcher.h"
#include "mm3_crash_diagnostics.h"
#include "mm3_world_lighting.h"
#include "mm3_voice_fixes.h"
#include "mm3_collision_profile.h"
#include "mm3_direct_log.h"
#include <xbox_host.h>
#include <xinput_xbox.h>
#include <video/fb_present.h>
#include <video/nv2a_d3d11.h>

#define MM3_XBE_PATH "game_files/default.xbe"
#define MM3_GAME_DIR "game_files"
#define MM3_KERNEL_THUNK_ADDRESS 0x00361F00u
#define MM3_KERNEL_THUNK_COUNT 151u
#define MM3_MEMORY_MAP_SIZE (128u * 1024u * 1024u)

static HANDLE g_mm3_main_worker;
static HANDLE g_mm3_main_worker_ready;
static int g_mm3_inline_threads;
extern void sub_00083A6C_original(void);
extern void mm3_configure_diagnostics(void);
extern void mm3_widescreen_init(void);

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
    const CONTEXT *context = info ? info->ContextRecord : NULL;
    ULONG_PTR fault = record && record->NumberParameters > 1
        ? record->ExceptionInformation[1] : 0;
    uintptr_t module_base = (uintptr_t)GetModuleHandleW(NULL);
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
    fprintf(stderr, "[CRASH] code=0x%08lX address=%p guest_esp=0x%08X eax=0x%08X\n",
            record ? record->ExceptionCode : 0,
            record ? record->ExceptionAddress : NULL,
            g_esp, g_eax);
    fprintf(stderr, "[CRASH] fault=%p rip=%p rsp=%p rax=%llX rdx=%llX\n",
            (void *)fault,
            context ? (void *)context->Rip : NULL,
            context ? (void *)context->Rsp : NULL,
            context ? context->Rax : 0,
            context ? context->Rdx : 0);
    {
        void *frames[16];
        USHORT count = CaptureStackBackTrace(0, 16, frames, NULL);
        USHORT i;
        for (i = 0; i < count; ++i)
        {
            HMODULE frame_module = NULL;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR)frames[i], &frame_module);
            fprintf(stderr, "[CRASH_NATIVE] frame=%u address=%p module=%p rva=0x%llX\n",
                    (unsigned)i, frames[i], (void *)frame_module,
                    frame_module ? (unsigned long long)((uintptr_t)frames[i] -
                                                        (uintptr_t)frame_module) : 0);
        }
    }
    fprintf(stderr, "[CRASH] module=%p rip_rva=0x%llX\n",
            (void *)module_base,
            context ? (unsigned long long)((uintptr_t)context->Rip - module_base) : 0);
    fprintf(stderr, "[CRASH] xbox_mem_offset=%lld memory_base=%p\n",
            (long long)g_xbox_mem_offset, xbox_GetMemoryBase());
    fprintf(stderr, "[CRASH] guest fs=0x%08X ebp=0x%08X seh_ebp=0x%08X ecx=0x%08X ebx=0x%08X esi=0x%08X edi=0x%08X\n",
            g_fs_base, g_ebp, g_seh_ebp, g_ecx, g_ebx, g_esi, g_edi);
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
     * mm3_debug_teleport.c; snapshot them without calling into the game. */
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

int main(void)
{
    void *xbe_data = NULL;
    size_t xbe_size = 0;
    const wchar_t *entered_directory = mm3_enter_game_directory();

    mm3_direct_launch_log();
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
    if (!mm3_launcher_run()) return 0;
    /* Bootstrap capture uses the real audio engine without displaying a game
     * window or changing the user's device volume or saved preferences. */
    if (getenv("MM3_DSP_CAPTURE") && !strcmp(getenv("MM3_DSP_CAPTURE"), "1"))
        _putenv_s("RECOMP_FB_WINDOW", "");
    if (!getenv("RECOMP_DSP_PROGRAM_DIR") &&
        _putenv_s("RECOMP_DSP_PROGRAM_DIR", "game_files/dsp") != 0) {
        fprintf(stderr, "cannot select MM3 DSP program directory\n");
        return 1;
    }
    nv2a_d3d_prepare_distance_fog(!getenv("MM3_NATIVE_FOG") || strcmp(getenv("MM3_NATIVE_FOG"), "0"));
    xbox_FramebufferSetCloseHandler(mm3_close_game);
    nv2a_d3d_set_flare_depth_test(!getenv("MM3_MOD_FLARE_DEPTH") || strcmp(getenv("MM3_MOD_FLARE_DEPTH"), "0"));
    nv2a_d3d_set_night_glass(!getenv("MM3_NIGHT_GLASS") || strcmp(getenv("MM3_NIGHT_GLASS"), "0"));
    mm3_world_lighting_apply_gain();
    mm3_voice_fixes_install();
    if (!getenv("MM3_MOD_SKY") || strcmp(getenv("MM3_MOD_SKY"), "0"))
        nv2a_d3d_set_bspline_blend(0x304u, 0x305u);
    SetConsoleCtrlHandler(mm3_console_control, TRUE);
#ifndef MM3_VERSION
#define MM3_VERSION "development build"
#endif
    puts("MM3 static recompilation " MM3_VERSION);
    fprintf(stderr, "[MM3_VERSION] Midtown Madness 3 PC %s, built %s %s\n", MM3_VERSION, __DATE__, __TIME__);

    mm3_display_settings_init();

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

    /* MM3 releases large model buffers through NtFreeVirtualMemory when a
     * scene is destroyed. The shared runtime leaves NT heap reclamation
     * opt-in; without it each race/menu round trip retains those buffers
     * until the guest heap is exhausted. Enable before its first allocation,
     * including direct executable launches. */
    if (_putenv_s("RECOMP_HEAP_RECLAIM", "1") != 0) {
        fprintf(stderr, "cannot enable MM3 guest memory reclamation\n");
        return 1;
    }

    /* MM3 reads the triggers and left stick for driving. Keep the runtime's
     * legacy keys available, with arrows also driving this title. */
    if (!getenv("RECOMP_KEYBOARD_LAYOUT") &&
        _putenv_s("RECOMP_KEYBOARD_LAYOUT", "mm3") != 0) {
        fprintf(stderr, "cannot select MM3 keyboard controls\n");
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

    /* Keep the emulated RAM at 64 MB, but leave virtual address space above
     * it for MM3's large MEM_RESERVE requests. */
    xbox_SetMapSize(MM3_MEMORY_MAP_SIZE);
    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        fprintf(stderr, "xbox_MemoryLayoutInit failed\n");
        free(xbe_data);
        return 1;
    }

    g_xbox_mem_offset = xbox_GetMemoryOffset();
    mm3_widescreen_init();
    xbox_InputSetKeyboardDrivingProbe(mm3_keyboard_driving);
    mm3_debug_teleport_init();
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
    xbox_kernel_init();
    xbox_path_init(MM3_GAME_DIR, getenv("MM3_SAVE_DIR"));
    mm3_asset_cache_refresh();
    xbox_kernel_bridge_init();
    if (xbox_Nv2aMirrorFence(0x00351F48u, 0x2Cu, 0x30u) != 0 ||
        xbox_Nv2aCommandFence(0x00351F48u, 0xCu, 0u) != 0) {
        fprintf(stderr, "NV2A command fence registration failed\n");
        xbox_kernel_shutdown();
        xbox_MemoryLayoutShutdown();
        free(xbe_data);
        return 1;
    }
    /* D3D's GPU-notify KEVENT (device+0x196C, waited on in 0x344640),
     * raised by the NOP(5) it patches into the push buffer */
    xbox_Nv2aNotifyEvent(0x00351F48u, 0x196Cu, 5u);
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
    fprintf(stderr, "[CALLBACK_INIT] 392834=%08X 392838=%08X 391F10=%08X 391F14=%08X 391F18=%08X\n",
            MEM32(0x392834), MEM32(0x392838), MEM32(0x391F10),
            MEM32(0x391F14), MEM32(0x391F18));
    fprintf(stderr, "[CALLBACK_INIT] 391F00=%08X 391F04=%08X 391F08=%08X\n",
            MEM32(0x391F00), MEM32(0x391F04), MEM32(0x391F08));
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
