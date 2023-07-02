#pragma once

#include <d3d12.h>

#include <array>
#include <vector>

#include "d3d8.h"
#include "device_limits.h"
#include "util.h"
#include "utils/asserts.h"

namespace Dx8to12 {
struct RenderState {
  void Reset();

  // Retrieves a D3DRENDERSTATETYPE render state by its index.
  DWORD &GetEnumAtIndex(D3DRENDERSTATETYPE index);

  // This is all the state that is accessed using D3DRENDERSTATETYPE.
  D3DZBUFFERTYPE
  zbuffer_type =
      D3DZB_FALSE;  // TODO: Set to true if EnableAutoDepthStencil is set.
  DWORD zwrite_enable = TRUE;
  D3DFILLMODE fill_mode = D3DFILL_SOLID;
  D3DSHADEMODE shade_mode = D3DSHADE_GOURAUD;
  DWORD alpha_test_enable = FALSE;
  D3DBLEND src_blend = D3DBLEND_ONE;
  D3DBLEND dest_blend = D3DBLEND_ONE;
  D3DCULL cull_mode = D3DCULL_CCW;
  D3DCMPFUNC z_func = D3DCMP_LESSEQUAL;
  DWORD alpha_ref = 0;
  D3DCMPFUNC alpha_func = D3DCMP_ALWAYS;
  DWORD dither_enable = FALSE;
  DWORD alpha_blend_enable = FALSE;
  BOOL fog_enable = FALSE;
  DWORD specular_enable = FALSE;
  D3DCOLOR fog_color = 0;
  D3DFOGMODE fog_table_mode = D3DFOG_NONE;
  float fog_start = 0.f;
  float fog_end = 1.f;
  float fog_density = 1.f;
  DWORD edge_antialias = FALSE;
  LONG z_bias = 0;
  BOOL range_fog_enable = FALSE;
  BOOL stencil_enable = FALSE;
  D3DSTENCILOP stencil_pass = D3DSTENCILOP_KEEP;
  D3DCMPFUNC stencil_func = D3DCMP_ALWAYS;
  DWORD stencil_ref = 0;
  D3DCOLOR texture_factor = D3DCOLOR_ARGB(255, 255, 255, 255);
  DWORD lighting = TRUE;
  D3DCOLOR ambient = 0;
  D3DFOGMODE fog_vertex_mode = D3DFOG_NONE;
  DWORD color_vertex = TRUE;
  BOOL local_viewer = FALSE;
  DWORD normalized_normals = FALSE;
  D3DMATERIALCOLORSOURCE diffuse_material_source = D3DMCS_COLOR1;
  D3DMATERIALCOLORSOURCE specular_material_source = D3DMCS_COLOR2;
  D3DMATERIALCOLORSOURCE ambient_material_source = D3DMCS_COLOR2;
  D3DMATERIALCOLORSOURCE emissive_material_source = D3DMCS_COLOR2;
  float point_size = 1.f;
  float point_size_min = 0.f;
  BOOL point_sprite_enable = FALSE;
  BOOL point_scale_enable = FALSE;
  float point_scale_a = 1.f;
  float point_scale_b = 0.f;
  float point_scale_c = 0.f;
  DWORD multisample_antialias = TRUE;
  float point_size_max = 64.f;
  DWORD color_write_enable =
      D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
      D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA;
  D3DBLENDOP blend_op = D3DBLENDOP_ADD;

  CLANG_PUSH_IGNORE_FLOAT_EQUAL
  bool operator==(const RenderState &) const = default;
  CLANG_POP_IGNORE
};

struct TextureStageState {
  void Reset();

  D3DTEXTUREOP color_op = D3DTOP_DISABLE;
  DWORD color_arg1 = D3DTA_TEXTURE;
  DWORD color_arg2 = D3DTA_CURRENT;
  D3DTEXTUREOP alpha_op = D3DTOP_DISABLE;
  DWORD alpha_arg1 = D3DTA_TEXTURE;  // TODO: Is diffuse if no texture is set.
  DWORD alpha_arg2 = D3DTA_CURRENT;
  DWORD texcoord_index = 0;
  D3DTEXTUREADDRESS address_u = D3DTADDRESS_WRAP;
  D3DTEXTUREADDRESS address_v = D3DTADDRESS_WRAP;
  D3DTEXTUREFILTERTYPE mag_filter = D3DTEXF_POINT;
  D3DTEXTUREFILTERTYPE min_filter = D3DTEXF_POINT;
  D3DTEXTUREFILTERTYPE mip_filter = D3DTEXF_NONE;
  float mipmap_lod_bias = 0.f;
  DWORD max_anisotropy = 1;
  D3DTEXTURETRANSFORMFLAGS transform_flags = D3DTTFF_DISABLE;
  D3DTEXTUREADDRESS address_w = D3DTADDRESS_WRAP;

  DWORD &GetAtIndex(size_t index);

  CLANG_PUSH_IGNORE_FLOAT_EQUAL
  bool operator==(const TextureStageState &) const = default;
  CLANG_POP_IGNORE
};

// This object is used purely to cache pipeline state objects.
struct PSOState {
  RenderState rs;
  std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
  ID3DBlob *vs;
  ID3DBlob *ps;
  D3DPRIMITIVETYPE prim_type;
  DXGI_FORMAT dsv_format;
  // TODO: Add RTV format and DSV format when we support SetRenderTarget.
  bool operator==(const PSOState &other) const {
    return vs == other.vs && ps == other.ps && rs == other.rs &&
           input_elements.size() == other.input_elements.size() &&
           dsv_format == other.dsv_format &&
           memcmp(input_elements.data(), other.input_elements.data(),
                  sizeof(D3D12_INPUT_ELEMENT_DESC) * input_elements.size()) ==
               0;
  }
};

// Compactly encapsulates all state used to generate a pixel shader. Used a key
// to cache fixed-function pixel shaders.
struct PixelShaderState {
  PixelShaderState(const RenderState &rs,
                   const bool stage_has_texture[kMaxTexStages],
                   const TextureStageState texture_stage_states[kMaxTexStages]);

  bool color_vertex : 1;
  D3DMATERIALCOLORSOURCE diffuse_material_source : 2;
  // TODO: Any stage after a non-active stage is also not active.
  uint8_t stage_has_texture_flag : 8;
  uint8_t alpha_func_minus1 : 3;
  // TODO: Only take into account active stages.
  std::array<TextureStageState, kMaxTexStages> ts;

  bool stage_has_texture(int stage) const {
    ASSERT(stage < kMaxTexStages);
    return HasFlag(stage_has_texture_flag, 1 << stage);
  }

  D3DCMPFUNC alpha_func() const {
    return static_cast<D3DCMPFUNC>(alpha_func_minus1 + 1);
  }

  bool operator==(const PixelShaderState &) const = default;
};

static_assert(kMaxTexStages == 8, "Unexpected number of texture stages.");

}  // namespace Dx8to12

template <>
struct ::std::hash<Dx8to12::RenderState> {
  size_t operator()(Dx8to12::RenderState const &) const;
};

template <>
struct ::std::hash<Dx8to12::PSOState> {
  size_t operator()(Dx8to12::PSOState const &) const;
};

template <>
struct ::std::hash<Dx8to12::PixelShaderState> {
  size_t operator()(Dx8to12::PixelShaderState const &) const;
};
