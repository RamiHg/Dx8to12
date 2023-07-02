#include "common.hlsl"

Texture2D<float4> g_texture0 : register(t0);
SamplerState g_sampler0 : register(s0);
Texture2D<float4> g_texture1 : register(t1);
SamplerState g_sampler1 : register(s1);
Texture2D<float4> g_texture2 : register(t2);
SamplerState g_sampler2 : register(s2);

TextureCube<float4> g_texCube0 : register(t0);
TextureCube<float4> g_texCube1 : register(t1);