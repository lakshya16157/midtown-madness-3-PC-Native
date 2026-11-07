/* The MM3 runtime patch adds these renderer entry points to the Direct3D 11 backend
 * (tools/xboxrecomp/src/video/nv2a_d3d11.c) only. The SDL3 GPU (Vulkan) backend does not
 * have them yet, so with that renderer they are accepted and do nothing: the game runs
 * with the shared mm3-recomp look (no MM3 sky-fog ramp/tree fog). Render scale, display aspect, present
 * options, HUD hide, local lighting, the sky blend, night glass and paint softness
 * are implemented, and exhaust smoke in src/mods/world_tris_sdlgpu.c in nv2a_sdlgpu.c.
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

/* Render-target read-back fence (D3D11 only): never waits, reports off. */
int nv2a_d3d_readback_idle_wait(void) { return 0; }
int nv2a_d3d_set_readback_fence(int enabled) { return enabled == 0 || enabled == 1; }
int nv2a_d3d_get_readback_fence(void) { return 0; }
int nv2a_d3d_set_readback_poll_us(int us) { return us >= 0 && us <= 1000; }
int nv2a_d3d_get_readback_poll_us(void) { return 0; }

/* Settings: accepted with the D3D11 range checks, not applied yet. */
void nv2a_d3d_prepare_distance_fog(int enabled) { (void)enabled; }
