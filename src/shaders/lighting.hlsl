#define D3DMCS_MATERIAL 0
#define D3DMCS_COLOR1 1
#define D3DMCS_COLOR2 2

#define D3DLIGHT_POINT 1
#define D3DLIGHT_SPOT 2
#define D3DLIGHT_DIRECTIONAL 3

// sizeof(Light) is 7 float4s.
struct Light {
  float4 diffuse;
  float4 specular;
  float4 ambient;
  float3 position;
  int type;
  float3 direction;
  float range;
  float falloff;
  float attentuation0, attentuation1, attentuation2;
  float theta;
  float phi;
  float2 pad;
};

cbuffer Lights : register(b2) {
  Light lights[8];
  int num_lights;
  int diffuse_material_source;
  int ambient_material_source;
  int specular_material_source;
  int specular_enable;
  int3 pad1;
  float4 global_ambient;
};

float4 ComputeLighting(float3 view_pos, float3 view_normal,
                       float4 vertex_color1, float4 vertex_color2,
                       out float4 specular_lighting) {
  float4 diffuse_color =
      diffuse_material_source == D3DMCS_MATERIAL
          ? material_diffuse
          : (diffuse_material_source == D3DMCS_COLOR1 ? vertex_color1
                                                      : vertex_color2);
  float4 ambient_color =
      ambient_material_source == D3DMCS_MATERIAL
          ? material_ambient
          : (ambient_material_source == D3DMCS_COLOR1 ? vertex_color1
                                                      : vertex_color2);
  float4 specular_color =
      specular_material_source == D3DMCS_MATERIAL
          ? material_specular
          : (specular_material_source == D3DMCS_COLOR1 ? vertex_color1
                                                       : vertex_color2);
  float3 diffuse_lighting = 0;
  float3 ambient_lighting = global_ambient.xyz;
  specular_lighting = float4(0, 0, 0, 1);

  for (int i = 0; i < min(num_lights, 8); ++i) {
    Light light = lights[i];
    float3 dir_to_light;
    float attenuation;

    switch (light.type) {
      case D3DLIGHT_POINT:
      case D3DLIGHT_SPOT: {
        dir_to_light = light.position - view_pos;
        float dist_sq = dot(dir_to_light, dir_to_light);
        if (dist_sq > light.range * light.range)
          attenuation = 0;
        else {
          float dist = sqrt(dist_sq);
          dir_to_light /= dist;
          attenuation =
              saturate(rcp(light.attentuation0 + light.attentuation1 * dist +
                           light.attentuation2 * dist_sq));
        }
        break;
      }
      case D3DLIGHT_DIRECTIONAL:
        dir_to_light = -normalize(light.direction);
        attenuation = 1;
        break;
      default:
        dir_to_light = 0;
        attenuation = 0;
        break;
    }
    if (light.type == D3DLIGHT_SPOT) attenuation = 0;

    diffuse_lighting += saturate(dot(view_normal, dir_to_light)) * attenuation *
                        light.diffuse.xyz;
    ambient_lighting += light.ambient.xyz;

#if 0
    // TODO: LOCALVIEWER
    float3 H = normalize(normalize(camera_position - world_pos) + dir_to_light);
#else
    float3 H = normalize(float3(0, 0, 1) + dir_to_light);
#endif
    float ndoth = dot(view_normal, H);
    if (ndoth > 0)
      specular_lighting.xyz +=
          light.specular.xyz * pow(abs(ndoth), material_power) * attenuation;
  }

  if (!specular_enable)
    specular_lighting = 0;
  else
    specular_lighting *= specular_color;

  diffuse_lighting = saturate(diffuse_lighting * diffuse_color.rgb);
  ambient_lighting = saturate(ambient_lighting * ambient_color.rgb);

  diffuse_lighting = saturate(diffuse_lighting + ambient_lighting);

  return float4(diffuse_lighting, diffuse_color.a);
}