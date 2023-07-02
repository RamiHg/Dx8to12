#pragma once

#include <cstdint>

#include "d3d8.h"
#include "util.h"

namespace Dx8to12 {
class Device;
class GpuTexture;
class CpuTexture;

enum class SurfaceKind {
  Gpu,
  Cpu,
  Backbuffer,
};

class BaseSurface : public IDirect3DSurface8, public RefCounted {
 public:
  SurfaceKind kind() const { return kind_; }

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

  /*** IDirect3DSurface8 methods ***/
  HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* pDesc) override;

  virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** ppDevice)
      VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE
  SetPrivateData(REFGUID refguid, CONST void* pData, DWORD SizeOfData,
                 DWORD Flags) VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE GetPrivateData(
      REFGUID refguid, void* pData, DWORD* pSizeOfData) VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid)
      VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE
  GetContainer(REFIID riid, void** ppContainer) VIRT_NOT_IMPLEMENTED;

  virtual HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* pLockedRect,
                                             CONST RECT* pRect,
                                             DWORD Flags) VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE UnlockRect(THIS) VIRT_NOT_IMPLEMENTED;

 protected:
  BaseSurface(SurfaceKind kind) : kind_(kind) {}

  SurfaceKind kind_;
  D3DSURFACE_DESC desc_;
};

class GpuSurface : public BaseSurface {
 public:
  GpuSurface(Device* device, GpuTexture* texture, uint32_t subresource);

  GpuTexture* texture() { return texture_.get(); }
  uint32_t subresource() const { return subresource_; }

 private:
  Device* device_;  // texture_ already holds device ref.

  D3DSURFACE_DESC desc_;
  ComPtr<GpuTexture> texture_;
  uint32_t subresource_;
};

class CpuSurface : public BaseSurface {
 public:
  char* GetPtr() const { return data_ptr_; }

  const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint() const {
    return footprint_;
  }

  int compact_pitch() const { return compact_pitch_; }

 private:
  CpuSurface(CpuTexture* texture, int level,
             const D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint,
             int compact_pitch, char* data_ptr);

  ComPtr<CpuTexture> texture_;

  D3DSURFACE_DESC desc_;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_;
  int compact_pitch_;
  char* data_ptr_;

  friend class CpuTexture;
};

class BackbufferSurface : public BaseSurface {
 public:
  BackbufferSurface(int index, const D3D12_RESOURCE_DESC& desc);

  int index() const { return index_; }

 private:
  int index_;
};
}  // namespace Dx8to12