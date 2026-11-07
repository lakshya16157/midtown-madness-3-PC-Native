/* The MM3 runtime patch adds these renderer entry points to the Direct3D 11 backend
 * (tools/xboxrecomp/src/video/nv2a_d3d11.c) only. The SDL3 GPU (Vulkan) backend does not
 * have them yet, so with that renderer they are accepted and do nothing: the game runs
 * with the shared mm3-recomp look (no MM3 distance fog, local-light shaping, night glass,
 * paint softness, exhaust triangles, HUD hide, frame hold, render scale, display aspect).
 * Each moves into nv2a_sdlgpu.c as it is ported (DESIGN.md, "Renderer parity").
 * Built only with XBOXRECOMP_RENDERER=SDL3_GPU. */
#include <stdint.h>
#include <video/nv2a_d3d11.h>

/* Executor host commands (nv2a_pb_exec.c): ignored. */
void nv2a_d3d_distance_fog(const NvD3DState *st, float depth_to_world, float far_clip,
                           float start, float end, uint32_t argb, uint32_t flags,
                           const float sky_fade[4])
{ (void)st; (void)depth_to_world; (void)far_clip; (void)start; (void)end; (void)argb; (void)flags; (void)sky_fade; }
void nv2a_d3d_sky_fog_ramp(uint32_t addr, uint32_t format, float row, float scale, float knee)
{ (void)addr; (void)format; (void)row; (void)scale; (void)knee; }
void nv2a_d3d_world_tris(const NvD3DState *st, uint32_t seq, float depth_to_world,
                         const float view[12], const float proj[4])
{ (void)st; (void)seq; (void)depth_to_world; (void)view; (void)proj; }
void nv2a_d3d_world_tris_publish(uint32_t seq, const NvWorldVertex *v, uint32_t count)
{ (void)seq; (void)v; (void)count; }
void nv2a_d3d_hold_frame(void) {}

/* Render-target read-back fence (D3D11 only): never waits, reports off. */
int nv2a_d3d_readback_idle_wait(void) { return 0; }
int nv2a_d3d_set_readback_fence(int enabled) { return enabled == 0 || enabled == 1; }
int nv2a_d3d_get_readback_fence(void) { return 0; }
int nv2a_d3d_set_readback_poll_us(int us) { return us >= 0 && us <= 1000; }
int nv2a_d3d_get_readback_poll_us(void) { return 0; }

/* Settings: accepted with the D3D11 range checks, not applied yet. */
void nv2a_d3d_prepare_distance_fog(int enabled) { (void)enabled; }
void nv2a_d3d_set_bspline_blend(uint32_t sfactor, uint32_t dfactor) { (void)sfactor; (void)dfactor; }
void nv2a_d3d_set_display_aspect(float aspect) { (void)aspect; }
void nv2a_d3d_set_hud_hidden(int hidden) { (void)hidden; }
void nv2a_d3d_set_night_glass(int enabled) { (void)enabled; }
void nv2a_d3d_set_paint_softness(float pixels) { (void)pixels; }
int nv2a_d3d_set_present_options(int vsync, int max_fps)
{ return (vsync == 0 || vsync == 1) && max_fps >= 1 && max_fps <= 120; }
static float s_render_scale = 1.0f;
void nv2a_d3d_set_render_scale(float scale) { s_render_scale = scale; }
float nv2a_d3d_get_render_scale(void) { return s_render_scale; }
int nv2a_d3d_set_local_light_exposure(float knee, float ceiling)
{ return ceiling == 0.0f || ((knee >= 0.1f && knee <= 1.0f) && (ceiling >= knee + 0.05f && ceiling <= 1.5f)); }
void nv2a_d3d_set_local_light_falloff(int enabled) { (void)enabled; }
void nv2a_d3d_set_local_light_gain(float gain) { (void)gain; }
void nv2a_d3d_set_local_light_overlap(int enabled) { (void)enabled; }
float nv2a_d3d_set_local_light_shape(float source, float edge, float power, float white)
{ (void)source; (void)edge; (void)power; (void)white; return 0.0f; }
void nv2a_d3d_set_local_light_softness(int enabled) { (void)enabled; }
void nv2a_d3d_set_local_light_sum(int enabled) { (void)enabled; }
