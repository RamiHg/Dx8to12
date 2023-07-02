#include "common.hlsl"
#include "lighting.hlsl"

FFVertexOutput VSMain(VertexInput IN) {
  FFVertexOutput OUT = (FFVertexOutput)0;
  float4 vertex_diffuse = material_diffuse;
  float4 vertex_specular = material_specular;
  float4 specular_lighting = 0;

#ifdef HAS_DIFFUSE
  vertex_diffuse = IN.input_reg5;
#endif
#ifdef HAS_SPECULAR
  vertex_specular = IN.input_reg6;
#endif

#ifndef HAS_TRANSFORM
  OUT.oPos = mul(world_view_proj, float4(IN.input_reg0, 1.f));

#ifdef HAS_NORMAL
  // TODO: Don't normalize if normalized_normals is set.
  float3 view_normal =
      normalize(mul(world_view, float4(IN.input_reg3, 0.f)).xyz);
  float3 view_pos = mul(world_view, float4(IN.input_reg0, 1.f)).xyz;
  OUT.oViewPos = view_pos;
  OUT.oViewNormal = view_normal;
  OUT.oViewReflect = normalize(reflect(view_pos, view_normal));
  vertex_diffuse = ComputeLighting(view_pos, view_normal, vertex_diffuse,
                                   vertex_specular, specular_lighting);
#endif
#else
  OUT.oPos = IN.input_reg0;
  OUT.oPos.xy = (OUT.oPos.xy + 0.5f) * invView2 - 1.f;
  OUT.oPos.y *= -1.f;

#ifndef HAS_DIFFUSE
  vertex_diffuse = float4(1, 1, 1, 1);
#endif
#ifndef HAS_SPECULAR
  specular_lighting = 0;
#endif
#endif

  OUT.oD0 = vertex_diffuse;
  OUT.oD1 = specular_lighting;

// Forward texture coordinates.
#ifdef HAS_T0
  OUT.oT0 = float4(IN.input_reg7, 0.f, 0.f);
#endif
#ifdef HAS_T1
  OUT.oT1 = float4(IN.input_reg8, 0.f, 0.f);
#endif
#ifdef HAS_T2
  OUT.oT2 = float4(IN.input_reg9, 0.f, 0.f);
#endif
#ifdef HAS_T3
  OUT.oT3 = float4(IN.input_reg10, 0.f, 0.f);
#endif
#ifdef HAS_T4
  OUT.oT4 = float4(IN.input_reg11, 0.f, 0.f);
#endif
#ifdef HAS_T5
  OUT.oT5 = float4(IN.input_reg12, 0.f, 0.f);
#endif
#ifdef HAS_T6
  OUT.oT6 = float4(IN.input_reg13, 0.f, 0.f);
#endif
#ifdef HAS_T7
  OUT.oT7 = float4(IN.input_reg14, 0.f, 0.f);
#endif

  return OUT;
}