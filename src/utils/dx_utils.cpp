#include "dx_utils.h"

#include <d3d12.h>
#include <dxgi.h>

#include <algorithm>

#include "render_state.h"

namespace Dx8to12 {
static D3D12_FILTER_TYPE ConvertFilterType(D3DTEXTUREFILTERTYPE d3d8_type) {
  switch (d3d8_type) {
    case D3DTEXF_NONE:
      // FAIL("TODO: D3DTEXF_NONE for disabling mips.");
      // LOG(WARNING) << "TODO: D3DTEXF_NONE for disabling mips.\n";
      return D3D12_FILTER_TYPE_POINT;
    case D3DTEXF_POINT:
      return D3D12_FILTER_TYPE_POINT;
    case D3DTEXF_LINEAR:
      return D3D12_FILTER_TYPE_LINEAR;
    default:
      FAIL("Unexpected filter type %d", d3d8_type);
  }
}

static D3D12_FILTER EncodeFilter(D3DTEXTUREFILTERTYPE min_filter,
                                 D3DTEXTUREFILTERTYPE mag_filter,
                                 D3DTEXTUREFILTERTYPE mip_filter) {
  if (min_filter == D3DTEXF_ANISOTROPIC || mag_filter == D3DTEXF_ANISOTROPIC ||
      mip_filter == D3DTEXF_ANISOTROPIC) {
    return D3D12_ENCODE_ANISOTROPIC_FILTER(
        D3D12_FILTER_REDUCTION_TYPE_STANDARD);
  } else {
    return D3D12_ENCODE_BASIC_FILTER(
        ConvertFilterType(min_filter), ConvertFilterType(mag_filter),
        ConvertFilterType(mip_filter), D3D12_FILTER_REDUCTION_TYPE_STANDARD);
  }
}

SamplerDesc::SamplerDesc(const TextureStageState &ts) {
  *this = D3D12_SAMPLER_DESC{
      .Filter = EncodeFilter(ts.min_filter, ts.mag_filter, ts.mip_filter),
      // Luckily D3D12_TEXTURE_ADDRESS_MODE maps directly.
      .AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(ts.address_u),
      .AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(ts.address_v),
      .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      // .AddressW = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(ts.address_w),
      .MipLODBias = std::clamp(ts.mipmap_lod_bias, -16.f, 15.99f),
      .MaxAnisotropy = ts.max_anisotropy,
      .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
      .BorderColor = {},  // TODO
      .MinLOD = 0.f,      // TODO
      .MaxLOD = 0.f       // TODO
  };
}

bool SamplerDesc::operator==(const SamplerDesc &other) const {
  return memcmp(this, &other, sizeof(SamplerDesc)) == 0;
}

D3DFORMAT DXGIToD3DFormat(DXGI_FORMAT dxgi_format) {
  switch (dxgi_format) {
    case DXGI_FORMAT_B8G8R8X8_UNORM:
      return D3DFMT_R8G8B8;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
      return D3DFMT_A8R8G8B8;
    case DXGI_FORMAT_B5G6R5_UNORM:
      return D3DFMT_R5G6B5;
    case DXGI_FORMAT_B4G4R4A4_UNORM:
      return D3DFMT_A4R4G4B4;
    case DXGI_FORMAT_B5G5R5A1_UNORM:
      return D3DFMT_A1R5G5B5;
    case DXGI_FORMAT_R32G32_FLOAT:
      return D3DFMT_R3G3B2;
    case DXGI_FORMAT_D32_FLOAT:
      return D3DFMT_D32;
    case DXGI_FORMAT_D16_UNORM:
      return D3DFMT_D16;
    case DXGI_FORMAT_A8_UNORM:
      return D3DFMT_A8;
    default:
      FAIL("Unimplemented DXGI_FORMAT %d\n", dxgi_format);
  }
}

DXGI_FORMAT DXGIFromD3DFormat(D3DFORMAT d3d_format) {
  switch (d3d_format) {
    case D3DFMT_X8R8G8B8:
      return DXGI_FORMAT_B8G8R8X8_UNORM;
    case D3DFMT_A8R8G8B8:
      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_R5G6B5:
      return DXGI_FORMAT_B5G6R5_UNORM;
    case D3DFMT_A4R4G4B4:
      return DXGI_FORMAT_B4G4R4A4_UNORM;
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
      return DXGI_FORMAT_B5G5R5A1_UNORM;
    case D3DFMT_R3G3B2:
      return DXGI_FORMAT_R32G32_FLOAT;
    case D3DFMT_A8:
      return DXGI_FORMAT_A8_UNORM;
    case D3DFMT_D32:
      return DXGI_FORMAT_D32_FLOAT;
    case D3DFMT_D16:
      return DXGI_FORMAT_D16_UNORM;
    case D3DFMT_INDEX16:
      return DXGI_FORMAT_R16_UINT;
    case D3DFMT_V8U8:
      return DXGI_FORMAT_R8G8_SNORM;
    case D3DFMT_Q8W8V8U8:
      return DXGI_FORMAT_R8G8B8A8_SNORM;
    case D3DFMT_V16U16:
      return DXGI_FORMAT_R16G16_SNORM;
    case D3DFMT_P8:
    case D3DFMT_L8:
    case D3DFMT_A8L8:
    case D3DFMT_A4L4:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A8P8:
    case D3DFMT_L6V5U5:
    case D3DFMT_X8L8V8U8:
    case D3DFMT_W11V11U10:
    case D3DFMT_A2W10V10U10:
    case D3DFMT_UYVY:
    case D3DFMT_YUY2:
      return DXGI_FORMAT_UNKNOWN;

    case D3DFMT_DXT1:
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
      return DXGI_FORMAT_UNKNOWN;

    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D24X4S4:
      return DXGI_FORMAT_UNKNOWN;

    case D3DFMT_R8G8B8:
      return DXGI_FORMAT_UNKNOWN;
    default:
      FAIL("Unimplemented D3DFORMAT %d\n", d3d_format);
  }
}

int DXGIFormatSize(DXGI_FORMAT format) {
  switch (format) {
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_D32_FLOAT:
      return 4;
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
      return 2;
    case DXGI_FORMAT_B8G8R8X8_UNORM:
      // This is tricky. We need to make sure DX8 can never lock R8G8B8
      // textures.
      return 4;
    default:
      FAIL("Unexpected format %d", format);
  }
}

ScopedGpuMarker::ScopedGpuMarker(ID3D12GraphicsCommandList *cmd_list,
                                 const char *annotation)
    : cmd_list_(cmd_list) {
  // LTO should hopefully remove the useless strlens.
  cmd_list->BeginEvent(1, annotation, strlen(annotation) + 1);
}

ScopedGpuMarker::~ScopedGpuMarker() { cmd_list_->EndEvent(); }
}  // namespace Dx8to12