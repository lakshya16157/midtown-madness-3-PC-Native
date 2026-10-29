/* Native-menu integration without a renderer: exercise the real bridge with
 * guest allocator/constructor/namespace fixtures and saved-settings spies. */
#include <assert.h>
#include <stdio.h>
#include "../src/mm3_graphics_menu.c"

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi;
RECOMP_TLS uint32_t g_ebp, g_seh_ebp, g_fs_base;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top, g_fp_cmp, g_df;
RECOMP_TLS uint16_t g_fp_cc, g_fp_control_word;
RECOMP_TLS RecompXmm g_xmm0, g_xmm1, g_xmm2, g_xmm3;
RECOMP_TLS RecompXmm g_xmm4, g_xmm5, g_xmm6, g_xmm7;

static unsigned char memory[0x100000];
static const uint32_t test_stack = 0xF0000, test_root = 0x30000;
static const uint32_t test_key = 0x20000, test_wsdata = 0x28000;
static uint32_t bindings[GRAPHICS_ACTION_COUNT];
static uint32_t next_binding = 0x40000;
static unsigned allocated, constructed, registered, loaded, original_execute, original_getter;
static unsigned commits, fail_commit;
static MM3DisplaySettings saved_settings;
static GraphicsGuestState expected_loader_state;

void mm3_display_settings_get(MM3DisplaySettings *settings) { *settings = saved_settings; }
int mm3_display_settings_commit(const MM3DisplaySettings *settings, char *message, size_t capacity)
{
    ++commits;
    if (fail_commit) { snprintf(message, capacity, "Fixture save failure"); return 0; }
    saved_settings = *settings;
    snprintf(message, capacity, "Saved. Display changes applied.");
    return 1;
}
/* What the launcher fixes for the run (mm3_display_settings.c), as fixtures. */
static int fixed_minimap, fixed_distance;
const char *mm3_display_setting_fixed(MM3FixedSetting setting)
{
    if (setting == MM3_FIXED_MINIMAP && fixed_minimap) return "Fixture: Xbox gameplay keeps the stock minimap.";
    if (setting == MM3_FIXED_DISTANCE && fixed_distance) return "Fixture: the distances are fixed.";
    return NULL;
}
void mm3_display_settings_keep_fixed(MM3DisplaySettings *to, const MM3DisplaySettings *from)
{
    if (fixed_minimap) to->minimap_improved = from->minimap_improved;
    if (fixed_distance) { to->draw_distance = from->draw_distance; to->view_distance = from->view_distance; }
}
static int circuit_mode = -1;
static uint32_t circuit_city_screen, circuit_setup_screen;
static unsigned circuit_name_blocks;
void mm3_circuit_set_mode(int on) { circuit_mode = on; }
static unsigned quits;
void mm3_quit_game(void) { ++quits; }
void mm3_circuit_set_screens(uint32_t city_select, uint32_t race_setup)
{
    circuit_city_screen = city_select;
    circuit_setup_screen = race_setup;
}
void mm3_display_settings_defaults(MM3DisplaySettings *settings, const char *profile)
{
    (void)profile;
    memset(settings, 0, sizeof *settings);
    settings->window_width = 800; settings->window_height = 600;
    settings->fps_limit = 60; settings->render_scale = 1;
    settings->minimap_improved = 1;
    strcpy(settings->aspect, "auto");
    settings->msaa = settings->aniso = 1; settings->draw_distance = 1;
    settings->view_distance = 1;
}

void sub_0016FF04(void)
{
    uint32_t size = MEM32(g_esp + 4u);
    /* Bindings are 16 bytes; the circuit screen names one 96-byte block. */
    assert(size == 16u || (size == 96u && !circuit_name_blocks));
    assert(MEM32(g_esp) == 0x001268C8u);
    g_eax = next_binding;
    next_binding += size;
    if (size == 16u) ++allocated;
    else ++circuit_name_blocks;
    g_esp += 8u; /* allocator RET 4 consumes the size argument */
}

void sub_0006B3B1(void)
{
    char name[64], expected_name[64];
    unsigned action = MEM32(g_esp + 4u) - GRAPHICS_OWNER;
    uint32_t binding = g_ecx;
    assert(action < GRAPHICS_ACTION_COUNT);
    assert(guest_string(g_eax, name, sizeof name));
    action_name(action, expected_name, sizeof expected_name);
    assert(!strcmp(name, expected_name));
    assert(MEM32(g_esp + 8u) == GRAPHICS_CALLBACK);
    assert(MEM32(g_esp) == 0x001268AFu);
    MEM32(binding) = 0x37D278u;
    MEM32(binding + 8u) = GRAPHICS_OWNER + action;
    MEM32(binding + 12u) = GRAPHICS_CALLBACK;
    bindings[action] = binding;
    ++constructed;
    g_eax = binding;
    g_ecx = 0xAA; g_edx = 0xBB;
    g_esp += 12u;
}

void sub_001C5875(void)
{
    char path[20];
    assert(g_edi == test_root);
    assert(guest_string(MEM32(g_esp + 4u), path, sizeof path));
    assert(!strcmp(path, "functions"));
    assert(MEM32(g_esp) == 0x001268C2u);
    assert(MEM32(g_esi + 8u) >= GRAPHICS_OWNER);
    assert(MEM32(g_esi + 8u) < GRAPHICS_OWNER + GRAPHICS_ACTION_COUNT);
    ++registered;
    g_esp += 8u; g_eax = 1;
    /* The registration seam restores every register, including incidental
     * x87/XMM state, before forwarding to the real loader. */
    g_ebx = g_esi = g_edi = g_ebp = g_seh_ebp = g_fs_base = 0xBAD;
    g_fp_top = g_fp_cmp = g_df = 7;
    g_fp_cc = g_fp_control_word = 0xBAD;
    memset(g_fp_stack, 0xBB, sizeof g_fp_stack);
    memset(&g_xmm0, 0xBB, sizeof g_xmm0);
}

void sub_001C5E11_original(void)
{
    GraphicsGuestState actual;
    memset(&actual, 0, sizeof actual);
    save_guest(&actual);
    assert(!memcmp(&actual, &expected_loader_state, sizeof actual));
    ++loaded;
    g_eax = 0x1111; g_esp += 8u;
}
void sub_0006B3EF_original(void) { ++original_execute; g_eax = 0x2222; g_esp += 8u; }
void sub_0011C650_original(void) { ++original_getter; g_eax = 0x3333; g_esp += 12u; }

static void prepare_state(void)
{
    unsigned i;
    g_esp = test_stack; g_eax = 0x101; g_ecx = test_root; g_edx = 0x103;
    g_ebx = 0x104; g_esi = 0x105; g_edi = 0x106;
    g_ebp = 0x107; g_seh_ebp = 0x108; g_fs_base = 0x109;
    g_fp_top = 2; g_fp_cmp = 3; g_df = 0;
    g_fp_control_word = 0x37F; g_fp_cc = 0x4000;
    for (i = 0; i < 8; ++i) g_fp_stack[i] = 1.5 + i;
    memset(&g_xmm0, 0x11, sizeof g_xmm0); memset(&g_xmm1, 0x12, sizeof g_xmm1);
    memset(&g_xmm2, 0x13, sizeof g_xmm2); memset(&g_xmm3, 0x14, sizeof g_xmm3);
    memset(&g_xmm4, 0x15, sizeof g_xmm4); memset(&g_xmm5, 0x16, sizeof g_xmm5);
    memset(&g_xmm6, 0x17, sizeof g_xmm6); memset(&g_xmm7, 0x18, sizeof g_xmm7);
    MEM32(test_stack) = 0x001193A2u;
}

static void execute(unsigned action)
{
    prepare_state();
    g_ecx = bindings[action];
    MEM32(test_stack + 4u) = 0;
    sub_0006B3EF();
    assert(g_esp == test_stack + 8u && g_eax == 1);
    assert(g_ecx == bindings[action] && g_edx == 0x103 && g_ebx == 0x104);
    assert(g_ebp == 0x107 && g_seh_ebp == 0x108 && g_fs_base == 0x109);
    assert(g_fp_stack[0] == 1.5 && g_xmm0.u[0] == 0x11111111);
}

static void get_native_label(const char *key, char *output, size_t capacity)
{
    size_t i;
    memcpy((char *)XBOX_PTR(test_key), key, strlen(key) + 1u);
    MEM32(test_wsdata + 0xCu) = test_key;
    for (i = 0; i < capacity; ++i) {
        prepare_state(); g_ecx = test_wsdata;
        MEM32(test_stack + 4u) = test_root;
        MEM32(test_stack + 8u) = (uint32_t)i;
        sub_0011C650();
        assert(g_esp == test_stack + 12u && g_ecx == test_wsdata);
        assert(g_edx == 0x103 && g_fp_stack[0] == 1.5);
        assert(g_eax < 128u);
        output[i] = (char)g_eax;
        if (!output[i]) return;
    }
    assert(!"Native label did not terminate");
}

int main(void)
{
    char label[256];
    g_xbox_mem_offset = (ptrdiff_t)memory;
    mm3_display_settings_defaults(&saved_settings, NULL);
    assert(choice_action(SETTING_DISTANCE, 0) == 54u);
    assert(choice_action(SETTING_MINIMAP, 0) == 59u);
    assert(choice_action(SETTING_VIEW_DISTANCE, 0) == 61u);
    saved_settings.window_width = 1920; saved_settings.window_height = 1080;
    saved_settings.render_scale = 0;

    prepare_state();
    memcpy((char *)XBOX_PTR(test_key), "Frontend", sizeof "Frontend");
    MEM32(test_stack + 4u) = test_key;
    memset(&expected_loader_state, 0, sizeof expected_loader_state);
    save_guest(&expected_loader_state);
    sub_001C5E11();
    assert(allocated == GRAPHICS_ACTION_COUNT && constructed == allocated && registered == allocated);
    assert(loaded == 1 && g_eax == 0x1111 && g_esp == test_stack + 8u);
    assert(circuit_name_blocks == 1 && circuit_setup_screen == circuit_city_screen + 48u);
    assert(guest_string(circuit_city_screen, label, sizeof label) &&
           !strcmp(label, "singlePlayer/screenCircuitCitySelect"));
    assert(guest_string(circuit_setup_screen, label, sizeof label) &&
           !strcmp(label, "singlePlayer/screenCircuitRaceSetup"));

    execute(GRAPHICS_BEGIN);
    get_native_label("SID_MM3_GRAPHICS_FPS", label, sizeof label);
    assert(!strcmp(label, "Maximum FPS: 60"));
    execute(choice_action(SETTING_FPS, 2)); /* Directly select 30, not the next value. */
    get_native_label("SID_MM3_GRAPHICS_FPS", label, sizeof label);
    assert(!strcmp(label, "Maximum FPS: 30"));
    execute(choice_action(SETTING_FPS, 2)); /* Choosing the same value is idempotent. */
    assert(draft.fps_limit == 30 && commits == 0 && saved_settings.fps_limit == 60);
    get_native_label("SID_MM3_GRAPHICS_CHOICE_FPS_2", label, sizeof label);
    assert(!strcmp(label, "30 FPS  (Selected)"));
    get_native_label("SID_MM3_GRAPHICS_CHOICE_FPS_6", label, sizeof label);
    assert(!strcmp(label, "60 FPS"));
    execute(GRAPHICS_DISCARD);
    assert(draft.fps_limit == 60);

    /* Exercise every real registered choice binding, including both resolution
     * pages and independent boolean values. Exactly one choice is marked, and
     * the original persisted snapshot stays intact until Apply. */
    for (unsigned setting = 0; setting < GRAPHICS_SETTING_COUNT; ++setting) {
        for (unsigned choice = 0; choice < choice_counts[setting]; ++choice) {
            unsigned selected = 0;
            execute(choice_action(setting, choice));
            assert(choice_selected(setting, choice));
            for (unsigned candidate = 0; candidate < choice_counts[setting]; ++candidate) {
                char key[96];
                snprintf(key, sizeof key, "SID_MM3_GRAPHICS_CHOICE_%s_%u", setting_keys[setting], candidate);
                get_native_label(key, label, sizeof label);
                if (strstr(label, "(Selected)")) {
                    ++selected;
                    assert(candidate == choice);
                }
            }
            assert(selected == 1 && commits == 0 && saved_settings.fps_limit == 60);
        }
    }
    assert(draft.window_width == 3840 && draft.window_height == 2160);
    assert(draft.fullscreen == 1 && draft.fps_limit == 60 && draft.vsync == 1 && draft.show_fps == 1);
    assert(draft.render_scale == 4 && !strcmp(draft.aspect, "21:9"));
    assert(draft.msaa == 8 && draft.aniso == 16 && draft.draw_distance == 4 && draft.view_distance == 4);
    assert(draft.minimap_improved == 1);
    execute(choice_action(SETTING_RESOLUTION, 9));
    execute(choice_action(SETTING_FPS, 1));
    execute(choice_action(SETTING_MODE, 0));
    execute(choice_action(SETTING_SCALE, 2));
    execute(choice_action(SETTING_VSYNC, 0));
    execute(choice_action(SETTING_SHOW_FPS, 0));
    execute(choice_action(SETTING_ASPECT, 3));
    execute(choice_action(SETTING_MSAA, 2));
    execute(choice_action(SETTING_ANISO, 3));
    execute(choice_action(SETTING_DISTANCE, 2));
    execute(choice_action(SETTING_VIEW_DISTANCE, 3));
    execute(choice_action(SETTING_MINIMAP, 0));
    assert(draft.fps_limit == 24 && draft.fullscreen == 0 && draft.render_scale == .75f);
    assert(draft.vsync == 0 && draft.show_fps == 0 && !strcmp(draft.aspect, "16:9"));
    assert(draft.msaa == 4 && draft.aniso == 8 && draft.draw_distance == 2 && draft.view_distance == 3);
    get_native_label("SID_MM3_GRAPHICS_DISTANCE", label, sizeof label);
    assert(!strcmp(label, "Object Detail Distance: 2x"));
    get_native_label("SID_MM3_GRAPHICS_VIEW_DISTANCE", label, sizeof label);
    assert(!strcmp(label, "Building View Distance: 3x"));
    assert(draft.minimap_improved == 0 && saved_settings.minimap_improved == 1);
    execute(GRAPHICS_APPLY);
    assert(commits == 1 && saved_settings.fps_limit == 24 && saved_settings.window_width == 2560 && saved_settings.window_height == 1440);
    assert(saved_settings.minimap_improved == 0);
    assert(saved_settings.draw_distance == 2.f && saved_settings.view_distance == 3.f);
    get_native_label("SID_MM3_GRAPHICS_HELP_APPLY", label, sizeof label);
    assert(!strcmp(label, "Saved. Display changes applied."));

    execute(choice_action(SETTING_FPS, 5)); fail_commit = 1; execute(GRAPHICS_APPLY);
    assert(commits == 2 && saved_settings.fps_limit == 24);
    get_native_label("SID_MM3_GRAPHICS_FPS", label, sizeof label);
    assert(!strcmp(label, "Maximum FPS: 50"));
    get_native_label("SID_MM3_GRAPHICS_STATUS", label, sizeof label);
    assert(!strcmp(label, "Could not save settings. Check the game log."));
    execute(GRAPHICS_DISCARD);
    assert(draft.fps_limit == 24);
    execute(GRAPHICS_DEFAULTS);
    assert(saved_settings.fps_limit == 24 && draft.fps_limit == 60);
    execute(GRAPHICS_DISCARD);
    /* Custom values from the host panel stay untouched on entering any picker.
     * Its presets show no misleading selected marker until a choice is made. */
    draft.render_scale = 1.25f;
    for (unsigned choice = 0; choice < choice_counts[SETTING_SCALE]; ++choice) {
        char key[96];
        snprintf(key, sizeof key, "SID_MM3_GRAPHICS_CHOICE_SCALE_%u", choice);
        get_native_label(key, label, sizeof label);
        assert(!strstr(label, "(Selected)"));
    }
    assert(draft.render_scale == 1.25f);
    get_native_label("SID_MM3_GRAPHICS_PICKER_RESOLUTION", label, sizeof label);
    assert(!strcmp(label, "Resolution"));
    get_native_label("SID_MM3_GRAPHICS_MORE_RESOLUTIONS", label, sizeof label);
    assert(!strcmp(label, "Other Resolutions..."));
    assert(!mm3_graphics_menu_label("SID_MM3_GRAPHICS_CHOICE_FPS_7"));
    assert(!mm3_graphics_menu_label("SID_MM3_GRAPHICS_CHOICE_FPS_-1"));
    assert(!mm3_graphics_menu_label("SID_MM3_GRAPHICS_CHOICE_FPS_1extra"));
    assert(!mm3_graphics_menu_label("SID_MM3_GRAPHICS_PICKER_UNKNOWN"));
    assert(!mm3_graphics_menu_label("SID_MM3_GRAPHICS_HELP_UNKNOWN"));

    prepare_state(); g_ecx = test_wsdata;
    MEM32(test_wsdata + 8u) = 0; MEM32(test_wsdata + 12u) = GRAPHICS_CALLBACK;
    sub_0006B3EF();
    assert(original_execute == 1 && g_eax == 0x2222 && g_esp == test_stack + 8u);
    prepare_state(); g_ecx = test_wsdata;
    memcpy((char *)XBOX_PTR(test_key), "SID_OPTIONS", sizeof "SID_OPTIONS");
    MEM32(test_wsdata + 12u) = test_key;
    sub_0011C650();
    assert(original_getter == 1 && g_eax == 0x3333 && g_esp == test_stack + 12u);
    assert(!mm3_graphics_menu_label("SID_OPTIONS"));
    assert(!mm3_graphics_menu_label("SID_MM3_GRAPHICS_UNKNOWN"));

    prepare_state();
    memcpy((char *)XBOX_PTR(test_key), "+Menu/PostRace/", sizeof "+Menu/PostRace/");
    MEM32(test_stack + 4u) = test_key;
    memset(&expected_loader_state, 0, sizeof expected_loader_state);
    save_guest(&expected_loader_state);
    sub_001C5E11();
    assert(registered == GRAPHICS_ACTION_COUNT && loaded == 2);
    prepare_state();
    memcpy((char *)XBOX_PTR(test_key), "PauseMenu", sizeof "PauseMenu");
    MEM32(test_stack + 4u) = test_key;
    memset(&expected_loader_state, 0, sizeof expected_loader_state);
    save_guest(&expected_loader_state);
    sub_001C5E11();
    assert(registered == GRAPHICS_ACTION_COUNT * 2 && loaded == 3 && circuit_name_blocks == 1);
    get_native_label("SID_MM3_GRAPHICS_HUD_MINIMAP_STOCK", label, sizeof label);
    assert(!strcmp(label, "Minimap: Stock  (Selected)"));
    fail_commit = 0;
    execute(GRAPHICS_MINIMAP_IMPROVED);
    assert(commits == 3 && saved_settings.minimap_improved == 1);
    assert(saved_settings.fps_limit == 24 && saved_settings.window_width == 2560);
    get_native_label("SID_MM3_GRAPHICS_HUD_MINIMAP_IMPROVED", label, sizeof label);
    assert(!strcmp(label, "Minimap: Improved  (Selected)"));
    fail_commit = 1;
    execute(GRAPHICS_MINIMAP_STOCK);
    assert(commits == 4 && saved_settings.minimap_improved == 1);
    /* The single player mode rows switch the circuit race list, nothing else. */
    execute(CIRCUIT_BEGIN);
    assert(circuit_mode == 1 && commits == 4);
    execute(CIRCUIT_END);
    assert(circuit_mode == 0 && commits == 4);
    /* The main menu's Quit row closes the game and touches no setting. */
    assert(quits == 0);
    execute(GAME_QUIT);
    assert(quits == 1 && circuit_mode == 0 && commits == 4);
    /* Settings the launcher fixes for the run (Xbox gameplay's minimap, the
     * distances with their mod off) are marked, say why, and keep their value
     * through their choices, Restore Defaults and the pause menu's minimap. */
    fail_commit = 0;
    fixed_minimap = fixed_distance = 1;
    execute(GRAPHICS_DISCARD);
    {
        float distance = draft.draw_distance;
        execute(choice_action(SETTING_DISTANCE, 4));
        assert(draft.draw_distance == distance);
        execute(choice_action(SETTING_MINIMAP, 1));
        assert(draft.minimap_improved == saved_settings.minimap_improved);
        get_native_label("SID_MM3_GRAPHICS_STATUS", label, sizeof label);
        assert(!strcmp(label, "Fixture: Xbox gameplay keeps the stock minimap."));
        get_native_label("SID_MM3_GRAPHICS_MINIMAP", label, sizeof label);
        assert(!strcmp(label, "Minimap Style: Stock (Fixed)"));
        get_native_label("SID_MM3_GRAPHICS_HELP_DISTANCE", label, sizeof label);
        assert(!strcmp(label, "Fixture: the distances are fixed."));
        get_native_label("SID_MM3_GRAPHICS_CHOICE_DISTANCE_4", label, sizeof label);
        assert(!strcmp(label, "4x  (Fixed)"));
        execute(GRAPHICS_DEFAULTS);
        assert(draft.draw_distance == distance && draft.fps_limit == 60);
    }
    get_native_label("SID_MM3_GRAPHICS_HUD_MINIMAP_STOCK", label, sizeof label);
    assert(!strcmp(label, "Minimap: Stock  (Selected)"));
    get_native_label("SID_MM3_GRAPHICS_HUD_MINIMAP_IMPROVED", label, sizeof label);
    assert(!strcmp(label, "Minimap: Improved  (Fixed)"));
    execute(GRAPHICS_MINIMAP_STOCK);
    assert(commits == 4);
    puts("Native graphics menu contract passed.");
    return 0;
}
