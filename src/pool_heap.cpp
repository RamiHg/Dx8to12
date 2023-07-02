#include "pool_heap.h"

#include <cstdint>

namespace Dx8to12 {
DescriptorPoolHeap::DescriptorPoolHeap(ID3D12Device *device,
                                       D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
                                       int num_descriptors) {
  D3D12_DESCRIPTOR_HEAP_DESC desc{
      .Type = heap_type,
      .NumDescriptors = static_cast<UINT>(num_descriptors),
      .Flags = heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
                       heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
                   ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                   : D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
  ASSERT_HR(
      device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap_.GetForInit())));
  cpu_start_ = heap_->GetCPUDescriptorHandleForHeapStart();
  if (HasFlag(desc.Flags, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
    gpu_start_ = heap_->GetGPUDescriptorHandleForHeapStart();
  increment_ =
      static_cast<int>(device->GetDescriptorHandleIncrementSize(heap_type));
  num_descriptors_ = num_descriptors;

  FreeAll();
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorPoolHeap::Allocate() {
  ASSERT(heap_);
  ASSERT(!free_list_.empty());
  if (free_list_.empty()) return {};
  int back = free_list_.back();
  free_list_.pop_back();
  return {.ptr = static_cast<size_t>(back)};
}

void DescriptorPoolHeap::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  const ptrdiff_t diff = static_cast<ptrdiff_t>(handle.ptr - cpu_start_.ptr);
  ASSERT(diff >= 0 && diff < num_descriptors_ * increment_);
  free_list_.push_back(handle.ptr);
}

void DescriptorPoolHeap::FreeAll() {
  free_list_.clear();
  for (int i = num_descriptors_ - 1; i >= 0; --i) {
    free_list_.push_back(cpu_start_.ptr + i * increment_);
  }
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorPoolHeap::GetGPUHandleFor(
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle) const {
  ptrdiff_t diff = static_cast<ptrdiff_t>(cpu_handle.ptr) -
                   static_cast<ptrdiff_t>(cpu_start_.ptr);
  ASSERT(diff >= 0);
  ASSERT((diff % increment_) == 0);
  int index = static_cast<int>(diff / increment_);
  // TODO: Just check for < num_descriptors_ * increment_.
  ASSERT(index < num_descriptors_);
  return {.ptr = gpu_start_.ptr + index * increment_};
};

}  // namespace Dx8to12