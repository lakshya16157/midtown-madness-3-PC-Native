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
        "RECOMP_KEYMAP", "MM3_DRIVE_KEYMAP", "RECOMP_PADMAP", "RECOMP_ASPECT", "RECOMP_DRAW_DISTANCE", "RECOMP_VIEW_DISTANCE",
        "MM3_GLOW_LIFT", "MM3_NIGHT_GLASS", "MM3_PAINT_SOFTNESS", "MM3_DAMAGE_PERCENT", "MM3_TIME_PERCENT"};
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
    assert(config.damage_percent == 100 && config.time_percent == 100);
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
    /* Gameplay mods' damage and time limits, in percent of the Xbox's. */
    assert(!mm3_launcher_config_assign(&config, "MM3_DAMAGE_PERCENT", "-1"));
    assert(!mm3_launcher_config_assign(&config, "MM3_DAMAGE_PERCENT", "401"));
    assert(!mm3_launcher_config_assign(&config, "MM3_TIME_PERCENT", "0"));
    assert(!mm3_launcher_config_assign(&config, "MM3_TIME_PERCENT", "1.5"));
    assert(mm3_launcher_config_assign(&config, "MM3_DAMAGE_PERCENT", "0"));
    assert(mm3_launcher_config_assign(&config, "MM3_TIME_PERCENT", "150"));
    assert(config.damage_percent == 0 && config.time_percent == 150);
    before = config;
    config.time_percent = 24; assert(!mm3_launcher_config_valid(&config)); config = before;
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
    /* Improved look and Modded gameplay: every choice as ticked. */
    assert(loaded.option[MM3_LAUNCH_IMPROVED] && !loaded.option[MM3_LAUNCH_GAMEPLAY]);
    loaded.option[MM3_LAUNCH_GAMEPLAY] = 1;
    loaded.option[MM3_LAUNCH_DISTANCE] = 1; loaded.option[MM3_LAUNCH_TREES] = 1;
    loaded.option[MM3_LAUNCH_SKY] = 1; loaded.option[MM3_LAUNCH_UNLOCK] = 1;
    loaded.option[MM3_LAUNCH_RACE_ARROW] = 0; loaded.option[MM3_LAUNCH_NORTH_MARKER] = 0;
    assert(mm3_launcher_config_apply(&loaded, error, sizeof error));
    assert(!strcmp(getenv("MM3_FALLING_TREES"), "1") && !getenv("MM3_GLOW_LIFT") && !getenv("MM3_NIGHT_GLASS") &&
           !getenv("MM3_PAINT_SOFTNESS"));
    assert(!strcmp(getenv("MM3_GAMEPLAY_MODS"), "1") && !strcmp(getenv("MM3_HUD_RACE_ARROW"), "0"));
    assert(!strcmp(getenv("MM3_DAMAGE_PERCENT"), "0") && !strcmp(getenv("MM3_TIME_PERCENT"), "150"));
    /* The Original look: the Xbox visuals, menus and HUD markers; the
     * graphics/audio upgrades and the gameplay as chosen. */
    loaded.option[MM3_LAUNCH_IMPROVED] = 0;
    assert(mm3_launcher_config_apply(&loaded, error, sizeof error));
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        if (mm3_launcher_option_master(i) == MM3_LAUNCH_IMPROVED)
            assert(!strcmp(getenv(mm3_launcher_options[i].key), i == MM3_LAUNCH_NORTH_MARKER ? "1" : "0"));
    assert(!strcmp(getenv("MM3_IMPROVED"), "0") && !strcmp(getenv("MM3_GLOW_LIFT"), "0"));
    assert(!strcmp(getenv("MM3_NIGHT_GLASS"), "0") && !strcmp(getenv("MM3_PAINT_SOFTNESS"), "0"));
    assert(!strcmp(getenv("MM3_MOD_SKY"), "1") && !strcmp(getenv("MM3_UNLOCK_ALL_CARS"), "1"));
    assert(!strcmp(getenv("MM3_FALLING_TREES"), "1") && !strcmp(getenv("MM3_HUD_RACE_ARROW"), "0"));
    assert(!strcmp(getenv("RECOMP_DRAW_DISTANCE"), "1") && !strcmp(getenv("RECOMP_VIEW_DISTANCE"), "1"));
    /* Xbox gameplay: every gameplay mod off and the game's own race arrow;
     * the look as chosen. */
    loaded.option[MM3_LAUNCH_IMPROVED] = 1; loaded.option[MM3_LAUNCH_GAMEPLAY] = 0;
    assert(mm3_launcher_config_apply(&loaded, error, sizeof error));
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        if (mm3_launcher_option_master(i) == MM3_LAUNCH_GAMEPLAY)
            assert(!strcmp(getenv(mm3_launcher_options[i].key), i == MM3_LAUNCH_RACE_ARROW ? "1" : "0"));
    assert(!strcmp(getenv("MM3_GAMEPLAY_MODS"), "0") && !strcmp(getenv("MM3_IMPROVED"), "1"));
    assert(!strcmp(getenv("MM3_DAMAGE_PERCENT"), "100") && !strcmp(getenv("MM3_TIME_PERCENT"), "100"));
    assert(loaded.damage_percent == 0 && loaded.time_percent == 150);     /* kept for Modded gameplay */
    assert(!strcmp(getenv("MM3_MOD_DRAW_DISTANCE"), "1") && !strcmp(getenv("MM3_HUD_NORTH_MARKER"), "0"));
    /* Which switch each option belongs to. */
    static const int always[] = {MM3_LAUNCH_KEYBOARD, MM3_LAUNCH_WIDESCREEN, MM3_LAUNCH_SKY, MM3_LAUNCH_NATIVE_FOG,
        MM3_LAUNCH_DSP, MM3_LAUNCH_NETWORK, MM3_LAUNCH_IMPROVED, MM3_LAUNCH_GAMEPLAY};
    static const int look[] = {MM3_LAUNCH_DISTANCE, MM3_LAUNCH_CAR_LIGHT, MM3_LAUNCH_LAMP_LIGHT, MM3_LAUNCH_WORLD_LIGHT,
        MM3_LAUNCH_FLARE_DEPTH, MM3_LAUNCH_LOADING, MM3_LAUNCH_NORTH_MARKER, MM3_LAUNCH_GRAPHICS_MENU,
        MM3_LAUNCH_DLC_INFO, MM3_LAUNCH_EXHAUST, MM3_LAUNCH_QUIT};
    int counted = 0;
    for (int i = 0; i < (int)(sizeof always / sizeof always[0]); ++i, ++counted)
        assert(mm3_launcher_option_master(always[i]) == -1);
    for (int i = 0; i < (int)(sizeof look / sizeof look[0]); ++i, ++counted)
        assert(mm3_launcher_option_master(look[i]) == MM3_LAUNCH_IMPROVED);
    for (int i = 0; i < MM3_LAUNCH_OPTION_COUNT; ++i)
        counted += mm3_launcher_option_master(i) == MM3_LAUNCH_GAMEPLAY;
    assert(counted == MM3_LAUNCH_OPTION_COUNT);   /* everything else is a gameplay mod */
    assert(mm3_launcher_option_master(MM3_LAUNCH_TREES) == MM3_LAUNCH_GAMEPLAY &&
           mm3_launcher_option_master(MM3_LAUNCH_UNLOCK) == MM3_LAUNCH_GAMEPLAY &&
           mm3_launcher_option_master(MM3_LAUNCH_ROUTE_NAVIGATION) == MM3_LAUNCH_GAMEPLAY);
    file = fopen(path, "wb"); assert(file);
    fputs("[launcher]\nUNKNOWN=1\nMM3_FOV_DEGREES=200\nRECOMP_USB_PADS=0\nRECOMP_KEYBOARD=0\nRECOMP_KEYMAP=A:45,B:zz\n", file);
    for (int i = 0; i < 3000; ++i) fputc('x', file);
    fputs("\nMM3_MOD_SKY=0\n", file); fclose(file);
    mm3_launcher_config_defaults(&loaded);
    assert(mm3_launcher_config_load(&loaded, error, sizeof error));
    assert(loaded.fov_degrees == 0 && !loaded.option[MM3_LAUNCH_KEYBOARD]);
    assert(loaded.keys[0] == 'Z' && !loaded.option[MM3_LAUNCH_SKY]);
    assert(loaded.controller_count == 1); /* Old or invalid files retain the default. */
    /* Driving assists start off, as on the Xbox. A file saved before settings
     * revision 2 had them on by default: they go off once; later choices stay. */
    mm3_launcher_config_defaults(&loaded);
    assert(!loaded.option[MM3_LAUNCH_DRAFTING] && !loaded.option[MM3_LAUNCH_ROLL_GUARD]);
    assert(!loaded.option[MM3_LAUNCH_GAMEPLAY] && loaded.option[MM3_LAUNCH_IMPROVED]);   /* Xbox gameplay */
    file = fopen(path, "wb"); assert(file);
    fputs("[launcher]\nMM3_DRAFTING=1\nMM3_ROLL_GUARD=1\nMM3_MOD_SKY=0\n", file); fclose(file);
    assert(mm3_launcher_config_load(&loaded, error, sizeof error) && !*error);
    assert(!loaded.option[MM3_LAUNCH_DRAFTING] && !loaded.option[MM3_LAUNCH_ROLL_GUARD]);
    assert(!loaded.option[MM3_LAUNCH_SKY]);
    loaded.option[MM3_LAUNCH_DRAFTING] = 1;
    assert(mm3_launcher_config_save(&loaded, error, sizeof error));
    mm3_launcher_config_defaults(&config);
    assert(mm3_launcher_config_load(&config, error, sizeof error) && !*error);
    assert(config.option[MM3_LAUNCH_DRAFTING] && !config.option[MM3_LAUNCH_ROLL_GUARD]);
    file = fopen(path, "rb"); assert(file);
    size = fread(saved, 1, sizeof saved - 1, file); saved[size] = 0; fclose(file);
    assert(strstr(saved, "\nMM3_SETTINGS_REVISION=2\n"));
    assert(!_putenv_s("MM3_ROLL_GUARD", "1"));   /* Explicit environment still wins. */
    assert(mm3_launcher_config_startup(&config, error, sizeof error));
    assert(config.option[MM3_LAUNCH_ROLL_GUARD]);
    clear_environment(); DeleteFileA(path);
    puts("PASS launcher config: controller count/defaults, strict maps, atomic roundtrip, invalid fields, environment precedence, unchanged USB hub override, legacy developer scripts, the Original look, Xbox gameplay and the one-time driving-assist reset");
    return 0;
}
