/* Steam's overlay for a game that Steam started through a script (issue #47). See mm3_steam_overlay.h.
 *
 * Steam hooks the process it creates and, normally, the processes that one creates in turn. A
 * shortcut to "Play Midtown Madness 3.cmd" starts cmd.exe, which starts mm3_recomp.exe, and that
 * second step did not get the overlay: no FPS counter, and the game recorder found no game. The
 * log of a shortcut to the exe says "Steam overlay loaded" ([MM3_DIRECT]); the script's did not.
 *
 * Valve's way to get the overlay into a program Steam started but did not hook is to load
 * GameOverlayRenderer64.dll from the Steam folder before the program creates its graphics device;
 * the DLL finds Steam from the SteamGameId / SteamOverlayGameId environment the child inherited. */
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "mm3_steam_overlay.h"

#pragma comment(lib, "advapi32.lib")

#ifdef _WIN64
#define OVERLAY_DLL L"GameOverlayRenderer64.dll"
#else
#define OVERLAY_DLL L"GameOverlayRenderer.dll"
#endif

enum { ANCESTOR_LIMIT = 8 };

static int present(const char *value)
{
    return value && *value;
}

int mm3_steam_overlay_env_is_steam(const char *game_id, const char *overlay_game_id, const char *client_launch)
{
    return present(game_id) || present(overlay_game_id) || present(client_launch);
}

/* Steam starts a game as its own child, so steam.exe is among the program's ancestors (a script
 * or the Vulkan hand-off may sit in between). The walk is bounded: a process id can be reused. */
static int steam_is_an_ancestor(void)
{
    PROCESSENTRY32W entry;
    DWORD pid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int depth, found = 0;
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    for (depth = 0; depth < ANCESTOR_LIMIT && !found; ++depth) {
        DWORD parent = 0;
        BOOL ok;
        entry.dwSize = sizeof entry;
        for (ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
            if (entry.th32ProcessID == pid) { parent = entry.th32ParentProcessID; break; }
        if (!parent || parent == pid) break;
        entry.dwSize = sizeof entry;
        for (ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
            if (entry.th32ProcessID == parent) {
                found = !_wcsicmp(entry.szExeFile, L"steam.exe");
                pid = parent;
                break;
            }
        if (pid != parent) break;   /* the parent has exited: the chain ends here */
    }
    CloseHandle(snapshot);
    return found;
}

static void slashes_to_backslashes(wchar_t *path)
{
    for (; *path; ++path)
        if (*path == L'/') *path = L'\\';
}

static int read_registry(HKEY root, const wchar_t *key, const wchar_t *name, wchar_t *out, DWORD chars)
{
    DWORD bytes = chars * (DWORD)sizeof(wchar_t);
    out[0] = 0;
    return RegGetValueW(root, key, name, RRF_RT_REG_SZ, NULL, out, &bytes) == ERROR_SUCCESS && out[0];
}

/* The Steam folders to try, most reliable first: the folder Steam told this process about, then
 * where Windows says Steam is installed. Returns how many were written to dirs[]. */
static int steam_folders(wchar_t dirs[4][MAX_PATH])
{
    wchar_t value[MAX_PATH];
    int count = 0;
    if (GetEnvironmentVariableW(L"SteamPath", value, MAX_PATH) && value[0])
        wcscpy_s(dirs[count++], MAX_PATH, value);
    if (read_registry(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", value, MAX_PATH))
        wcscpy_s(dirs[count++], MAX_PATH, value);
    if (read_registry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", value, MAX_PATH))
        wcscpy_s(dirs[count++], MAX_PATH, value);
    if (count < 4 && GetEnvironmentVariableW(L"ProgramFiles(x86)", value, MAX_PATH) && value[0] &&
        swprintf(dirs[count], MAX_PATH, L"%ls\\Steam", value) > 0)
        ++count;
    return count;
}

/* Wine and Proton (Linux, Steam Deck) have their own, native overlay: never load Windows' one there. */
static int running_under_wine(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != NULL;
}

static int file_exists(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* A path for the log, as UTF-8 (a user name with accents must not cut the line short). */
static const char *utf8(const wchar_t *wide, char *out, size_t size)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)size, NULL, NULL)) snprintf(out, size, "?");
    return out;
}

static int report(int code, const char *text)
{
    fprintf(stderr, "[MM3_STEAM] %s\n", text);
    fflush(stderr);
    return code;
}

int mm3_steam_overlay_attach(void)
{
    const char *setting = getenv("MM3_STEAM_OVERLAY");
    const char *game_id = getenv("SteamGameId"), *overlay_id = getenv("SteamOverlayGameId");
    int by_environment, by_ancestor = 0;
    wchar_t dirs[4][MAX_PATH], dll[MAX_PATH + 32];
    char text[MAX_PATH * 3 + 160], narrow[MAX_PATH * 3];
    int count, i;
    if ((setting && !strcmp(setting, "0")) || running_under_wine()) return MM3_STEAM_OVERLAY_NOT_STEAM;
    by_environment = mm3_steam_overlay_env_is_steam(game_id, overlay_id, getenv("SteamClientLaunch"));
    if (!by_environment) by_ancestor = steam_is_an_ancestor();
    if (!by_environment && !by_ancestor) return MM3_STEAM_OVERLAY_NOT_STEAM;
    if (GetModuleHandleW(OVERLAY_DLL)) return report(MM3_STEAM_OVERLAY_ALREADY, "Steam overlay already loaded");

    count = steam_folders(dirs);
    for (i = 0; i < count; ++i) {
        size_t length;
        slashes_to_backslashes(dirs[i]);
        length = wcslen(dirs[i]);
        while (length && dirs[i][length - 1] == L'\\') dirs[i][--length] = 0;
        if (swprintf(dll, MAX_PATH + 32, L"%ls\\%ls", dirs[i], OVERLAY_DLL) < 0 || !file_exists(dll)) continue;
        /* The DLL's own neighbours in the Steam folder come first in the search order. */
        if (LoadLibraryExW(dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH)) {
            snprintf(text, sizeof text, "started by Steam (%s) without its overlay: loaded %s (SteamGameId=%s)",
                     by_environment ? "environment" : "parent process", utf8(dll, narrow, sizeof narrow),
                     present(game_id) ? game_id : "-");
            return report(MM3_STEAM_OVERLAY_LOADED, text);
        }
        snprintf(text, sizeof text, "could not load %s (Windows error %lu); no overlay or recording",
                 utf8(dll, narrow, sizeof narrow), (unsigned long)GetLastError());
        return report(MM3_STEAM_OVERLAY_FAILED, text);
    }
    return report(MM3_STEAM_OVERLAY_MISSING, "started by Steam but GameOverlayRenderer64.dll was not found in the Steam folder");
}

/* ---- Library pictures -------------------------------------------------------------------------
 * A shortcut added to Steam by hand has no pictures. Steam takes custom ones from
 * <Steam>\userdata\<user>\config\grid\<number>p.png (portrait), <number>.png (wide), _hero, _logo,
 * _icon, where <number> is the shortcut's own: Steam passes it to the game as SteamGameId, (number << 32)
 * | 0x02000000. Setup draws the pictures into <install>\steam-artwork (installer.py) and copies them for a
 * shortcut that exists then; this copies them the first time Steam starts the game, for a shortcut made
 * afterwards. Nothing that is already there is replaced. Steam shows them after it restarts. */
static const struct { const wchar_t *suffix, *file; } art_files[] = {
    {L"p", L"capsule-portrait.png"}, {L"", L"capsule-wide.png"}, {L"_hero", L"hero.png"},
    {L"_logo", L"logo.png"}, {L"_icon", L"icon.png"}};
static const wchar_t *const art_extensions[] = {L"png", L"jpg", L"jpeg", L"webp"};

unsigned mm3_steam_shortcut_number(const char *game_id)
{
    unsigned long long id;
    char *end;
    if (!present(game_id)) return 0;
    id = _strtoui64(game_id, &end, 10);
    if (*end || (id & 0xFFFFFFFFull) != 0x02000000ull || (id >> 32) == 0) return 0;   /* a Steam game, not a shortcut */
    return (unsigned)(id >> 32);
}

int mm3_steam_art_copy(const wchar_t *source_dir, const wchar_t *grid_dir, unsigned number)
{
    wchar_t source[MAX_PATH + 40], target[MAX_PATH + 64];
    int copied = 0;
    size_t i, e;
    for (i = 0; i < sizeof art_files / sizeof art_files[0]; ++i) {
        int taken = 0;
        if (swprintf(source, MAX_PATH + 40, L"%ls\\%ls", source_dir, art_files[i].file) < 0 || !file_exists(source)) continue;
        for (e = 0; e < sizeof art_extensions / sizeof art_extensions[0]; ++e)
            if (swprintf(target, MAX_PATH + 64, L"%ls\\%u%ls.%ls", grid_dir, number, art_files[i].suffix,
                         art_extensions[e]) > 0 && GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES)
                taken = 1;
        if (taken) continue;                                  /* their own picture, or ours from before */
        CreateDirectoryW(grid_dir, NULL);
        swprintf(target, MAX_PATH + 64, L"%ls\\%u%ls.png", grid_dir, number, art_files[i].suffix);
        if (CopyFileW(source, target, TRUE)) ++copied;
    }
    return copied;
}

int mm3_steam_art_place(void)
{
    const char *setting = getenv("MM3_STEAM_ART");
    unsigned number = mm3_steam_shortcut_number(getenv("SteamGameId"));
    wchar_t dirs[4][MAX_PATH], exe[MAX_PATH], source[MAX_PATH + 16], config[MAX_PATH + 64], grid[MAX_PATH + 72];
    DWORD user = 0, bytes = sizeof user, length;
    char text[MAX_PATH * 3 + 160], narrow[MAX_PATH * 3];
    int count, i, copied;
    if (!number || (setting && !strcmp(setting, "0")) || running_under_wine()) return 0;
    length = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!length || length >= MAX_PATH) return 0;
    while (length && exe[length - 1] != L'\\' && exe[length - 1] != L'/') --length;
    exe[length ? length - 1 : 0] = 0;
    if (swprintf(source, MAX_PATH + 16, L"%ls\\steam-artwork", exe) < 0 || GetFileAttributesW(source) == INVALID_FILE_ATTRIBUTES)
        return 0;                                             /* no pictures were made for this install */
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", RRF_RT_REG_DWORD, NULL,
                     &user, &bytes) != ERROR_SUCCESS || !user)
        return 0;                                             /* Steam is not logged in */
    count = steam_folders(dirs);
    for (i = 0; i < count; ++i) {
        size_t n;
        slashes_to_backslashes(dirs[i]);
        n = wcslen(dirs[i]);
        while (n && dirs[i][n - 1] == L'\\') dirs[i][--n] = 0;
        if (swprintf(config, MAX_PATH + 64, L"%ls\\userdata\\%lu\\config", dirs[i], (unsigned long)user) < 0 ||
            GetFileAttributesW(config) == INVALID_FILE_ATTRIBUTES)
            continue;
        swprintf(grid, MAX_PATH + 72, L"%ls\\grid", config);
        copied = mm3_steam_art_copy(source, grid, number);
        if (copied)
            snprintf(text, sizeof text, "Steam library pictures for shortcut %u: copied %d into %s (restart Steam to see them)",
                     number, copied, utf8(grid, narrow, sizeof narrow));
        else
            snprintf(text, sizeof text, "Steam library pictures for shortcut %u: already there, or none to copy (%s)",
                     number, utf8(grid, narrow, sizeof narrow));
        report(0, text);
        return copied;
    }
    return 0;
}

#endif /* _WIN32 */
