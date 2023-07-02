#define NUM_VS_CONST_REGS 96
#define NUM_VS_TEMP_REGS 12

#define NUM_PS_CONST_REGS 8
#define NUM_PS_TEMP_REGS 6

cbuffer Globals : register(b0) {
  float4x4 world_view_proj;
  float4x4 world_view;
  float3 camera_position;
  float pad;
  // float4x4 texture_coord_transforms[8];
};

cbuffer PixelGlobals : register(b1) {
  // Material data.
  float4 material_diffuse;
  float4 material_ambient;
  float4 material_specular;
  float4 material_emissive;
  float material_power;
  float alpha_ref;
  float4 texture_factor;
};

struct FFVertexOutput {
  float4 oPos : SV_POSITION;
  float4 oD0 : COLOR0;
  float4 oD1 : COLOR1;
  float4 oT0 : TEXCOORD0;
  float4 oT1 : TEXCOORD1;
  float4 oT2 : TEXCOORD2;
  float4 oT3 : TEXCOORD3;
  float4 oT4 : TEXCOORD4;
  float4 oT5 : TEXCOORD5;
  float4 oT6 : TEXCOORD6;
  float4 oT7 : TEXCOORD7;

  float3 oViewNormal : NORMAL0;
  float3 oViewReflect : NORMAL1;
  float3 oViewPos : NORMAL2;

  float oFog : FOG;
};

// #define FFVertexOutput VertexOutput

// Some helper functions to make generation of programmable shaders easier.
float4 mydot4(float4 a, float4 b) { return dot(a, b).xxxx; }
float4 mydot3(float3 a, float3 b) { return dot(a, b).xxxx; }
float4 mylerp(float4 s, float4 a, float4 b) { return lerp(a, b, s); }