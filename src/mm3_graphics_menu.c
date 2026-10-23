#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "recomp_types.h"
#include "mm3_display_config.h"
#include "mm3_display_settings.h"
#include "mm3_graphics_menu.h"
#include "mm3_circuit_races.h"

extern void sub_001C5E11_original(void);
extern void sub_0006B3EF_original(void);
extern void sub_0011C650_original(void);
extern void sub_0016FF04(void);
extern void sub_0006B3B1(void);
extern void sub_001C5875(void);

/* Functions and WSidData are native menu objects. The existing guest function
 * wrapper reaches the host only for our tagged owner; no host address is ever
 * stored in the guest graph. Its fallback callback is a guest RET leaf. */
#define GRAPHICS_OWNER 0x47465800u
#define GRAPHICS_CALLBACK 0x000120B3u

enum GraphicsAction {
    GRAPHICS_BEGIN,
    GRAPHICS_APPLY,
    GRAPHICS_DEFAULTS,
    GRAPHICS_DISCARD,
    GRAPHICS_MINIMAP_STOCK,
    GRAPHICS_MINIMAP_IMPROVED,
    GRAPHICS_COMMAND_COUNT
};

enum GraphicsSetting {
    SETTING_RESOLUTION,
    SETTING_MODE,
    SETTING_FPS,
    SETTING_VSYNC,
    SETTING_SCALE,
    SETTING_SHOW_FPS,
    SETTING_ASPECT,
    SETTING_MSAA,
    SETTING_ANISO,
    SETTING_DISTANCE,
    SETTING_MINIMAP,
    SETTING_VIEW_DISTANCE, /* Append to keep every existing guest action ID stable. */
    GRAPHICS_SETTING_COUNT
};

static const char *const command_names[GRAPHICS_COMMAND_COUNT] = {
    "graphicsBegin", "graphicsApply", "graphicsDefaults", "graphicsDiscard",
    "hudMinimapStock", "hudMinimapImproved"
};
static const char *const setting_keys[GRAPHICS_SETTING_COUNT] = {
    "RESOLUTION", "MODE", "FPS", "VSYNC", "SCALE", "SHOW_FPS",
    "ASPECT", "MSAA", "ANISO", "DISTANCE", "MINIMAP", "VIEW_DISTANCE"
};
static const char *const setting_titles[GRAPHICS_SETTING_COUNT] = {
    "Resolution", "Display Mode", "Maximum FPS", "VSync", "Render Scale",
    "Show FPS", "Aspect Ratio", "Anti-aliasing", "Texture Filtering", "Object Detail Distance", "Minimap Style",
    "Building View Distance"
};
static const unsigned choice_counts[GRAPHICS_SETTING_COUNT] = {12, 2, 7, 2, 9, 2, 5, 4, 5, 5, 2, 5};
#define GRAPHICS_CHOICE_COUNT 60u
/* The single player Circuit row and the other mode rows (mm3_circuit_races.c),
 * after the choices so every earlier action ID stays the same. */
enum { CIRCUIT_BEGIN = GRAPHICS_COMMAND_COUNT + GRAPHICS_CHOICE_COUNT, CIRCUIT_END };
#define GRAPHICS_ACTION_COUNT (CIRCUIT_END + 1u)
static const int resolutions[][2] = {
    {800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1366, 768},
    {1536, 960}, {1600, 900}, {1920, 1080}, {1920, 1200},
    {2560, 1440}, {2560, 1600}, {3840, 2160}
};
static const int rates[] = {15, 24, 30, 40, 45, 50, 60};
static const int msaa[] = {1, 2, 4, 8};
static const int aniso[] = {1, 2, 4, 8, 16};
static const float scales[] = {0.f, .5f, .75f, 1.f, 1.5f, 2.f, 2.5f, 3.f, 4.f};
static const float distances[] = {1.f, 1.5f, 2.f, 3.f, 4.f};
static const char *const aspects[] = {"auto", "4:3", "16:10", "16:9", "21:9"};

/* Every native choice binds a stable setting and value index. Opening a picker
 * has no side effects; activating a value sets it directly and the native
 * action list returns to its parent. Values never depend on the previous one. */
static unsigned choice_action(unsigned setting, unsigned choice)
{
    unsigned i, action = GRAPHICS_COMMAND_COUNT;
    for (i = 0; i < setting; ++i) action += choice_counts[i];
    return action + choice;
}

static int decode_choice(unsigned action, unsigned *setting, unsigned *choice)
{
    unsigned i;
    if (action < GRAPHICS_COMMAND_COUNT || action >= GRAPHICS_COMMAND_COUNT + GRAPHICS_CHOICE_COUNT) return 0;
    for (i = 0; i < GRAPHICS_SETTING_COUNT; ++i) {
        unsigned first = choice_action(i, 0);
        if (action >= first && action < first + choice_counts[i]) {
            *setting = i; *choice = action - first;
            return 1;
        }
    }
    return 0;
}

static void action_name(unsigned action, char *name, size_t capacity)
{
    unsigned setting, choice;
    if (action < GRAPHICS_COMMAND_COUNT)
        snprintf(name, capacity, "%s", command_names[action]);
    else if (action == CIRCUIT_BEGIN || action == CIRCUIT_END)
        snprintf(name, capacity, "%s", action == CIRCUIT_BEGIN ? "circuitBegin" : "circuitEnd");
    else if (decode_choice(action, &setting, &choice))
        snprintf(name, capacity, "graphicsSelect%s%u", setting_keys[setting], choice);
    else if (capacity) name[0] = 0;
}

/* These drafts are touched only by the title's menu thread. Applying publishes
 * the same complete snapshot as the host settings panel. */
static MM3DisplaySettings draft;
static int draft_ready;
static char status[256];

static int trace_enabled(void)
{
    static RECOMP_TLS int enabled = -1;
    if (enabled < 0) enabled = getenv("MM3_GRAPHICS_MENU_TRACE") != NULL;
    return enabled;
}

static uint32_t guest_field(uint32_t object, uint32_t offset)
{
    return object >= 0x10000u && object < 0x08000000u - offset - 4u ?
        MEM32(object + offset) : 0;
}

typedef struct GraphicsGuestState {
    uint32_t a, c, d, b, sp, bp, si, di, seh, fs;
    int top, cmp, df;
    uint16_t cc, control;
    double fp[8];
    RecompXmm xmm[8];
} GraphicsGuestState;

static void save_guest(GraphicsGuestState *state)
{
    state->a = g_eax; state->c = g_ecx; state->d = g_edx; state->b = g_ebx;
    state->sp = g_esp; state->bp = g_ebp; state->si = g_esi; state->di = g_edi;
    state->seh = g_seh_ebp; state->fs = g_fs_base;
    state->top = g_fp_top; state->cmp = g_fp_cmp; state->df = g_df;
    state->cc = g_fp_cc; state->control = g_fp_control_word;
    memcpy(state->fp, g_fp_stack, sizeof state->fp);
    state->xmm[0] = g_xmm0; state->xmm[1] = g_xmm1;
    state->xmm[2] = g_xmm2; state->xmm[3] = g_xmm3;
    state->xmm[4] = g_xmm4; state->xmm[5] = g_xmm5;
    state->xmm[6] = g_xmm6; state->xmm[7] = g_xmm7;
}

static void restore_guest(const GraphicsGuestState *state)
{
    g_eax = state->a; g_ecx = state->c; g_edx = state->d; g_ebx = state->b;
    g_esp = state->sp; g_ebp = state->bp; g_esi = state->si; g_edi = state->di;
    g_seh_ebp = state->seh; g_fs_base = state->fs;
    g_fp_top = state->top; g_fp_cmp = state->cmp; g_df = state->df;
    g_fp_cc = state->cc; g_fp_control_word = state->control;
    memcpy(g_fp_stack, state->fp, sizeof state->fp);
    g_xmm0 = state->xmm[0]; g_xmm1 = state->xmm[1];
    g_xmm2 = state->xmm[2]; g_xmm3 = state->xmm[3];
    g_xmm4 = state->xmm[4]; g_xmm5 = state->xmm[5];
    g_xmm6 = state->xmm[6]; g_xmm7 = state->xmm[7];
}

static int guest_string(uint32_t address, char *text, size_t capacity)
{
    size_t i;
    if (!address || address < 0x10000u || address >= 0x08000000u - capacity)
        return 0;
    for (i = 0; i < capacity; ++i) {
        text[i] = (char)MEM8(address + (uint32_t)i);
        if (!text[i]) return 1;
    }
    return 0;
}

static void start_draft(void)
{
    mm3_display_settings_get(&draft);
    draft_ready = 1;
    strcpy(status, "Choose a setting, then select an exact value. Apply saves your settings.");
}

static void select_choice(unsigned setting, unsigned choice)
{
    switch (setting) {
    case SETTING_RESOLUTION:
        draft.window_width = resolutions[choice][0];
        draft.window_height = resolutions[choice][1];
        break;
    case SETTING_MODE: draft.fullscreen = (int)choice; break;
    case SETTING_FPS: draft.fps_limit = rates[choice]; break;
    case SETTING_VSYNC: draft.vsync = (int)choice; break;
    case SETTING_SCALE: draft.render_scale = scales[choice]; break;
    case SETTING_SHOW_FPS: draft.show_fps = (int)choice; break;
    case SETTING_ASPECT: strcpy(draft.aspect, aspects[choice]); break;
    case SETTING_MSAA: draft.msaa = msaa[choice]; break;
    case SETTING_ANISO: draft.aniso = aniso[choice]; break;
    case SETTING_DISTANCE: draft.draw_distance = distances[choice]; break;
    case SETTING_VIEW_DISTANCE: draft.view_distance = distances[choice]; break;
    case SETTING_MINIMAP: draft.minimap_improved = (int)choice; break;
    default: return;
    }
    strcpy(status, "Changes pending. Apply saves; Back discards changes.");
}

static int choice_selected(unsigned setting, unsigned choice)
{
    switch (setting) {
    case SETTING_RESOLUTION: return draft.window_width == resolutions[choice][0] && draft.window_height == resolutions[choice][1];
    case SETTING_MODE: return draft.fullscreen == (int)choice;
    case SETTING_FPS: return draft.fps_limit == rates[choice];
    case SETTING_VSYNC: return draft.vsync == (int)choice;
    case SETTING_SCALE: return fabsf(draft.render_scale - scales[choice]) < 0.0001f;
    case SETTING_SHOW_FPS: return draft.show_fps == (int)choice;
    case SETTING_ASPECT: return !strcmp(draft.aspect, aspects[choice]);
    case SETTING_MSAA: return draft.msaa == msaa[choice];
    case SETTING_ANISO: return draft.aniso == aniso[choice];
    case SETTING_DISTANCE: return fabsf(draft.draw_distance - distances[choice]) < 0.0001f;
    case SETTING_VIEW_DISTANCE: return fabsf(draft.view_distance - distances[choice]) < 0.0001f;
    case SETTING_MINIMAP: return draft.minimap_improved == (int)choice;
    default: return 0;
    }
}

static void choice_label(unsigned setting, unsigned choice, char *label, size_t capacity)
{
    char value[64];
    switch (setting) {
    case SETTING_RESOLUTION: snprintf(value, sizeof value, "%d x %d", resolutions[choice][0], resolutions[choice][1]); break;
    case SETTING_MODE: snprintf(value, sizeof value, "%s", choice ? "Borderless Fullscreen" : "Windowed"); break;
    case SETTING_FPS: snprintf(value, sizeof value, "%d FPS", rates[choice]); break;
    case SETTING_VSYNC:
    case SETTING_SHOW_FPS: snprintf(value, sizeof value, "%s", choice ? "On" : "Off"); break;
    case SETTING_SCALE:
        if (!choice) strcpy(value, "Automatic (match window)");
        else snprintf(value, sizeof value, "%.3gx", scales[choice]);
        break;
    case SETTING_ASPECT: snprintf(value, sizeof value, "%s", choice ? aspects[choice] : "Automatic (match window)"); break;
    case SETTING_MSAA:
        if (!choice) strcpy(value, "Off");
        else snprintf(value, sizeof value, "%dx", msaa[choice]);
        break;
    case SETTING_ANISO:
        if (!choice) strcpy(value, "Off");
        else snprintf(value, sizeof value, "%dx", aniso[choice]);
        break;
    case SETTING_DISTANCE:
    case SETTING_VIEW_DISTANCE: snprintf(value, sizeof value, "%.3gx", distances[choice]); break;
    case SETTING_MINIMAP: snprintf(value, sizeof value, "%s", choice ? "Improved" : "Stock"); break;
    default: value[0] = 0; break;
    }
    snprintf(label, capacity, "%s%s", value, choice_selected(setting, choice) ? "  (Selected)" : "");
}

static void run_action(unsigned action)
{
    unsigned setting, choice;
    if (action == CIRCUIT_BEGIN || action == CIRCUIT_END) {
        mm3_circuit_set_mode(action == CIRCUIT_BEGIN);
        return;
    }
    /* Pause HUD choices publish just this preference, using a fresh snapshot
     * so the F10 panel and an old graphics draft cannot overwrite each other. */
    if (action == GRAPHICS_MINIMAP_STOCK || action == GRAPHICS_MINIMAP_IMPROVED) {
        MM3DisplaySettings current;
        mm3_display_settings_get(&current);
        current.minimap_improved = action == GRAPHICS_MINIMAP_IMPROVED;
        if (mm3_display_settings_commit(&current, status, sizeof status)) {
            if (draft_ready) draft.minimap_improved = current.minimap_improved;
        } else {
            fprintf(stderr, "[MM3_HUD_MENU] save failed: %s\n", status);
        }
        return;
    }
    if (!draft_ready || action == GRAPHICS_BEGIN || action == GRAPHICS_DISCARD) {
        start_draft();
        if (action == GRAPHICS_BEGIN || action == GRAPHICS_DISCARD) return;
    }
    if (decode_choice(action, &setting, &choice)) {
        select_choice(setting, choice);
        return;
    }
    switch (action) {
    case GRAPHICS_DEFAULTS:
        mm3_display_settings_defaults(&draft, getenv("MM3_PERFORMANCE_PROFILE"));
        strcpy(status, "Defaults selected. Apply saves your settings.");
        return;
    case GRAPHICS_APPLY:
        if (!mm3_display_settings_commit(&draft, status, sizeof status)) {
            fprintf(stderr, "[MM3_GRAPHICS_MENU] save failed: %s\n", status);
            strcpy(status, "Could not save settings. Check the game log.");
        }
        return;
    default: return;
    }
}

static int choice_key(const char *key, unsigned *setting, unsigned *choice)
{
    unsigned i;
    for (i = 0; i < GRAPHICS_SETTING_COUNT; ++i) {
        size_t length = strlen(setting_keys[i]);
        char *end;
        unsigned long index;
        if (strncmp(key, setting_keys[i], length) || key[length] != '_') continue;
        key += length + 1;
        if (*key < '0' || *key > '9') return 0;
        index = strtoul(key, &end, 10);
        if (*end || index >= choice_counts[i]) return 0;
        *setting = i; *choice = (unsigned)index;
        return 1;
    }
    return 0;
}

const char *mm3_graphics_menu_label(const char *key)
{
    static RECOMP_TLS char label[128];
    const char *field;
    unsigned setting, choice;
    if (!key || strncmp(key, "SID_MM3_GRAPHICS_", 17) != 0) return NULL;
    field = key + 17;
    if (!strcmp(field, "HUD")) return "HUD Settings";
    if (!strcmp(field, "HUD_MINIMAP_STOCK") || !strcmp(field, "HUD_MINIMAP_IMPROVED")) {
        MM3DisplaySettings current;
        int improved = !strcmp(field, "HUD_MINIMAP_IMPROVED");
        mm3_display_settings_get(&current);
        snprintf(label, sizeof label, "Minimap: %s%s", improved ? "Improved" : "Stock",
                 current.minimap_improved == improved ? "  (Selected)" : "");
        return label;
    }
    if (!draft_ready) start_draft();
    if (!strncmp(field, "CHOICE_", 7)) {
        if (!choice_key(field + 7, &setting, &choice)) return NULL;
        choice_label(setting, choice, label, sizeof label);
        return label;
    }
    if (!strncmp(field, "PICKER_", 7)) {
        if (!strcmp(field + 7, "HELP")) return "Choose an exact value. Select confirms; Back keeps your current setting. Apply saves.";
        for (setting = 0; setting < GRAPHICS_SETTING_COUNT; ++setting)
            if (!strcmp(field + 7, setting_keys[setting])) return setting_titles[setting];
        return NULL;
    }
    if (!strncmp(field, "HELP_", 5)) {
        static const char *const help[GRAPHICS_SETTING_COUNT] = {
            "Choose an output resolution. Apply saves your selection.",
            "Choose Windowed or Borderless Fullscreen. Apply saves your selection.",
            "Choose a frame-rate limit. 60 FPS gives the smoothest gameplay.",
            "VSync reduces screen tearing. Choose On or Off, then Apply.",
            "Automatic follows the window. Higher scales sharpen the image but use more GPU power.",
            "Show or hide the frame-rate counter during gameplay.",
            "Automatic matches the window. Gameplay shows more scenery without stretching menus.",
            "Smooth jagged edges. Higher values use more GPU power.",
            "Sharpen distant road and building textures. Higher values use more GPU power.",
            "Keep detailed objects visible farther away. Higher values use more CPU and GPU power. Restart required.",
            "Stock uses the original map. Improved shows transparent gray roads and a blue race route.",
            "Show the city and buildings farther away. Object detail has its own distance. Restart required."
        };
        for (setting = 0; setting < GRAPHICS_SETTING_COUNT; ++setting)
            if (!strcmp(field + 5, setting_keys[setting])) return help[setting];
        if (!strcmp(field + 5, "APPLY")) return status;
        if (!strcmp(field + 5, "DEFAULTS")) return "Restore recommended defaults. Apply Settings saves them.";
        if (!strcmp(field + 5, "ADVANCED")) return "Aspect ratio, anti-aliasing, texture filtering, building view, object detail and minimap style.";
        return NULL;
    }
    if (!strcmp(field, "MORE_RESOLUTIONS")) return "Other Resolutions...";
    if (!strcmp(field, "TITLE")) return "Graphics Settings";
    if (!strcmp(field, "ADVANCED")) return "Advanced Graphics";
    if (!strcmp(field, "HELP")) return "Change display and graphics settings.";
    if (!strcmp(field, "APPLY")) return "Apply Settings";
    if (!strcmp(field, "DEFAULTS")) return "Restore Defaults";
    if (!strcmp(field, "STATUS")) return status;
    if (!strcmp(field, "RESOLUTION")) snprintf(label, sizeof label, "Resolution: %d x %d", draft.window_width, draft.window_height);
    else if (!strcmp(field, "MODE")) snprintf(label, sizeof label, "Display Mode: %s", draft.fullscreen ? "Borderless Fullscreen" : "Windowed");
    else if (!strcmp(field, "FPS")) snprintf(label, sizeof label, "Maximum FPS: %d", draft.fps_limit);
    else if (!strcmp(field, "VSYNC")) snprintf(label, sizeof label, "VSync: %s", draft.vsync ? "On" : "Off");
    else if (!strcmp(field, "SCALE")) {
        if (draft.render_scale == 0.f) strcpy(label, "Render Scale: Automatic");
        else snprintf(label, sizeof label, "Render Scale: %.3gx", draft.render_scale);
    }
    else if (!strcmp(field, "SHOW_FPS")) snprintf(label, sizeof label, "Show FPS: %s", draft.show_fps ? "On" : "Off");
    else if (!strcmp(field, "ASPECT")) snprintf(label, sizeof label, "Aspect Ratio: %s", !strcmp(draft.aspect, "auto") ? "Automatic" : draft.aspect);
    else if (!strcmp(field, "MSAA")) {
        if (draft.msaa == 1) strcpy(label, "Anti-aliasing: Off");
        else snprintf(label, sizeof label, "Anti-aliasing: %dx", draft.msaa);
    }
    else if (!strcmp(field, "ANISO")) {
        if (draft.aniso == 1) strcpy(label, "Texture Filtering: Off");
        else snprintf(label, sizeof label, "Texture Filtering: %dx", draft.aniso);
    }
    else if (!strcmp(field, "DISTANCE")) snprintf(label, sizeof label, "Object Detail Distance: %.3gx", draft.draw_distance);
    else if (!strcmp(field, "MINIMAP")) snprintf(label, sizeof label, "Minimap Style: %s", draft.minimap_improved ? "Improved" : "Stock");
    else if (!strcmp(field, "VIEW_DISTANCE")) snprintf(label, sizeof label, "Building View Distance: %.3gx", draft.view_distance);
    else return NULL;
    return label;
}

/* The native WSidData getter normally refreshes only after a SID or language
 * change. Our values can change while the page is open, so return their current
 * characters directly. Other localization objects retain their normal cache. */
void sub_0011C650(void)
{
    char key[80];
    const char *label;
    uint32_t object = g_ecx;
    if (object >= 0x10000u && object < 0x08000000u - 0x10u &&
        guest_string(MEM32(object + 0xCu), key, sizeof key) &&
        (label = mm3_graphics_menu_label(key)) != NULL) {
        uint32_t index = MEM32(g_esp + 8u);
        g_eax = index < strlen(label) ? (unsigned char)label[index] : 0;
        g_esp += 12u; /* native RET 8 */
        return;
    }
    sub_0011C650_original();
}

void sub_0006B3EF(void)
{
    uint32_t object = g_ecx;
    uint32_t owner = guest_field(object, 8u);
    int custom = guest_field(object, 12u) == GRAPHICS_CALLBACK &&
        owner >= GRAPHICS_OWNER && owner < GRAPHICS_OWNER + GRAPHICS_ACTION_COUNT;
    static RECOMP_TLS unsigned reports, stock_reports;
    if (trace_enabled() && (custom ? reports++ < 64u : stock_reports++ < 8u)) {
        char name[64] = "?";
        guest_string(guest_field(object, 4u), name, sizeof name);
        fprintf(stderr, "[MM3_GRAPHICS_FUNCTION] function=%08X name=%s vt=%08X owner=%08X callback=%08X caller=%08X\n",
            object, name, guest_field(object, 0u), guest_field(object, 8u),
            guest_field(object, 12u), MEM32(g_esp));
    }
    if (object >= 0x10000u && object < 0x08000000u - 16u &&
        MEM32(object + 0xCu) == GRAPHICS_CALLBACK) {
        if (owner >= GRAPHICS_OWNER && owner < GRAPHICS_OWNER + GRAPHICS_ACTION_COUNT) {
            run_action(owner - GRAPHICS_OWNER);
            g_eax = 1;
            g_esp += 8u; /* native Function::Execute RET 4 */
            return;
        }
    }
    sub_0006B3EF_original();
}

/* This is the same construction and root-namespace registration sequence used
 * by movieBonus, resumeRace and the other native menu callbacks. Register before
 * the graph deserializer resolves CallFunctionAction's Function references. */
#define CIRCUIT_SCREEN_NAME_BYTES 48u
#define CIRCUIT_CITY_SCREEN "singlePlayer/screenCircuitCitySelect"
#define CIRCUIT_SETUP_SCREEN "singlePlayer/screenCircuitRaceSetup"
static uint32_t circuit_screens;

static void register_functions(uint32_t root)
{
    GraphicsGuestState saved;
    uint32_t scratch, name, path;
    char binding_name[64];
    unsigned action;
    static RECOMP_TLS unsigned reports;
    save_guest(&saved);
    scratch = saved.sp - 160u;
    name = saved.sp - 128u;
    path = saved.sp - 32u;
    memcpy((char *)XBOX_PTR(path), "functions", sizeof "functions");
    for (action = 0; action < GRAPHICS_ACTION_COUNT; ++action) {
        uint32_t binding;
        action_name(action, binding_name, sizeof binding_name);
        memcpy((char *)XBOX_PTR(name), binding_name, strlen(binding_name) + 1);
        g_esp = scratch;
        PUSH32(g_esp, 16u);
        PUSH32(g_esp, 0x001268C8u);
        sub_0016FF04();
        binding = g_eax;
        if (!binding) {
            fprintf(stderr, "[MM3_GRAPHICS_MENU] cannot allocate %s\n", binding_name);
            continue;
        }
        g_ecx = binding;
        g_eax = name;
        PUSH32(g_esp, GRAPHICS_CALLBACK);
        PUSH32(g_esp, GRAPHICS_OWNER + action);
        PUSH32(g_esp, 0x001268AFu);
        sub_0006B3B1();
        g_edi = root;
        g_esi = binding;
        PUSH32(g_esp, path);
        PUSH32(g_esp, 0x001268C2u);
        sub_001C5875();
        if (trace_enabled() && reports++ < 56u)
            fprintf(stderr, "[MM3_GRAPHICS_REGISTER] root=%08X manager=%08X name=%s binding=%08X vt=%08X owner=%08X callback=%08X result=%08X esp=%08X expected=%08X\n",
                root, guest_field(root, 0x30u), binding_name, binding,
                guest_field(binding, 0u), guest_field(binding, 8u),
                guest_field(binding, 12u), g_eax, g_esp, scratch);
        if (!g_eax) fprintf(stderr, "[MM3_GRAPHICS_MENU] cannot register %s\n", binding_name);
    }
    /* The circuit screens' names, for the menu path rebuilt after a race
     * (mm3_circuit_races.c); allocated once and never freed. */
    if (!circuit_screens) {
        g_esp = scratch;
        PUSH32(g_esp, CIRCUIT_SCREEN_NAME_BYTES * 2u);
        PUSH32(g_esp, 0x001268C8u);
        sub_0016FF04();
        circuit_screens = g_eax;
        if (circuit_screens) {
            snprintf((char *)XBOX_PTR(circuit_screens), CIRCUIT_SCREEN_NAME_BYTES, "%s", CIRCUIT_CITY_SCREEN);
            snprintf((char *)XBOX_PTR(circuit_screens + CIRCUIT_SCREEN_NAME_BYTES), CIRCUIT_SCREEN_NAME_BYTES,
                     "%s", CIRCUIT_SETUP_SCREEN);
            mm3_circuit_set_screens(circuit_screens, circuit_screens + CIRCUIT_SCREEN_NAME_BYTES);
        } else {
            fprintf(stderr, "[MM3_GRAPHICS_MENU] cannot allocate the circuit screen names\n");
        }
    }
    restore_guest(&saved);
}

void sub_001C5E11(void)
{
    char path[80];
    uint32_t root = g_ecx;
    static RECOMP_TLS unsigned reports;
    int have_path = guest_string(MEM32(g_esp + 4u), path, sizeof path);
    int report = trace_enabled() && reports++ < 16u;
    if (report)
        fprintf(stderr, "[MM3_GRAPHICS_LOAD] root=%08X path=%s manager=%08X\n",
            root, have_path ? path : "?", guest_field(root, 0x30u));
    /* The root already owns its +Menu/FrontEnd/ prefix. The loader argument
     * is the FrontEnd resource name, not the path passed to its constructor. */
    if (have_path && (!_stricmp(path, "FrontEnd") || !_stricmp(path, "PauseMenu")))
        register_functions(g_ecx);
    sub_001C5E11_original();
    if (report)
        fprintf(stderr, "[MM3_GRAPHICS_LOADED] root=%08X manager=%08X\n",
            root, guest_field(root, 0x30u));
}
