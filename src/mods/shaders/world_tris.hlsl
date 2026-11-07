/* MM3 exhaust smoke (world-space triangles from src/mm3_exhaust.c) for the SDL3 GPU
 * renderer: the same maths as the D3D11 backend's nv2a_world_tris.h. Positions go
 * through the title's camera; each pixel fades out over its last 0.25 m in front of
 * the scene's stored W depth, so the smoke meets the ground without a hard edge. */
#include "nv2a_bindings.hlsli"
cbuffer Tris : CBUF(0) { float4 view[3]; float4 proj; float4 band; };
struct VI { float3 p : POSITION; float4 c : COLOR; };
struct VO { float4 pos : SV_Position; float4 c : COLOR; float z : TEXCOORD0; };
#ifdef STAGE_VS
VO tris_vs(VI i) {
  VO o; float4 w = float4(i.p, 1);
  float3 v = float3(dot(w, view[0]), dot(w, view[1]), dot(w, view[2]));
  o.pos = float4(v.x * proj.x + v.z * proj.z, v.y * proj.y + v.z * proj.w, 0.5 * v.z, v.z);
  o.c = i.c; o.z = v.z;
  return o;
}
#else
PS_TEX(Texture2D<float>, Depth, 0);
PS_SAMP(DepthS, 0);
float4 tris_ps(VO i) : SV_Target {
  float z = Depth.Load(int3(int2(i.pos.xy), 0));
  float4 c = i.c;
  if (z < 0.99999) c.a *= saturate((z * band.x - i.z + band.y) * band.z);
  if (c.a <= 0.002) discard;
  return c;
}
#endif
