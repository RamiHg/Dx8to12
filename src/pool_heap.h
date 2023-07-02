#pragma once

#include <d3d12.h>

#include <cstdint>
#include <vector>

#include "util.h"

namespace Dx8to12 {
class Device;

class DescriptorPoolHeap {
 public:
  // Initializing not really needed since heap_ is null.
  DescriptorPoolHeap()
      : cpu_start_({}), gpu_start_({}), increment_(0), num_descriptors_(0) {}

  DescriptorPoolHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
                     int num_descriptors);

  D3D12_CPU_DESCRIPTOR_HANDLE Allocate();
  void Free(D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void FreeAll();

  D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandleFor(
      D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle) const;

  ID3D12DescriptorHeap* heap() { return heap_.get(); }

 private:
  ComPtr<ID3D12DescriptorHeap> heap_;
  std::vector<intptr_t> free_list_;

  D3D12_CPU_DESCRIPTOR_HANDLE cpu_start_ = {};
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_start_ = {};
  int increment_;
  int num_descriptors_;
};
}  // namespace Dx8to12
