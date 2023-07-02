#pragma once
// Helper classes to work with D3D12 and D3D8 values.

#include <d3d12.h>
#include <dxgi.h>

#include <cstdint>

#include "SimpleMath.h"
#include "d3d8.h"
#include "util.h"
#include "utils/murmur_hash.h"

namespace Dx8to12 {
struct TextureStageState;

D3DFORMAT DXGIToD3DFormat(DXGI_FORMAT dxgi_format);
DXGI_FORMAT DXGIFromD3DFormat(D3DFORMAT d3d_format);
int DXGIFormatSize(DXGI_FORMAT format);

struct GpuPtr {
 public:
  GpuPtr() : ptr(0) {}
  GpuPtr(int64_t value) : ptr(value) {}
  operator D3D12_GPU_VIRTUAL_ADDRESS() const {
    return static_cast<uint64_t>(ptr);
  }

  GpuPtr WithOffset(int offset) const { return GpuPtr(ptr + offset); }

  int64_t ptr;
};

inline D3D12_RESOURCE_BARRIER CreateBufferTransition(ID3D12Resource *resource,
                                                     D3D12_RESOURCE_STATES from,
                                                     D3D12_RESOURCE_STATES to) {
  return {.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
          .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
          .Transition = {.pResource = resource,
                         .Subresource = 0,
                         .StateBefore = from,
                         .StateAfter = to}};
}

class SamplerDesc : public D3D12_SAMPLER_DESC {
 public:
  SamplerDesc(const TextureStageState &ts);

  using D3D12_SAMPLER_DESC::operator=;

  bool operator==(const SamplerDesc &) const;
};

class ScopedGpuMarker {
 public:
  ScopedGpuMarker(ID3D12GraphicsCommandList *cmd_list, const char *annotation);
  ~ScopedGpuMarker();

 private:
  ID3D12GraphicsCommandList *cmd_list_;
};

inline DirectX::SimpleMath::Matrix MatrixFromD3D(const D3DMATRIX &m) {
  DirectX::SimpleMath::Matrix result;
  memcpy(&result, &m, sizeof(m));
  return result;
}

inline DirectX::SimpleMath::Vector3 VectorFromD3D(const D3DVECTOR &v) {
  return DirectX::SimpleMath::Vector3(v.x, v.y, v.z);
}
}  // namespace Dx8to12

namespace Dx8 {
class Usage {
 public:
  enum Value : uint32_t {
    Default = 0,
    RenderTarget = D3DUSAGE_RENDERTARGET,
    DepthStencil = D3DUSAGE_DEPTHSTENCIL,
    WriteOnly = D3DUSAGE_WRITEONLY,
    SoftwareProcessing = D3DUSAGE_SOFTWAREPROCESSING,
    Dynamic = D3DUSAGE_DYNAMIC,
  };

  Usage() : value_(Value::Default) {}
  Usage(DWORD value) : value_(static_cast<Value>(value)) {
    ASSERT((value & ~kAllMask) == 0);
  }
  Usage(Value value) : value_(value) { ASSERT((value & ~kAllMask) == 0); }

  bool Has(Value to_check) const { return (value_ & to_check) == to_check; }

  operator DWORD() const { return value_; }

 private:
  static constexpr uint32_t kAllMask = RenderTarget | DepthStencil | WriteOnly |
                                       SoftwareProcessing | D3DUSAGE_DONOTCLIP |
                                       D3DUSAGE_POINTS | D3DUSAGE_RTPATCHES |
                                       D3DUSAGE_NPATCHES | Dynamic;

  Value value_;
};

struct Color {
  explicit Color(D3DCOLOR unorm) : combined(unorm) {}
  explicit Color(D3DCOLORVALUE fp) {
    c.b = static_cast<uint8_t>(fp.b * 255.f);
    c.g = static_cast<uint8_t>(fp.g * 255.f);
    c.r = static_cast<uint8_t>(fp.r * 255.f);
    c.a = static_cast<uint8_t>(fp.a * 255.f);
  }

  D3DCOLORVALUE ToValue() const {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
  }

  union {
    uint32_t combined;
    struct {
      uint8_t b, g, r, a;
    } c;
  };
};

[[maybe_unused]] static const char *LockFlagToString(DWORD flags) {
  using ::Dx8to12::HasFlag;
  if (HasFlag(flags, D3DLOCK_NOOVERWRITE) && HasFlag(flags, D3DLOCK_DISCARD))
    return "D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE";
  else if (HasFlag(flags, D3DLOCK_NOOVERWRITE))
    return "D3DLOCK_NOOVERWRITE";
  else if (HasFlag(flags, D3DLOCK_DISCARD))
    return "D3DLOCK_DISCARD";
  else
    return "";
}
}  // namespace Dx8

namespace std {
template <>
struct hash<Dx8to12::SamplerDesc> {
  size_t operator()(Dx8to12::SamplerDesc const &desc) const {
    return Dx8to12::MurmurHashTo32(&desc, sizeof(desc));
  }
};
}  // namespace std
