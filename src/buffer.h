#pragma once

#include <d3d12.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "d3d8.h"
#include "dynamic_ring_buffer.h"
#include "util.h"
#include "utils/dx_utils.h"
#include "utils/range_set.h"

namespace D3D12MA {
class Allocation;
}

namespace Dx8to12 {
class Device;

class Buffer : public IDirect3DVertexBuffer8,
               public IDirect3DIndexBuffer8,
               public RefCounted {
 public:
  Buffer() : resource_desc_({}), fvf_(0), d3d8_pool_(D3DPOOL_DEFAULT) {}

  void InitAsVertexBuffer(Device* device, size_t size_in_bytes,
                          Dx8::Usage usage, D3DPOOL pool, DWORD fvf);
  void InitAsIndexBuffer(Device* device, size_t size_in_bytes, Dx8::Usage usage,
                         D3DFORMAT d3d8_format, D3DPOOL pool);
  void InitAsBuffer(Device* device, size_t size_in_bytes, Dx8::Usage usage,
                    D3DPOOL pool);

  void AcquireDevice() {}
  void ReleaseDevice() {}

  bool IsDynamic() const { return usage_.Has(Dx8::Usage::Dynamic); }
  // Called at the end of a frame to persist any changes made to dynamic
  // buffers.
  virtual void PersistDynamicChanges();
  virtual GpuPtr GetGpuPtr();

  ID3D12Resource* resource();
  D3D12_RESOURCE_DESC resource_desc() const { return resource_desc_; }

  DXGI_FORMAT index_buffer_fmt() const {
    ASSERT(index_buffer_fmt_ != DXGI_FORMAT_UNKNOWN);
    return index_buffer_fmt_;
  }

#ifdef DX8TO12_ENABLE_VALIDATION
  const std::wstring& name() const { return name_; }
#endif

 public:
#undef PURE
#define PURE VIRT_NOT_IMPLEMENTED
  /*** IUnknown methods ***/
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                                   void** ppvObj) PURE;
  virtual ULONG STDMETHODCALLTYPE AddRef(THIS) override {
    return RefCounted::AddRef();
  }
  virtual ULONG STDMETHODCALLTYPE Release(THIS) override {
    return RefCounted::Release();
  }

  /*** IDirect3DResource8 methods ***/
  virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** ppDevice) PURE;
  virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid,
                                                   CONST void* pData,
                                                   DWORD SizeOfData,
                                                   DWORD Flags) PURE;
  virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData,
                                                   DWORD* pSizeOfData) PURE;
  virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) PURE;
  virtual DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) PURE;
  virtual DWORD STDMETHODCALLTYPE GetPriority(THIS) PURE;
  virtual void STDMETHODCALLTYPE PreLoad(THIS) PURE;
  virtual D3DRESOURCETYPE STDMETHODCALLTYPE GetType(THIS) PURE;

  virtual HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock,
                                         BYTE** ppbData, DWORD Flags) override;
  virtual HRESULT STDMETHODCALLTYPE Unlock(THIS) override;
  virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* pDesc) PURE;
  virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* pDesc) PURE;

 protected:
  Device* device_;
#ifdef USE_ALLOCATOR
  ComPtr<D3D12MA::Allocation> allocation_;
#else
  ComPtr<ID3D12Resource> resource_;
#endif
  D3D12_RESOURCE_DESC resource_desc_;
  DWORD fvf_ = 0;
  D3DPOOL d3d8_pool_ = D3DPOOL_DEFAULT;
  Dx8::Usage usage_;
  DXGI_FORMAT index_buffer_fmt_ = DXGI_FORMAT_UNKNOWN;
  int size_ = 0;

#ifdef DX8TO12_ENABLE_VALIDATION
  std::wstring name_;
#endif
};

class DynamicBuffer : public Buffer {
 public:
  DynamicBuffer() : Buffer() {}

  HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock,
                                 BYTE** ppbData, DWORD Flags) noexcept override;
  HRESULT STDMETHODCALLTYPE Unlock() noexcept override;

  GpuPtr GetGpuPtr() override;

  void PersistDynamicChanges() override;

 private:
  void PersistSpeculativeWrite(int alloc_size);
  void UpdateCbvForRingBuffer(int offset, int size);

  // We store the last dynamic write that the user has done.
  std::vector<char> speculative_write_cache_;
  bool is_speculative_write_persisted_ = false;

  DynamicRingBuffer::Allocation current_ring_alloc_ = {};
  D3D12_CPU_DESCRIPTOR_HANDLE prev_csv_handle_ = {};
  uint64_t prev_lock_frame_ = 0;

  RangeSet written_ranges_;
  // static constexpr bool use_cbv_ = false;
};

#undef PURE
#define PURE = 0

}  // namespace Dx8to12