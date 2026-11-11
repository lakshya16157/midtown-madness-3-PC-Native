/* The in-game Settings window (F10 on Linux, debug_overlay.cpp) without a GPU: its real ImGui code runs
 * with fake backends. The window stays open over many frames, a click on "Freeze opponents" reaches the
 * cheat switch, a drop-down choice reaches the display commit, Escape closes it, and with it closed (and
 * the debug tools off) a frame draws nothing. */
#define XBOXRECOMP_SDL3 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "imgui_internal.h"
#include "../src/mods/debug_overlay.cpp"

/* ---- fakes for what the overlay talks to ---- */
static int g_frozen, g_commits, g_last_fps, g_last_night;
static MM3DisplaySettings g_settings;

extern "C" {
void xbox_FramebufferSetHostOverlay(const XboxHostOverlay *) {}
struct SDL_Window *xbox_FramebufferSdlWindow(void) { return nullptr; }
uint32_t xbox_InputHostPad(uint32_t, uint16_t *, uint8_t *, uint8_t *, int16_t *, int16_t *, int16_t *, int16_t *) { return 1; }
void mm3_teleport_state(Mm3TeleportState *s) { memset(s, 0, sizeof *s); }
int mm3_teleport_queue(float, float, float) { return 1; }
int mm3_dev_tools_request(unsigned) { return 0; }
void mm3_dev_tools_status(char *text, unsigned size) { snprintf(text, size, "off"); }
int mm3_cheat_ai_frozen(void) { return g_frozen; }
void mm3_cheat_set_ai_frozen(int on) { g_frozen = on; }
void mm3_display_settings_get(MM3DisplaySettings *s) { *s = g_settings; }
int mm3_display_settings_commit(const MM3DisplaySettings *s, char *status, size_t capacity)
{
    g_settings = *s; g_commits++; g_last_fps = s->fps_limit; g_last_night = s->night_light;
    snprintf(status, capacity, "Saved. Display changes applied.");
    return 1;
}
const char *mm3_display_setting_fixed(MM3FixedSetting) { return nullptr; }
}
bool ImGui_ImplSDL3_InitForSDLGPU(SDL_Window *) { return true; }
void ImGui_ImplSDL3_Shutdown() {}
void ImGui_ImplSDL3_NewFrame() {}
bool ImGui_ImplSDL3_ProcessEvent(const SDL_Event *) { return false; }
bool ImGui_ImplSDLGPU3_Init(ImGui_ImplSDLGPU3_InitInfo *) { return true; }
void ImGui_ImplSDLGPU3_NewFrame() {}
void Imgui_ImplSDLGPU3_PrepareDrawData(ImDrawData *, SDL_GPUCommandBuffer *) {}
void ImGui_ImplSDLGPU3_RenderDrawData(ImDrawData *, SDL_GPUCommandBuffer *, SDL_GPURenderPass *, SDL_GPUGraphicsPipeline *) {}

static int vertices;
static void frame(float mouse_x = -1, float mouse_y = -1, bool down = false)
{
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 800);
    io.DeltaTime = 1.0f / 60;
    if (mouse_x >= 0) io.AddMousePosEvent(mouse_x, mouse_y);
    io.AddMouseButtonEvent(0, down);
    ImGui::NewFrame();
    if (settings_visible) draw_settings();
    ImGui::Render();
    vertices = ImGui::GetDrawData()->TotalVtxCount;
}

int main()
{
    settings_enabled = true;
    g_settings.fps_limit = 60; g_settings.vsync = 1; g_settings.show_fps = 1;
    g_settings.render_scale = 1.5f; g_settings.night_light = MM3_NIGHT_LIGHT_DEFAULT;
    create_context();
    unsigned char *pixels; int w, h;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::GetIO().Fonts->SetTexID((ImTextureID)1);

    /* Closed: nothing in the window path runs, and a frame has no settings window. */
    frame();
    assert(!settings_visible);

    /* Open: it stays open and draws. */
    settings_visible.exchange(1);
    for (int i = 0; i < 20; ++i) frame();
    assert(settings_visible);
    assert(vertices > 500);

    /* The window opens centred, 560 x 470: its first checkbox, Freeze opponents, is near the top left. */
    ImGuiWindow *win = ImGui::FindWindowByName("Midtown Madness 3 | Settings (F10)");
    assert(win);
    /* Inside the window: padding 18, then the title bar, then the first row. */
    float x = win->Pos.x + 18 + 40, y = win->Pos.y + win->TitleBarHeight + 18 + 8;
    printf("window at %.0f,%.0f size %.0fx%.0f; clicking %.0f,%.0f\n", win->Pos.x, win->Pos.y, win->Size.x, win->Size.y, x, y);
    assert(g_frozen == 0);
    frame(x, y, false); frame(x, y, false); frame(x, y, true); frame(x, y, false);
    assert(g_frozen == 1);
    frame(x, y, true); frame(x, y, false);
    assert(g_frozen == 0);                       /* a second click unticks it */
    assert(settings_visible);

    /* A drop-down choice is committed (Maximum FPS to 120 through the same helper the window uses). */
    {
        MM3DisplaySettings next = g_settings;
        next.fps_limit = 120;
        settings_commit(next);
        assert(g_commits == 1 && g_last_fps == 120 && strstr(settings_status, "Saved"));
    }

    /* Escape closes it. */
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    frame();
    assert(!settings_visible);

    puts("Settings window contract passed.");
    return 0;
}
