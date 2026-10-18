#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm3_launcher_config.h"

static void clear_environment(void)
{
    static const char *const extra[] = {"RECOMP_USB_PADS", "RECOMP_USB_NDP", "MM3_FOV_DEGREES", "RECOMP_NET_PORT", "RECOMP_NET_PEERS",
        "RECOMP_KEYMAP", "MM3_DRIVE_KEYMAP", "RECOMP_PADMAP", "RECOMP_ASPECT", "RECOMP_DRAW_DISTANCE", "RECOMP_VIEW_DISTANCE"};
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i) assert(!_putenv_s(mm3_launcher_options[i].key, ""));
    for (int i = 0; i < (int)(sizeof extra / sizeof extra[0]); ++i) assert(!_putenv_s(extra[i], ""));
}
int main(int argc, char **argv)
{
    MM3LauncherConfig config, loaded, before;
    char path[32768], error[512], saved[32768];
    FILE *file;
    assert(argc == 2);
    assert(!_putenv_s("MM3_LAUNCHER_CONFIG", argv[1]));
    clear_environment();
    mm3_launcher_config_defaults(&config);
    assert(mm3_launcher_config_valid(&config));
    assert(config.controller_count == 1);
    assert(config.pad[4] == 5 && config.pad[5] == 4);
    assert(config.keys[0] == 'Z' && config.drive_keys[0] == VK_UP);
    assert(mm3_launcher_config_path(path, sizeof path, error, sizeof error));
    assert(!strcmp(path, argv[1]));
    assert(mm3_launcher_config_load(&config, error, sizeof error)); /* Missing is harmless. */
    before = config;
    static const char *const invalid_counts[] = {"", "0", "5", "-1", "two", "2x", "99999999999999999999"};
    for (unsigned i = 0; i < sizeof invalid_counts / sizeof invalid_counts[0]; ++i) {
        assert(!mm3_launcher_config_assign(&config, "RECOMP_USB_PADS", invalid_counts[i]));
        assert(!memcmp(&before, &config, sizeof config));
    }
    assert(mm3_launcher_config_assign(&config, "RECOMP_USB_PADS", "4"));
    assert(config.controller_count == 4);
    before = config;
    config.controller_count = 0; assert(!mm3_launcher_config_valid(&config));
    config.controller_count = 5; assert(!mm3_launcher_config_valid(&config)); config = before;
    assert(mm3_launcher_config_assign(&config, "RECOMP_KEYMAP", "A:20,START:0D,LX-:00"));
    assert(config.keys[0] == VK_SPACE && config.keys[16] == 0);
    assert(mm3_launcher_config_assign(&config, "MM3_DRIVE_KEYMAP", "THROTTLE:57,HANDBRAKE:NONE"));
    assert(config.drive_keys[0] == 'W' && config.drive_keys[4] == 0);
    assert(mm3_launcher_config_assign(&config, "RECOMP_PADMAP", "A:RT,BLACK:LB,LT:NONE"));
    assert(config.pad[0] == 15 && config.pad[4] == 4 && config.pad[14] == 16);
    before = config;
    assert(!mm3_launcher_config_assign(&config, "RECOMP_KEYMAP", "A:41,B:100"));
    assert(!memcmp(&before, &config, sizeof config));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_KEYMAP", "TYPO:41"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_PADMAP", "A:UNKNOWN"));
    assert(mm3_launcher_config_assign(&config, "MM3_DRIVE_KEYMAP", " throttle : 0x41 "));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_KEYMAP", "A:41,A:42"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_KEYMAP", "A:79"));
    assert(!mm3_launcher_config_assign(&config, "MM3_DRIVE_KEYMAP", "THROTTLE:7A"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_KEYBOARD", "2"));
    assert(!mm3_launcher_config_assign(&config, "MM3_FOV_DEGREES", "59"));
    assert(!mm3_launcher_config_assign(&config, "MM3_FOV_DEGREES", "111"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_NET_PORT", "65536"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_NET_PEERS", "host:0"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_NET_PEERS", "host:+3075"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_NET_PEERS", "host: 3075"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_NET_PEERS", "host:12,"));
    assert(!mm3_launcher_config_assign(&config, "RECOMP_NET_PEERS", "host:12\nA=1"));
    assert(mm3_launcher_config_assign(&config, "RECOMP_NET_PEERS", "127.0.0.1:3076,example.local:3077"));
    assert(mm3_launcher_config_assign(&config, "RECOMP_NET", "nat"));
    assert(mm3_launcher_config_assign(&config, "MM3_FOV_DEGREES", "90"));
    assert(mm3_launcher_config_save(&config, error, sizeof error));
    mm3_launcher_config_defaults(&loaded);
    assert(mm3_launcher_config_load(&loaded, error, sizeof error));
    assert(!memcmp(&loaded, &config, sizeof config));
    file = fopen(path, "rb"); assert(file);
    size_t size = fread(saved, 1, sizeof saved - 1, file); saved[size] = 0; fclose(file);
    before = config; config.pad[0] = 100;
    assert(!mm3_launcher_config_save(&config, error, sizeof error));
    file = fopen(path, "rb"); assert(file);
    char after[32768]; size_t after_size = fread(after, 1, sizeof after - 1, file); after[after_size] = 0; fclose(file);
    assert(size == after_size && !strcmp(saved, after)); config = before;
    assert(!_putenv_s("RECOMP_KEYMAP", "A:42"));
    assert(!_putenv_s("MM3_FOV_DEGREES", "100"));
    assert(!_putenv_s("RECOMP_USB_PADS", "2"));
    assert(!_putenv_s("MM3_DEV_HUD", "1"));
    assert(mm3_launcher_config_startup(&loaded, error, sizeof error));
    assert(loaded.keys[0] == 'B' && loaded.fov_degrees == 100);
    assert(loaded.controller_count == 2); /* Explicit environment wins over saved four. */
    assert(loaded.option[MM3_LAUNCH_DEV_TOOLS] == 1);
    assert(!_putenv_s("MM3_MOD_DEV_TOOLS", "0"));
    assert(mm3_launcher_config_startup(&loaded, error, sizeof error));
    assert(!loaded.option[MM3_LAUNCH_DEV_TOOLS]);
    assert(!_putenv_s("MM3_FOV_DEGREES", "invalid"));
    assert(!_putenv_s("RECOMP_USB_PADS", "5"));
    assert(mm3_launcher_config_startup(&loaded, error, sizeof error));
    assert(loaded.fov_degrees == 90 && *error);
    assert(loaded.controller_count == 4); /* Invalid explicit count preserves saved value. */
    loaded.option[MM3_LAUNCH_WIDESCREEN] = 0; loaded.option[MM3_LAUNCH_DISTANCE] = 0;
    assert(!_putenv_s("RECOMP_USB_NDP", "1"));
    assert(mm3_launcher_config_apply(&loaded, error, sizeof error));
    assert(!strcmp(getenv("RECOMP_ASPECT"), "4:3"));
    assert(!strcmp(getenv("RECOMP_DRAW_DISTANCE"), "1"));
    assert(!strcmp(getenv("RECOMP_VIEW_DISTANCE"), "1"));
    assert(!strcmp(getenv("RECOMP_NET"), "nat"));
    assert(strstr(getenv("RECOMP_KEYMAP"), "A:42"));
    for (int count = 1; count <= 4; ++count) {
        loaded.controller_count = count;
        assert(mm3_launcher_config_apply(&loaded, error, sizeof error));
        assert(getenv("RECOMP_USB_PADS")[0] == '0' + count && !getenv("RECOMP_USB_PADS")[1]);
        assert(!strcmp(getenv("RECOMP_USB_NDP"), "1")); /* OHCI raises ports as needed. */
    }
    file = fopen(path, "wb"); assert(file);
    fputs("[launcher]\nUNKNOWN=1\nMM3_FOV_DEGREES=200\nRECOMP_USB_PADS=0\nRECOMP_KEYBOARD=0\nRECOMP_KEYMAP=A:45,B:zz\n", file);
    for (int i = 0; i < 3000; ++i) fputc('x', file);
    fputs("\nMM3_MOD_SKY=0\n", file); fclose(file);
    mm3_launcher_config_defaults(&loaded);
    assert(mm3_launcher_config_load(&loaded, error, sizeof error));
    assert(loaded.fov_degrees == 0 && !loaded.option[MM3_LAUNCH_KEYBOARD]);
    assert(loaded.keys[0] == 'Z' && !loaded.option[MM3_LAUNCH_SKY]);
    assert(loaded.controller_count == 1); /* Old or invalid files retain the default. */
    clear_environment(); DeleteFileA(path);
    puts("PASS launcher config: controller count/defaults, strict maps, atomic roundtrip, invalid fields, environment precedence, unchanged USB hub override and legacy developer scripts");
    return 0;
}
