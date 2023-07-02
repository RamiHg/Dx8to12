#include "buffer.h"

#include <sstream>

#include "aixlog.hpp"
#include "device.h"
#include "dynamic_ring_buffer.h"
#include "util.h"

#ifdef USE_ALLOCATOR
#include "D3D12MemAlloc.h"
#endif

namespace Dx8to12 {

static AixLog::Severity kLog = AixLog::Severity::trace;

void Buffer::InitAsBuffer(Device* device, size_t size_in_bytes,
                          Dx8::Usage usage, D3DPOOL pool) {
  ASSERT(pool != D3DPOOL_SCRATCH);
  size_in_bytes = AlignUp(size_in_bytes, 256);
  resource_desc_ = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                    .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                    .Width = static_cast<UINT64>(size_in_bytes),
                    .Height = 1,
                    .DepthOrArraySize = 1,
                    .MipLevels = 1,
                    .Format = DXGI_FORMAT_UNKNOWN,
                    .SampleDesc = {.Count = 1, .Quality = 0},
                    .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                    .Flags = D3D12_RESOURCE_FLAG_NONE};
  d3d8_pool_ = pool;
  usage_ = usage;
  device_ = device;
  size_ = safe_cast<int>(size_in_bytes);
#ifdef DX8TO12_USE_ALLOCATOR
  D3D12MA::ALLOCATION_DESC alloc_desc{.HeapType = D3D12_HEAP_TYPE_UPLOAD};
  ASSERT_HR(device->allocator()->CreateResource(
      &alloc_desc, &resource_desc_, D3D12_RESOURCE_STATE_COMMON, nullptr,
      allocation_.GetForInit(), IID_NULL, nullptr));
#else
  // TODO: Actually put the buffers in GPU mem..
  D3D12_HEAP_PROPERTIES heap_props = kSystemMemHeapProps;
  ASSERT_HR(device->device()->CreateCommittedResource(
      &heap_props, D3D12_HEAP_FLAG_NONE, &resource_desc_,
      D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(resource_.GetForInit())));
#endif

  // wchar_t name[128];
  // _snwprintf(name, 128, L"addr:%p", this);
  // if (IsDynamic()) resource_->SetName(name);
}

void Buffer::InitAsVertexBuffer(Device* device, size_t size_in_bytes,
                                Dx8::Usage usage, D3DPOOL pool, DWORD fvf) {
  fvf_ = fvf;
  InitAsBuffer(device, size_in_bytes, usage, pool);
#ifdef DX8TO12_ENABLE_VALIDATION
  static int name_index = 0;
  std::wstringstream name;
  name << "VBuffer" << std::dec << name_index++ << ":" << std::hex << fvf;
  name_ = name.str();
  resource_->SetName(name_.c_str());
#endif
}

void Buffer::InitAsIndexBuffer(Device* device, size_t size_in_bytes,
                               Dx8::Usage usage, D3DFORMAT format,
                               D3DPOOL pool) {
  InitAsBuffer(device, size_in_bytes, usage, pool);
  index_buffer_fmt_ = DXGIFromD3DFormat(format);
}

ID3D12Resource* Buffer::resource() {
#ifdef DX8TO12_USE_ALLOCATOR
  return allocation_->GetResource();
#else
  return resource_.get();
#endif
}

// BIG TODO: Persist dynamic buffers at the end of the frame in case they are
// read the next frame.
HRESULT STDMETHODCALLTYPE Buffer::Lock(UINT OffsetToLock, UINT SizeToLock,
                                       BYTE** ppbData, DWORD Flags) {
  ASSERT(OffsetToLock <= INT32_MAX);
  ASSERT(SizeToLock <= INT32_MAX);
  ASSERT(!HasFlag(Flags, D3DLOCK_DISCARD));
  ASSERT((int)SizeToLock <= size_);

  LOG(kLog) << "Going into static lock.\n";

  if (SizeToLock == 0) SizeToLock = size_;
  D3D12_RANGE range{.Begin = OffsetToLock,
                    .End = OffsetToLock +
                           SizeToLock};  // TODO: Don't do if we're not reading.
  ASSERT_HR(resource()->Map(0, &range, reinterpret_cast<void**>(ppbData)));
  *ppbData += OffsetToLock;

  return S_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Unlock() {
  resource()->Unmap(0, nullptr);
  return S_OK;
}

void Buffer::PersistDynamicChanges() {
  FAIL("Unexpected dynamic change persist in static buffer.");
}

GpuPtr Buffer::GetGpuPtr() { return resource()->GetGPUVirtualAddress(); }

// BIG TODO: Persist dynamic buffers at the end of the frame in case they are
// read the next frame.
HRESULT STDMETHODCALLTYPE DynamicBuffer::Lock(UINT OffsetToLock,
                                              UINT SizeToLock, BYTE** ppbData,
                                              DWORD Flags) noexcept {
  ASSERT(OffsetToLock <= INT32_MAX);
  ASSERT(SizeToLock <= INT32_MAX);

  if (SizeToLock == 0) SizeToLock = size_;

  const int offset = safe_cast<int>(OffsetToLock);
  int size_to_lock = safe_cast<int>(SizeToLock);

  // LOG(kLog) << "(Dynamic) Locking " << std::hex << this << " ("
  //           << Dx8::LockFlagToString(Flags) << ") offset " << OffsetToLock
  //           << " size " << size_to_lock << ". Buffer size " << std::dec <<
  //           size_
  //           << "\n";

  const bool is_discard = HasFlag(Flags, D3DLOCK_DISCARD);
  const bool is_nooverwrite = HasFlag(Flags, D3DLOCK_NOOVERWRITE);
  // const bool is_entire_buffer = size_to_lock == size_;

  if (is_nooverwrite && prev_lock_frame_ < device_->CurrentFrame()) {
    return Buffer::Lock(OffsetToLock, SizeToLock, ppbData, Flags);
  }

  // We're modifying the contents of the buffer. We have to persist the last
  // modification.
  device_->MarkBufferForPersist(this);

  prev_lock_frame_ = device_->CurrentFrame();
  if (is_discard) {
    ASSERT(offset == 0);
    if (!speculative_write_cache_.empty()) {
      // This was either not used, or already persisted by a call to GetGpuPtr.
      speculative_write_cache_.clear();
    }
    current_ring_alloc_ = {};
    // Speculatively cache this write.
    speculative_write_cache_.resize(size_to_lock);
    is_speculative_write_persisted_ = false;
    // But save a spot in the CSV heap for it.
    *ppbData = reinterpret_cast<BYTE*>(speculative_write_cache_.data());

    written_ranges_.ranges.clear();
    written_ranges_.insert({offset, size_to_lock});
  } else {
    ASSERT(is_nooverwrite);
    ASSERT(prev_lock_frame_ == device_->CurrentFrame());
    // This is a no overwrite.
    if (!speculative_write_cache_.empty()) {
      // Previous value was a discard. We now know what we're appending data. So
      // allocate the entire buffer size and copy the previous value.
      PersistSpeculativeWrite(size_);
      speculative_write_cache_.clear();
    }
    char* dest =
        device_->dynamic_ring_buffer()->GetCpuPtrFor(current_ring_alloc_) +
        offset;
    *ppbData = reinterpret_cast<BYTE*>(dest);
    written_ranges_.insert({offset, size_to_lock});
  }

  return S_OK;
}

HRESULT STDMETHODCALLTYPE DynamicBuffer::Unlock() noexcept {
  if (prev_lock_frame_ < device_->CurrentFrame()) return Buffer::Unlock();
  return S_OK;
}

void DynamicBuffer::PersistSpeculativeWrite(int alloc_size) {
  // if (use_cbv_) alloc_size = AlignUp(alloc_size, 256);
  current_ring_alloc_ = device_->dynamic_ring_buffer()->Allocate(alloc_size);
  char* dest =
      device_->dynamic_ring_buffer()->GetCpuPtrFor(current_ring_alloc_);
  memcpy(dest, speculative_write_cache_.data(),
         speculative_write_cache_.size());
  prev_lock_frame_ = device_->CurrentFrame();

  is_speculative_write_persisted_ = true;
}

GpuPtr DynamicBuffer::GetGpuPtr() {
  if (!is_speculative_write_persisted_ && !speculative_write_cache_.empty()) {
    // Persist the speculative write.
    PersistSpeculativeWrite(speculative_write_cache_.size());
  } else if (prev_lock_frame_ < device_->CurrentFrame()) {
    LOG(kLog) << "Using backing buffer for " << std::hex << this << ".\n";
    return Buffer::GetGpuPtr();  // Boooo.
  }

  return device_->dynamic_ring_buffer()->GetGpuPtrFor(current_ring_alloc_);
}

void DynamicBuffer::PersistDynamicChanges() {
  LOG(kLog) << "Persisting changes for " << std::hex << this << "\n";
  // Make sure any speculative writes are committed.
  GetGpuPtr();
  ASSERT(current_ring_alloc_.frame == device_->CurrentFrame());
  ASSERT(current_ring_alloc_.size > 0);
  ASSERT(written_ranges_.ranges.size() == 1);
  for (auto [offset, size] : written_ranges_.ranges) {
    device_->CopyBuffer(resource(), 0,
                        device_->dynamic_ring_buffer()->GetBackingResource(),
                        current_ring_alloc_.offset + offset, size);
  }
  written_ranges_.ranges.clear();
  current_ring_alloc_ = {};
}
}  // namespace Dx8to12