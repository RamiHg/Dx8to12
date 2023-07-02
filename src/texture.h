#pragma once

#include <d3d12.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "d3d8.h"
#include "dynamic_ring_buffer.h"
#include "util.h"
#include "utils/dx_utils.h"

namespace Dx8to12 {
class Device;

enum class TextureKind {
  Texture2d,
  Cube,
};

class CpuTexture;

// Doesn't own any resources or data. Provides boilerplate code related to
// footprints and descriptions.
class BaseTexture : public IDirect3DTexture8,
                    public IDirect3DCubeTexture8,
                    public RefCounted {
 public:
  static BaseTexture* Create(Device* device, TextureKind kind, uint32_t width,
                             uint32_t height, uint32_t depth,
                             uint32_t mip_levels, DWORD d3d8_usage,
                             D3DFORMAT format, D3DPOOL pool);

  D3DSURFACE_DESC GetSurfaceDesc(uint32_t subresource) const;

  TextureKind kind() const { return kind_; }

 protected:
  BaseTexture(Device* device, TextureKind kind, Dx8::Usage usage, D3DPOOL pool,
              const D3D12_RESOURCE_DESC& resource_desc);

 public:
  /*** IUnknown methods ***/
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj)
      VIRT_NOT_IMPLEMENTED;
  virtual ULONG STDMETHODCALLTYPE AddRef(THIS) override {
    return RefCounted::AddRef();
  }
  virtual ULONG STDMETHODCALLTYPE Release(THIS) override {
    return RefCounted::Release();
  }

  virtual HRESULT STDMETHODCALLTYPE
  GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) override;

  /*** IDirect3DResource8 methods ***/
  virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** ppDevice)
      VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE
  SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData,
                 DWORD Flags) VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE GetPrivateData(
      REFGUID refguid, void* pData, DWORD* pSizeOfData) VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid)
      VIRT_NOT_IMPLEMENTED;
  virtual DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew)
      VIRT_NOT_IMPLEMENTED;
  virtual DWORD STDMETHODCALLTYPE GetPriority() VIRT_NOT_IMPLEMENTED;
  virtual void STDMETHODCALLTYPE PreLoad() override {}  // Do nothing.
  virtual D3DRESOURCETYPE STDMETHODCALLTYPE GetType() VIRT_NOT_IMPLEMENTED;
  /*** IDirect3DBaseTexture8 methods ***/
  virtual DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) VIRT_NOT_IMPLEMENTED;
  virtual DWORD STDMETHODCALLTYPE GetLOD() VIRT_NOT_IMPLEMENTED;
  DWORD STDMETHODCALLTYPE GetLevelCount() override;
  using IDirect3DTexture8::AddDirtyRect;
  using IDirect3DTexture8::LockRect;
  using IDirect3DTexture8::UnlockRect;
  /*** IDirect3DCubeTexture8 methods ***/
  virtual HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES FaceType,
                                             UINT Level,
                                             D3DLOCKED_RECT* pLockedRect,
                                             CONST RECT* pRect,
                                             DWORD Flags) VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES FaceType,
                                               UINT Level) VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE AddDirtyRect(
      D3DCUBEMAP_FACES FaceType, CONST RECT* pDirtyRect) VIRT_NOT_IMPLEMENTED;

 protected:
  Device* device_;
  TextureKind kind_;
  Dx8::Usage usage_;
  D3DPOOL pool_;

  D3D12_RESOURCE_DESC resource_desc_;
  // One footprint per level.
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints_;

  // Some games expect the pitch to be width*Bpp. So we give it that pitch, and
  // copy to the DX12 minimum pitch later.
  std::vector<int> compact_pitches_;
  std::vector<int> compact_offsets_;
  size_t total_compact_size_;

  // Actually per-slice offets when the texture is placed in its native tiling
  // mode.
  std::vector<uint32_t> gpu_slice_sizes_;
};

class GpuTexture : public BaseTexture {
 public:
  ~GpuTexture() override;

  // Creates a texture from an existing resource. This is only used with the
  // backbuffer. As such, d3d8_usage is set to D3DUSAGE_RENDERTARGET.
  static GpuTexture* InitFromResource(Device* device,
                                      ComPtr<ID3D12Resource> resource);

  ID3D12Resource* resource() { return resource_.get(); }
  D3D12_CPU_DESCRIPTOR_HANDLE srv_handle() const {
    ASSERT(srv_handle_.ptr != 0);
    return srv_handle_;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle() const {
    ASSERT(rtv_handle_.ptr != 0);
    return rtv_handle_;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle() const {
    ASSERT(dsv_handle_.ptr != 0);
    return dsv_handle_;
  }

  const D3D12_RESOURCE_DESC& resource_desc() const { return resource_desc_; }
  D3D12_RESOURCE_STATES current_state() const { return current_state_; }
  void set_state(D3D12_RESOURCE_STATES state) { current_state_ = state; }

  DWORD d3d8_usage() const { return usage_; }
  D3DPOOL d3d8_pool() const { return pool_; }

  void SetName(const std::string& name);

 public:
  ULONG STDMETHODCALLTYPE AddRef() override { return RefCounted::AddRef(); }
  ULONG STDMETHODCALLTYPE Release(THIS) override {
    return RefCounted::Release();
  }
  virtual HRESULT STDMETHODCALLTYPE
  GetSurfaceLevel(UINT Level, IDirect3DSurface8** ppSurfaceLevel) override;
  virtual HRESULT STDMETHODCALLTYPE LockRect(UINT Level,
                                             D3DLOCKED_RECT* pLockedRect,
                                             CONST RECT* pRect,
                                             DWORD Flags) override;
  virtual HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override;
  virtual HRESULT STDMETHODCALLTYPE AddDirtyRect(CONST RECT* pDirtyRect)
      VIRT_NOT_IMPLEMENTED;

  /*** IDirect3DCubeTexture8 methods ***/
  HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES FaceType, UINT Level,
                                     D3DLOCKED_RECT* pLockedRect,
                                     CONST RECT* pRect, DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES FaceType,
                                       UINT Level) override;
  STDMETHODIMP GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level,
                                 IDirect3DSurface8** ppCubeMapSurface) override;
  using IDirect3DCubeTexture8::AddDirtyRect;
  using IDirect3DCubeTexture8::LockRect;
  using IDirect3DCubeTexture8::UnlockRect;

 protected:
  GpuTexture(Device* device, ComPtr<ID3D12Resource> resource);
  GpuTexture(Device* device, TextureKind kind, Dx8::Usage usage, D3DPOOL pool,
             const D3D12_RESOURCE_DESC& resource_desc);

  ComPtr<ID3D12Resource> resource_;
  D3D12_CPU_DESCRIPTOR_HANDLE srv_handle_ = {};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle_ = {};
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle_ = {};

  D3D12_RESOURCE_STATES current_state_;

  ComPtr<CpuTexture> cpu_tex_;
  uint64_t last_update_frame_ = 0;

 private:
  void InitViews();

  friend BaseTexture* BaseTexture::Create(Device* device, TextureKind kind,
                                          uint32_t width, uint32_t height,
                                          uint32_t depth, uint32_t mip_levels,
                                          DWORD d3d8_usage, D3DFORMAT format,
                                          D3DPOOL pool);
};

class CpuTexture : public BaseTexture {
 public:
  CpuTexture(Device* device, TextureKind kind, Dx8::Usage usage,
             const D3D12_RESOURCE_DESC& resource_desc);

  void CopyToGpuTexture(GpuTexture* dest);
  void CopySubresourceToGpuTexture(
      uint32_t subresource, const D3D12_TEXTURE_COPY_LOCATION& dst_location);

  ULONG STDMETHODCALLTYPE Release(THIS) override {
    return RefCounted::Release();
  }

 public:
  HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect,
                                     CONST RECT* pRect, DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override;

  HRESULT STDMETHODCALLTYPE
  GetSurfaceLevel(UINT Level, IDirect3DSurface8** ppSurfaceLevel) override;
  HRESULT STDMETHODCALLTYPE AddDirtyRect(CONST RECT* pDirtyRect)
      VIRT_NOT_IMPLEMENTED;

  /*** IDirect3DCubeTexture8 methods ***/
  HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES FaceType, UINT Level,
                                     D3DLOCKED_RECT* pLockedRect,
                                     CONST RECT* pRect, DWORD Flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES FaceType,
                                       UINT Level) override;
  STDMETHODIMP GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level,
                                 IDirect3DSurface8** ppCubeMapSurface) override;
  using IDirect3DCubeTexture8::AddDirtyRect;

 private:
  // The most up-to-date texture contents.
  std::unique_ptr<char[]> data_;
};

// A little twist on GpuTexture to allow dynamic mapping. Not as complicated as
// dynamic buffers (because it only allows discard writes).
class DynamicTexture : public GpuTexture {
 public:
  DynamicTexture(Device* device, TextureKind kind, Dx8::Usage usage,
                 const D3D12_RESOURCE_DESC& resource_desc);

  virtual HRESULT STDMETHODCALLTYPE LockRect(UINT Level,
                                             D3DLOCKED_RECT* pLockedRect,
                                             CONST RECT* pRect,
                                             DWORD Flags) override;
  virtual HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override;

 private:
  bool is_locked_ = false;
};

}  // namespace Dx8to12