/* A Steam shortcut or a double-clicked exe starts the game without
 * scripts/play.ps1, so nothing records its output: stderr is a console
 * window (or nothing) and a slow session left no evidence. Write such a
 * launch's stderr to logs\direct-<UTC>.log with the same light timing as
 * "Play with performance diagnostics": once-a-second renderer summaries,
 * hitch records over 8 ms and a per-frame CSV. Those cost nothing
 * measurable; the smooth 19 October runs had them on. Only the newest few
 * direct logs are kept. MM3_DIRECT_LOG=0 keeps the console instead. */
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
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

#else /* !_WIN32 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "mm3_direct_log.h"

enum { MM3_DIRECT_LOGS_KEPT = 5 };

static int compare_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* Keeps the newest `keep` logs (their names sort by time) and removes the older ones and their frame tables. */
static void prune_direct_logs(int keep)
{
    static char names[256][256];
    DIR *dir = opendir("logs");
    struct dirent *entry;
    int count = 0, i;
    if (!dir) return;
    while ((entry = readdir(dir)) != NULL) {
        size_t length = strlen(entry->d_name);
        if (count < 256 && length > 4 && length < 200 && !strncmp(entry->d_name, "direct-", 7) &&
            !strcmp(entry->d_name + length - 4, ".log"))
            strcpy(names[count++], entry->d_name);
    }
    closedir(dir);
    qsort(names, (size_t)count, sizeof names[0], compare_names);
    for (i = 0; i < count - keep; ++i) {
        char path[512];
        size_t length = strlen(names[i]);
        snprintf(path, sizeof path, "logs/%s", names[i]);
        remove(path);
        snprintf(path, sizeof path, "logs/%.*s-frames.csv", (int)(length - 4), names[i]);
        remove(path);
    }
}

static void default_setting(const char *key, const char *value)
{
    const char *current = getenv(key);
    if (!current || !*current) setenv(key, value, 1);
}

/* The Linux start script writes logs/last-run.log itself. A Steam shortcut (Game Mode on the Steam Deck)
 * or a menu entry that runs mm3_recomp directly has no such file: stderr is a pipe or nothing, so the
 * session left no evidence. Write that launch's stderr to logs/direct-<UTC>.log with the renderer's
 * once-a-second summary, hitch records and a frame table, as the Windows build does. */
void mm3_direct_launch_log(void)
{
    const char *setting = getenv("MM3_DIRECT_LOG");
    struct stat info;
    char stamp[32], path[512], csv[512], cwd[1024];
    time_t now = time(NULL);
    struct tm utc;
    int fd;
    if (setting && !strcmp(setting, "0")) return;
    if (fstat(STDERR_FILENO, &info) == 0 && S_ISREG(info.st_mode)) return;   /* a file already: the start script's */
    mkdir("logs", 0755);
    prune_direct_logs(MM3_DIRECT_LOGS_KEPT - 1);
    gmtime_r(&now, &utc);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &utc);
    snprintf(path, sizeof path, "logs/direct-%s.log", stamp);
    snprintf(csv, sizeof csv, "logs/direct-%s-frames.csv", stamp);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    fflush(stderr);
    if (dup2(fd, STDERR_FILENO) < 0) { close(fd); return; }
    close(fd);
    default_setting("RECOMP_D3D_PROFILE", "1");
    default_setting("RECOMP_HITCH_TRACE", "1");
    default_setting("RECOMP_FRAME_CSV", csv);
    default_setting("RECOMP_SDLGPU_SPEC", "log");
    if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, ".");
    fprintf(stderr, "[MM3_DIRECT] launched without the start script at %s UTC; log=%s/%s; frame_csv=%s "
            "(MM3_DIRECT_LOG=0 disables)\n", stamp, cwd, path, csv);
    fflush(stderr);
}
/* ---- system watch (Linux): why a handheld runs slower than it should ----
 * Every 10 s one [MM3_SYS] line: processor load and clocks, graphics clock and
 * temperatures, battery or mains, free memory and the game's own memory. A slow or
 * hitching session's log then says whether the machine was throttling, starved of
 * memory or simply busy. The first line also names the machine. */
#include <pthread.h>

static int read_text(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f) return 0;
    n = fread(out, 1, size - 1, f);
    fclose(f);
    out[n] = 0;
    while (n && (out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
    return n > 0;
}

static long read_long(const char *path, long fallback)
{
    char text[64];
    return read_text(path, text, sizeof text) ? strtol(text, NULL, 10) : fallback;
}

static void cpu_times(unsigned long long *busy, unsigned long long *total)
{
    unsigned long long v[8] = {0};
    FILE *f = fopen("/proc/stat", "r");
    *busy = *total = 0;
    if (!f) return;
    if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) >= 4) {
        int i;
        for (i = 0; i < 8; i++) *total += v[i];
        *busy = *total - v[3] - v[4];
    }
    fclose(f);
}

static void sys_describe(void)
{
    char text[256] = "", model[256] = "unknown", board[128] = "", kernel[128] = "";
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[512];
    int cores = 0, named = 0;
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "model name", 10) && !named) {
                named = 1;
                char *colon = strchr(line, ':');
                if (colon) { snprintf(model, sizeof model, "%s", colon + 2); model[strcspn(model, "\n")] = 0; }
            }
            if (!strncmp(line, "processor", 9)) cores++;
        }
        fclose(f);
    }
    read_text("/sys/devices/virtual/dmi/id/product_name", board, sizeof board);
    f = fopen("/proc/version", "r");
    if (f) { if (fgets(kernel, sizeof kernel, f)) kernel[strcspn(kernel, "\n")] = 0; fclose(f); }
    if (read_text("/etc/os-release", text, sizeof text)) { char *p = strstr(text, "PRETTY_NAME="); if (p) memmove(text, p + 12, strlen(p + 12) + 1); text[strcspn(text, "\n")] = 0; }
    fprintf(stderr, "[MM3_SYS] machine: %s | cpu: %s, %d threads | os: %s | session: %s | kernel: %.100s\n",
            board[0] ? board : "unknown", model, cores, text, getenv("XDG_SESSION_TYPE") ? getenv("XDG_SESSION_TYPE") : "?",
            kernel);
    fprintf(stderr, "[MM3_SYS] env: SteamDeck=%s SteamGameId=%s gamescope=%s MESA_SHADER_CACHE_DIR=%s DISPLAY=%s WAYLAND=%s\n",
            getenv("SteamDeck") ? getenv("SteamDeck") : "-", getenv("SteamGameId") ? getenv("SteamGameId") : "-",
            getenv("GAMESCOPE_WAYLAND_DISPLAY") ? "yes" : "no", getenv("MESA_SHADER_CACHE_DIR") ? getenv("MESA_SHADER_CACHE_DIR") : "-",
            getenv("DISPLAY") ? getenv("DISPLAY") : "-", getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "-");
    fflush(stderr);
}

/* The current graphics clock (the starred line of pp_dpm_sclk), 0 when the card has none. */
static long gpu_clock_mhz(void)
{
    int card;
    for (card = 0; card < 4; card++) {
        char path[128], text[512], *star;
        snprintf(path, sizeof path, "/sys/class/drm/card%d/device/pp_dpm_sclk", card);
        if (read_text(path, text, sizeof text) && (star = strchr(text, '*')) != NULL) {
            char *start = star;
            while (start > text && start[-1] != '\n') start--;
            return strtol(strchr(start, ':') ? strchr(start, ':') + 1 : start, NULL, 10);
        }
    }
    return 0;
}

static long hottest_millidegrees(void)
{
    long best = 0;
    int i;
    for (i = 0; i < 16; i++) {
        char path[128];
        long t;
        snprintf(path, sizeof path, "/sys/class/hwmon/hwmon%d/temp1_input", i);
        t = read_long(path, 0);
        if (t > best) best = t;
    }
    return best;
}

static void *sys_watch(void *unused)
{
    unsigned long long busy0, total0, busy1, total1;
    (void)unused;
    sys_describe();
    cpu_times(&busy0, &total0);
    for (;;) {
        char cap[32] = "", status[32] = "", mem[64] = "", line[256];
        long freq_sum = 0, freq_min = 0, freq_max = 0, rss_pages = 0, free_kb = -1;
        int cpu, seen = 0;
        FILE *f;
        sleep(10);
        for (cpu = 0; cpu < 32; cpu++) {
            char path[96];
            long khz;
            snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
            khz = read_long(path, 0);
            if (!khz) continue;
            seen++;
            freq_sum += khz;
            if (!freq_min || khz < freq_min) freq_min = khz;
            if (khz > freq_max) freq_max = khz;
        }
        cpu_times(&busy1, &total1);
        read_text("/sys/class/power_supply/BAT1/capacity", cap, sizeof cap) || read_text("/sys/class/power_supply/BAT0/capacity", cap, sizeof cap);
        read_text("/sys/class/power_supply/BAT1/status", status, sizeof status) || read_text("/sys/class/power_supply/BAT0/status", status, sizeof status);
        f = fopen("/proc/meminfo", "r");
        if (f) {
            while (fgets(line, sizeof line, f))
                if (!strncmp(line, "MemAvailable:", 13)) { free_kb = strtol(line + 13, NULL, 10); break; }
            fclose(f);
        }
        f = fopen("/proc/self/statm", "r");
        if (f) { long size; if (fscanf(f, "%ld %ld", &size, &rss_pages) != 2) rss_pages = 0; fclose(f); }
        snprintf(mem, sizeof mem, "%ld MB free, game %ld MB", free_kb / 1024, rss_pages * (sysconf(_SC_PAGESIZE) / 1024) / 1024);
        fprintf(stderr, "[MM3_SYS] cpu %.0f%% busy, clock %ld-%ld MHz (mean %ld) | gpu clock %ld MHz | hottest sensor %ld C | "
                "battery %s%% %s | memory %s\n",
                total1 > total0 ? 100.0 * (double)(busy1 - busy0) / (double)(total1 - total0) : 0.0,
                freq_min / 1000, freq_max / 1000, seen ? freq_sum / seen / 1000 : 0, gpu_clock_mhz(),
                hottest_millidegrees() / 1000, cap[0] ? cap : "-", status[0] ? status : "-", mem);
        fflush(stderr);
        busy0 = busy1;
        total0 = total1;
    }
    return NULL;
}

void mm3_power_watch(void)
{
    pthread_t thread;
    if (!pthread_create(&thread, NULL, sys_watch, NULL)) pthread_detach(thread);
}
#endif /* _WIN32 */
