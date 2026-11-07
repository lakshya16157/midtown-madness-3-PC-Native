/* MM3 exhaust smoke for the SDL3 GPU renderer: nv2a_d3d_world_tris_publish and
 * nv2a_d3d_world_tris, which the MM3 runtime patch implements in the D3D11 backend
 * only (tools/xboxrecomp/src/video/nv2a_world_tris.h). Same ring, constants and
 * shader maths, drawn through the renderer's host-draw hook like the fog pass.
 * Difference from D3D11: the batch is drawn when its packet arrives instead of
 * being held until the frame's first 2D draw, so MM3's stencil shadow quads that
 * come later in the frame can darken it a little. Built with SDL3_GPU only. */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <video/nv2a_d3d11.h>
#include "mm3_tris_vs.spirv.h"
#include "mm3_tris_ps.spirv.h"
#ifdef _WIN32
#include "mm3_tris_vs.dxil.h"
#include "mm3_tris_ps.dxil.h"
#endif

typedef struct { float view[3][4], proj[4], band[4]; } TrisConsts;
enum { SLOTS = 8 };
static struct { uint32_t seq, count; NvWorldVertex v[NV2A_WORLD_TRIS_MAX]; } s_ring[SLOTS];
static SDL_SpinLock s_lock;               /* the title thread publishes, the executor copies */

void nv2a_d3d_world_tris_publish(uint32_t seq, const NvWorldVertex *v, uint32_t count)
{
    uint32_t slot = seq % SLOTS;
    if (count > NV2A_WORLD_TRIS_MAX) count = NV2A_WORLD_TRIS_MAX;
    count -= count % 3u;
    SDL_LockSpinlock(&s_lock);
    if (count) memcpy(s_ring[slot].v, v, count * sizeof *v);
    s_ring[slot].count = count;
    s_ring[slot].seq = seq;
    SDL_UnlockSpinlock(&s_lock);
}

static SDL_GPUShader *s_vs, *s_ps;
static SDL_GPUGraphicsPipeline *s_pipe;
static SDL_GPUSampler *s_sampler;
static SDL_GPUBuffer *s_vb;
static SDL_GPUTransferBuffer *s_tb;
static int s_state;                        /* 0 not tried, 1 ready, -1 failed */

static SDL_GPUShader *shader(SDL_GPUDevice *device, int fragment)
{
    SDL_GPUShaderCreateInfo info = {0};
    info.entrypoint = fragment ? "tris_ps" : "tris_vs";
    info.stage = fragment ? SDL_GPU_SHADERSTAGE_FRAGMENT : SDL_GPU_SHADERSTAGE_VERTEX;
    info.num_samplers = fragment ? 1 : 0;
    info.num_uniform_buffers = 1;
#ifdef _WIN32
    if (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_DXIL) {
        info.format = SDL_GPU_SHADERFORMAT_DXIL;
        info.code = fragment ? mm3_tris_ps_dxil : mm3_tris_vs_dxil;
        info.code_size = fragment ? sizeof mm3_tris_ps_dxil : sizeof mm3_tris_vs_dxil;
    } else
#endif
    {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = fragment ? mm3_tris_ps_spirv : mm3_tris_vs_spirv;
        info.code_size = fragment ? sizeof mm3_tris_ps_spirv : sizeof mm3_tris_vs_spirv;
    }
    return SDL_CreateGPUShader(device, &info);
}

static int init(SDL_GPUDevice *device)
{
    SDL_GPUGraphicsPipelineCreateInfo info = {0};
    SDL_GPUColorTargetDescription colour = {0};
    SDL_GPUVertexBufferDescription vbd = {0};
    SDL_GPUVertexAttribute attr[2] = {{0}};
    SDL_GPUSamplerCreateInfo sampler = {0};
    SDL_GPUBufferCreateInfo bci = {0};
    SDL_GPUTransferBufferCreateInfo tci = {0};
    if (s_state) return s_state > 0;
    s_state = -1;
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bci.size = NV2A_WORLD_TRIS_MAX * sizeof(NvWorldVertex);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = bci.size;
    if (!(s_vs = shader(device, 0)) || !(s_ps = shader(device, 1)) ||
        !(s_sampler = SDL_CreateGPUSampler(device, &sampler)) ||
        !(s_vb = SDL_CreateGPUBuffer(device, &bci)) || !(s_tb = SDL_CreateGPUTransferBuffer(device, &tci)))
        goto fail;
    colour.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    colour.blend_state.enable_blend = true;
    colour.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colour.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colour.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    colour.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour.blend_state.color_blend_op = colour.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    vbd.slot = 0;
    vbd.pitch = sizeof(NvWorldVertex);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    attr[0].location = 0; attr[0].buffer_slot = 0;
    attr[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3; attr[0].offset = 0;
    attr[1].location = 1; attr[1].buffer_slot = 0;
    attr[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM; attr[1].offset = 12;
    info.vertex_shader = s_vs;
    info.fragment_shader = s_ps;
    info.vertex_input_state.vertex_buffer_descriptions = &vbd;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attr;
    info.vertex_input_state.num_vertex_attributes = 2;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &colour;
    info.target_info.num_color_targets = 1;
    if (!(s_pipe = SDL_CreateGPUGraphicsPipeline(device, &info))) goto fail;
    s_state = 1;
    return 1;
fail:
    fprintf(stderr, "[TRIS] SDL GPU init failed: %s\n", SDL_GetError());
    return 0;
}

typedef struct { const TrisConsts *c; uint32_t seq; } Job;

static int draw(const NvHostTarget *target, void *user)
{
    const Job *job = (const Job *)user;
    SDL_GPUDevice *device = (SDL_GPUDevice *)target->device;
    SDL_GPUCommandBuffer *commands = (SDL_GPUCommandBuffer *)target->context;
    uint32_t slot = job->seq % SLOTS, count = 0;
    TrisConsts c = *job->c;
    static int logged;
    void *map;
    if (!init(device)) return 0;
    c.band[0] *= target->depth_bits == 16 ? 65536.f : 16777216.f;
    if (!isfinite(c.band[0])) return 0;
    if (!(map = SDL_MapGPUTransferBuffer(device, s_tb, true))) return 0;
    SDL_LockSpinlock(&s_lock);
    if (s_ring[slot].seq == job->seq) {
        count = s_ring[slot].count;
        memcpy(map, s_ring[slot].v, count * sizeof(NvWorldVertex));
    }
    SDL_UnlockSpinlock(&s_lock);
    SDL_UnmapGPUTransferBuffer(device, s_tb);
    if (!count) return 0;
    {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTransferBufferLocation src = {s_tb, 0};
        SDL_GPUBufferRegion dst = {s_vb, 0, count * (uint32_t)sizeof(NvWorldVertex)};
        if (!copy) return 0;
        SDL_UploadToGPUBuffer(copy, &src, &dst, true);
        SDL_EndGPUCopyPass(copy);
    }
    {
        SDL_GPUColorTargetInfo colour = {0};
        SDL_GPURenderPass *pass;
        SDL_GPUViewport vp = {0, 0, (float)target->width, (float)target->height, 0, 1};
        SDL_Rect scissor = {0, 0, (int)target->width, (int)target->height};
        SDL_GPUTextureSamplerBinding binding = {(SDL_GPUTexture *)target->depth, s_sampler};
        SDL_GPUBufferBinding vb = {s_vb, 0};
        colour.texture = (SDL_GPUTexture *)target->colour;
        colour.load_op = SDL_GPU_LOADOP_LOAD;
        colour.store_op = SDL_GPU_STOREOP_STORE;
        if (!(pass = SDL_BeginGPURenderPass(commands, &colour, 1, NULL))) return 0;
        SDL_SetGPUViewport(pass, &vp);
        SDL_SetGPUScissor(pass, &scissor);
        SDL_BindGPUGraphicsPipeline(pass, s_pipe);
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_PushGPUVertexUniformData(commands, 0, &c, sizeof c);
        SDL_PushGPUFragmentUniformData(commands, 0, &c, sizeof c);
        SDL_DrawGPUPrimitives(pass, count, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
    if (!logged) {
        logged = 1;
        fprintf(stderr, "[TRIS] first world triangle batch: %u vertices on %ux%u (SDL GPU)\n",
                count, target->width, target->height);
    }
    return 1;
}

/* Executor thread: the packet, with the frame's camera (see nv2a_world_tris.h). */
void nv2a_d3d_world_tris(const NvD3DState *st, uint32_t seq, float depth_to_world,
                         const float view[12], const float proj[4])
{
    TrisConsts c;
    Job job;
    unsigned i;
    if (!st || !isfinite(depth_to_world) || depth_to_world <= 0) return;
    for (i = 0; i < 12; ++i) if (!isfinite(view[i]) || fabsf(view[i]) > 1.0e7f) return;
    for (i = 0; i < 4; ++i) if (!isfinite(proj[i]) || fabsf(proj[i]) > 1.0e4f) return;
    memset(&c, 0, sizeof c);
    for (i = 0; i < 3; ++i) {
        c.view[i][0] = view[i];
        c.view[i][1] = view[3 + i];
        c.view[i][2] = view[6 + i];
        c.view[i][3] = view[9 + i];
    }
    memcpy(c.proj, proj, sizeof c.proj);
    c.band[0] = depth_to_world;               /* times the depth range, in draw() */
    c.band[1] = 0.02f;                        /* metres: surfaces it lies on */
    c.band[2] = 4.0f;                         /* fades over 0.25 m */
    job.c = &c;
    job.seq = seq;
    nv2a_d3d_host_draw(st, draw, &job);
}
