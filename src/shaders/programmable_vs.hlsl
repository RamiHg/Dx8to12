#include "common.hlsl"

cbuffer ConstantData : register(b10) { float4 c[NUM_VS_CONST_REGS]; }

FFVertexOutput VSMain(VertexInput IN) {
  FFVertexOutput OUT = (FFVertexOutput)0;
  float4 temp_reg[NUM_VS_TEMP_REGS];
  int4 addr_reg;  // Only int4 to handle addr_reg = value.xxxx.
