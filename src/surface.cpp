#include "surface.h"

#include <utility>

#include "device.h"
#include "texture.h"

namespace Dx8to12 {

HRESULT STDMETHODCALLTYPE BaseSurface::GetDesc(D3DSURFACE_DESC* pDesc) {
  if (pDesc == nullptr) return D3DERR_INVALIDCALL;
  *pDesc = desc_;
  return S_OK;
}

GpuSurface::GpuSurface(Device* device, GpuTexture* texture,
                       uint32_t subresource)
    : BaseSurface(SurfaceKind::Gpu),
      device_(device),
      texture_(ComWrap(texture)),
      subresource_(subresource) {
  desc_ = texture_->GetSurfaceDesc(subresource);
}

CpuSurface::CpuSurface(CpuTexture* texture, int level,
                       const D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint,
                       int compact_pitch, char* data_ptr)
    : BaseSurface(SurfaceKind::Cpu),
      texture_(ComWrap(texture)),
      footprint_(footprint),
      compact_pitch_(compact_pitch),
      data_ptr_(data_ptr) {
  ASSERT_HR(texture_->GetLevelDesc(level, &desc_));
}

BackbufferSurface::BackbufferSurface(int index, const D3D12_RESOURCE_DESC& desc)
    : BaseSurface(SurfaceKind::Backbuffer), index_(index) {
  desc_ = D3DSURFACE_DESC{.Format = DXGIToD3DFormat(desc.Format),
                          .Type = D3DRTYPE_SURFACE,
                          .Usage = D3DUSAGE_RENDERTARGET,
                          .Pool = D3DPOOL_DEFAULT,
                          .Size = safe_cast<UINT>(desc.Width * desc.Height *
                                                  DXGIFormatSize(desc.Format)),
                          .MultiSampleType = D3DMULTISAMPLE_NONE,
                          .Width = safe_cast<UINT>(desc.Width),
                          .Height = desc.Height};
}
}  // namespace Dx8to12