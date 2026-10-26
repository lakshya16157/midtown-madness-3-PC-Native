#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <wchar.h>
#include "mm3_display_config.h"
#include "mm3_launcher_config.h"
#include "../tools/xboxrecomp/src/input/input_bindings.h"

const MM3LauncherOptionInfo mm3_launcher_options[MM3_LAUNCH_OPTION_COUNT] = {
    {"RECOMP_KEYBOARD", "Enable keyboard controls (player 1)", 1},
    {"MM3_MOD_WIDESCREEN", "Widescreen view and HUD", 1},
    {"MM3_MOD_DRAW_DISTANCE", "Longer view distance", 1},
    {"MM3_MOD_SKY", "Smooth skyline", 1},
    {"MM3_MOD_CAR_LIGHTING", "Improved car lights", 1},
    {"MM3_MOD_LAMP_LIGHTING", "Improved street lamps", 1},
    {"MM3_MOD_WORLD_LIGHTING", "Improved world lighting", 1},
    {"MM3_MOD_FLARE_DEPTH", "Flares hide behind objects", 1},
    {"MM3_UNLOCK_ALL_CARS", "Unlock all vehicles", 0},
    {"MM3_MOD_DEV_TOOLS", "Retail developer tools", 0},
    {"MM3_DEV_HUD", "Developer HUD", 0},
    {"MM3_DEBUG_MENU", "Developer menu", 0},
    {"MM3_EXTENDED_CAMERAS", "Extended camera modes", 0},
    {"MM3_E3_PRESET", "E3 demonstration preset", 0},
    {"RECOMP_LOADING_SPINNER", "Loading animation", 1},
    {"MM3_NATIVE_FOG", "Native depth fog", 1},
    {"RECOMP_APU_DSP", "Accurate sound processor", 1},
    {"RECOMP_NET", "System Link networking", 0},
    {"MM3_HUD_NORTH_MARKER", "Compass pins on minimap", 0},
    {"MM3_HUD_RACE_ARROW", "Race direction arrow", 0},
    {"MM3_ROUTE_NAVIGATION", "Route line on the map", 1},
    {"MM3_DRAFTING", "Slipstream drafting", 0},
    {"MM3_ROLL_GUARD", "Rollover assist", 0},
    {"MM3_AI_ALL_CARS", "Circuit AI uses all cars", 1},
    {"MM3_IMPROVED", "Improved look", 1},
    {"MM3_GRAPHICS_MENU", "In-game Graphics menu", 1},
    {"MM3_EXTRA_CIRCUITS", "Extra circuit races", 1},
    {"MM3_EXTRA_CARS", "Extra installed cars", 1},
    {"MM3_DLC_RACE_INFO", "DLC race names and pictures", 1},
    {"MM3_FALLING_TREES", "Trees fall over when hit", 1},
    {"MM3_EXHAUST_TRIANGLES", "Exhaust smoke", 0},
    {"MM3_GAMEPLAY_MODS", "Modded gameplay", 0},
    {"MM3_MENU_QUIT", "Quit in the main menu", 1}
};

typedef struct { int option; const char *value; } MM3ForcedOption;
/* The Original look (MM3_IMPROVED=0): the Xbox's own visuals, menus and HUD;
 * the graphics and audio upgrades (widescreen, skyline, native fog, sound
 * processor) stay as chosen. Nothing here changes the gameplay. */
static const MM3ForcedOption original_look[] = {
    {MM3_LAUNCH_DISTANCE, "0"}, {MM3_LAUNCH_CAR_LIGHT, "0"}, {MM3_LAUNCH_LAMP_LIGHT, "0"},
    {MM3_LAUNCH_WORLD_LIGHT, "0"}, {MM3_LAUNCH_FLARE_DEPTH, "0"}, {MM3_LAUNCH_LOADING, "0"},
    {MM3_LAUNCH_NORTH_MARKER, "1"}, {MM3_LAUNCH_GRAPHICS_MENU, "0"}, {MM3_LAUNCH_DLC_INFO, "0"},
    {MM3_LAUNCH_EXHAUST, "0"}, {MM3_LAUNCH_QUIT, "0"}
};
/* Xbox gameplay (MM3_GAMEPLAY_MODS=0): driving, races, cars, progression and
 * navigation as on the Xbox. No driving assists, falling trees, circuits,
 * extra cars, unlocks or developer commands; the game's own race arrow and no
 * route line (the retail minimap too: mm3_minimap_improved). */
static const MM3ForcedOption xbox_gameplay[] = {
    {MM3_LAUNCH_DRAFTING, "0"}, {MM3_LAUNCH_ROLL_GUARD, "0"}, {MM3_LAUNCH_TREES, "0"},
    {MM3_LAUNCH_CIRCUITS, "0"}, {MM3_LAUNCH_EXTRA_CARS, "0"}, {MM3_LAUNCH_AI_ALL_CARS, "0"},
    {MM3_LAUNCH_UNLOCK, "0"}, {MM3_LAUNCH_ROUTE_NAVIGATION, "0"}, {MM3_LAUNCH_RACE_ARROW, "1"},
    {MM3_LAUNCH_DEV_TOOLS, "0"}, {MM3_LAUNCH_DEV_HUD, "0"}, {MM3_LAUNCH_DEBUG_MENU, "0"},
    {MM3_LAUNCH_CAMERAS, "0"}, {MM3_LAUNCH_E3, "0"}
};

/* The launcher saves every option, so a changed default would never reach a
 * saved file. Files from before this revision get these options back at
 * their new default once. Revision 2: drafting and the rollover assist change
 * how cars drive, so they start off, as on the Xbox. */
#define SETTINGS_REVISION 2
static const int revision_defaults[] = {MM3_LAUNCH_DRAFTING, MM3_LAUNCH_ROLL_GUARD};

int mm3_launcher_option_master(int option)
{
    for (size_t i = 0; i < sizeof original_look / sizeof original_look[0]; ++i)
        if (original_look[i].option == option) return MM3_LAUNCH_IMPROVED;
    for (size_t i = 0; i < sizeof xbox_gameplay / sizeof xbox_gameplay[0]; ++i)
        if (xbox_gameplay[i].option == option) return MM3_LAUNCH_GAMEPLAY;
    return -1;
}
const char *const mm3_launcher_key_names[MM3_LAUNCHER_KEYS] = {
    "A", "B", "X", "Y", "WHITE", "BLACK", "LT", "RT", "START", "BACK",
    "UP", "DOWN", "LEFT", "RIGHT", "LS", "RS", "LX-", "LX+", "LY-", "LY+",
    "RX-", "RX+", "RY-", "RY+"
};
const char *const mm3_launcher_drive_names[MM3_LAUNCHER_DRIVE_KEYS] = {
    "THROTTLE", "BRAKE", "LEFT", "RIGHT", "HANDBRAKE", "GEAR_UP", "GEAR_DOWN", "CAMERA"
};
const char *const mm3_launcher_pad_names[MM3_LAUNCHER_PAD_INPUTS] = {
    "A", "B", "X", "Y", "BLACK", "WHITE", "START", "BACK", "LS", "RS",
    "UP", "DOWN", "LEFT", "RIGHT", "LT", "RT"
};
const char *const mm3_launcher_pad_sources[MM3_LAUNCHER_PAD_SOURCES] = {
    "A", "B", "X", "Y", "LB", "RB", "START", "BACK", "LS", "RS",
    "UP", "DOWN", "LEFT", "RIGHT", "LT", "RT", "NONE"
};

static void error_set(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
}
static char *trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    return text;
}
static int integer(const char *text, long minimum, long maximum, int *result)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (text == end || errno || *end || value < minimum || value > maximum) return 0;
    *result = (int)value;
    return 1;
}
static int peers_valid(const char *value)
{
    const char *at = value;
    int count = 0;
    if (strlen(value) >= 512) return 0;
    while (*at) {
        const char *colon, *comma;
        char port[8];
        size_t size;
        int number;
        if (++count > 8) return 0;
        comma = strchr(at, ',');
        if (!comma) comma = at + strlen(at);
        colon = memchr(at, ':', (size_t)(comma - at));
        if (!colon || colon == at || colon - at > 253) return 0;
        for (const char *p = at; p < colon; ++p)
            if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-' && *p != '_') return 0;
        size = (size_t)(comma - colon - 1);
        if (!size || size >= sizeof port) return 0;
        for (const char *p = colon + 1; p < comma; ++p)
            if (*p < '0' || *p > '9') return 0;
        memcpy(port, colon + 1, size); port[size] = 0;
        if (!integer(port, 1, 65535, &number)) return 0;
        at = *comma ? comma + 1 : comma;
        if (*comma && !*at) return 0;
    }
    return 1;
}

void mm3_launcher_config_defaults(MM3LauncherConfig *config)
{
    XbInputBindings bindings;
    xb_bindings_defaults(&bindings);
    memset(config, 0, sizeof *config);
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        config->option[i] = mm3_launcher_options[i].default_value;
    for (int i = 0; i < MM3_LAUNCHER_KEYS; ++i) config->keys[i] = bindings.keys[i];
    for (int i = 0; i < MM3_LAUNCHER_DRIVE_KEYS; ++i) config->drive_keys[i] = bindings.drive[i];
    for (int i = 0; i < MM3_LAUNCHER_PAD_INPUTS; ++i) config->pad[i] = bindings.pad[i];
    config->controller_count = 1;
    config->network_port = 3075;
}

int mm3_launcher_config_valid(const MM3LauncherConfig *config)
{
    if (!config || config->controller_count < 1 || config->controller_count > 4 ||
        (config->fov_degrees != 0 &&
        (config->fov_degrees < 60 || config->fov_degrees > 110)) ||
        config->network_port < 1 || config->network_port > 65535 ||
        !memchr(config->peers, 0, sizeof config->peers) || !peers_valid(config->peers)) return 0;
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        if (config->option[i] < 0 || config->option[i] > 1) return 0;
    for (int i = 0; i < MM3_LAUNCHER_KEYS; ++i)
        if (config->keys[i] < 0 || config->keys[i] > 255 || config->keys[i] == VK_F10 || config->keys[i] == VK_F11) return 0;
    for (int i = 0; i < MM3_LAUNCHER_DRIVE_KEYS; ++i)
        if (config->drive_keys[i] < 0 || config->drive_keys[i] > 255 || config->drive_keys[i] == VK_F10 || config->drive_keys[i] == VK_F11) return 0;
    for (int i = 0; i < MM3_LAUNCHER_PAD_INPUTS; ++i)
        if (config->pad[i] < 0 || config->pad[i] >= MM3_LAUNCHER_PAD_SOURCES) return 0;
    return 1;
}

static int parse_map(const char *value, int *map, int count,
                     const char *const *names, int pad)
{
    uint16_t copy[MM3_LAUNCHER_KEYS];
    for (int i = 0; i < count; ++i) copy[i] = (uint16_t)(pad && map[i] == 16 ? XB_PAD_NONE : map[i]);
    if (!xb_bindings_parse(value, names, (unsigned)count, copy, pad)) return 0;
    for (int i = 0; i < count; ++i)
        if (!pad && (copy[i] == VK_F10 || copy[i] == VK_F11)) return 0;
    for (int i = 0; i < count; ++i) map[i] = pad && copy[i] == XB_PAD_NONE ? 16 : copy[i];
    return 1;
}

int mm3_launcher_config_assign(MM3LauncherConfig *config, const char *key, const char *value)
{
    int number;
    if (!config || !key || !value) return 0;
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i) {
        if (strcmp(key, mm3_launcher_options[i].key)) continue;
        if (i == MM3_LAUNCH_NETWORK && (!*value || !_stricmp(value, "off"))) number = 0;
        else if (i == MM3_LAUNCH_NETWORK && !_stricmp(value, "nat")) number = 1;
        else if (!integer(value, 0, 1, &number)) return 0;
        config->option[i] = number;
        return 1;
    }
    if (!strcmp(key, "MM3_FOV_DEGREES")) {
        if (!*value) number = 0;
        else if (!integer(value, 0, 110, &number) || (number && number < 60)) return 0;
        config->fov_degrees = number; return 1;
    }
    if (!strcmp(key, "RECOMP_USB_PADS")) {
        if (!integer(value, 1, 4, &number)) return 0;
        config->controller_count = number; return 1;
    }
    if (!strcmp(key, "RECOMP_NET_PORT")) {
        if (!integer(value, 1, 65535, &number)) return 0;
        config->network_port = number; return 1;
    }
    if (!strcmp(key, "RECOMP_NET_PEERS")) {
        if (!peers_valid(value)) return 0;
        strcpy(config->peers, value); return 1;
    }
    if (!strcmp(key, "RECOMP_KEYMAP"))
        return parse_map(value, config->keys, MM3_LAUNCHER_KEYS, mm3_launcher_key_names, 0);
    if (!strcmp(key, "MM3_DRIVE_KEYMAP"))
        return parse_map(value, config->drive_keys, MM3_LAUNCHER_DRIVE_KEYS, mm3_launcher_drive_names, 0);
    if (!strcmp(key, "RECOMP_PADMAP"))
        return parse_map(value, config->pad, MM3_LAUNCHER_PAD_INPUTS, mm3_launcher_pad_names, 1);
    return 0;
}

int mm3_launcher_config_path(char *path, size_t capacity, char *error, size_t error_capacity)
{
    const wchar_t *override = _wgetenv(L"MM3_LAUNCHER_CONFIG");
    char display[32768], *separator;
    int length;
    error_set(error, error_capacity, "");
    if (override && *override) {
        if (capacity <= INT_MAX && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            override, -1, path, (int)capacity, NULL, NULL)) return 1;
        error_set(error, error_capacity, "Launcher settings path is too long or invalid."); return 0;
    }
    if (!mm3_display_settings_path(display, sizeof display, error, error_capacity)) return 0;
    separator = strrchr(display, '\\');
    { char *other = strrchr(display, '/'); if (other && (!separator || other > separator)) separator = other; }
    if (separator) separator[1] = 0; else display[0] = 0;
    length = snprintf(path, capacity, "%slauncher.ini", display);
    if (length >= 0 && (size_t)length < capacity) return 1;
    error_set(error, error_capacity, "Launcher settings path is too long."); return 0;
}

int mm3_launcher_config_load(MM3LauncherConfig *config, char *error, size_t capacity)
{
    char path[32768], line[2048];
    wchar_t wide[32768];
    FILE *file;
    int failed, revision = 1;
    error_set(error, capacity, "");
    if (!mm3_launcher_config_path(path, sizeof path, error, capacity)) return 0;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 32768)) return 0;
    file = _wfopen(wide, L"rb");
    if (!file) {
        if (errno == ENOENT) return 1;
        error_set(error, capacity, "Cannot read launcher settings."); return 0;
    }
    while (fgets(line, sizeof line, file)) {
        char *key, *value, *equals;
        if (!strchr(line, '\n') && !feof(file)) {
            int ch; do { ch = fgetc(file); } while (ch != '\n' && ch != EOF);
            continue;
        }
        key = trim(line);
        if (!*key || *key == '#' || *key == ';' || *key == '[') continue;
        equals = strchr(key, '=');
        if (!equals) continue;
        *equals = 0; key = trim(key); value = trim(equals + 1);
        if (!strcmp(key, "MM3_SETTINGS_REVISION")) {
            if (!integer(value, 1, INT_MAX, &revision))
                error_set(error, capacity, "Some unknown or invalid launcher settings were ignored.");
            continue;
        }
        if (!mm3_launcher_config_assign(config, key, value))
            error_set(error, capacity, "Some unknown or invalid launcher settings were ignored.");
    }
    failed = ferror(file);
    if (fclose(file)) failed = 1;
    if (failed) error_set(error, capacity, "Cannot finish reading launcher settings.");
    else if (revision < SETTINGS_REVISION)
        for (size_t i = 0; i < sizeof revision_defaults / sizeof revision_defaults[0]; ++i)
            config->option[revision_defaults[i]] = mm3_launcher_options[revision_defaults[i]].default_value;
    return !failed;
}

static int format_map(char *text, size_t capacity, const int *map, int count,
                       const char *const *names, int pad)
{
    size_t at = 0;
    for (int i = 0; i < count; ++i) {
        int written;
        if (pad) written = snprintf(text + at, capacity - at, "%s%s:%s", i ? "," : "", names[i],
                                     mm3_launcher_pad_sources[map[i]]);
        else written = snprintf(text + at, capacity - at, "%s%s:%02X", i ? "," : "", names[i], map[i]);
        if (written < 0 || (size_t)written >= capacity - at) return 0;
        at += (size_t)written;
    }
    return 1;
}

int mm3_launcher_config_save(const MM3LauncherConfig *config, char *error, size_t capacity)
{
    char path[32768], keys[1024], drive[512], pad[512];
    wchar_t wide[32768], temporary[32768], parent[32768], *slash;
    static volatile LONG sequence;
    HANDLE handle;
    FILE *file;
    int descriptor, failed = 0;
    error_set(error, capacity, "");
    if (!mm3_launcher_config_valid(config)) {
        error_set(error, capacity, "Invalid launcher settings; preferences were not saved."); return 0;
    }
    if (!mm3_launcher_config_path(path, sizeof path, error, capacity) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 32768)) return 0;
    wcscpy(parent, wide); slash = wcsrchr(parent, L'\\');
    if (!slash) slash = wcsrchr(parent, L'/');
    if (slash) { *slash = 0; CreateDirectoryW(parent, NULL); }
    if (swprintf(temporary, 32768, L"%ls.%lu.%lu.tmp", wide, GetCurrentProcessId(),
                 (unsigned long)InterlockedIncrement(&sequence)) < 0) {
        error_set(error, capacity, "Launcher settings path is too long."); return 0;
    }
    handle = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        error_set(error, capacity, "Cannot create temporary launcher settings."); return 0;
    }
    descriptor = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
    file = descriptor >= 0 ? _fdopen(descriptor, "wb") : NULL;
    if (!file) {
        if (descriptor >= 0) _close(descriptor); else CloseHandle(handle);
        DeleteFileW(temporary); error_set(error, capacity, "Cannot write launcher settings."); return 0;
    }
    format_map(keys, sizeof keys, config->keys, MM3_LAUNCHER_KEYS, mm3_launcher_key_names, 0);
    format_map(drive, sizeof drive, config->drive_keys, MM3_LAUNCHER_DRIVE_KEYS, mm3_launcher_drive_names, 0);
    format_map(pad, sizeof pad, config->pad, MM3_LAUNCHER_PAD_INPUTS, mm3_launcher_pad_names, 1);
    if (fprintf(file, "# Midtown Madness 3 launcher preferences\n[launcher]\nMM3_SETTINGS_REVISION=%d\n",
                SETTINGS_REVISION) < 0) failed = 1;
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        if (fprintf(file, "%s=%d\n", mm3_launcher_options[i].key, config->option[i]) < 0) failed = 1;
    if (fprintf(file, "RECOMP_USB_PADS=%d\nMM3_FOV_DEGREES=%d\nRECOMP_NET_PORT=%d\nRECOMP_NET_PEERS=%s\n"
                "RECOMP_KEYMAP=%s\nMM3_DRIVE_KEYMAP=%s\nRECOMP_PADMAP=%s\n",
                config->controller_count, config->fov_degrees, config->network_port, config->peers, keys, drive, pad) < 0) failed = 1;
    if (fflush(file) || _commit(_fileno(file))) failed = 1;
    if (fclose(file)) failed = 1;
    if (!failed && !MoveFileExW(temporary, wide, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) failed = 1;
    if (failed) {
        DeleteFileW(temporary);
        error_set(error, capacity, "Cannot replace launcher settings; the previous file was preserved.");
    }
    return !failed;
}

int mm3_launcher_config_startup(MM3LauncherConfig *config, char *error, size_t capacity)
{
    static const char *const extra[] = {"RECOMP_USB_PADS", "MM3_FOV_DEGREES", "RECOMP_NET_PORT", "RECOMP_NET_PEERS",
                                       "RECOMP_KEYMAP", "MM3_DRIVE_KEYMAP", "RECOMP_PADMAP"};
    int loaded;
    mm3_launcher_config_defaults(config);
    loaded = mm3_launcher_config_load(config, error, capacity);
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i) {
        const char *key = mm3_launcher_options[i].key, *value = getenv(key);
        if (value && !mm3_launcher_config_assign(config, key, value))
            error_set(error, capacity, "Invalid explicit launcher environment setting ignored.");
    }
    for (int i = 0; i < (int)(sizeof extra / sizeof extra[0]); ++i) {
        const char *value = getenv(extra[i]);
        if (value && !mm3_launcher_config_assign(config, extra[i], value))
            error_set(error, capacity, "Invalid explicit launcher environment setting ignored.");
    }
    /* Preserve scripts written before developer tools had a master switch.
     * A saved child flag alone does not override an explicitly disabled master. */
    if (!getenv("MM3_MOD_DEV_TOOLS")) {
        static const char *const legacy[] = {"MM3_DEV_HUD", "MM3_DEBUG_MENU", "MM3_EXTENDED_CAMERAS", "MM3_E3_PRESET"};
        for (int i = 0; i < (int)(sizeof legacy / sizeof legacy[0]); ++i) {
            const char *value = getenv(legacy[i]);
            if (value && !strcmp(value, "1")) config->option[MM3_LAUNCH_DEV_TOOLS] = 1;
        }
    }
    return loaded;
}

int mm3_launcher_config_apply(const MM3LauncherConfig *config, char *error, size_t capacity)
{
    char keys[1024], drive[512], pad[512], number[32];
    error_set(error, capacity, "");
    if (!mm3_launcher_config_valid(config)) {
        error_set(error, capacity, "Cannot apply invalid launcher settings."); return 0;
    }
#define SET(key, value) do { if (_putenv_s((key), (value))) { \
    error_set(error, capacity, "Cannot apply launcher environment settings."); return 0; } } while (0)
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i) {
        const char *value = config->option[i] ? "1" : "0";
        if (i == MM3_LAUNCH_NETWORK) value = config->option[i] ? "nat" : "0";
        SET(mm3_launcher_options[i].key, value);
    }
    /* OHCI automatically exposes at least this many root-hub ports. Keep an
     * explicitly supplied RECOMP_USB_NDP (and the default two-port hub). */
    snprintf(number, sizeof number, "%d", config->controller_count); SET("RECOMP_USB_PADS", number);
    snprintf(number, sizeof number, "%d", config->fov_degrees); SET("MM3_FOV_DEGREES", number);
    snprintf(number, sizeof number, "%d", config->network_port); SET("RECOMP_NET_PORT", number);
    SET("RECOMP_NET_PEERS", config->peers);
    format_map(keys, sizeof keys, config->keys, MM3_LAUNCHER_KEYS, mm3_launcher_key_names, 0);
    format_map(drive, sizeof drive, config->drive_keys, MM3_LAUNCHER_DRIVE_KEYS, mm3_launcher_drive_names, 0);
    format_map(pad, sizeof pad, config->pad, MM3_LAUNCHER_PAD_INPUTS, mm3_launcher_pad_names, 1);
    SET("RECOMP_KEYMAP", keys); SET("MM3_DRIVE_KEYMAP", drive); SET("RECOMP_PADMAP", pad);
    if (!config->option[MM3_LAUNCH_IMPROVED]) {
        for (size_t i = 0; i < sizeof original_look / sizeof original_look[0]; ++i)
            SET(mm3_launcher_options[original_look[i].option].key, original_look[i].value);
        SET("MM3_GLOW_LIFT", "0");         /* lamp glows at their retail positions */
        SET("MM3_NIGHT_GLASS", "0");       /* night glass keeps the daylight screen copy */
    }
    if (!config->option[MM3_LAUNCH_GAMEPLAY])
        for (size_t i = 0; i < sizeof xbox_gameplay / sizeof xbox_gameplay[0]; ++i)
            SET(mm3_launcher_options[xbox_gameplay[i].option].key, xbox_gameplay[i].value);
    if (!config->option[MM3_LAUNCH_WIDESCREEN]) SET("RECOMP_ASPECT", "4:3");
    if (!config->option[MM3_LAUNCH_DISTANCE] || !config->option[MM3_LAUNCH_IMPROVED]) {
        SET("RECOMP_DRAW_DISTANCE", "1"); SET("RECOMP_VIEW_DISTANCE", "1");
    }
#undef SET
    return 1;
}
