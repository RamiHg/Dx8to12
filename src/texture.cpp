#include "texture.h"

#include <d3d12.h>

#include <bit>
#include <cstdint>

#include "aixlog.hpp"
#include "device.h"
#include "surface.h"
#include "util.h"

namespace Dx8to12 {

constexpr D3D12_RESOURCE_DIMENSION kTextureKindToDimension[] = {
    D3D12_RESOURCE_DIMENSION_TEXTURE2D, D3D12_RESOURCE_DIMENSION_TEXTURE2D};

constexpr D3D12_SRV_DIMENSION kTextureKindToSrvDimension[] = {
    D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_SRV_DIMENSION_TEXTURECUBE};

static int CalcNumberOfMips(uint32_t width, uint32_t height) {
  return std::bit_width<uint32_t>(std::max(width, height));
}

static uint32_t CalcSubresourceIndex(uint32_t array_slice, uint32_t mip,
                                     uint32_t num_mips) {
  return mip + array_slice * num_mips;
}

BaseTexture *BaseTexture::Create(Device *device, TextureKind kind,
                                 uint32_t width, uint32_t height,
                                 uint32_t depth, uint32_t mip_levels,
                                 DWORD d3d8_usage, D3DFORMAT format,
                                 D3DPOOL pool) {
  if (HasFlag(d3d8_usage, D3DUSAGE_DYNAMIC) && pool != D3DPOOL_DEFAULT)
    return nullptr;

  D3D12_RESOURCE_DESC resource_desc = {
      .Dimension = kTextureKindToDimension[(int)kind],
      .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      .Width = width,
      .Height = height,
      .DepthOrArraySize = safe_cast<uint16_t>(depth),
      .MipLevels = safe_cast<uint16_t>(mip_levels),
      .Format = DXGIFromD3DFormat(format),
      .SampleDesc = {.Count = 1, .Quality = 0},
      .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN};
  if (resource_desc.MipLevels == 0) {
    resource_desc.MipLevels = safe_cast<uint16_t>(CalcNumberOfMips(
        safe_cast<uint32_t>(resource_desc.Width), resource_desc.Height));
  }
  if (d3d8_usage & D3DUSAGE_RENDERTARGET)
    resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (d3d8_usage & D3DUSAGE_DEPTHSTENCIL)
    resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if (pool == D3DPOOL_SYSTEMMEM) {
    return new CpuTexture(device, kind, d3d8_usage, resource_desc);
  } else {
    if (HasFlag(d3d8_usage, D3DUSAGE_DYNAMIC)) {
      FAIL("Dynamic textures are untested.");
      // return new DynamicTexture(device, kind, d3d8_usage, resource_desc);
    } else {
      return new GpuTexture(device, kind, d3d8_usage, pool, resource_desc);
    }
  }
}

BaseTexture::BaseTexture(Device *device, TextureKind kind, Dx8::Usage usage,
                         D3DPOOL pool, const D3D12_RESOURCE_DESC &resource_desc)
    : device_(device),
      kind_(kind),
      usage_(usage),
      pool_(pool),
      resource_desc_(resource_desc) {
  // Grab all copyable footprints.
  footprints_.resize(resource_desc_.DepthOrArraySize *
                     resource_desc_.MipLevels);
  gpu_slice_sizes_.resize(footprints_.size());
  std::unique_ptr<uint64_t[]> gpu_row_strides(new uint64_t[footprints_.size()]);
  device_->device()->GetCopyableFootprints(
      &resource_desc_, 0, footprints_.size(), 0, footprints_.data(),
      gpu_slice_sizes_.data(), gpu_row_strides.get(), nullptr);
  compact_pitches_.resize(footprints_.size());
  compact_offsets_.resize(footprints_.size());
  const int format_size = DXGIFormatSize(resource_desc_.Format);
  size_t num_bytes = 0;
  for (size_t i = 0; i < footprints_.size(); ++i) {
    compact_offsets_[i] = num_bytes;
    // SOME games choose not to respect the row pitch that you give them, and
    // decide to compute their own pitch values.
    compact_pitches_[i] = footprints_[i].Footprint.Width * format_size;
    gpu_slice_sizes_[i] *= (uint32_t)gpu_row_strides[i];

    num_bytes += compact_pitches_[i] * footprints_[i].Footprint.Height;
  }
  total_compact_size_ = num_bytes;
}

D3DSURFACE_DESC BaseTexture::GetSurfaceDesc(uint32_t subresource) const {
  ASSERT(subresource < footprints_.size());
  const D3D12_SUBRESOURCE_FOOTPRINT &footprint =
      footprints_[subresource].Footprint;
  D3DSURFACE_DESC desc, *pDesc = &desc;
  pDesc->Format = DXGIToD3DFormat(resource_desc_.Format);
  pDesc->Type = D3DRTYPE_TEXTURE;
  pDesc->Usage = usage_;
  if (resource_desc_.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
    pDesc->Usage |= D3DUSAGE_RENDERTARGET;
  if (resource_desc_.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
    pDesc->Usage |= D3DUSAGE_DEPTHSTENCIL;
  pDesc->Pool = pool_;
  // TODO: This isn't technically correct - we should report the real surface
  // size.
  pDesc->Size = footprint.RowPitch * footprint.Height;
  pDesc->MultiSampleType = D3DMULTISAMPLE_NONE;
  pDesc->Width = footprint.Width;
  pDesc->Height = footprint.Height;
  return *pDesc;
}

HRESULT STDMETHODCALLTYPE BaseTexture::GetLevelDesc(UINT Level,
                                                    D3DSURFACE_DESC *pDesc) {
  if (Level >= resource_desc_.MipLevels) return D3DERR_INVALIDCALL;
  uint32_t index = CalcSubresourceIndex(0, Level, resource_desc_.MipLevels);
  *pDesc = GetSurfaceDesc(index);
  return S_OK;
}

DWORD STDMETHODCALLTYPE BaseTexture::GetLevelCount() {
  return resource_desc_.MipLevels;
}

CpuTexture::CpuTexture(Device *device, TextureKind kind, Dx8::Usage usage,
                       const D3D12_RESOURCE_DESC &resource_desc)
    : BaseTexture(device, kind, usage, D3DPOOL_SYSTEMMEM, resource_desc) {
  data_.reset(new char[total_compact_size_]);
  ASSERT(data_);
  memset(data_.get(), 0, total_compact_size_);
}

HRESULT STDMETHODCALLTYPE CpuTexture::LockRect(UINT Level,
                                               D3DLOCKED_RECT *pLockedRect,
                                               CONST RECT *pRect, DWORD Flags) {
  TRACE_ENTRY(this, resource_desc_.Width, resource_desc_.Height, Level,
              pLockedRect, pRect, Flags);
  if (Level >= footprints_.size()) return D3DERR_INVALIDCALL;
  ASSERT(pRect == nullptr);
  *pLockedRect = D3DLOCKED_RECT{.Pitch = compact_pitches_[Level],
                                .pBits = data_.get() + compact_offsets_[Level]};
  return S_OK;
}

HRESULT STDMETHODCALLTYPE CpuTexture::UnlockRect(UINT Level) {
  if (Level >= footprints_.size()) return D3DERR_INVALIDCALL;
  // TODO: Check if actually locked.
  return S_OK;
}

HRESULT STDMETHODCALLTYPE CpuTexture::LockRect(D3DCUBEMAP_FACES FaceType,
                                               UINT Level,
                                               D3DLOCKED_RECT *pLockedRect,
                                               CONST RECT *pRect, DWORD Flags) {
  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z ||
      Level >= resource_desc_.MipLevels)
    return D3DERR_INVALIDCALL;
  ASSERT(pRect == nullptr && kind_ == TextureKind::Cube);
  // Hackily use 2D texture's LockRect.
  return LockRect(
      CalcSubresourceIndex(FaceType, Level, resource_desc_.MipLevels),
      pLockedRect, pRect, Flags);
}

HRESULT STDMETHODCALLTYPE CpuTexture::UnlockRect(D3DCUBEMAP_FACES FaceType,
                                                 UINT Level) {
  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z ||
      Level >= resource_desc_.MipLevels)
    return D3DERR_INVALIDCALL;
  return UnlockRect(
      CalcSubresourceIndex(FaceType, Level, resource_desc_.MipLevels));
}

HRESULT STDMETHODCALLTYPE
CpuTexture::GetSurfaceLevel(UINT Level, IDirect3DSurface8 **ppSurfaceLevel) {
  TRACE_ENTRY(Level);
  ASSERT(kind_ == TextureKind::Texture2d);
  if (Level >= footprints_.size()) return D3DERR_INVALIDCALL;
  *ppSurfaceLevel = new CpuSurface(this, (int)Level, footprints_[Level],
                                   compact_pitches_[Level],
                                   data_.get() + compact_offsets_[Level]);
  return S_OK;
}

STDMETHODIMP CpuTexture::GetCubeMapSurface(
    D3DCUBEMAP_FACES FaceType, UINT Level,
    IDirect3DSurface8 **ppCubeMapSurface) {
  ASSERT(kind_ == TextureKind::Cube);
  uint32_t index =
      CalcSubresourceIndex(FaceType, Level, resource_desc_.MipLevels);
  if (index >= footprints_.size()) return D3DERR_INVALIDCALL;
  *ppCubeMapSurface = new CpuSurface(this, (int)index, footprints_[index],
                                     compact_pitches_[index],
                                     data_.get() + compact_offsets_[index]);
  return S_OK;
}

void CpuTexture::CopyToGpuTexture(GpuTexture *dest) {
  ASSERT(dest->kind() == kind_);
  for (uint32_t i = 0; i < footprints_.size(); ++i) {
    D3D12_TEXTURE_COPY_LOCATION dst_location{
        .pResource = dest->resource(),
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        .SubresourceIndex = i};
    CopySubresourceToGpuTexture(i, dst_location);
  }
}

void CpuTexture::CopySubresourceToGpuTexture(
    uint32_t subresource, const D3D12_TEXTURE_COPY_LOCATION &dst_location) {
  // First, copy over the data from our compact-pitch format to the pitch that
  // the GPU expects (and also to the upload heap).
  const D3D12_SUBRESOURCE_FOOTPRINT &footprint =
      footprints_[subresource].Footprint;
  const size_t num_bytes =
      static_cast<size_t>(footprint.RowPitch * footprint.Height);
  DynamicRingBuffer::Allocation ring_alloc =
      device_->dynamic_ring_buffer()->Allocate(num_bytes);
  char *source_ring_ptr =
      device_->dynamic_ring_buffer()->GetCpuPtrFor(ring_alloc);
  const uint32_t compact_pitch =
      safe_cast<uint32_t>(compact_pitches_[subresource]);
  if (compact_pitch == footprint.RowPitch)
    memcpy(source_ring_ptr, data_.get() + compact_offsets_[subresource],
           num_bytes);
  else {
    for (uint32_t i = 0; i < footprint.Height; ++i) {
      memcpy(source_ring_ptr + i * footprint.RowPitch,
             data_.get() + compact_offsets_[subresource] + i * compact_pitch,
             compact_pitch);
    }
  }
  // Issue the CopyTextureRegion.
  D3D12_TEXTURE_COPY_LOCATION src_location{
      .pResource = device_->dynamic_ring_buffer()->GetBackingResource(),
      .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
      .PlacedFootprint = {.Offset = safe_cast<uint64_t>(ring_alloc.offset),
                          .Footprint = footprint}};

  device_->cmd_list()->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location,
                                         nullptr);
}

GpuTexture::GpuTexture(Device *device, TextureKind kind, Dx8::Usage usage,
                       D3DPOOL pool, const D3D12_RESOURCE_DESC &resource_desc)
    : BaseTexture(device, kind, usage, pool, resource_desc) {
  ASSERT(pool_ == D3DPOOL_DEFAULT || pool_ == D3DPOOL_MANAGED);

  if (pool_ == D3DPOOL_MANAGED && !kDisableManagedResources) {
    cpu_tex_ = ComOwn(new CpuTexture(device, kind, usage, resource_desc_));
  }

  D3D12_HEAP_PROPERTIES heap_props{.Type = D3D12_HEAP_TYPE_DEFAULT};
  D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
  if (HasFlag(usage_, D3DUSAGE_DEPTHSTENCIL))
    current_state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  else
    current_state_ = D3D12_RESOURCE_STATE_COMMON;

  D3D12_CLEAR_VALUE clear_value = {.Format = resource_desc_.Format};
  if (HasFlag(resource_desc_.Flags, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
    clear_value.Color[0] = 1.f;
  D3D12_CLEAR_VALUE *p_clear_value =
      resource_desc_.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                              D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
          ? &clear_value
          : nullptr;
  ASSERT_HR(device_->device()->CreateCommittedResource(
      &heap_props, heap_flags, &resource_desc_, current_state_, p_clear_value,
      IID_PPV_ARGS(resource_.GetForInit())));

  InitViews();
}

GpuTexture::GpuTexture(Device *device, ComPtr<ID3D12Resource> resource)
    : BaseTexture(device, TextureKind::Texture2d, D3DUSAGE_RENDERTARGET,
                  D3DPOOL_DEFAULT, resource->GetDesc()),
      resource_(resource),
      current_state_(D3D12_RESOURCE_STATE_COMMON) {
  InitViews();
}

void GpuTexture::InitViews() {
  // Allocate a spot in the SRV heap.
  if (!HasFlag(usage_, D3DUSAGE_DEPTHSTENCIL)) {
    srv_handle_ = device_->srv_heap().Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{
        .Format = resource_desc_.Format,
        .ViewDimension = kTextureKindToSrvDimension[static_cast<int>(kind_)],
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        // Hacky, but TextureCube and Texture2D share the same layout.
        .TextureCube = {
            .MostDetailedMip = 0,
            .MipLevels = resource_desc_.MipLevels,
        }};
    device_->device()->CreateShaderResourceView(resource_.get(), &srv_desc,
                                                srv_handle_);
  }
  // Allocate an RTV.
  if (HasFlag(usage_, D3DUSAGE_RENDERTARGET)) {
    ASSERT(kind_ == TextureKind::Texture2d);
    ASSERT_TODO(resource_desc_.MipLevels == 1, "Render targets with > 1 mip.");
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc{
        .Format = resource_desc_.Format,
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MipSlice = 0, .PlaneSlice = 0}};
    rtv_handle_ = device_->rtv_heap()->Allocate();
    device_->device()->CreateRenderTargetView(resource_.get(), &rtv_desc,
                                              rtv_handle_);
  }
  // And a DSV.
  if (HasFlag(usage_, D3DUSAGE_DEPTHSTENCIL)) {
    ASSERT(kind_ == TextureKind::Texture2d);
    ASSERT_TODO(resource_desc_.MipLevels == 1, "Depth targets with > 1 mip.");
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc{
        .Format = resource_desc_.Format,
        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
        .Flags = D3D12_DSV_FLAG_NONE,
        .Texture2D = {}};
    dsv_handle_ = device_->dsv_heap()->Allocate();
    device_->device()->CreateDepthStencilView(resource_.get(), &dsv_desc,
                                              dsv_handle_);
  }
}

GpuTexture::~GpuTexture() {
  if (srv_handle_.ptr != 0) device_->srv_heap().Free(srv_handle_);
  if (rtv_handle_.ptr != 0) device_->rtv_heap()->Free(rtv_handle_);
  srv_handle_ = {};
}

void GpuTexture::SetName(const std::string &name) {
  (void)name;
#ifdef DX8TO12_ENABLE_VALIDATION
  ASSERT_HR(
      resource_->SetName(WStringFromChar(name.c_str(), name.size()).c_str()));
#endif
}

GpuTexture *GpuTexture::InitFromResource(Device *device,
                                         ComPtr<ID3D12Resource> resource) {
  return new GpuTexture(device, resource);
}

HRESULT STDMETHODCALLTYPE GpuTexture::LockRect(UINT Level,
                                               D3DLOCKED_RECT *pLockedRect,
                                               CONST RECT *pRect, DWORD Flags) {
  TRACE_ENTRY(this, resource_desc_.Width, resource_desc_.Height, Level,
              pLockedRect, pRect, Flags);
  if (pool_ != D3DPOOL_MANAGED || Level >= footprints_.size()) {
    return D3DERR_INVALIDCALL;
  }
  ASSERT(pRect == nullptr);
  if (kDisableManagedResources) {
    // Allocate the CPU texture now.
    if (!cpu_tex_)
      cpu_tex_ = ComOwn(new CpuTexture(device_, kind_, usage_, resource_desc_));
    else
      cpu_tex_->AddRef();
  }
  ASSERT(cpu_tex_);
  return cpu_tex_->LockRect(Level, pLockedRect, pRect, Flags);
}

HRESULT STDMETHODCALLTYPE GpuTexture::UnlockRect(UINT Level) {
  if (Level >= footprints_.size()) return D3DERR_INVALIDCALL;
  ASSERT(cpu_tex_);
  cpu_tex_->UnlockRect(Level);
  // Copy over the CPU data to our resource.
  D3D12_TEXTURE_COPY_LOCATION dst_location{
      .pResource = resource_.get(),
      .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
      .SubresourceIndex = Level};
  device_->TransitionTexture(this, Level, D3D12_RESOURCE_STATE_COPY_DEST);
  cpu_tex_->CopySubresourceToGpuTexture(Level, dst_location);
  // Issue a barrier (because CopySubresourceToGpuTexture does not).
  device_->TransitionTexture(this, Level,
                             D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  device_->MarkResourceAsUsed(InternalPtr(this));
  if (kDisableManagedResources) {
    // Free the cpu texture.
    cpu_tex_.DecrementRef();
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE GpuTexture::LockRect(D3DCUBEMAP_FACES FaceType,
                                               UINT Level,
                                               D3DLOCKED_RECT *pLockedRect,
                                               CONST RECT *pRect, DWORD Flags) {
  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z ||
      Level >= resource_desc_.MipLevels)
    return D3DERR_INVALIDCALL;
  ASSERT(pRect == nullptr && kind_ == TextureKind::Cube);
  // Hackily use 2D texture's LockRect.
  return LockRect(
      CalcSubresourceIndex(FaceType, Level, resource_desc_.MipLevels),
      pLockedRect, pRect, Flags);
}

HRESULT STDMETHODCALLTYPE GpuTexture::UnlockRect(D3DCUBEMAP_FACES FaceType,
                                                 UINT Level) {
  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z ||
      Level >= resource_desc_.MipLevels)
    return D3DERR_INVALIDCALL;
  return UnlockRect(
      CalcSubresourceIndex(FaceType, Level, resource_desc_.MipLevels));
}

STDMETHODIMP
GpuTexture::GetSurfaceLevel(UINT Level, IDirect3DSurface8 **ppSurfaceLevel) {
  TRACE_ENTRY(Level);
  ASSERT(kind_ == TextureKind::Texture2d);
  if (Level >= resource_desc_.MipLevels) return D3DERR_INVALIDCALL;
  *ppSurfaceLevel = new GpuSurface(device_, this, Level);
  return S_OK;
}

STDMETHODIMP GpuTexture::GetCubeMapSurface(
    D3DCUBEMAP_FACES FaceType, UINT Level,
    IDirect3DSurface8 **ppCubeMapSurface) {
  ASSERT(kind_ == TextureKind::Cube);
  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z ||
      Level >= resource_desc_.MipLevels)
    return D3DERR_INVALIDCALL;
  uint32_t index =
      CalcSubresourceIndex(FaceType, Level, resource_desc_.MipLevels);
  ASSERT(index < footprints_.size());
  *ppCubeMapSurface = new GpuSurface(device_, this, index);
  return S_OK;
}

// WARNING: Dynamic textures are untested.
DynamicTexture::DynamicTexture(Device *device, TextureKind kind,
                               Dx8::Usage usage,
                               const D3D12_RESOURCE_DESC &resource_desc)
    : GpuTexture(device, kind, usage, D3DPOOL_DEFAULT, resource_desc) {}

HRESULT STDMETHODCALLTYPE DynamicTexture::LockRect(UINT Level,
                                                   D3DLOCKED_RECT *pLockedRect,
                                                   CONST RECT *pRect,
                                                   DWORD Flags) {
  if (Level >= footprints_.size() ||
      (Level != 0 && HasFlag(Flags, D3DLOCK_DISCARD)))
    return D3DERR_INVALIDCALL;
  ASSERT(HasFlag(Flags, D3DLOCK_DISCARD));
  ASSERT(Level == 0);
  ASSERT(!is_locked_);
  ASSERT(pRect == nullptr);
  is_locked_ = true;
  return cpu_tex_->LockRect(Level, pLockedRect, pRect, 0);
}

HRESULT STDMETHODCALLTYPE DynamicTexture::UnlockRect(UINT Level) {
  if (Level >= footprints_.size()) return D3DERR_INVALIDCALL;
  ASSERT(Level == 0);
  ASSERT(is_locked_);
  // Allocate a texture in GPU ring buffer memory.
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = footprints_[Level];
  DynamicRingBuffer::Allocation alloc =
      device_->dynamic_gpu_ring_buffer()->Allocate(
          gpu_slice_sizes_[Level], D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
  footprint.Offset = safe_cast<UINT64>(alloc.offset);

  // Copy the CPU buffer to our new GPU ring texture location.
  D3D12_TEXTURE_COPY_LOCATION ring_location{
      .pResource = device_->dynamic_gpu_ring_buffer()->GetBackingResource(),
      .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
      .PlacedFootprint = footprint};
  cpu_tex_->CopySubresourceToGpuTexture(Level, ring_location);

  cpu_tex_->UnlockRect(Level);
  is_locked_ = false;
  return S_OK;
}
}  // namespace Dx8to12