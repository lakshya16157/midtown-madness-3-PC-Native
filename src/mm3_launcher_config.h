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
    MM3_LAUNCH_OPTION_COUNT
};

typedef struct MM3LauncherConfig {
    int option[MM3_LAUNCH_OPTION_COUNT];
    int controller_count; /* 1..4 USB controllers exposed to the game. */
    int fov_degrees; /* 0 = retail, otherwise 60..110 horizontal degrees. */
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

#endif
