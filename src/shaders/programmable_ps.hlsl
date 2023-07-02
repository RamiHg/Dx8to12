#include "ps_common.hlsl"

struct VertexOutput {
  float4 oPos : SV_POSITION;
  float4 input_reg0 : COLOR0;
  float4 input_reg1 : COLOR1;
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

cbuffer ConstantData : register(b10) { float4 c[NUM_VS_CONST_REGS]; }

float4 PSMain(VertexOutput IN) : SV_Target {
  float4 t0 = 0, t1 = 0, t2 = 0, t3 = 0;
  float4 temp_reg[NUM_PS_TEMP_REGS];
