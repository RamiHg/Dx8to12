#pragma once

#include <d3d12.h>

#include <climits>
#include <cstdint>
#include <deque>

#include "util.h"
#include "utils/dx_utils.h"

interface ID3D12Device;
interface ID3D12Resource;

namespace Dx8to12 {
class Device;

// Simple ring buffer that needs to be reset each frame.
class DynamicRingBuffer {
 public:
  struct Allocation {
    uint64_t frame;
    int offset;
    int size;
  };

  // Initializes current_frame to 1.
  DynamicRingBuffer(ID3D12Device* device, size_t size);
  ~DynamicRingBuffer();

  void SetCurrentFrame(uint64_t frame);
  void HasCompletedFrame(uint64_t frame);

  Allocation Allocate(
      size_t num_bytes,
      uint32_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
  char* GetCpuPtrFor(Allocation offset);
  GpuPtr GetGpuPtrFor(Allocation offset);

  ID3D12Resource* GetBackingResource() { return buffer_.get(); }

 private:
  ComPtr<ID3D12Resource> buffer_;
  char* cpu_ptr_;
  GpuPtr gpu_ptr_;

  const size_t max_size_;
  size_t head_;
  size_t tail_;

  std::deque<std::pair<uint64_t, size_t>> frame_heads_;
  uint64_t current_frame_ = 0;

  const uint32_t min_align_ = 256;
};
}  // namespace Dx8to12
