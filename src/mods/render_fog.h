#ifndef MM3_RENDER_FOG_H
#define MM3_RENDER_FOG_H
#include "nv2a_d3d11.h"
#define MM3_FOG_SKY_MASK NV2A_HOST_FOG_SKY_MASK
#define MM3_FOG_SKY_FILL NV2A_HOST_FOG_SKY_FILL
#define MM3_FOG_SKY_FADE NV2A_HOST_FOG_SKY_FADE
void mm3_render_fog(const NvD3DState *state,float depth_to_world,float far_clip,
                    float start,float end,uint32_t argb,uint32_t flags,const float sky_fade[4]);
#endif
