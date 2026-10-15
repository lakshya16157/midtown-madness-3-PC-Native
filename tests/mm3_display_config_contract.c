#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "mm3_display_config.h"

static const char *s_environment[] = {
    "RECOMP_WINDOW_SIZE", "RECOMP_FULLSCREEN", "RECOMP_FPS_LIMIT", "RECOMP_VSYNC",
    "MM3_SHOW_FPS", "MM3_MINIMAP_IMPROVED", "RECOMP_RENDER_SCALE", "RECOMP_ASPECT", "RECOMP_MSAA",
    "RECOMP_ANISO", "RECOMP_DRAW_DISTANCE", "MM3_PERFORMANCE_PROFILE"
};

static void environment(const char *key, const char *value)
{
    assert(_putenv_s(key, value ? value : "") == 0);
}

static void clear_environment(void)
{
    size_t i;
    for (i = 0; i < sizeof s_environment / sizeof s_environment[0]; ++i)
        environment(s_environment[i], NULL);
}

static void equal_settings(const MM3DisplaySettings *a, const MM3DisplaySettings *b)
{
    assert(a->window_width == b->window_width);
    assert(a->window_height == b->window_height);
    assert(a->fullscreen == b->fullscreen);
    assert(a->fps_limit == b->fps_limit);
    assert(a->vsync == b->vsync);
    assert(a->show_fps == b->show_fps);
    assert(a->minimap_improved == b->minimap_improved);
    assert(a->render_scale == b->render_scale);
    assert(strcmp(a->aspect, b->aspect) == 0);
    assert(a->msaa == b->msaa);
    assert(a->aniso == b->aniso);
    assert(a->draw_distance == b->draw_distance);
}

static void write_config(const char *contents)
{
    FILE *file = fopen("display.ini", "wb");
    assert(file);
    assert(fputs(contents, file) >= 0);
    assert(fclose(file) == 0);
}

static void no_temporary_files(void)
{
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA("display.ini.*.tmp", &data);
    assert(find == INVALID_HANDLE_VALUE);
    assert(GetLastError() == ERROR_FILE_NOT_FOUND);
}

static void persistence(void)
{
    MM3DisplaySettings expected, loaded, invalid;
    char error[256], stale[128];
    FILE *file;
    HANDLE locked;
    mm3_display_settings_defaults(&expected, "Balanced");
    assert(expected.minimap_improved == 1);
    loaded = expected;
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    assert(!*error);
    equal_settings(&loaded, &expected);
    assert(GetFileAttributesA("display.ini") == INVALID_FILE_ATTRIBUTES);

    expected.window_width = 1366;
    expected.window_height = 768;
    expected.fullscreen = 1;
    expected.fps_limit = 45;
    expected.vsync = 1;
    expected.show_fps = 0;
    expected.minimap_improved = 0;
    expected.render_scale = 1.875f;
    strcpy(expected.aspect, "21:9");
    expected.msaa = 2;
    expected.aniso = 3;
    expected.draw_distance = 2.25f;

    /* A stale temp from a previous interrupted save cannot be overwritten or
     * block saving, even when its PID/sequence match the first attempt. */
    snprintf(stale, sizeof stale, "display.ini.%lu.1.tmp", GetCurrentProcessId());
    file = fopen(stale, "wb");
    assert(file && fputs("unrelated stale data", file) >= 0 && fclose(file) == 0);
    assert(mm3_display_settings_save(&expected, error, sizeof error));
    file = fopen(stale, "rb");
    assert(file && fgetc(file) == 'u' && fclose(file) == 0);
    assert(DeleteFileA(stale));
    no_temporary_files();
    mm3_display_settings_defaults(&loaded, "Quality");
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    equal_settings(&loaded, &expected);

    invalid = expected;
    invalid.fps_limit = 61;
    assert(!mm3_display_settings_save(&invalid, error, sizeof error));
    assert(strstr(error, "fps_limit"));
    invalid = expected;
    invalid.minimap_improved = 2;
    assert(!mm3_display_settings_save(&invalid, error, sizeof error));
    assert(strstr(error, "minimap_improved"));
    invalid = expected;
    invalid.render_scale = NAN;
    assert(!mm3_display_settings_save(&invalid, error, sizeof error));
    invalid = expected;
    memset(invalid.aspect, 'x', sizeof invalid.aspect);
    assert(!mm3_display_settings_save(&invalid, error, sizeof error));
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    equal_settings(&loaded, &expected);
    no_temporary_files();

    /* Sharing restrictions make the atomic replace fail after writing its
     * temp. The old settings remain intact and the failed temp is removed. */
    locked = CreateFileA("display.ini", GENERIC_READ, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(locked != INVALID_HANDLE_VALUE);
    invalid = expected;
    invalid.fps_limit = 30;
    assert(!mm3_display_settings_save(&invalid, error, sizeof error));
    assert(strstr(error, "replace"));
    CloseHandle(locked);
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    equal_settings(&loaded, &expected);
    no_temporary_files();
}

static void malformed_fields(void)
{
    MM3DisplaySettings expected, loaded;
    char error[256];
    FILE *file;
    int i;
    mm3_display_settings_defaults(&expected, "Balanced");
    loaded = expected;
    write_config("# partial config\n[display]\nwindow_width=1366\nwindow_height=768\n"
                 "fullscreen=2\nfps_limit=45 ; accepted comment\nvsync=-1\nshow_fps=0\n"
                 "minimap_improved=2\nrender_scale=nan\naspect=16:0\nmsaa=3\naniso=17\n"
                 "draw_distance=2.25junk\nunknown_key=999\n");
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    expected.window_width = 1366;
    expected.window_height = 768;
    expected.fps_limit = 45;
    expected.show_fps = 0;
    equal_settings(&loaded, &expected);
    write_config("fps_limit=999999999999999999999999999999999\nrender_scale=inf\n"
                 "draw_distance=-inf\nwindow_width=8000\nwindow_height=100\n"
                 "aniso=0\naspect=NaN\n");
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    equal_settings(&loaded, &expected);

    file = fopen("display.ini", "wb");
    assert(file);
    assert(fputs("fps_limit=", file) >= 0);
    for (i = 0; i < 3000; ++i) assert(fputc('3', file) != EOF);
    assert(fputs("\nfps_limit=30\nrender_scale=auto\naspect=32:9\naniso=7", file) >= 0);
    assert(fclose(file) == 0);
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    assert(loaded.fps_limit == 30);
    assert(loaded.render_scale == 0);
    assert(fabs(atof(loaded.aspect) - 32.0 / 9.0) < 1e-7);
    assert(loaded.aniso == 7);
    assert(mm3_display_settings_save(&loaded, error, sizeof error));
    expected = loaded;
    mm3_display_settings_defaults(&loaded, "LowPower");
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    equal_settings(&loaded, &expected);
}

static void precedence(void)
{
    MM3DisplaySettings expected, loaded;
    char error[256];
    clear_environment();
    environment("MM3_PERFORMANCE_PROFILE", "LowPower");
    mm3_display_settings_defaults(&expected, "Balanced");
    expected.fps_limit = 45;
    expected.render_scale = 2.5f;
    assert(mm3_display_settings_save(&expected, error, sizeof error));
    assert(mm3_display_settings_startup(&loaded, error, sizeof error));
    assert(!*error);
    equal_settings(&loaded, &expected);
    assert(strcmp(getenv("RECOMP_FULLSCREEN"), "0") == 0);
    assert(strcmp(getenv("RECOMP_FPS_LIMIT"), "45") == 0);

    clear_environment();
    environment("MM3_PERFORMANCE_PROFILE", "Quality");
    environment("RECOMP_WINDOW_SIZE", "1920x1080");
    environment("RECOMP_FPS_LIMIT", " 30 ");
    environment("RECOMP_RENDER_SCALE", "5.25");
    environment("RECOMP_ASPECT", "1.75");
    environment("RECOMP_ANISO", "3");
    environment("RECOMP_FULLSCREEN", "1");
    environment("MM3_MINIMAP_IMPROVED", "0");
    assert(mm3_display_settings_startup(&loaded, error, sizeof error));
    assert(!*error);
    expected.window_width = 1920;
    expected.window_height = 1080;
    expected.fps_limit = 30;
    expected.render_scale = 5.25f;
    strcpy(expected.aspect, "1.75");
    expected.aniso = 3;
    expected.fullscreen = 1;
    expected.minimap_improved = 0;
    equal_settings(&loaded, &expected);
    assert(strcmp(getenv("RECOMP_FPS_LIMIT"), "30") == 0);
    assert(strcmp(getenv("RECOMP_WINDOW_SIZE"), "1920x1080") == 0);

    clear_environment();
    environment("RECOMP_RENDER_SCALE", "auto");
    environment("RECOMP_ASPECT", "100:1");
    assert(mm3_display_settings_startup(&loaded, error, sizeof error));
    assert(loaded.render_scale == 0);
    assert(strcmp(getenv("RECOMP_RENDER_SCALE"), "auto") == 0);
    assert(strcmp(loaded.aspect, "4") == 0);

    clear_environment();
    environment("RECOMP_ASPECT", "3:4");
    assert(mm3_display_settings_startup(&loaded, error, sizeof error));
    assert(strcmp(loaded.aspect, "0.75") == 0);

    clear_environment();
    environment("RECOMP_ASPECT", "1:100");
    assert(mm3_display_settings_startup(&loaded, error, sizeof error));
    assert(strcmp(loaded.aspect, "0.5") == 0);

    clear_environment();
    environment("RECOMP_WINDOW_SIZE", "1024x768junk");
    environment("RECOMP_FPS_LIMIT", "0");
    environment("RECOMP_RENDER_SCALE", "nan");
    environment("RECOMP_MSAA", "3");
    environment("RECOMP_ASPECT", "16:0");
    environment("RECOMP_FULLSCREEN", "   ");
    environment("MM3_MINIMAP_IMPROVED", "2");
    assert(mm3_display_settings_startup(&loaded, error, sizeof error));
    assert(*error);
    mm3_display_settings_defaults(&expected, "Balanced");
    expected.fps_limit = 45;
    expected.render_scale = 2.5f;
    equal_settings(&loaded, &expected);
    assert(strcmp(getenv("RECOMP_FPS_LIMIT"), "45") == 0);
    assert(strcmp(getenv("RECOMP_ASPECT"), "auto") == 0);
    assert(strcmp(getenv("RECOMP_MSAA"), "4") == 0);
    assert(strcmp(getenv("RECOMP_FULLSCREEN"), "0") == 0);

    environment("RECOMP_FPS_LIMIT", "25");
    assert(mm3_display_settings_apply_environment(&expected, 0) == NULL);
    assert(strcmp(getenv("RECOMP_FPS_LIMIT"), "25") == 0);
    assert(mm3_display_settings_apply_environment(&expected, 1) == NULL);
    assert(strcmp(getenv("RECOMP_FPS_LIMIT"), "45") == 0);
    clear_environment();
}

static void paths(const char *folder)
{
    MM3DisplaySettings expected, loaded;
    char error[256], path[4096], directory[4096], tiny[1];
    environment("MM3_DISPLAY_CONFIG", NULL);
    environment("LOCALAPPDATA", folder);
    snprintf(directory, sizeof directory, "%s\\MM3Recomp", folder);
    assert(GetFileAttributesA(directory) == INVALID_FILE_ATTRIBUTES);
    assert(mm3_display_settings_path(path, sizeof path, error, sizeof error));
    assert(strstr(path, "MM3Recomp\\display.ini"));
    assert(GetFileAttributesA(directory) == INVALID_FILE_ATTRIBUTES);
    mm3_display_settings_defaults(&expected, "Quality");
    assert(mm3_display_settings_save(&expected, error, sizeof error));
    assert(GetFileAttributesA(directory) != INVALID_FILE_ATTRIBUTES);
    mm3_display_settings_defaults(&loaded, "LowPower");
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    equal_settings(&loaded, &expected);
    assert(!mm3_display_settings_path(tiny, sizeof tiny, error, sizeof error));
    assert(!tiny[0] && *error);

    assert(_wputenv_s(L"MM3_DISPLAY_CONFIG", L"display-\u00e9-\u4e2d.ini") == 0);
    assert(mm3_display_settings_save(&expected, error, sizeof error));
    mm3_display_settings_defaults(&loaded, "Balanced");
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    equal_settings(&loaded, &expected);
    assert(DeleteFileW(L"display-\u00e9-\u4e2d.ini"));

    environment("MM3_DISPLAY_CONFIG", "missing-parent/display.ini");
    assert(mm3_display_settings_load(&loaded, error, sizeof error));
    assert(!mm3_display_settings_save(&expected, error, sizeof error));
    assert(*error);
    environment("MM3_DISPLAY_CONFIG", ".");
    assert(!mm3_display_settings_load(&loaded, error, sizeof error));
    assert(*error);
    assert(!mm3_display_settings_startup(&loaded, error, sizeof error));
    mm3_display_settings_defaults(&expected, "Balanced");
    equal_settings(&loaded, &expected);
    assert(*error);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    clear_environment();
    environment("MM3_DISPLAY_CONFIG", "display.ini");
    persistence();
    malformed_fields();
    precedence();
    paths(argv[1]);
    puts("Display configuration contracts passed.");
    return 0;
}
