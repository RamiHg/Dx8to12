#include "dynamic_ring_buffer.h"

#include <d3d12.h>

#include "aixlog.hpp"
#include "device.h"

namespace Dx8to12 {

static constexpr D3D12_HEAP_PROPERTIES kHeapProps = {
    .Type = D3D12_HEAP_TYPE_CUSTOM,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_L0};

DynamicRingBuffer::DynamicRingBuffer(ID3D12Device *device, size_t size)
    : max_size_((size + D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1) &
                ~(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1)),
      head_(0),
      tail_(0) {
  // Align size up to alignment.
  D3D12_RESOURCE_DESC desc{
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      .Width = static_cast<UINT64>(max_size_),
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_UNKNOWN,
      .SampleDesc = {.Count = 1, .Quality = 0},
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
      .Flags = D3D12_RESOURCE_FLAG_NONE};
  ASSERT_HR(device->CreateCommittedResource(
      &kHeapProps, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &desc,
      D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(buffer_.GetForInit())));
  buffer_->SetName(L"DynamicRingBuffer");
  gpu_ptr_ = buffer_->GetGPUVirtualAddress();
  // Map the buffer forever.
  D3D12_RANGE no_reads = {};
  ASSERT_HR(buffer_->Map(0, &no_reads, reinterpret_cast<void **>(&cpu_ptr_)));
}

DynamicRingBuffer::~DynamicRingBuffer() {
  // Unmap the buffer.
  buffer_->Unmap(0, nullptr);
}

DynamicRingBuffer::Allocation DynamicRingBuffer::Allocate(size_t num_bytes,
                                                          uint32_t align) {
  ASSERT(head_ <= max_size_ && tail_ <= max_size_);
  ASSERT(IsPow2(align));
  align = std::max(align, min_align_);
  // LOG(TRACE) << "Allocating " << std::dec << num_bytes << " bytes. At "
  //  << std::dec << offset_ / 1024 << "kB.\n";
  bool is_oom = false;
  Allocation alloc{.frame = current_frame_, .size = (int)num_bytes};
  if (tail_ >= head_) {
    if (AlignUp(tail_, align) + num_bytes <= max_size_) {
      alloc.offset = AlignUp((int)tail_, align);
      tail_ = AlignUp((int)tail_, align) + num_bytes;
    } else if (head_ >= num_bytes) {
      alloc.offset = 0;
      tail_ = num_bytes;
    } else {
      is_oom = true;
    }
  } else if (AlignUp(tail_, align) + num_bytes <= head_) {
    alloc.offset = AlignUp((int)tail_, align);
    tail_ = AlignUp((int)tail_, align) + num_bytes;
  } else {
    is_oom = true;
  }
  if (is_oom) {
    FAIL("OOM: Could not allocate %zu bytes.", num_bytes);
  }
  ASSERT(alloc.offset + alloc.size <= (int)max_size_);
  return alloc;
}

char *DynamicRingBuffer::GetCpuPtrFor(Allocation alloc) {
  ASSERT(alloc.frame == current_frame_);
  ASSERT(alloc.offset + alloc.size <= (int)max_size_);
  return (char *)cpu_ptr_ + alloc.offset;
}

GpuPtr DynamicRingBuffer::GetGpuPtrFor(Allocation alloc) {
  ASSERT(alloc.frame == current_frame_);
  ASSERT(alloc.offset + alloc.size <= (int)max_size_);
  return gpu_ptr_.WithOffset(alloc.offset);
}

void DynamicRingBuffer::SetCurrentFrame(uint64_t frame) {
  if (frame > current_frame_) {
    current_frame_ = frame;
    frame_heads_.push_back({current_frame_, tail_});
  }
}

void DynamicRingBuffer::HasCompletedFrame(uint64_t frame) {
  while (!frame_heads_.empty() && frame_heads_.front().first <= frame) {
    frame_heads_.pop_front();
  }
  if (!frame_heads_.empty()) {
    head_ = frame_heads_.front().second;
  } else {
    head_ = 0;
    tail_ = 0;
  }
}

}  // namespace Dx8to12