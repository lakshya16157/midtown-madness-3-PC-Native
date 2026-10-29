/* A Steam shortcut or a double-clicked exe starts the game without
 * scripts/play.ps1, so nothing records its output: stderr is a console
 * window (or nothing) and a slow session left no evidence. Write such a
 * launch's stderr to logs\direct-<UTC>.log with the same light timing as
 * "Play with performance diagnostics": once-a-second renderer summaries,
 * hitch records over 8 ms and a per-frame CSV. Those cost nothing
 * measurable; the smooth 19 October runs had them on. Only the newest few
 * direct logs are kept. MM3_DIRECT_LOG=0 keeps the console instead. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "mm3_direct_log.h"

enum { MM3_DIRECT_LOGS_KEPT = 5, MM3_OVERLAY_CHECK_MS = 30000, MM3_POWER_CHECK_MS = 30000 };

/* play.ps1 and the benchmark harness hand the game a pipe or a file. */
static int stderr_redirected(void)
{
    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD type;
    if (!handle || handle == INVALID_HANDLE_VALUE) return 0;
    type = GetFileType(handle);
    return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
}

static int compare_names(const void *a, const void *b)
{
    return wcscmp((const wchar_t *)a, (const wchar_t *)b);
}

/* Names sort by their timestamp. Remove the oldest logs, their frame CSVs,
 * black-frame pictures and F9 captures (picture and table), so that `keep`
 * remain before the new one is created. */
static void prune_direct_logs(int keep)
{
    static wchar_t names[256][MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(L"logs\\direct-*.log", &found);
    int count = 0, i;
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && count < 256)
            wcscpy_s(names[count++], MAX_PATH, found.cFileName);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    qsort(names, (size_t)count, sizeof names[0], compare_names);
    for (i = 0; i < count - keep; ++i) {
        wchar_t path[MAX_PATH + 32];
        size_t length = wcslen(names[i]);
        swprintf(path, MAX_PATH + 32, L"logs\\%ls", names[i]);
        DeleteFileW(path);
        if (length > 4) {
            swprintf(path, MAX_PATH + 32, L"logs\\%.*ls-frames.csv", (int)(length - 4), names[i]);
            DeleteFileW(path);
            swprintf(path, MAX_PATH + 32, L"logs\\%.*ls-black-*", (int)(length - 4), names[i]);
            search = FindFirstFileW(path, &found);
            if (search != INVALID_HANDLE_VALUE) {
                do {
                    wchar_t picture[MAX_PATH + 16];
                    swprintf(picture, MAX_PATH + 16, L"logs\\%ls", found.cFileName);
                    DeleteFileW(picture);
                } while (FindNextFileW(search, &found));
                FindClose(search);
            }
        }
    }
}

static void parent_name(char *out, size_t size)
{
    PROCESSENTRY32W entry;
    DWORD self = GetCurrentProcessId(), parent = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    snprintf(out, size, "unknown");
    if (snapshot == INVALID_HANDLE_VALUE) return;
    entry.dwSize = sizeof entry;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        if (entry.th32ProcessID == self) { parent = entry.th32ParentProcessID; break; }
    entry.dwSize = sizeof entry;
    for (BOOL ok = parent && Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        if (entry.th32ProcessID == parent) {
            snprintf(out, size, "%ls (pid %lu)", entry.szExeFile, (unsigned long)parent);
            break;
        }
    CloseHandle(snapshot);
}

/* Steam injects its overlay (and Steam Input's XInput) after start-up. */
static DWORD WINAPI report_overlay(LPVOID unused)
{
    (void)unused;
    Sleep(MM3_OVERLAY_CHECK_MS);
    fprintf(stderr, "[MM3_DIRECT] after %u s: Steam overlay %s\n", MM3_OVERLAY_CHECK_MS / 1000,
            GetModuleHandleW(L"gameoverlayrenderer64.dll") ? "loaded" : "not loaded");
    fflush(stderr);
    return 0;
}

/* A laptop on battery runs its processor and graphics slower (Windows'
 * battery saver slower still), so a slow session's log says which. */
static void report_power(int *last)
{
    SYSTEM_POWER_STATUS power;
    char charge[16] = "";
    int state;
    if (!GetSystemPowerStatus(&power)) return;
    state = (power.ACLineStatus == 0 ? 1 : power.ACLineStatus == 1 ? 0 : 2) | (power.SystemStatusFlag & 1) << 2;
    if (state == *last) return;
    *last = state;
    if (power.BatteryLifePercent <= 100) snprintf(charge, sizeof charge, " (%u%%)", power.BatteryLifePercent);
    if ((state & 3) == 1)
        fprintf(stderr, "[MM3_POWER] on battery%s%s: the processor and graphics can run slower than on mains power\n",
                charge, state & 4 ? ", battery saver on" : "");
    else
        fprintf(stderr, "[MM3_POWER] %s%s\n", (state & 3) ? "power source unknown" : "on mains power",
                state & 4 ? ", battery saver on" : "");
    fflush(stderr);
}

static DWORD WINAPI watch_power(LPVOID unused)
{
    int last = -1;
    (void)unused;
    for (;;) {
        report_power(&last);
        Sleep(MM3_POWER_CHECK_MS);
    }
}

void mm3_power_watch(void)
{
    HANDLE thread = CreateThread(NULL, 0, watch_power, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

static void default_setting(const char *key, const char *value)
{
    const char *current = getenv(key);
    if (!current || !*current) _putenv_s(key, value);
}

void mm3_direct_launch_log(void)
{
    const char *setting = getenv("MM3_DIRECT_LOG");
    wchar_t log_path[MAX_PATH], csv_path[MAX_PATH], full[MAX_PATH];
    char csv[MAX_PATH * 3], parent[MAX_PATH + 32], log_utf8[MAX_PATH * 3];
    SYSTEMTIME now;
    HANDLE file, thread;
    int fd;
    if ((setting && !strcmp(setting, "0")) || stderr_redirected()) return;
    CreateDirectoryW(L"logs", NULL);
    prune_direct_logs(MM3_DIRECT_LOGS_KEPT - 1);
    GetSystemTime(&now);
    swprintf(log_path, MAX_PATH, L"logs\\direct-%04u%02u%02u-%02u%02u%02u.log",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    swprintf(csv_path, MAX_PATH, L"logs\\direct-%04u%02u%02u-%02u%02u%02u-frames.csv",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    file = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    fd = _open_osfhandle((intptr_t)file, _O_WRONLY | _O_TEXT);
    if (fd < 0) { CloseHandle(file); return; }
    fflush(stderr);
    if (_dup2(fd, _fileno(stderr)) != 0) { _close(fd); return; }
    _close(fd);
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(_fileno(stderr)));

    if (!GetFullPathNameW(csv_path, MAX_PATH, full, NULL) ||
        !WideCharToMultiByte(CP_UTF8, 0, full, -1, csv, (int)sizeof csv, NULL, NULL))
        csv[0] = 0;
    default_setting("RECOMP_D3D_PROFILE", "1");
    default_setting("RECOMP_HITCH_TRACE", "1");
    if (csv[0]) default_setting("RECOMP_FRAME_CSV", csv);
    parent_name(parent, sizeof parent);
    if (!GetFullPathNameW(log_path, MAX_PATH, full, NULL) ||
        !WideCharToMultiByte(CP_UTF8, 0, full, -1, log_utf8, (int)sizeof log_utf8, NULL, NULL))
        snprintf(log_utf8, sizeof log_utf8, "logs\\direct-*.log");
    fprintf(stderr, "[MM3_DIRECT] launched without play.ps1 at %04u-%02u-%02uT%02u:%02u:%02uZ; "
            "parent=%s; log=%s; frame_csv=%s (MM3_DIRECT_LOG=0 disables)\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            parent, log_utf8, csv[0] ? csv : "off");
    fflush(stderr);
    thread = CreateThread(NULL, 0, report_overlay, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}
