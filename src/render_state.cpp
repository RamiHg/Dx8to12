#include "render_state.h"

#include "util.h"
#include "utils/murmur_hash.h"

namespace Dx8to12 {

void RenderState::Reset() { *this = RenderState(); }

DWORD &RenderState::GetEnumAtIndex(D3DRENDERSTATETYPE index) {
  switch (index) {
    case D3DRS_ZENABLE:
      return reinterpret_cast<DWORD &>(zbuffer_type);
    case D3DRS_ZWRITEENABLE:
      return zwrite_enable;
    case D3DRS_SHADEMODE:
      return reinterpret_cast<DWORD &>(shade_mode);
    case D3DRS_FILLMODE:
      return reinterpret_cast<DWORD &>(fill_mode);
    case D3DRS_ALPHATESTENABLE:
      return alpha_test_enable;
    case D3DRS_SRCBLEND:
      return reinterpret_cast<DWORD &>(src_blend);
    case D3DRS_DESTBLEND:
      return reinterpret_cast<DWORD &>(dest_blend);
    case D3DRS_CULLMODE:
      return reinterpret_cast<DWORD &>(cull_mode);
    case D3DRS_ZFUNC:
      return reinterpret_cast<DWORD &>(z_func);
    case D3DRS_ALPHAREF:
      return alpha_ref;
    case D3DRS_ALPHAFUNC:
      return reinterpret_cast<DWORD &>(alpha_func);
    case D3DRS_DITHERENABLE:
      return dither_enable;
    case D3DRS_ALPHABLENDENABLE:
      return alpha_blend_enable;
    case D3DRS_FOGENABLE:
      return reinterpret_cast<DWORD &>(fog_enable);
    case D3DRS_SPECULARENABLE:
      return specular_enable;
    case D3DRS_FOGCOLOR:
      return fog_color;
    case D3DRS_FOGTABLEMODE:
      return reinterpret_cast<DWORD &>(fog_table_mode);
    case D3DRS_FOGSTART:
      return reinterpret_cast<DWORD &>(fog_start);
    case D3DRS_FOGEND:
      return reinterpret_cast<DWORD &>(fog_end);
    case D3DRS_FOGDENSITY:
      return reinterpret_cast<DWORD &>(fog_density);
    case D3DRS_EDGEANTIALIAS:
      return edge_antialias;
    case D3DRS_ZBIAS:
      return reinterpret_cast<DWORD &>(z_bias);
    case D3DRS_RANGEFOGENABLE:
      return reinterpret_cast<DWORD &>(range_fog_enable);
    case D3DRS_STENCILENABLE:
      return reinterpret_cast<DWORD &>(stencil_enable);
    case D3DRS_STENCILPASS:
      return reinterpret_cast<DWORD &>(stencil_pass);
    case D3DRS_STENCILFUNC:
      return reinterpret_cast<DWORD &>(stencil_func);
    case D3DRS_STENCILREF:
      return stencil_ref;
    case D3DRS_TEXTUREFACTOR:
      return texture_factor;
    case D3DRS_LIGHTING:
      return lighting;
    case D3DRS_AMBIENT:
      return reinterpret_cast<DWORD &>(ambient);
    case D3DRS_FOGVERTEXMODE:
      return reinterpret_cast<DWORD &>(fog_vertex_mode);
    case D3DRS_COLORVERTEX:
      return color_vertex;
    case D3DRS_LOCALVIEWER:
      return reinterpret_cast<DWORD &>(local_viewer);
    case D3DRS_NORMALIZENORMALS:
      return normalized_normals;
    case D3DRS_DIFFUSEMATERIALSOURCE:
      return reinterpret_cast<DWORD &>(diffuse_material_source);
    case D3DRS_SPECULARMATERIALSOURCE:
      return reinterpret_cast<DWORD &>(specular_material_source);
    case D3DRS_AMBIENTMATERIALSOURCE:
      return reinterpret_cast<DWORD &>(ambient_material_source);
    case D3DRS_EMISSIVEMATERIALSOURCE:
      return reinterpret_cast<DWORD &>(emissive_material_source);
    case D3DRS_POINTSIZE:
      return reinterpret_cast<DWORD &>(point_size);
    case D3DRS_POINTSIZE_MIN:
      return reinterpret_cast<DWORD &>(point_size_min);
    case D3DRS_POINTSPRITEENABLE:
      return reinterpret_cast<DWORD &>(point_sprite_enable);
    case D3DRS_POINTSCALEENABLE:
      return reinterpret_cast<DWORD &>(point_scale_enable);
    case D3DRS_POINTSCALE_A:
      return reinterpret_cast<DWORD &>(point_scale_a);
    case D3DRS_POINTSCALE_B:
      return reinterpret_cast<DWORD &>(point_scale_b);
    case D3DRS_POINTSCALE_C:
      return reinterpret_cast<DWORD &>(point_scale_c);
    case D3DRS_MULTISAMPLEANTIALIAS:
      return multisample_antialias;
    case D3DRS_POINTSIZE_MAX:
      return reinterpret_cast<DWORD &>(point_size_max);
    case D3DRS_COLORWRITEENABLE:
      return color_write_enable;
    case D3DRS_BLENDOP:
      return reinterpret_cast<DWORD &>(blend_op);
    default:
      FAIL("Unexpected render state %d", index);
  }
}

PixelShaderState::PixelShaderState(
    const RenderState &rs, const bool stage_has_texture[kMaxTexStages],
    const TextureStageState texture_stage_states[kMaxTexStages]) {
  // Zero out memory to make sure to clear any bit-field padding.
  memset(this, 0, sizeof(*this));

  color_vertex = rs.color_vertex;
  diffuse_material_source = rs.diffuse_material_source;
  ASSERT(rs.alpha_func != 0 && rs.alpha_func <= 8);
  alpha_func_minus1 =
      static_cast<uint8_t>(rs.alpha_test_enable ? rs.alpha_func
                                                : D3DCMP_ALWAYS) -
      1;

  for (int i = 0; i < kMaxTexStages; ++i) {
    const TextureStageState &stage = texture_stage_states[i];
    if (stage.color_op == D3DTOP_DISABLE ||
        (stage.color_arg1 == D3DTA_TEXTURE && !stage_has_texture[i])) {
      ts[i].color_op = D3DTOP_DISABLE;  // D3DTOP_DISABLE=1, so must set!
      break;
    }
    if (stage_has_texture[i]) stage_has_texture_flag |= 1 << i;
    // We don't use all texture stage states in generating the pixel shader.
    // This is really error-prone though, so make sure to double-check this
    // constructor every time you modify ff_pixel_shader.cpp.
    ts[i] = TextureStageState{.color_op = stage.color_op,
                              .color_arg1 = stage.color_arg1,
                              .color_arg2 = stage.color_arg2,
                              .alpha_op = stage.alpha_op,
                              .alpha_arg1 = stage.alpha_arg1,
                              .alpha_arg2 = stage.alpha_arg2,
                              .texcoord_index = stage.texcoord_index,
                              .transform_flags = stage.transform_flags};
    if (stage.alpha_arg1 == D3DTA_TEXTURE && !stage_has_texture[i]) {
      // Default argument is DIFFUSE if no texture is set.
      ts[i].alpha_arg1 = D3DTA_DIFFUSE;
    }
  }
}

void TextureStageState::Reset() { *this = TextureStageState(); }

DWORD &TextureStageState::GetAtIndex(size_t index) {
  switch (index) {
    case D3DTSS_COLOROP:
      return reinterpret_cast<DWORD &>(color_op);
    case D3DTSS_COLORARG1:
      return color_arg1;
    case D3DTSS_COLORARG2:
      return color_arg2;
    case D3DTSS_ALPHAOP:
      return reinterpret_cast<DWORD &>(alpha_op);
    case D3DTSS_ALPHAARG1:
      return alpha_arg1;
    case D3DTSS_ALPHAARG2:
      return alpha_arg2;
    case D3DTSS_TEXCOORDINDEX:
      return texcoord_index;
    case D3DTSS_ADDRESSU:
      return reinterpret_cast<DWORD &>(address_u);
    case D3DTSS_ADDRESSV:
      return reinterpret_cast<DWORD &>(address_v);
    case D3DTSS_MAGFILTER:
      return reinterpret_cast<DWORD &>(mag_filter);
    case D3DTSS_MINFILTER:
      return reinterpret_cast<DWORD &>(min_filter);
    case D3DTSS_MIPFILTER:
      return reinterpret_cast<DWORD &>(mip_filter);
    case D3DTSS_MIPMAPLODBIAS:
      return reinterpret_cast<DWORD &>(mipmap_lod_bias);
    case D3DTSS_MAXANISOTROPY:
      return max_anisotropy;
    case D3DTSS_TEXTURETRANSFORMFLAGS:
      return reinterpret_cast<DWORD &>(transform_flags);
    case D3DTSS_ADDRESSW:
      return reinterpret_cast<DWORD &>(address_w);
    default:
      FAIL("Unexpected texture stage state %zu", index);
  }
}
}  // namespace Dx8to12

size_t std::hash<Dx8to12::RenderState>::operator()(
    Dx8to12::RenderState const &rs) const {
  // RenderState shouldn't have any padding - so this should be relatively
  // correct. Even if RenderState has padding, resetting zeros the entire class.
  uint32_t result = ::Dx8to12::MurmurHashTo32(&rs, sizeof(rs));
  return static_cast<size_t>(result);
}

size_t std::hash<Dx8to12::PSOState>::operator()(
    Dx8to12::PSOState const &pso_state) const {
  using ::Dx8to12::MurmurHashTo32;
  using ::Dx8to12::RenderState;

  uint32_t hash_elements[] = {
      std::hash<RenderState>()(pso_state.rs),
      MurmurHashTo32(
          pso_state.input_elements.data(),
          pso_state.input_elements.size() * sizeof(D3D12_INPUT_ELEMENT_DESC)),
      MurmurHashTo32(pso_state.vs, sizeof(pso_state.vs)),
      MurmurHashTo32(pso_state.ps, sizeof(pso_state.ps)),
      MurmurHashTo32(&pso_state.prim_type, sizeof(pso_state.prim_type)),
      static_cast<uint32_t>(pso_state.dsv_format)};
  return MurmurHashTo32(hash_elements, sizeof(hash_elements));
}

size_t std::hash<Dx8to12::PixelShaderState>::operator()(
    Dx8to12::PixelShaderState const &state) const {
  using ::Dx8to12::PixelShaderState;
  // Since we zero-out the entire struct, it's safe to just hash the object.
  return MurmurHashTo32(&state, sizeof(PixelShaderState));
}
