#pragma once

#include <d3d12.h>

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <vector>

#include "SimpleMath.h"
#include "d3d8.h"
#include "util.h"
#include "utils/dx_utils.h"

namespace Dx8to12 {

struct ShaderLightMarshall {
  explicit ShaderLightMarshall(const DirectX::SimpleMath::Matrix& view,
                               const D3DLIGHT8& l)
      : diffuse(l.Diffuse),
        specular(l.Specular),
        ambient(l.Ambient),
        position(DirectX::SimpleMath::Vector3::Transform(
            VectorFromD3D(l.Position), view)),
        type(l.Type),
        direction(DirectX::SimpleMath::Vector3::TransformNormal(
            VectorFromD3D(l.Direction), view)),
        range(l.Range),
        falloff(l.Falloff),
        attenuation0(l.Attenuation0),
        attenuation1(l.Attenuation1),
        attenuation2(l.Attenuation2),
        theta(l.Theta),
        phi(l.Phi),
        pad{} {}
  D3DCOLORVALUE diffuse;
  D3DCOLORVALUE specular;
  D3DCOLORVALUE ambient;
  DirectX::SimpleMath::Vector3 position;
  D3DLIGHTTYPE type;
  DirectX::SimpleMath::Vector3 direction;
  float range;
  float falloff;
  float attenuation0, attenuation1, attenuation2;
  float theta;
  float phi;
  float pad[2];
};
static_assert(sizeof(ShaderLightMarshall) == 7 * 16,
              "ShaderLightMarshall size check.");

struct VertexCBuffer {
  DirectX::SimpleMath::Matrix world_view_proj;
  DirectX::SimpleMath::Matrix world_view;
  DirectX::SimpleMath::Vector3 camera_position;
  float pad;
  // D3DMATRIX texture_coord_transforms[8];
};
struct LightsCBuffer {
  ShaderLightMarshall lights[8];
  int num_lights;
  D3DMATERIALCOLORSOURCE diffuse_material_source;
  D3DMATERIALCOLORSOURCE ambient_material_source;
  D3DMATERIALCOLORSOURCE specular_material_source;
  int specular_enable;
  int pad[3];
  D3DCOLORVALUE global_ambient;
};
struct PixelCBuffer {
  D3DCOLORVALUE material_diffuse;
  D3DCOLORVALUE material_ambient;
  D3DCOLORVALUE material_specular;
  D3DCOLORVALUE material_emissive;
  float material_power;
  float alpha_ref;
  float pad[2];
  D3DCOLORVALUE texture_factor;
};

struct ConstantRegData {
  uint32_t data[4];
};

struct VertexShaderDeclaration {
  // Creates a VertexShaderDeclaration from an fvf desc passed to
  // SetVertexShader.
  static VertexShaderDeclaration CreateFromFVFDesc(DWORD fvf);

  std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
  std::array<int, 16> buffer_strides = {};
  std::array<bool, 16> has_inputs = {};
  std::map<int, ConstantRegData> constant_reg_init;

  friend std::ostream& operator<<(std::ostream& os,
                                  const VertexShaderDeclaration& decl);
};

VertexShaderDeclaration ParseShaderDeclaration(const DWORD* declaration);

struct VertexShader : public RefCounted {
  VertexShaderDeclaration decl;
  ComPtr<ID3DBlob> blob;
  DWORD fvf_desc;
};

struct PixelShader : public RefCounted {
  ComPtr<ID3DBlob> blob;
};

VertexShader CreateFixedFunctionVertexShader(
    const D3D12_VIEWPORT& viewport, const DWORD fvf_desc,
    const VertexShaderDeclaration& declaration);
}  // namespace Dx8to12