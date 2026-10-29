#ifndef MM3_LAUNCHER_CONFIG_H
#define MM3_LAUNCHER_CONFIG_H

#include <stddef.h>

enum { MM3_LAUNCHER_KEYS = 24, MM3_LAUNCHER_DRIVE_KEYS = 8,
       MM3_LAUNCHER_PAD_INPUTS = 16, MM3_LAUNCHER_PAD_SOURCES = 17 };
enum MM3LauncherOption {
    MM3_LAUNCH_KEYBOARD, MM3_LAUNCH_WIDESCREEN, MM3_LAUNCH_DISTANCE,
    MM3_LAUNCH_SKY, MM3_LAUNCH_CAR_LIGHT, MM3_LAUNCH_LAMP_LIGHT,
    MM3_LAUNCH_WORLD_LIGHT, MM3_LAUNCH_FLARE_DEPTH, MM3_LAUNCH_UNLOCK,
    MM3_LAUNCH_DEV_TOOLS, MM3_LAUNCH_DEV_HUD, MM3_LAUNCH_DEBUG_MENU,
    MM3_LAUNCH_CAMERAS, MM3_LAUNCH_E3, MM3_LAUNCH_LOADING, MM3_LAUNCH_NATIVE_FOG,
    MM3_LAUNCH_DSP, MM3_LAUNCH_NETWORK,
    MM3_LAUNCH_NORTH_MARKER, MM3_LAUNCH_RACE_ARROW, MM3_LAUNCH_ROUTE_NAVIGATION,
    MM3_LAUNCH_DRAFTING, MM3_LAUNCH_ROLL_GUARD, MM3_LAUNCH_AI_ALL_CARS,
    /* 0 = the Original look: the Xbox visuals, menus and HUD at the PC's
     * resolution with the graphics and audio upgrades (mm3_launcher_config_apply). */
    MM3_LAUNCH_IMPROVED,
    MM3_LAUNCH_GRAPHICS_MENU, MM3_LAUNCH_CIRCUITS, MM3_LAUNCH_EXTRA_CARS, MM3_LAUNCH_DLC_INFO,
    MM3_LAUNCH_TREES, MM3_LAUNCH_EXHAUST,
    /* 0 = Xbox gameplay: every gameplay mod off, the game's own race arrow
     * and minimap (mm3_launcher_config_apply). */
    MM3_LAUNCH_GAMEPLAY,
    MM3_LAUNCH_QUIT,             /* the main menu's Sign In row becomes Quit */
    MM3_LAUNCH_SAME_CAR,         /* race opponents drive the player's car */
    MM3_LAUNCH_OPTION_COUNT
};

typedef struct MM3LauncherConfig {
    int option[MM3_LAUNCH_OPTION_COUNT];
    int controller_count; /* 1..4 USB controllers exposed to the game. */
    int fov_degrees; /* 0 = retail, otherwise 60..110 horizontal degrees. */
    /* Gameplay mods, percent of the Xbox's (100); Xbox gameplay uses 100. */
    int damage_percent; /* crash damage to every car, 0..400 */
    int time_percent;   /* race and job time limits and time bonuses, 25..400 */
    int network_port;
    char peers[512];
    int keys[MM3_LAUNCHER_KEYS]; /* Win32 VK, zero = unbound. */
    int drive_keys[MM3_LAUNCHER_DRIVE_KEYS];
    int pad[MM3_LAUNCHER_PAD_INPUTS]; /* index into mm3_launcher_pad_sources. */
} MM3LauncherConfig;

typedef struct MM3LauncherOptionInfo {
    const char *key;
    const char *label;
    int default_value;
} MM3LauncherOptionInfo;

extern const MM3LauncherOptionInfo mm3_launcher_options[MM3_LAUNCH_OPTION_COUNT];
extern const char *const mm3_launcher_key_names[MM3_LAUNCHER_KEYS];
extern const char *const mm3_launcher_drive_names[MM3_LAUNCHER_DRIVE_KEYS];
extern const char *const mm3_launcher_pad_names[MM3_LAUNCHER_PAD_INPUTS];
extern const char *const mm3_launcher_pad_sources[MM3_LAUNCHER_PAD_SOURCES];

void mm3_launcher_config_defaults(MM3LauncherConfig *config);
int mm3_launcher_config_valid(const MM3LauncherConfig *config);
/* Defaults < saved valid fields < valid explicitly supplied environment. */
int mm3_launcher_config_startup(MM3LauncherConfig *config, char *error, size_t capacity);
int mm3_launcher_config_path(char *path, size_t capacity, char *error, size_t error_capacity);
int mm3_launcher_config_load(MM3LauncherConfig *config, char *error, size_t capacity);
int mm3_launcher_config_save(const MM3LauncherConfig *config, char *error, size_t capacity);
/* Set one complete field. Invalid values leave the snapshot unchanged. */
int mm3_launcher_config_assign(MM3LauncherConfig *config, const char *key, const char *value);
int mm3_launcher_config_apply(const MM3LauncherConfig *config, char *error, size_t capacity);
/* The switch an option depends on: MM3_LAUNCH_IMPROVED for the visual and
 * audio mods (the Original look sets them as on the Xbox), MM3_LAUNCH_GAMEPLAY
 * for the gameplay mods (Xbox gameplay sets them as on the Xbox), or -1 for
 * an option that is always the player's choice. */
int mm3_launcher_option_master(int option);

#endif
