#include "mm3_display_config.h"
#include "mm3_performance_profile.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#define MM3_CONFIG_PATH_CAPACITY 32768

typedef enum SettingType { SETTING_INTEGER, SETTING_FLOAT, SETTING_ASPECT, SETTING_MONITOR } SettingType;
typedef struct SettingField {
    const char *name;
    const char *environment;
    SettingType type;
    size_t offset;
    float minimum;
    float maximum;
    unsigned int allowed;
} SettingField;

#define INT_FIELD(name, env, minimum, maximum, allowed) \
    {#name, env, SETTING_INTEGER, offsetof(MM3DisplaySettings, name), minimum, maximum, allowed}
#define FLOAT_FIELD(name, env, minimum, maximum) \
    {#name, env, SETTING_FLOAT, offsetof(MM3DisplaySettings, name), minimum, maximum, 0}
#define CHOICE(n) (1u << (n))

static const SettingField s_fields[] = {
    INT_FIELD(window_width, NULL, 640, 7680, 0),
    INT_FIELD(window_height, NULL, 480, 4320, 0),
    INT_FIELD(fullscreen, "RECOMP_FULLSCREEN", 0, 1, 0),
    INT_FIELD(fps_limit, "RECOMP_FPS_LIMIT", 1, 60, 0),
    INT_FIELD(vsync, "RECOMP_VSYNC", 0, 1, 0),
    INT_FIELD(show_fps, "MM3_SHOW_FPS", 0, 1, 0),
    INT_FIELD(minimap_improved, "MM3_MINIMAP_IMPROVED", 0, 1, 0),
    FLOAT_FIELD(render_scale, "RECOMP_RENDER_SCALE", 0.5f, 8.0f),
    {"aspect", "RECOMP_ASPECT", SETTING_ASPECT, offsetof(MM3DisplaySettings, aspect), 0, 0, 0},
    INT_FIELD(msaa, "RECOMP_MSAA", 1, 8, CHOICE(1) | CHOICE(2) | CHOICE(4) | CHOICE(8)),
    INT_FIELD(aniso, "RECOMP_ANISO", 1, 16, 0),
    FLOAT_FIELD(draw_distance, "RECOMP_DRAW_DISTANCE", 1.0f, 4.0f),
    FLOAT_FIELD(view_distance, "RECOMP_VIEW_DISTANCE", 1.0f, 4.0f),
    {"monitor", "RECOMP_MONITOR", SETTING_MONITOR, offsetof(MM3DisplaySettings, monitor), 0, 0, 0}
};

static void error_clear(char *error, size_t capacity)
{
    if (error && capacity) error[0] = '\0';
}

static void error_set(char *error, size_t capacity, const char *format, ...)
{
    va_list args;
    if (!error || !capacity) return;
    va_start(args, format);
    vsnprintf(error, capacity, format, args);
    va_end(args);
    error[capacity - 1] = '\0';
}

static int aspect_assign(char *destination, const char *value)
{
    static const char *choices[] = {"auto", "4:3", "16:10", "16:9", "21:9"};
    char *end;
    double width, height, ratio;
    size_t i;
    for (i = 0; i < sizeof choices / sizeof choices[0]; ++i)
        if (strcmp(value, choices[i]) == 0) {
            if (destination) strcpy(destination, value);
            return 1;
        }
    errno = 0;
    width = strtod(value, &end);
    if (end == value || errno || !isfinite(width) || width <= 0) return 0;
    if (*end == ':') {
        value = end + 1;
        height = strtod(value, &end);
        if (end == value || errno || !isfinite(height) || height <= 0) return 0;
        ratio = width / height;
    } else ratio = width;
    while (isspace((unsigned char)*end)) ++end;
    if (*end) return 0;
    if (ratio < 0.5) ratio = 0.5;
    if (ratio > 4.0) ratio = 4.0;
    if (destination) snprintf(destination, sizeof ((MM3DisplaySettings *)0)->aspect, "%.9g", ratio);
    return 1;
}

/* "primary", or a Windows display device name: \\.\ and then letters, digits
 * and underscores (\\.\DISPLAY2), short enough for MONITORINFOEX. */
static int monitor_assign(char *destination, const char *value)
{
    size_t length = strlen(value), i;
    if (strcmp(value, "primary") != 0) {
        if (length < 5 || length >= sizeof ((MM3DisplaySettings *)0)->monitor ||
            strncmp(value, "\\\\.\\", 4) != 0) return 0;
        for (i = 4; i < length; ++i)
            if (!isalnum((unsigned char)value[i]) && value[i] != '_') return 0;
    }
    if (destination) memcpy(destination, value, length + 1);
    return 1;
}

int mm3_display_monitor_valid(const char *monitor)
{
    return monitor && monitor_assign(NULL, monitor);
}

static int field_assign(MM3DisplaySettings *settings, const SettingField *field, const char *value)
{
    char *end;
    char *destination = (char *)settings + field->offset;
    errno = 0;
    if (field->type == SETTING_INTEGER) {
        long parsed = strtol(value, &end, 10);
        if (end == value) return 0;
        while (isspace((unsigned char)*end)) ++end;
        if (errno || *end || parsed < field->minimum || parsed > field->maximum)
            return 0;
        if (field->allowed && !(field->allowed & CHOICE((unsigned int)parsed))) return 0;
        *(int *)destination = (int)parsed;
    } else if (field->type == SETTING_FLOAT) {
        int render_scale = field->offset == offsetof(MM3DisplaySettings, render_scale);
        float parsed = strtof(value, &end);
        if (render_scale && strcmp(value, "auto") == 0) {
            *(float *)destination = 0.0f;
            return 1;
        }
        if (end == value) return 0;
        while (isspace((unsigned char)*end)) ++end;
        if (errno || *end || !isfinite(parsed) ||
            (!(render_scale && parsed == 0.0f) && parsed < field->minimum) ||
            parsed > field->maximum) return 0;
        *(float *)destination = parsed;
    } else if (field->type == SETTING_MONITOR) {
        if (!monitor_assign(destination, value)) return 0;
    } else {
        if (!aspect_assign(destination, value)) return 0;
    }
    return 1;
}

static int settings_valid(const MM3DisplaySettings *settings, const char **invalid)
{
    size_t i;
    for (i = 0; i < sizeof s_fields / sizeof s_fields[0]; ++i) {
        const SettingField *field = &s_fields[i];
        const char *source = (const char *)settings + field->offset;
        int valid;
        if (field->type == SETTING_INTEGER) {
            int value = *(const int *)source;
            valid = value >= field->minimum && value <= field->maximum &&
                (!field->allowed || (field->allowed & CHOICE((unsigned int)value)));
        } else if (field->type == SETTING_FLOAT) {
            float value = *(const float *)source;
            valid = isfinite(value) && (value >= field->minimum ||
                (field->offset == offsetof(MM3DisplaySettings, render_scale) && value == 0.0f)) &&
                value <= field->maximum;
        } else if (field->type == SETTING_MONITOR) {
            valid = memchr(source, '\0', sizeof settings->monitor) != NULL && monitor_assign(NULL, source);
        } else {
            valid = memchr(source, '\0', sizeof settings->aspect) != NULL && aspect_assign(NULL, source);
        }
        if (!valid) {
            if (invalid) *invalid = field->name;
            return 0;
        }
    }
    return 1;
}

void mm3_display_settings_defaults(MM3DisplaySettings *settings, const char *profile_name)
{
    const MM3PerformanceProfile *profile = mm3_performance_profile(profile_name);
    memset(settings, 0, sizeof *settings);
    settings->window_width = 1280;
    settings->window_height = 800;
    settings->fps_limit = atoi(profile->fps_limit);
    settings->show_fps = 1;
    settings->minimap_improved = 1;
    settings->render_scale = (float)atof(profile->render_scale);
    strcpy(settings->aspect, "auto");
    settings->msaa = atoi(profile->msaa);
    settings->aniso = atoi(profile->aniso);
    settings->draw_distance = (float)atof(profile->draw_distance);
    settings->view_distance = settings->draw_distance;
    strcpy(settings->monitor, "primary");
}

#ifdef _WIN32
static int path_to_wide(const char *path, wchar_t *wide)
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                               wide, MM3_CONFIG_PATH_CAPACITY) != 0;
}

static int path_from_wide(const wchar_t *wide, char *path, size_t capacity)
{
    if (capacity > INT_MAX) capacity = INT_MAX;
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                               path, (int)capacity, NULL, NULL) != 0;
}
#endif

int mm3_display_settings_path(char *path, size_t capacity, char *error, size_t error_capacity)
{
    error_clear(error, error_capacity);
    if (!path || !capacity) {
        error_set(error, error_capacity, "Display settings path buffer is empty.");
        return 0;
    }
#ifdef _WIN32
    {
        const wchar_t *override = _wgetenv(L"MM3_DISPLAY_CONFIG");
        const wchar_t *local = _wgetenv(L"LOCALAPPDATA");
        wchar_t wide[MM3_CONFIG_PATH_CAPACITY];
        if (override && *override) {
            if (path_from_wide(override, path, capacity)) return 1;
        } else if (local && *local) {
            if (swprintf(wide, MM3_CONFIG_PATH_CAPACITY, L"%ls\\MM3Recomp\\display.ini", local) >= 0 &&
                path_from_wide(wide, path, capacity)) return 1;
        } else {
            DWORD count = GetModuleFileNameW(NULL, wide, MM3_CONFIG_PATH_CAPACITY);
            wchar_t *separator;
            if (count && count < MM3_CONFIG_PATH_CAPACITY && (separator = wcsrchr(wide, L'\\')) != NULL) {
                *separator = L'\0';
                if (wcslen(wide) + wcslen(L"\\display.ini") < MM3_CONFIG_PATH_CAPACITY) {
                    wcscat(wide, L"\\display.ini");
                    if (path_from_wide(wide, path, capacity)) return 1;
                }
            }
        }
    }
#else
    {
        const char *override = getenv("MM3_DISPLAY_CONFIG");
        const char *local = getenv("LOCALAPPDATA");
        int length;
        if (override && *override) length = snprintf(path, capacity, "%s", override);
        else if (local && *local) length = snprintf(path, capacity, "%s/MM3Recomp/display.ini", local);
        else length = snprintf(path, capacity, "display.ini");
        if (length >= 0 && (size_t)length < capacity) return 1;
    }
#endif
    path[0] = '\0';
    error_set(error, error_capacity, "Display settings path is too long or cannot be resolved.");
    return 0;
}

static char *trim(char *text)
{
    char *end;
    while (isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int settings_load_fields(MM3DisplaySettings *settings, int *view_configured,
                                char *error, size_t error_capacity)
{
    char path[MM3_CONFIG_PATH_CAPACITY];
    char line[1024];
    FILE *file;
    int failed;
    /* -1 means no file was opened, so the public load API retains its input. */
    *view_configured = -1;
    if (!mm3_display_settings_path(path, sizeof path, error, error_capacity)) return 0;
#ifdef _WIN32
    {
        wchar_t wide[MM3_CONFIG_PATH_CAPACITY];
        if (!path_to_wide(path, wide)) {
            error_set(error, error_capacity, "Display settings path is not valid UTF-8.");
            return 0;
        }
        file = _wfopen(wide, L"rb");
    }
#else
    file = fopen(path, "rb");
#endif
    if (!file) {
        if (errno == ENOENT) return 1;
        error_set(error, error_capacity, "Cannot read display settings: %s.", strerror(errno));
        return 0;
    }
    *view_configured = 0;
    while (fgets(line, sizeof line, file)) {
        char *key, *value, *separator, *comment;
        size_t i;
        /* Skip the entire overlong line, including its continuation. */
        if (!strchr(line, '\n') && !feof(file)) {
            int ch;
            do { ch = fgetc(file); } while (ch != '\n' && ch != EOF);
            continue;
        }
        key = trim(line);
        if (!*key || *key == '#' || *key == ';' || *key == '[') continue;
        separator = strchr(key, '=');
        if (!separator) continue;
        *separator = '\0';
        key = trim(key);
        value = trim(separator + 1);
        for (comment = value; *comment; ++comment)
            if ((*comment == '#' || *comment == ';') &&
                (comment == value || isspace((unsigned char)comment[-1]))) {
                *comment = '\0';
                break;
            }
        value = trim(value);
        for (i = 0; i < sizeof s_fields / sizeof s_fields[0]; ++i)
            if (strcmp(key, s_fields[i].name) == 0) {
                if (field_assign(settings, &s_fields[i], value) &&
                    s_fields[i].offset == offsetof(MM3DisplaySettings, view_distance))
                    *view_configured = 1;
                break;
            }
    }
    failed = ferror(file);
    if (fclose(file)) failed = 1;
    if (failed) error_set(error, error_capacity, "Cannot finish reading display settings.");
    return !failed;
}

int mm3_display_settings_load(MM3DisplaySettings *settings, char *error, size_t error_capacity)
{
    int view_configured;
    int loaded = settings_load_fields(settings, &view_configured, error, error_capacity);
    if (view_configured == 0) settings->view_distance = settings->draw_distance;
    return loaded;
}

static int create_config_directory(char *error, size_t error_capacity)
{
#ifdef _WIN32
    const wchar_t *override = _wgetenv(L"MM3_DISPLAY_CONFIG");
    const wchar_t *local = _wgetenv(L"LOCALAPPDATA");
    wchar_t directory[MM3_CONFIG_PATH_CAPACITY];
    DWORD code;
    if ((override && *override) || !local || !*local) return 1;
    if (swprintf(directory, MM3_CONFIG_PATH_CAPACITY, L"%ls\\MM3Recomp", local) < 0) {
        error_set(error, error_capacity, "Display settings directory is too long.");
        return 0;
    }
    if (CreateDirectoryW(directory, NULL)) return 1;
    code = GetLastError();
    if (code == ERROR_ALREADY_EXISTS) return 1;
    error_set(error, error_capacity, "Cannot create display settings directory (Windows error %lu).", code);
#else
    const char *override = getenv("MM3_DISPLAY_CONFIG");
    const char *local = getenv("LOCALAPPDATA");
    char directory[MM3_CONFIG_PATH_CAPACITY];
    if ((override && *override) || !local || !*local) return 1;
    if (snprintf(directory, sizeof directory, "%s/MM3Recomp", local) >= (int)sizeof directory) {
        error_set(error, error_capacity, "Display settings directory is too long.");
        return 0;
    }
    if (!mkdir(directory, 0700) || errno == EEXIST) return 1;
    error_set(error, error_capacity, "Cannot create display settings directory: %s.", strerror(errno));
#endif
    return 0;
}

int mm3_display_settings_save(const MM3DisplaySettings *settings, char *error, size_t error_capacity)
{
    const char *invalid = NULL;
    char path[MM3_CONFIG_PATH_CAPACITY];
    char scale_value[64];
    FILE *file = NULL;
    int failed = 0;
#ifdef _WIN32
    wchar_t wide[MM3_CONFIG_PATH_CAPACITY], temporary[MM3_CONFIG_PATH_CAPACITY];
    static volatile LONG sequence;
    HANDLE handle = INVALID_HANDLE_VALUE;
    int descriptor, attempt;
#else
    char temporary[MM3_CONFIG_PATH_CAPACITY];
    int descriptor;
#endif
    error_clear(error, error_capacity);
    if (!settings_valid(settings, &invalid)) {
        error_set(error, error_capacity, "Invalid display setting: %s.", invalid);
        return 0;
    }
    if (!mm3_display_settings_path(path, sizeof path, error, error_capacity) ||
        !create_config_directory(error, error_capacity)) return 0;
#ifdef _WIN32
    if (!path_to_wide(path, wide)) {
        error_set(error, error_capacity, "Display settings path is not valid UTF-8.");
        return 0;
    }
    for (attempt = 0; attempt < 16; ++attempt) {
        if (swprintf(temporary, MM3_CONFIG_PATH_CAPACITY, L"%ls.%lu.%lu.tmp", wide,
                     GetCurrentProcessId(), (unsigned long)InterlockedIncrement(&sequence)) < 0) break;
        handle = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) break;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        error_set(error, error_capacity, "Cannot create temporary display settings (Windows error %lu).",
                  GetLastError());
        return 0;
    }
    descriptor = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
    if (descriptor >= 0) file = _fdopen(descriptor, "wb");
    if (!file) {
        if (descriptor >= 0) _close(descriptor);
        else CloseHandle(handle);
        DeleteFileW(temporary);
        error_set(error, error_capacity, "Cannot open temporary display settings stream.");
        return 0;
    }
#else
    if (snprintf(temporary, sizeof temporary, "%s.XXXXXX", path) >= (int)sizeof temporary) {
        error_set(error, error_capacity, "Display settings path is too long.");
        return 0;
    }
    descriptor = mkstemp(temporary);
    if (descriptor >= 0) file = fdopen(descriptor, "wb");
    if (!file) {
        if (descriptor >= 0) { close(descriptor); unlink(temporary); }
        error_set(error, error_capacity, "Cannot create temporary display settings: %s.", strerror(errno));
        return 0;
    }
#endif
    if (settings->render_scale == 0.0f) strcpy(scale_value, "auto");
    else snprintf(scale_value, sizeof scale_value, "%.9g", (double)settings->render_scale);
    if (fprintf(file,
                "# MM3 display settings. Changes made in the game are saved here.\n"
                "[display]\nwindow_width=%d\nwindow_height=%d\nfullscreen=%d\n"
                "fps_limit=%d\nvsync=%d\nshow_fps=%d\nminimap_improved=%d\nrender_scale=%s\n"
                "aspect=%s\nmsaa=%d\naniso=%d\ndraw_distance=%.9g\nview_distance=%.9g\n"
                "monitor=%s\n",
                settings->window_width, settings->window_height, settings->fullscreen,
                settings->fps_limit, settings->vsync, settings->show_fps, settings->minimap_improved,
                scale_value, settings->aspect, settings->msaa,
                settings->aniso, (double)settings->draw_distance,
                (double)settings->view_distance, settings->monitor) < 0) failed = 1;
    if (fflush(file)) failed = 1;
#ifdef _WIN32
    if (_commit(_fileno(file))) failed = 1;
#else
    if (fsync(fileno(file))) failed = 1;
#endif
    if (fclose(file)) failed = 1;
#ifdef _WIN32
    if (!failed && !MoveFileExW(temporary, wide, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error_set(error, error_capacity, "Cannot replace display settings (Windows error %lu).", GetLastError());
        failed = 1;
    }
    if (failed) DeleteFileW(temporary);
#else
    if (!failed && rename(temporary, path)) {
        error_set(error, error_capacity, "Cannot replace display settings: %s.", strerror(errno));
        failed = 1;
    }
    if (failed) unlink(temporary);
#endif
    if (failed && error && error_capacity && !*error)
        error_set(error, error_capacity, "Cannot finish writing display settings.");
    return !failed;
}

static int environment_set(const char *key, const char *value, int overwrite)
{
    const char *current = getenv(key);
    if (!overwrite && current && *current) return 0;
#ifdef _WIN32
    return _putenv_s(key, value);
#else
    return setenv(key, value, 1);
#endif
}

const char *mm3_display_settings_apply_environment(const MM3DisplaySettings *settings, int overwrite)
{
    char value[64];
    size_t i;
    const char *invalid;
    if (!settings_valid(settings, &invalid)) return invalid;
    snprintf(value, sizeof value, "%dx%d", settings->window_width, settings->window_height);
    if (environment_set("RECOMP_WINDOW_SIZE", value, overwrite)) return "RECOMP_WINDOW_SIZE";
    for (i = 0; i < sizeof s_fields / sizeof s_fields[0]; ++i) {
        const SettingField *field = &s_fields[i];
        const char *source = (const char *)settings + field->offset;
        if (!field->environment) continue;
        if (field->type == SETTING_INTEGER) snprintf(value, sizeof value, "%d", *(const int *)source);
        else if (field->type == SETTING_FLOAT) {
            if (field->offset == offsetof(MM3DisplaySettings, render_scale) && *(const float *)source == 0.0f)
                strcpy(value, "auto");
            else snprintf(value, sizeof value, "%.9g", (double)*(const float *)source);
        }
        else snprintf(value, sizeof value, "%s", source);
        if (environment_set(field->environment, value, overwrite)) return field->environment;
    }
    return NULL;
}

static int window_size_assign(MM3DisplaySettings *settings, const char *value)
{
    char *end;
    long width, height;
    errno = 0;
    width = strtol(value, &end, 10);
    if (end == value || errno || *end != 'x' || width < 640 || width > 7680) return 0;
    value = end + 1;
    height = strtol(value, &end, 10);
    if (end == value) return 0;
    while (isspace((unsigned char)*end)) ++end;
    if (errno || *end || height < 480 || height > 4320) return 0;
    settings->window_width = (int)width;
    settings->window_height = (int)height;
    return 1;
}

int mm3_display_settings_startup(MM3DisplaySettings *settings, char *error, size_t error_capacity)
{
    char load_error[256];
    const char *value, *failed;
    size_t i;
    int loaded, view_configured;
    mm3_display_settings_defaults(settings, getenv("MM3_PERFORMANCE_PROFILE"));
    loaded = settings_load_fields(settings, &view_configured, load_error, sizeof load_error);
    error_clear(error, error_capacity);
    if (!loaded) error_set(error, error_capacity, "%s", load_error);
    value = getenv("RECOMP_WINDOW_SIZE");
    if (value && *value && !window_size_assign(settings, value) && loaded)
        error_set(error, error_capacity, "Ignoring invalid RECOMP_WINDOW_SIZE; using saved/default resolution.");
    for (i = 0; i < sizeof s_fields / sizeof s_fields[0]; ++i) {
        if (!s_fields[i].environment) continue;
        value = getenv(s_fields[i].environment);
        if (value && *value) {
            int valid = field_assign(settings, &s_fields[i], value);
            if (valid && s_fields[i].offset == offsetof(MM3DisplaySettings, view_distance))
                view_configured = 1;
            if (!valid && loaded && (!error || !error_capacity || !*error))
                error_set(error, error_capacity, "Ignoring invalid %s; using its saved/default setting.",
                          s_fields[i].environment);
        }
    }
    /* Resolve legacy inheritance after explicit DRAW overrides, and before
     * publishing either environment key. A saved VIEW remains independent. */
    if (view_configured <= 0) settings->view_distance = settings->draw_distance;
    failed = mm3_display_settings_apply_environment(settings, 1);
    if (failed) {
        error_set(error, error_capacity, "Cannot initialize display environment setting %s.", failed);
        return 0;
    }
    return loaded;
}
