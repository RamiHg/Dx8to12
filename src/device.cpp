#include "device.h"

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <sstream>
#include <utility>

#include "SimpleMath.h"
#include "aixlog.hpp"
#include "buffer.h"
#include "dynamic_ring_buffer.h"
#include "shader_parser.h"
#include "surface.h"
#include "texture.h"
#include "utils/dx_utils.h"
#include "vertex_shader.h"

#ifdef DX8TO12_USE_ALLOCATOR
#include "D3D12MemAlloc.h"
#endif

#undef D3DERR_INVALIDCALL
#define D3DERR_INVALIDCALL            \
  []() {                              \
    LOG_ERROR() << "Invalid call!\n"; \
    return MAKE_D3DHRESULT(2156);     \
  }()

#define SCOPED_MARKER(annotation) ScopedGpuMarker(cmd_list_.Get(), annotation)

namespace Dx8to12 {

// static_assert(sizeof(void *) == 4, "Does not support 64-bit.");

Device::DirtyFlags &operator|=(Device::DirtyFlags &a, Device::DirtyFlags b) {
  a = static_cast<Device::DirtyFlags>(static_cast<uint32_t>(a) |
                                      static_cast<uint32_t>(b));
  return a;
}

Device::DirtyFlags &operator^=(Device::DirtyFlags &a, Device::DirtyFlags b) {
  a = static_cast<Device::DirtyFlags>(static_cast<uint32_t>(a) ^
                                      static_cast<uint32_t>(b));
  return a;
}

Device::Device(IDirect3D8 *direct3d8)
    : ref_count_(1), direct3d8_(ComWrap(direct3d8)) {
  // Set some default state for the first texture stage.
  texture_stage_states_[0].color_op = D3DTOP_MODULATE;
  texture_stage_states_[0].alpha_op = D3DTOP_SELECTARG1;
  for (size_t i = 0; i < texture_stage_states_.size(); ++i) {
    texture_stage_states_[i].texcoord_index = static_cast<DWORD>(i);
  }
}

HRESULT STDMETHODCALLTYPE Device::QueryInterface(REFIID riid, void **ppvObj) {
  if (ppvObj == nullptr)
    return E_POINTER;
  else if (riid == IID_IDirect3DDevice8 || riid == __uuidof(IUnknown)) {
    AddRef();
    *ppvObj = static_cast<IDirect3DDevice8 *>(this);
    return S_OK;
  } else {
    FAIL("Invalid Device::QueryInterface.");
    // return E_NOINTERFACE;
  }
}

static void __stdcall DebugInfoQueueMessageCallback(
    D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity,
    D3D12_MESSAGE_ID id, LPCSTR pDescription, void *pContext) {
  ASSERT(pDescription);
  AixLog::Severity log_severity;
  switch (severity) {
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
      log_severity = AixLog::Severity::debug;
      break;
    case D3D12_MESSAGE_SEVERITY_INFO:
      log_severity = AixLog::Severity::info;
      break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
      log_severity = AixLog::Severity::warning;
      break;
    case D3D12_MESSAGE_SEVERITY_ERROR:
      log_severity = AixLog::Severity::error;
      break;
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
      log_severity = AixLog::Severity::fatal;
      break;
  }
  OutputDebugStringA(pDescription);
  LOG(log_severity) << pDescription << "\n";
  if (severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
    FAIL("D3D12 Error:\r\n%s", pDescription);
  }
}

bool Device::Create(HWND window, ComPtr<IDXGIFactory2> factory,
                    ComPtr<IDXGIAdapter> adapter, int adapter_index,
                    const D3DPRESENT_PARAMETERS &presentParams) {
  window_ = window;
  dxgi_factory_ = std::move(factory);

  LOG(INFO) << "Creating device.\n";
#ifdef DX8TO12_ENABLE_VALIDATION
  ID3D12Debug *debug_iface = nullptr;
  ASSERT_HR(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_iface)));
  ASSERT_HR(
      debug_iface->QueryInterface(IID_PPV_ARGS(debug_interface_.GetForInit())));
  debug_iface->Release();
  debug_interface_->EnableDebugLayer();
  // debug_interface_->SetEnableSynchronizedCommandQueueValidation(TRUE);
  debug_interface_->SetEnableGPUBasedValidation(TRUE);
  // debug_interface_->SetEnableAutoName(TRUE);
#endif

  adapter_ = std::move(adapter);
  adapter_index_ = adapter_index;
  ASSERT(adapter_);
  if (HRESULT hr = D3D12CreateDevice(adapter_.get(), D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(d3d12_device_.GetForInit()));
      hr != S_OK) {
    FAIL("Failed to create device: %d", hr);
    return false;
  }
  // TODO: Pass in adapter output.
  // ASSERT_HR(adapter_->EnumOutputs(0, adapter_output_.GetForInit()));

// Create info queue.
#ifdef DX8TO12_ENABLE_VALIDATION
  if (SUCCEEDED(d3d12_device_->QueryInterface(
          IID_PPV_ARGS(info_queue_.GetForInit()))))
    info_queue_->RegisterMessageCallback(DebugInfoQueueMessageCallback,
                                         D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                         nullptr, &info_queue_cookie_);
#endif

  // D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12;
  // ASSERT_HR(d3d12_device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,
  //                                              &options12,
  //                                              sizeof(options12)));
  // ASSERT(options12.EnhancedBarriersSupported);

  ASSERT_HR(Init(presentParams));
  return true;
}

HRESULT Device::Init(const D3DPRESENT_PARAMETERS &presentParams) {
  fence_values_ = {};
  next_fence_ = 1;

  srv_heap_ = DescriptorPoolHeap(
      d3d12_device_.get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxNumSrvs);
  rtv_heap_ = DescriptorPoolHeap(d3d12_device_.get(),
                                 D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kMaxNumRtvs);
  dsv_heap_ = DescriptorPoolHeap(d3d12_device_.get(),
                                 D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kMaxNumRtvs);
  sampler_heap_ =
      DescriptorPoolHeap(d3d12_device_.get(),
                         D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kMaxSamplerStates);

  dynamic_ring_buffer_ = std::make_unique<DynamicRingBuffer>(
      d3d12_device_.get(), kDynamicRingBufferSize);

  dynamic_ring_buffer_->SetCurrentFrame(CurrentFrame());

#ifdef DX8TO12_USE_ALLOCATOR
  {
    D3D12MA::ALLOCATOR_DESC desc{.pDevice = d3d12_device_.get(),
                                 .PreferredBlockSize = 2 * 1024 * 1024,
                                 .pAdapter = adapter_.get()};
    ASSERT_HR(D3D12MA::CreateAllocator(&desc, allocator_.GetForInit()));
  }
#endif

  if (presentParams.EnableAutoDepthStencil) {
    LOG(INFO) << "Auto depth stencil.\n";
    D3DFORMAT depth_format = presentParams.AutoDepthStencilFormat;
    if (depth_format == D3DFMT_UNKNOWN) depth_format = D3DFMT_D32;
    ASSERT(depth_format == D3DFMT_D16 || depth_format == D3DFMT_D32);
    depth_stencil_tex_ = ComOwn(static_cast<GpuTexture *>(BaseTexture::Create(
        this, TextureKind::Texture2d, presentParams.BackBufferWidth,
        presentParams.BackBufferHeight, 1, 1, D3DUSAGE_DEPTHSTENCIL,
        depth_format, D3DPOOL_DEFAULT)));
  }

  viewport_.Width = static_cast<float>(presentParams.BackBufferWidth);
  viewport_.Height = static_cast<float>(presentParams.BackBufferHeight);

  caps_ = GetDefaultCaps(static_cast<UINT>(adapter_index_));

  // Create command queue.
  D3D12_COMMAND_QUEUE_DESC cmd_queue_desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
      .NodeMask = 0};
  ASSERT_HR(d3d12_device_->CreateCommandQueue(
      &cmd_queue_desc, IID_PPV_ARGS(cmd_queue_.GetForInit())));
  for (auto &allocator : cmd_allocators_) {
    ASSERT_HR(d3d12_device_->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetForInit())));
  }
  ASSERT_HR(d3d12_device_->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_allocators_[0].get(), nullptr,
      IID_PPV_ARGS(cmd_list_.GetForInit())));
  dirty_flags_ ^= DIRTY_FLAG_CMD_LIST_CLOSED;
  ASSERT_HR(d3d12_device_->CreateFence(
      0, D3D12_FENCE_FLAG_NONE,
      IID_PPV_ARGS(cmd_list_done_fence_.GetForInit())));
  cmd_list_done_event_handle_ =
      CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  ASSERT(cmd_list_done_event_handle_ != INVALID_HANDLE_VALUE);

  // Create the swap chain.
  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc{
      .Width = presentParams.BackBufferWidth,
      .Height = presentParams.BackBufferHeight,
      .Format = DXGIFromD3DFormat(presentParams.BackBufferFormat),
      .SampleDesc = {.Count = 1, .Quality = 0},
      .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
      .BufferCount = kNumBackBuffers,
      .Scaling = DXGI_SCALING_NONE,
      .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
  };
  // Don't crash if creating the swap chain fails. This might happen during
  // device reset.
  ComPtr<IDXGISwapChain1> swap_chain1;
  HR_OR_RETURN(dxgi_factory_->CreateSwapChainForHwnd(
      cmd_queue_.get(), window_, &swap_chain_desc, nullptr, nullptr,
      swap_chain1.GetForInit()));
  ASSERT_HR(swap_chain1->QueryInterface(swap_chain_.GetForInit()));

  current_back_buffer_ = swap_chain_->GetCurrentBackBufferIndex();

  // Create the back buffer.
  ASSERT(presentParams.BackBufferCount <= 1);
  ASSERT(back_buffers_.empty());
  for (uint32_t i = 0; i < swap_chain_desc.BufferCount; ++i) {
    ComPtr<ID3D12Resource> back_buffer_resource;
    ASSERT_HR(swap_chain_->GetBuffer(
        i, IID_PPV_ARGS(back_buffer_resource.GetForInit())));
    GpuTexture *back_buffer =
        GpuTexture::InitFromResource(this, back_buffer_resource);
    back_buffers_.push_back(ComOwn(back_buffer));
  }

  D3DPRESENT_PARAMETERS params = presentParams;
  ASSERT_HR(Reset(&params));

  InitRootSignatures();
  return S_OK;
}

Device::~Device() { WaitForFrame(next_fence_ - 1); }

HRESULT STDMETHODCALLTYPE
Device::Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) {
  TRACE_ENTRY(pPresentationParameters);
  if (!(dirty_flags_ & DIRTY_FLAG_CMD_LIST_CLOSED)) {
    LOG(INFO) << "Resetting device: Submitting commands..\n";
    SubmitAndWait(false);
    WaitForFrame(next_fence_ - 1);
    ASSERT_HR(cmd_list_->Close());
    dirty_flags_ |= DIRTY_FLAG_CMD_LIST_CLOSED;
  } else {
    LOG(INFO) << "Resetting device. Commands already submitted.\n";
  }
  bound_render_target_.Reset();
  bound_depth_target_.Reset();
  ASSERT(depth_stencil_tex_->total_ref_count() == 1);
  for (auto &rtv : back_buffers_) {
    ASSERT(rtv->total_ref_count() == 1);
  }
  back_buffers_.clear();
  depth_stencil_tex_.Reset();
  DXGI_FORMAT new_format =
      DXGIFromD3DFormat(pPresentationParameters->BackBufferFormat);

  DXGI_MODE_DESC mode_desc{.Width = pPresentationParameters->BackBufferWidth,
                           .Height = pPresentationParameters->BackBufferHeight,
                           .Format = new_format};

  ASSERT_HR(swap_chain_->ResizeTarget(&mode_desc));
  ASSERT_HR(swap_chain_->ResizeBuffers(
      2, pPresentationParameters->BackBufferWidth,
      pPresentationParameters->BackBufferHeight, new_format, 0));

  DXGI_SWAP_CHAIN_DESC swap_chain_desc;
  ASSERT_HR(swap_chain_->GetDesc(&swap_chain_desc));

  if (pPresentationParameters->EnableAutoDepthStencil) {
    D3DFORMAT depth_format = pPresentationParameters->AutoDepthStencilFormat;
    if (depth_format == D3DFMT_UNKNOWN) depth_format = D3DFMT_D32;
    ASSERT(depth_format == D3DFMT_D16 || depth_format == D3DFMT_D32);
    depth_stencil_tex_ = ComOwn(static_cast<GpuTexture *>(BaseTexture::Create(
        this, TextureKind::Texture2d, mode_desc.Width, mode_desc.Height, 1, 1,
        D3DUSAGE_DEPTHSTENCIL, depth_format, D3DPOOL_DEFAULT)));
    depth_stencil_tex_->SetName("depth_stencil_tex");
    bound_depth_target_ = InternalPtr(depth_stencil_tex_.Get());
  }

  ASSERT(back_buffers_.empty());
  for (uint32_t i = 0; i < swap_chain_desc.BufferCount; ++i) {
    ComPtr<ID3D12Resource> back_buffer_resource;
    ASSERT_HR(swap_chain_->GetBuffer(
        i, IID_PPV_ARGS(back_buffer_resource.GetForInit())));
    GpuTexture *back_buffer =
        GpuTexture::InitFromResource(this, back_buffer_resource);
    back_buffer->SetName(std::string("back_buffer_") + std::to_string(i));
    back_buffers_.push_back(ComOwn(back_buffer));
  }

  current_back_buffer_ = swap_chain_->GetCurrentBackBufferIndex();

  ASSERT_HR(cmd_allocators_[current_back_buffer_]->Reset());
  ASSERT_HR(
      cmd_list_->Reset(cmd_allocators_[current_back_buffer_].get(), nullptr));
  dirty_flags_ ^= DIRTY_FLAG_CMD_LIST_CLOSED;

  return S_OK;
}

D3DCAPS8 Device::GetDefaultCaps(UINT adapter_index) {
  D3DCAPS8 caps{
      .DeviceType = D3DDEVTYPE_HAL,
      .AdapterOrdinal = adapter_index,
      .Caps = 0,  // D3DCAPS_READ_SCANLINE or D3DCAPS_OVERLAY.
      .Caps2 = D3DCAPS2_CANRENDERWINDOWED | D3DCAPS2_CANMANAGERESOURCE |
               D3DCAPS2_DYNAMICTEXTURES,
      .Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD,
      .PresentationIntervals =
          D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE |
          D3DPRESENT_INTERVAL_TWO | D3DPRESENT_INTERVAL_THREE |
          D3DPRESENT_INTERVAL_FOUR,

      .CursorCaps = D3DCURSORCAPS_COLOR,

      .DevCaps =
          D3DDEVCAPS_EXECUTEVIDEOMEMORY | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
          D3DDEVCAPS_TLVERTEXVIDEOMEMORY | D3DDEVCAPS_TEXTURESYSTEMMEMORY |
          D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_DRAWPRIMTLVERTEX |
          D3DDEVCAPS_CANRENDERAFTERFLIP | D3DDEVCAPS_TEXTURENONLOCALVIDMEM |
          D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX |
          D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_CANBLTSYSTONONLOCAL |
          D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_PUREDEVICE,

      .PrimitiveMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE |
                           D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                           D3DPMISCCAPS_COLORWRITEENABLE |
                           D3DPMISCCAPS_CLIPPLANESCALEDPOINTS |
                           D3DPMISCCAPS_CLIPTLVERTS | D3DPMISCCAPS_BLENDOP,

      .RasterCaps = D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX |
                    D3DPRASTERCAPS_ANTIALIASEDGES |
                    D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ZBIAS |
                    D3DPRASTERCAPS_FOGRANGE | D3DPRASTERCAPS_ANISOTROPY |
                    D3DPRASTERCAPS_COLORPERSPECTIVE,

      .ZCmpCaps = 0xFF,
      .SrcBlendCaps = 0x1FFF,
      .DestBlendCaps = 0x1FFF,
      .AlphaCmpCaps = 0xFF,
      .ShadeCaps = 0xFFFFFFFF,
      .TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA |
                     D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_VOLUMEMAP |
                     D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_MIPVOLUMEMAP |
                     D3DPTEXTURECAPS_MIPCUBEMAP,
      .TextureFilterCaps =
          D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
          D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MIPFPOINT |
          D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
          D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC,
      // .CubeTextureFilterCaps =.VolumeTextureFilterCaps =.TextureFilterCaps,
      .TextureAddressCaps = 0xFF,
      .VolumeTextureAddressCaps = 0xFF,

      .LineCaps = 0,

      .MaxTextureWidth = 8182,
      .MaxTextureHeight = 8192,
      .MaxVolumeExtent = 2048,

      .MaxTextureRepeat = 128,
      .MaxTextureAspectRatio = 8192,
      .MaxAnisotropy = 16,
      .MaxVertexW = 1410065408,

      .GuardBandLeft = -FLT_MAX,
      .GuardBandTop = -FLT_MAX,
      .GuardBandRight = FLT_MAX,
      .GuardBandBottom = FLT_MAX,
      .ExtentsAdjust = 0,
      .StencilCaps = 0x1FF,

      .FVFCaps = D3DFVFCAPS_DONOTSTRIPELEMENTS |
                 D3DFVFCAPS_TEXCOORDCOUNTMASK,  // Do we need PSIZE?
      .TextureOpCaps = 0xFFFFFFFF,
      .MaxTextureBlendStages = 8,
      .MaxSimultaneousTextures = 8,

      .VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 |
                              D3DVTXPCAPS_DIRECTIONALLIGHTS |
                              D3DVTXPCAPS_POSITIONALLIGHTS,
      .MaxActiveLights = kMaxActiveLights,
      .MaxUserClipPlanes = 8,
      .MaxVertexBlendMatrices = 4,
      .MaxVertexBlendMatrixIndex = 0,  // ??

      .MaxPointSize = 1.f,

      .MaxPrimitiveCount = 0xFFFFFF,
      .MaxVertexIndex = 0xFFFFFF,  // Completely arbitrary.
      .MaxStreams = 16,
      .MaxStreamStride = 0xFF,

      .VertexShaderVersion = D3DVS_VERSION(1, 1),
      .MaxVertexShaderConst = kNumVsConstRegs,
      .PixelShaderVersion = D3DPS_VERSION(1, 3),
      .MaxPixelShaderValue = 65504.f};

  caps.CubeTextureFilterCaps = caps.VolumeTextureFilterCaps =
      caps.TextureFilterCaps;
  return caps;
}

void Device::InitRootSignatures() {
  std::vector<D3D12_ROOT_PARAMETER> root_params{
      {
          // Cbuffer 0: Transforms cbuffer.
          .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
          .Descriptor = {.ShaderRegister = 0},
          .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
      },
      {
          // Cbuffer 1: Material cbuffer.
          .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
          .Descriptor = {.ShaderRegister = 1},
          .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL,
      },
      {
          // CBuffer 2: Lights cbuffer.
          .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
          .Descriptor = {.ShaderRegister = 2},
          .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
      },
      {
          // CBuffer 3: Programmable vs constants.
          .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
          .Descriptor = {.ShaderRegister = 10},
          .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
      },
  };
  textures_start_bindslot_ = root_params.size();
  // Add all kMaxTexStages textures.
  std::array<D3D12_DESCRIPTOR_RANGE, kMaxTexStages> srv_ranges;
  std::array<D3D12_DESCRIPTOR_RANGE, kMaxTexStages> sampler_ranges;
  for (unsigned int i = 0; i < kMaxTexStages; ++i) {
    srv_ranges[i] = {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                     .NumDescriptors = 1,
                     .BaseShaderRegister = i,
                     .OffsetInDescriptorsFromTableStart = 0};
    sampler_ranges[i] = {.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
                         .NumDescriptors = 1,
                         .BaseShaderRegister = i};
    root_params.push_back(D3D12_ROOT_PARAMETER{
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
        .DescriptorTable = {.NumDescriptorRanges = 1,
                            .pDescriptorRanges = &srv_ranges[i]},
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
    });
  }
  // And all samplers.
  for (unsigned int i = 0; i < kMaxTexStages; ++i) {
    root_params.push_back(D3D12_ROOT_PARAMETER{
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
        .DescriptorTable = {.NumDescriptorRanges = 1,
                            .pDescriptorRanges = &sampler_ranges[i]},
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
    });
  }

  D3D12_ROOT_SIGNATURE_DESC sig_desc{
      .NumParameters = root_params.size(),
      .pParameters = root_params.data(),
      .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr,
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};

  ComPtr<ID3DBlob> sig_blob, error_blob;
  HRESULT hr = D3D12SerializeRootSignature(
      &sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, sig_blob.GetForInit(),
      error_blob.GetForInit());
  if (hr != S_OK) {
    FAIL("Could not create root signature:\r\n%s",
         (const char *)error_blob->GetBufferPointer());
  }

  ASSERT_HR(d3d12_device_->CreateRootSignature(
      0, sig_blob->GetBufferPointer(), sig_blob->GetBufferSize(),
      IID_PPV_ARGS(main_root_sig_.GetForInit())));

  // Create the cbuffers.
  vs_cbuffer_ = ComOwn(new DynamicBuffer());
  vs_cbuffer_->InitAsBuffer(this, sizeof(VertexCBuffer), Dx8::Usage::Dynamic,
                            D3DPOOL_SYSTEMMEM);
  lights_cbuffer_ = ComOwn(new DynamicBuffer());
  lights_cbuffer_->InitAsBuffer(this, sizeof(LightsCBuffer),
                                Dx8::Usage::Dynamic, D3DPOOL_SYSTEMMEM);
  ps_cbuffer_ = ComOwn(new DynamicBuffer());
  ps_cbuffer_->InitAsBuffer(this, sizeof(PixelCBuffer), Dx8::Usage::Dynamic,
                            D3DPOOL_SYSTEMMEM);

  vs_creg_cbuffer_ = ComOwn(new DynamicBuffer());
  vs_creg_cbuffer_->InitAsBuffer(this, sizeof(float[4]) * kNumVsConstRegs,
                                 Dx8::Usage::Dynamic, D3DPOOL_SYSTEMMEM);
  bound_vs_cregs_.resize(kNumVsConstRegs);

  ps_creg_cbuffer_ = ComOwn(new DynamicBuffer());
  ps_creg_cbuffer_->InitAsBuffer(this, sizeof(float[4]) * kNumPsConstRegs,
                                 Dx8::Usage::Dynamic, D3DPOOL_SYSTEMMEM);
}

HRESULT STDMETHODCALLTYPE Device::GetDeviceCaps(D3DCAPS8 *pCaps) {
  *pCaps = caps_;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::TestCooperativeLevel() { return S_OK; }

HRESULT STDMETHODCALLTYPE
Device::GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE Type,
                      IDirect3DSurface8 **ppBackBuffer) {
  TRACE_ENTRY(Type, ppBackBuffer);
  ASSERT(Type == D3DBACKBUFFER_TYPE_MONO);
  ASSERT(BackBuffer == 0);
  ASSERT(ppBackBuffer);
  *ppBackBuffer =
      new BackbufferSurface(BackBuffer, back_buffers_[0]->resource_desc());
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
Device::GetDepthStencilSurface(IDirect3DSurface8 **ppZStencilSurface) {
  TRACE_ENTRY(ppZStencilSurface);
  *ppZStencilSurface = new GpuSurface(this, depth_stencil_tex_.Get(), 0);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateTexture(UINT Width, UINT Height,
                                                UINT Levels, DWORD Usage,
                                                D3DFORMAT Format, D3DPOOL Pool,
                                                IDirect3DTexture8 **ppTexture) {
  TRACE_ENTRY(Width, Height, Levels, Usage, Format, Pool, ppTexture);
  *ppTexture = BaseTexture::Create(this, TextureKind::Texture2d, Width, Height,
                                   1, Levels, Usage, Format, Pool);
  return *ppTexture != nullptr;
}

HRESULT STDMETHODCALLTYPE Device::CreateCubeTexture(
    UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DCubeTexture8 **ppCubeTexture) {
  ASSERT(!(Usage & D3DUSAGE_DYNAMIC));
  *ppCubeTexture =
      BaseTexture::Create(this, TextureKind::Cube, EdgeLength, EdgeLength, 6,
                          Levels, Usage, Format, Pool);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
Device::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                           IDirect3DVertexBuffer8 **ppVertexBuffer) {
  ASSERT(!(Usage & D3DUSAGE_SOFTWAREPROCESSING));
  // Buffer *buffer = new Buffer();
  Buffer *buffer =
      HasFlag(Usage, D3DUSAGE_DYNAMIC) ? new DynamicBuffer() : new Buffer();
  buffer->InitAsVertexBuffer(this, static_cast<size_t>(Length), Usage, Pool,
                             FVF);
  *ppVertexBuffer = buffer;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
Device::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format,
                          D3DPOOL Pool, IDirect3DIndexBuffer8 **ppIndexBuffer) {
  ASSERT(!(Usage & D3DUSAGE_SOFTWAREPROCESSING));
  if (Format != D3DFMT_INDEX16 && Format != D3DFMT_INDEX32) {
    LOG_ERROR() << "Invalid Format for CreateIndexBuffer: " << Format << "\n";
    return D3DERR_INVALIDCALL;
  }
  Buffer *buffer =
      HasFlag(Usage, D3DUSAGE_DYNAMIC) ? new DynamicBuffer() : new Buffer();
  buffer->InitAsIndexBuffer(this, static_cast<size_t>(Length), Usage, Format,
                            Pool);
  *ppIndexBuffer = buffer;
  return S_OK;
}

void Device::TransitionTexture(GpuTexture *texture, uint32_t subresource,
                               D3D12_RESOURCE_STATES state_after) {
  if (texture->current_state() == state_after) return;
  LOG(TRACE) << "Transitioning " << std::hex << texture << "From "
             << texture->current_state() << " to " << state_after << "\n";

  D3D12_RESOURCE_BARRIER barrier{
      .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
      .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
      .Transition = {.pResource = texture->resource(),
                     .Subresource = subresource,
                     .StateBefore = texture->current_state(),
                     .StateAfter = state_after}};
  cmd_list_->ResourceBarrier(1, &barrier);
  texture->set_state(state_after);
  MarkResourceAsUsed(InternalPtr(texture));
}

void Device::CopyBuffer(ID3D12Resource *dest, int64_t dest_offset,
                        ID3D12Resource *src, int64_t src_offset,
                        int64_t num_bytes) {
  cmd_list_->CopyBufferRegion(dest, static_cast<UINT64>(dest_offset), src,
                              static_cast<UINT64>(src_offset),
                              static_cast<UINT64>(num_bytes));
  // Transition destination back to common.
  D3D12_RESOURCE_BARRIER barrier = CreateBufferTransition(
      dest, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  cmd_list_->ResourceBarrier(1, &barrier);
}

void Device::CopyBufferToTexture(
    GpuTexture *dest, uint32_t dest_subresource, ID3D12Resource *src,
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT src_footprint) {
  D3D12_TEXTURE_COPY_LOCATION dest_location{
      .pResource = dest->resource(),
      .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
      .SubresourceIndex = dest_subresource};
  D3D12_TEXTURE_COPY_LOCATION src_location{
      .pResource = src,
      .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
      .PlacedFootprint = src_footprint};

  TransitionTexture(dest, dest_subresource, D3D12_RESOURCE_STATE_COPY_DEST);

  cmd_list_->CopyTextureRegion(&dest_location, 0, 0, 0, &src_location, nullptr);
  // TODO: Transition away from copy destination back to whatever state the
  // texture was in, instead of transitioning back to common.
  TransitionTexture(dest, dest_subresource,
                    D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  MarkResourceAsUsed(InternalPtr(dest));
  // TODO: Mark src as used as well.
}

void Device::MarkBufferForPersist(Buffer *buffer) {
  buffers_to_persist_.insert(ComWrap(buffer));
}

HRESULT STDMETHODCALLTYPE Device::CopyRects(
    IDirect3DSurface8 *pSourceSurface, CONST RECT *pSourceRectsArray,
    UINT cRects, IDirect3DSurface8 *pDestinationSurface,
    CONST POINT *pDestPointsArray) {
  TRACE_ENTRY(pSourceSurface, pSourceRectsArray, cRects, pDestinationSurface,
              pDestPointsArray);
  ASSERT(pSourceRectsArray == nullptr);
  ASSERT(pDestPointsArray == nullptr);

  ASSERT(static_cast<BaseSurface *>(pSourceSurface)->kind() ==
         SurfaceKind::Cpu);
  CpuSurface *source_surface = static_cast<CpuSurface *>(pSourceSurface);
  ASSERT(static_cast<BaseSurface *>(pDestinationSurface)->kind() ==
         SurfaceKind::Gpu);
  GpuSurface *dest_surface = static_cast<GpuSurface *>(pDestinationSurface);

  // Allocate space in our ring buffer and move the source data.
  const D3D12_SUBRESOURCE_FOOTPRINT &source_footprint =
      source_surface->footprint().Footprint;
  const size_t num_bytes =
      static_cast<size_t>(source_footprint.RowPitch * source_footprint.Height);
  DynamicRingBuffer::Allocation ring_alloc =
      dynamic_ring_buffer()->Allocate(num_bytes);
  char *source_ring_ptr = dynamic_ring_buffer()->GetCpuPtrFor(ring_alloc);
  const uint32_t compact_pitch =
      safe_cast<uint32_t>(source_surface->compact_pitch());
  if (compact_pitch == source_footprint.RowPitch)
    memcpy(source_ring_ptr, source_surface->GetPtr(), num_bytes);
  else {
    for (uint32_t i = 0; i < source_footprint.Height; ++i) {
      memcpy(source_ring_ptr + i * source_footprint.RowPitch,
             source_surface->GetPtr() + i * compact_pitch, compact_pitch);
    }
  }
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT src_placed_footprint = {
      .Offset = safe_cast<uint64_t>(ring_alloc.offset),
      .Footprint = source_footprint};

  CopyBufferToTexture(dest_surface->texture(), dest_surface->subresource(),
                      dynamic_ring_buffer_->GetBackingResource(),
                      src_placed_footprint);

  MarkResourceAsUsed(InternalPtr(dest_surface));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
Device::UpdateTexture(IDirect3DBaseTexture8 *pSourceTexture,
                      IDirect3DBaseTexture8 *pDestinationTexture) {
  TRACE_ENTRY(pSourceTexture, pDestinationTexture);
  BaseTexture *source = dynamic_cast<BaseTexture *>(pSourceTexture);
  ASSERT(source->GetSurfaceDesc(0).Pool == D3DPOOL_SYSTEMMEM);
  BaseTexture *dest = dynamic_cast<BaseTexture *>(pDestinationTexture);
  ASSERT(dest->GetSurfaceDesc(0).Pool != D3DPOOL_SYSTEMMEM);
  // Transition dest.
  TransitionTexture(static_cast<GpuTexture *>(dest),
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_COPY_DEST);
  static_cast<CpuTexture *>(source)->CopyToGpuTexture(
      static_cast<GpuTexture *>(dest));
  // Transition dest.
  TransitionTexture(static_cast<GpuTexture *>(dest),
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
  MarkResourceAsUsed(InternalPtr(source));
  MarkResourceAsUsed(InternalPtr(dest));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetViewport(const D3DVIEWPORT8 *pViewport) {
  viewport_.TopLeftX = static_cast<float>(pViewport->X);
  viewport_.TopLeftY = static_cast<float>(pViewport->Y);
  viewport_.Width = static_cast<float>(pViewport->Width);
  viewport_.Height = static_cast<float>(pViewport->Height);
  viewport_.MinDepth = pViewport->MinZ;
  viewport_.MaxDepth = pViewport->MaxZ;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetTransform(D3DTRANSFORMSTATETYPE State,
                                               CONST D3DMATRIX *pMatrix) {
  if (State > 511 || State < D3DTS_VIEW ||
      (State > D3DTS_PROJECTION && State < D3DTS_TEXTURE0)) {
    LOG_ERROR() << "Invalid SetTransform index: " << State << "\n";
    return D3DERR_INVALIDCALL;
  }
  if (State == D3DTS_VIEW) {
    // Lights are uploaded to the GPU in view-space, so we must update them if
    // the view matrix changes.
    dirty_flags_ |= DIRTY_FLAG_LIGHTS;
  }
  transforms_[State] = *pMatrix;
  dirty_flags_ |= DIRTY_FLAG_TRANSFORMS;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetTransform(D3DTRANSFORMSTATETYPE State,
                                               D3DMATRIX *pMatrix) {
  if (State > 511 || State < D3DTS_VIEW ||
      (State > D3DTS_PROJECTION && State < D3DTS_TEXTURE0)) {
    LOG_ERROR() << "Invalid SetTransform index: " << State << "\n";
    return D3DERR_INVALIDCALL;
  }
  if (transforms_.contains(State)) {
    *pMatrix = transforms_[State];
  } else {
    static DirectX::SimpleMath::Matrix identity;
    memcpy(pMatrix, &identity, sizeof(identity));
  }
  return S_OK;
}

D3DMATRIX Device::GetTransform(D3DTRANSFORMSTATETYPE state) {
  D3DMATRIX matrix;
  ASSERT_HR(GetTransform(state, &matrix));
  return matrix;
}

HRESULT STDMETHODCALLTYPE Device::SetMaterial(const D3DMATERIAL8 *pMaterial) {
  material_ = *pMaterial;
  dirty_flags_ |= DIRTY_FLAG_PS_CBUFFER;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetLight(DWORD Index,
                                           CONST D3DLIGHT8 *light) {
  lights_[Index] = *light;
  if (enabled_lights_.contains(Index)) {
    dirty_flags_ |= DIRTY_FLAG_LIGHTS;
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::LightEnable(DWORD Index, BOOL Enable) {
  if (!lights_.contains(Index)) {
    // Create the default light if it does not already exist.
    lights_[Index] = D3DLIGHT8{.Type = D3DLIGHT_DIRECTIONAL,
                               .Diffuse = {1, 1, 1, 0},
                               .Direction = {0.f, 0.f, 1.f}};
  }
  if (Enable) {
    if (enabled_lights_.size() >= caps_.MaxActiveLights) {
      LOG_ERROR() << "Trying to enable more than " << caps_.MaxActiveLights
                  << " lights.\n";
      return D3DERR_INVALIDCALL;
    } else {
      enabled_lights_.insert(Index);
    }
  } else {
    enabled_lights_.erase(Index);
  }
  dirty_flags_ |= DIRTY_FLAG_LIGHTS;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetRenderState(D3DRENDERSTATETYPE State,
                                                 DWORD Value) {
  render_state_.GetEnumAtIndex(State) = Value;
  switch (State) {
    case D3DRS_TEXTUREFACTOR:
    case D3DRS_ALPHAREF:
      dirty_flags_ |= DIRTY_FLAG_PS_CBUFFER;
      break;
    case D3DRS_COLORVERTEX:
    case D3DRS_DIFFUSEMATERIALSOURCE:
    case D3DRS_AMBIENTMATERIALSOURCE:
    case D3DRS_SPECULARMATERIALSOURCE:
    case D3DRS_AMBIENT:
    case D3DRS_SPECULARENABLE:
    case D3DRS_NORMALIZENORMALS:
      dirty_flags_ |= DIRTY_FLAG_LIGHTS;
      break;
    default:
      break;
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetTextureStageState(
    DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) {
  NOT_IMPLEMENTED();
}

HRESULT STDMETHODCALLTYPE Device::SetTextureStageState(
    DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) {
  if (Stage >= texture_stage_states_.size()) return D3DERR_INVALIDCALL;
  if ((Type >= D3DTSS_ADDRESSU && Type <= D3DTSS_MAXANISOTROPY) ||
      Type == D3DTSS_ADDRESSW) {
    dirty_flags_ |= DIRTY_FLAG_PS_SAMPLERS;
  }
  texture_stage_states_[Stage].GetAtIndex(static_cast<size_t>(Type)) = Value;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetTexture(DWORD Stage,
                                             IDirect3DBaseTexture8 *pTexture) {
  TRACE_ENTRY(Stage, pTexture);
  if (Stage >= bound_textures_.size()) return D3DERR_INVALIDCALL;
  if (pTexture)
    ASSERT(dynamic_cast<BaseTexture *>(pTexture)->GetSurfaceDesc(0).Pool !=
           D3DPOOL_SYSTEMMEM);
  GpuTexture *texture = dynamic_cast<GpuTexture *>(pTexture);
  bound_textures_[Stage] = InternalPtr(texture);
  dirty_flags_ |= DIRTY_FLAG_PS_TEXTURES;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetRenderTarget(
    IDirect3DSurface8 *pRenderTarget, IDirect3DSurface8 *pNewZStencil) {
  if (pRenderTarget) {
    SCOPED_MARKER("SetRenderTarget");
    if (bound_render_target_) {
      // Transition out of render target into common.
      TransitionTexture(bound_render_target_.Get(), 0,
                        D3D12_RESOURCE_STATE_COMMON);
    }

    BaseSurface *base_surface = static_cast<BaseSurface *>(pRenderTarget);
    GpuTexture *texture = nullptr;
    D3D12_RESOURCE_DESC resource_desc = {};
    switch (base_surface->kind()) {
      case SurfaceKind::Gpu:
        texture = static_cast<GpuSurface *>(base_surface)->texture();
        resource_desc = texture->resource_desc();
        ASSERT(resource_desc.Format ==
               back_buffers_.at(0)->resource_desc().Format);
        TransitionTexture(texture, 0, D3D12_RESOURCE_STATE_RENDER_TARGET);
        break;
      case SurfaceKind::Backbuffer:
        ASSERT(static_cast<BackbufferSurface *>(base_surface)->index() == 0);
        texture = nullptr;
        resource_desc = back_buffers_.at(0)->resource_desc();
        break;
      case SurfaceKind::Cpu:
        LOG_ERROR() << "Cannot set SYSTEMMEM surface as render target.\n";
        return D3DERR_INVALIDCALL;
    }
    bound_render_target_ = InternalPtr(texture);

    // Reset viewport to the size of this one.
    D3DVIEWPORT8 viewport{.Width = safe_cast<DWORD>(resource_desc.Width),
                          .Height = resource_desc.Height,
                          .MaxZ = 1.f};
    ASSERT_HR(SetViewport(&viewport));
  }
  if (pNewZStencil) {
    SCOPED_MARKER("SetDepthTarget");
    BaseSurface *base_surface = dynamic_cast<BaseSurface *>(pNewZStencil);
    GpuTexture *texture = nullptr;
    switch (base_surface->kind()) {
      case SurfaceKind::Gpu:
        texture = static_cast<GpuSurface *>(base_surface)->texture();
        break;
      case SurfaceKind::Backbuffer:
        ASSERT(static_cast<BackbufferSurface *>(base_surface)->index() == 0);
        texture = depth_stencil_tex_.Get();
        break;
      case SurfaceKind::Cpu:
        LOG_ERROR() << "Cannot set SYSTEMMEM surface as render target.\n";
        return D3DERR_INVALIDCALL;
    }
    // TODO: Support actually binding a separate depth stencil tex.
    ASSERT(texture == depth_stencil_tex_.Get());
    bound_depth_target_ = InternalPtr(depth_stencil_tex_.Get());
    // TODO: Update viewport.
    ASSERT(viewport_.Width == bound_depth_target_->resource_desc().Width);
    ASSERT(viewport_.Height == bound_depth_target_->resource_desc().Height);
  } else {
    bound_depth_target_.Reset();
  }
  dirty_flags_ |= DIRTY_FLAG_OM;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateVertexShader(const DWORD *pDeclaration,
                                                     const DWORD *pFunction,
                                                     DWORD *pHandle,
                                                     DWORD Usage) {
  auto decl = ParseShaderDeclaration(pDeclaration);

  VertexShader shader;
  if (pFunction == nullptr) {
    shader = CreateFixedFunctionVertexShader(viewport_, 0, decl);
  } else {
    shader = ParseProgrammableVertexShader(decl, pFunction);
  }

  ASSERT(next_shader_handle_ < UINT32_MAX);
  DWORD handle = next_shader_handle_++;
  ASSERT(handle >= kFirstShaderHandle);
  vertex_shaders_[handle] = InternalPtr(new VertexShader(std::move(shader)));
  *pHandle = handle;

  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreatePixelShader(const DWORD *pFunction,
                                                    DWORD *pHandle) {
  if (!pFunction) return D3DERR_INVALIDCALL;
  PixelShader shader = ParsePixelShader(pFunction);
  ASSERT(next_shader_handle_ < UINT32_MAX);
  *pHandle = next_shader_handle_++;
  pixel_shaders_[*pHandle] = InternalPtr(new PixelShader(std::move(shader)));
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::DeleteVertexShader(DWORD Handle) {
  ASSERT(Handle >= kFirstShaderHandle);
  auto found = vertex_shaders_.erase(Handle);
  ASSERT(found != 0);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::DeletePixelShader(DWORD Handle) {
  auto found = pixel_shaders_.erase(Handle);
  ASSERT(found != 0);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetVertexShader(DWORD handle) {
  if (handle < kFirstShaderHandle) {
    // This is a fixed-function shader. Create it if it does not already
    // exist.
    if (!vertex_shaders_.contains(handle)) {
      vertex_shaders_[handle] =
          InternalPtr(new VertexShader(CreateFixedFunctionVertexShader(
              viewport_, handle,
              VertexShaderDeclaration::CreateFromFVFDesc(handle))));
    }
  } else {
    ASSERT(vertex_shaders_.contains(handle));
  }
  bound_vertex_shader_ = handle;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetPixelShader(DWORD Handle) {
  if (Handle != 0 && !pixel_shaders_.contains(Handle))
    return D3DERR_INVALIDCALL;
  bound_pixel_shader_ = Handle;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetVertexShaderConstant(
    DWORD Register, CONST void *pConstantData, DWORD ConstantCount) {
  if ((Register + ConstantCount) >= kNumVsConstRegs || pConstantData == nullptr)
    return D3DERR_INVALIDCALL;

  memcpy(&bound_vs_cregs_.at(Register), pConstantData,
         ConstantCount * sizeof(float[4]));
  dirty_flags_ |= DIRTY_FLAG_VS_CBUFFER;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetStreamSource(
    UINT StreamNumber, IDirect3DVertexBuffer8 *pStreamData, UINT Stride) {
  TRACE_ENTRY(StreamNumber, pStreamData, Stride);
  if (StreamNumber >= kMaxVertexStreams) return D3DERR_INVALIDCALL;
  if (Stride > caps_.MaxStreamStride) return D3DERR_INVALIDCALL;
  Buffer *buffer = static_cast<Buffer *>(pStreamData);
  bound_vertex_streams_[StreamNumber] = InternalPtr(buffer);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetIndices(IDirect3DIndexBuffer8 *pIndexData,
                                             UINT BaseVertexIndex) {
  bound_index_buffer_ = InternalPtr(static_cast<Buffer *>(pIndexData));
  bound_base_vertex_ = BaseVertexIndex;
  return S_OK;
}

ComPtr<ID3D12PipelineState> Device::CreatePSO(D3DPRIMITIVETYPE d3d8_prim_type) {
  std::array<bool, kMaxTexStages> stage_has_texture = {};
  for (int i = 0; i < 8; ++i) {
    stage_has_texture[i] = bound_textures_[i];
    if (!stage_has_texture[i]) break;
  }
  ASSERT(bound_vertex_shader_ != 0);
  VertexShader *vertex_shader = vertex_shaders_.at(bound_vertex_shader_).Get();
  // If no pixel shader is bound, generate a fixed-function shader.
  ComPtr<ID3DBlob> pixel_shader;
  if (bound_pixel_shader_ == 0) {
    // Try to find the fixed-function pixel shader in our cache.
    PixelShaderState key(render_state_, stage_has_texture.data(),
                         texture_stage_states_.data());
    auto iter = ps_cache_.find(key);
    if (iter != ps_cache_.end()) {
      pixel_shader = iter->second;
    } else {
      pixel_shader = CreatePixelShaderFromState(key);
      if (!kDisablePixelShaderCache)
        ps_cache_.emplace_hint(iter, key, pixel_shader);
    }
  } else {
    auto iter = pixel_shaders_.find(bound_pixel_shader_);
    ASSERT(iter != pixel_shaders_.end());
    pixel_shader = iter->second->blob;
  }

  // Now that we know our pixel shader, try to look into the PSO cache.
  PSOState pso_key{
      .rs = render_state_,
      .input_elements = vertex_shader->decl.input_elements,
      .vs = vertex_shader->blob.get(),
      .ps = pixel_shader.get(),
      .prim_type = d3d8_prim_type,
      .dsv_format = bound_depth_target_
                        ? bound_depth_target_->resource_desc().Format
                        : DXGI_FORMAT_UNKNOWN};

  // Some things don't get used here. (TODO: Move to PSOState constructor).
  pso_key.rs.texture_factor = 0;
  pso_key.rs.ambient = 0;
  pso_key.rs.diffuse_material_source = pso_key.rs.specular_material_source =
      pso_key.rs.ambient_material_source = pso_key.rs.emissive_material_source =
          D3DMCS_MATERIAL;
  // pso_key.rs.alpha_ref = 0;
  // pso_key.rs.dither_enable = 0;
  // pso_key.rs.fog_enable = 0;
  // pso_key.rs.fog_color = 0;
  // pso_key.rs.fog_table_mode = D3DFOG_NONE;
  // pso_key.rs.fog_start = 0;
  // pso_key.rs.fog_end = 0;
  // pso_key.rs.fog_density = 0;
  // pso_key.rs.range_fog_enable = 0;
  // pso_key.rs.fog_vertex_mode = D3DFOG_NONE;
  // pso_key.rs.color_vertex = 0;
  // pso_key.rs.local_viewer = FALSE;
  // pso_key.rs.normalized_normals = FALSE;

  auto pso_cache_iter = pso_cache_.find(pso_key);
  if (pso_cache_iter != pso_cache_.end()) {
    return pso_cache_iter->second;
  }

  // LOG(INFO) << "Num PSOs: " << std::dec << pso_cache_.size() << "\n";

  ASSERT(render_state_.zbuffer_type <= 1);

  D3D12_PRIMITIVE_TOPOLOGY_TYPE d3d12_prim_type;
  switch (d3d8_prim_type) {
    case D3DPT_POINTLIST:
      d3d12_prim_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
      break;
    case D3DPT_LINELIST:
      d3d12_prim_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      break;
    case D3DPT_TRIANGLELIST:
    case D3DPT_TRIANGLESTRIP:
      d3d12_prim_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      break;
    default:
      FAIL("Unimplemented primitive type %d", d3d8_prim_type);
  }
  ASSERT(render_state_.src_blend <= D3DBLEND_SRCALPHASAT);
  ASSERT(render_state_.dest_blend <= D3DBLEND_SRCALPHASAT);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{
      .pRootSignature = main_root_sig_.get(),
      .VS = {.pShaderBytecode = vertex_shader->blob->GetBufferPointer(),
             .BytecodeLength = vertex_shader->blob->GetBufferSize()},
      .PS = {.pShaderBytecode = pixel_shader->GetBufferPointer(),
             .BytecodeLength = pixel_shader->GetBufferSize()},
      .BlendState =
          {.RenderTarget = {{
               .BlendEnable = render_state_.alpha_blend_enable != 0,
               .SrcBlend = static_cast<D3D12_BLEND>(render_state_.src_blend),
               .DestBlend = static_cast<D3D12_BLEND>(render_state_.dest_blend),
               .BlendOp = static_cast<D3D12_BLEND_OP>(render_state_.blend_op),
               .SrcBlendAlpha = D3D12_BLEND_ONE,
               .DestBlendAlpha = D3D12_BLEND_ZERO,
               .BlendOpAlpha = D3D12_BLEND_OP_ADD,
               .LogicOp = D3D12_LOGIC_OP_NOOP,
               .RenderTargetWriteMask =
                   safe_cast<uint8_t>(render_state_.color_write_enable),
           }}},
      .SampleMask = UINT_MAX,
      .RasterizerState =
          {
              .FillMode = static_cast<D3D12_FILL_MODE>(render_state_.fill_mode),
              .CullMode = render_state_.cull_mode != D3DCULL_NONE
                              ? D3D12_CULL_MODE_BACK
                              : D3D12_CULL_MODE_NONE,
              .FrontCounterClockwise = render_state_.cull_mode == D3DCULL_CW,
              .DepthBias = 0,         // TODO.
              .DepthBiasClamp = 0.f,  // TODO.
              .MultisampleEnable = render_state_.multisample_antialias != 0,
              .AntialiasedLineEnable = render_state_.edge_antialias != 0,
          },
      .DepthStencilState =
          {
              .DepthEnable = render_state_.zbuffer_type && bound_depth_target_,
              .DepthWriteMask = static_cast<D3D12_DEPTH_WRITE_MASK>(
                  render_state_.zwrite_enable != 0),
              .DepthFunc =
                  static_cast<D3D12_COMPARISON_FUNC>(render_state_.z_func),
          },
      .InputLayout = {.pInputElementDescs =
                          vertex_shader->decl.input_elements.data(),
                      .NumElements = vertex_shader->decl.input_elements.size()},
      .PrimitiveTopologyType = d3d12_prim_type,
      .NumRenderTargets = 1,
      .RTVFormats = {back_buffers_[0]->resource_desc().Format},
      .DSVFormat = bound_depth_target_
                       ? bound_depth_target_->resource_desc().Format
                       : DXGI_FORMAT_UNKNOWN,
      .SampleDesc = {.Count = 1, .Quality = 0}};
  ComPtr<ID3D12PipelineState> pso;
  ASSERT_HR(d3d12_device_->CreateGraphicsPipelineState(
      &desc, IID_PPV_ARGS(pso.GetForInit())));
  if (!kDisablePsoCache)
    pso_cache_.emplace_hint(pso_cache_iter, std::move(pso_key), pso);
  return pso;
}

HRESULT STDMETHODCALLTYPE Device::BeginScene() {
  TRACE_ENTRY();
  // Set viewports.
  cmd_list_->RSSetViewports(1, &viewport_);
  D3D12_RECT scissors = {.left = 0,
                         .top = 0,
                         .right = static_cast<LONG>(viewport_.Width),
                         .bottom = static_cast<LONG>(viewport_.Height)};
  cmd_list_->RSSetScissorRects(1, &scissors);

  ID3D12DescriptorHeap *heaps[] = {srv_heap_.heap(), sampler_heap_.heap()};
  cmd_list_->SetDescriptorHeaps(sizeof(heaps) / sizeof(heaps[0]), heaps);

  GpuTexture *render_target =
      bound_render_target_ ? bound_render_target_.Get()
                           : back_buffers_.at(current_back_buffer_).Get();

  // Transition the back buffer from present (or common) to render target.
  TransitionTexture(render_target, 0, D3D12_RESOURCE_STATE_RENDER_TARGET);

  // Set the default render targets.
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = render_target->rtv_handle();
  D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
  if (bound_depth_target_) {
    dsv_handle = bound_depth_target_->dsv_handle();
    MarkResourceAsUsed(InternalPtr(bound_depth_target_.Get()));
  }
  cmd_list_->OMSetRenderTargets(1, &rtv_handle, 1,
                                bound_depth_target_ ? &dsv_handle : nullptr);
  MarkResourceAsUsed(InternalPtr(render_target));
  dirty_flags_ ^= DIRTY_FLAG_OM;
  return S_OK;
}
HRESULT STDMETHODCALLTYPE Device::EndScene() { return S_OK; }

HRESULT STDMETHODCALLTYPE Device::Clear(DWORD Count, CONST D3DRECT *pRects,
                                        DWORD Flags, D3DCOLOR Color, float Z,
                                        DWORD Stencil) {
  D3D12_RECT rect, *rects = nullptr;
  if (pRects) {
    ASSERT(Count == 1);
    rect.left = pRects->x1;
    rect.top = pRects->y1;
    rect.right = pRects->x2;
    rect.bottom = pRects->y2;
    rects = &rect;
  }

  if (Flags & D3DCLEAR_TARGET) {
    // Clear can be called before BeginScene - so make sure to transition the
    // render taret.
    GpuTexture *render_target = bound_render_target_
                                    ? bound_render_target_.Get()
                                    : back_buffers_[current_back_buffer_].Get();
    TransitionTexture(render_target, 0, D3D12_RESOURCE_STATE_RENDER_TARGET);
    float color[4] = {((Color >> 16) & 0xFF) / 255.f,
                      ((Color >> 8) & 0xFF) / 255.f, (Color & 0xFF) / 255.f,
                      ((Color >> 24) & 0xFF) / 255.f};
    cmd_list_->ClearRenderTargetView(render_target->rtv_handle(), color, 0,
                                     rects);
  }
  if (Flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) {
    if (!bound_depth_target_) {
      LOG_ERROR()
          << "Do not have any depth stencil texture allocated to clear.\n";
      return D3DERR_INVALIDCALL;
    }
    D3D12_CLEAR_FLAGS clear_flags = {};
    if (Flags & D3DCLEAR_ZBUFFER) clear_flags |= D3D12_CLEAR_FLAG_DEPTH;
    if (Flags & D3DCLEAR_STENCIL) clear_flags |= D3D12_CLEAR_FLAG_STENCIL;
    cmd_list_->ClearDepthStencilView(bound_depth_target_->dsv_handle(),
                                     clear_flags, Z,
                                     static_cast<UINT8>(Stencil), 0, rects);
  }
  return S_OK;
}

HRESULT Device::PrepareDrawCall(D3DPRIMITIVETYPE PrimitiveType,
                                int start_vertex, int num_vertices) {
  if (PrimitiveType > D3DPT_TRIANGLEFAN) {
    LOG_ERROR() << "Invalid primitive type " << PrimitiveType << "\n";
    return D3DERR_INVALIDCALL;
  }
  ASSERT(PrimitiveType !=
         D3DPT_TRIANGLEFAN);  // We don't actually support fans.

  // Configure the output-merger stage if anything reset it (like flushes).
  if (dirty_flags_ & DIRTY_FLAG_OM) {
    BeginScene();
  }

  cmd_list_->IASetPrimitiveTopology(
      static_cast<D3D12_PRIMITIVE_TOPOLOGY>(PrimitiveType));

  ASSERT(bound_vertex_shader_ != 0);
  VertexShader *vertex_shader = vertex_shaders_.at(bound_vertex_shader_).Get();
  if (bound_vertex_shader_ >= kFirstShaderHandle) {
    MarkResourceAsUsed(InternalPtr(vertex_shader));
  }
  if (bound_pixel_shader_) {
    MarkResourceAsUsed(
        InternalPtr(pixel_shaders_.at(bound_pixel_shader_).Get()));
  }

  std::array<D3D12_VERTEX_BUFFER_VIEW, kMaxVertexStreams> vbuffer_views = {};
  size_t max_index = 0;
  for (size_t i = 0; i < bound_vertex_streams_.size(); ++i) {
    if (vertex_shader->decl.buffer_strides[i] > 0) {
      auto &d3d_buffer = bound_vertex_streams_[i];
      if (d3d_buffer) {
        Buffer *buffer = static_cast<Buffer *>(d3d_buffer.Get());
        int stride = vertex_shader->decl.buffer_strides[i];
        vbuffer_views[i] = {.BufferLocation = buffer->GetGpuPtr(),
                            .SizeInBytes = static_cast<UINT>(
                                stride * (start_vertex + num_vertices)),
                            .StrideInBytes = static_cast<UINT>(stride)};
        if (i > max_index) max_index = i;
        MarkResourceAsUsed(bound_vertex_streams_[i]);
      } else {
        // FAIL("Shader requires bound buffer at slot %d, but none are bound.",
        // i);
      }
    }
  }

  cmd_list_->IASetVertexBuffers(0, max_index + 1, vbuffer_views.data());

  ComPtr<ID3D12PipelineState> pso = CreatePSO(PrimitiveType);
  cmd_list_->SetPipelineState(pso.get());
  // MarkResourceAsUsed(pso);
  using ::DirectX::SimpleMath::Matrix;
  const Matrix view = MatrixFromD3D(GetTransform(D3DTS_VIEW));

  // Set the vertex cbuffer.
  if (dirty_flags_ & DIRTY_FLAG_TRANSFORMS) {
    VertexCBuffer *cbuffer;
    ASSERT_HR(vs_cbuffer_->Lock(0, sizeof(VertexCBuffer), (BYTE **)&cbuffer,
                                D3DLOCK_DISCARD));
    Matrix proj = MatrixFromD3D(GetTransform(D3DTS_PROJECTION));
    Matrix world = MatrixFromD3D(GetTransform(D3DTS_WORLD));
    cbuffer->world_view_proj = world * view * proj;
    cbuffer->world_view = world * view;
    cbuffer->camera_position = DirectX::SimpleMath::Vector3(0, 0, 0);
    ASSERT_HR(vs_cbuffer_->Unlock());
    dirty_flags_ ^= DIRTY_FLAG_TRANSFORMS;
  }
  if (dirty_flags_ & DIRTY_FLAG_VS_CBUFFER) {
    // TODO: Only copy changed constants.
    BYTE *cbuffer;
    ASSERT_HR(vs_creg_cbuffer_->Lock(
        0, bound_vs_cregs_.size() * sizeof(bound_vs_cregs_[0]), &cbuffer,
        D3DLOCK_DISCARD));
    memcpy(cbuffer, bound_vs_cregs_.data(),
           bound_vs_cregs_.size() * sizeof(bound_vs_cregs_[0]));
    ASSERT_HR(vs_creg_cbuffer_->Unlock());
    dirty_flags_ ^= DIRTY_FLAG_VS_CBUFFER;
  }
  if (dirty_flags_ & DIRTY_FLAG_LIGHTS) {
    LightsCBuffer *cbuffer;
    ASSERT_HR(lights_cbuffer_->Lock(0, sizeof(LightsCBuffer),
                                    reinterpret_cast<BYTE **>(&cbuffer),
                                    D3DLOCK_DISCARD));
    int i = 0;
    ASSERT(enabled_lights_.size() <= kMaxActiveLights);
    for (auto light_index : enabled_lights_) {
      // ASSERT(render_state_.lighting);
      cbuffer->lights[i] = ShaderLightMarshall(view, lights_[light_index]);
      ASSERT(cbuffer->lights[i].type != D3DLIGHT_SPOT);
      ++i;
    }
    cbuffer->num_lights = i;
    cbuffer->diffuse_material_source =
        render_state_.color_vertex ? render_state_.diffuse_material_source
                                   : D3DMCS_MATERIAL;
    cbuffer->ambient_material_source =
        render_state_.color_vertex ? render_state_.ambient_material_source
                                   : D3DMCS_MATERIAL;
    cbuffer->specular_material_source =
        render_state_.color_vertex ? render_state_.specular_material_source
                                   : D3DMCS_MATERIAL;
    cbuffer->specular_enable = render_state_.specular_enable;
    cbuffer->global_ambient = Dx8::Color(render_state_.ambient).ToValue();
    ASSERT_HR(lights_cbuffer_->Unlock());
    dirty_flags_ ^= DIRTY_FLAG_LIGHTS;
  }
  if (dirty_flags_ & DIRTY_FLAG_PS_CBUFFER) {
    // And pixel cbuffer.
    PixelCBuffer *cbuffer;
    ASSERT_HR(ps_cbuffer_->Lock(0, sizeof(PixelCBuffer), (BYTE **)&cbuffer,
                                D3DLOCK_DISCARD));
    cbuffer->material_diffuse = material_.Diffuse;
    cbuffer->material_ambient = material_.Ambient;
    cbuffer->material_specular = material_.Specular;
    cbuffer->material_power = material_.Power;

    cbuffer->alpha_ref = (render_state_.alpha_ref & 0xFF) / 255.f;
    cbuffer->texture_factor =
        Dx8::Color(render_state_.texture_factor).ToValue();
    ASSERT_HR(ps_cbuffer_->Unlock());
    dirty_flags_ ^= DIRTY_FLAG_PS_CBUFFER;
  }
  cmd_list_->SetGraphicsRootSignature(main_root_sig_.get());

  // Set all the necessary roots.
  cmd_list_->SetGraphicsRootConstantBufferView(0, vs_cbuffer_->GetGpuPtr());
  cmd_list_->SetGraphicsRootConstantBufferView(1, ps_cbuffer_->GetGpuPtr());
  cmd_list_->SetGraphicsRootConstantBufferView(2, lights_cbuffer_->GetGpuPtr());
  cmd_list_->SetGraphicsRootConstantBufferView(3,
                                               vs_creg_cbuffer_->GetGpuPtr());

  if (dirty_flags_ & DIRTY_FLAG_PS_TEXTURES) {
    // And all the textures.
    for (int i = 0; i < kMaxTexStages; ++i) {
      if (bound_textures_[i]) {
        const auto gpu_handle =
            srv_heap_.GetGPUHandleFor(bound_textures_[i]->srv_handle());
        cmd_list_->SetGraphicsRootDescriptorTable(textures_start_bindslot_ + i,
                                                  gpu_handle);
        MarkResourceAsUsed(bound_textures_[i]);
      }
    }
    dirty_flags_ ^= DIRTY_FLAG_PS_TEXTURES;
  }

  if (dirty_flags_ & DIRTY_FLAG_PS_SAMPLERS) {
    // Set all the samplers.
    for (int i = 0; i < kMaxTexStages; ++i) {
      SamplerDesc desc(texture_stage_states_[i]);
      auto iter = sampler_cache_.find(desc);
      if (iter == sampler_cache_.end()) {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = sampler_heap_.Allocate();
        d3d12_device_->CreateSampler(&desc, cpu_handle);
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle =
            sampler_heap_.GetGPUHandleFor(cpu_handle);
        iter = sampler_cache_.insert(iter, std::pair(desc, gpu_handle));
      }
      ASSERT(iter->second.ptr != 0);
      cmd_list_->SetGraphicsRootDescriptorTable(
          textures_start_bindslot_ + kMaxTexStages + i, iter->second);
    }
    dirty_flags_ ^= DIRTY_FLAG_PS_SAMPLERS;
  }
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,
                                                UINT StartVertex,
                                                UINT PrimitiveCount) {
  int vertex_count;
  switch (PrimitiveType) {
    case D3DPT_LINELIST:
      vertex_count = 2 * PrimitiveCount;
      break;
    case D3DPT_TRIANGLELIST:
      vertex_count = 3 * PrimitiveCount;
      break;
    case D3DPT_TRIANGLESTRIP:
      vertex_count = 2 + PrimitiveCount;
      break;
    default:
      FAIL("TODO: Count number of vertices for PrimitiveType of %d",
           PrimitiveType);
      break;
  }
  HR_OR_RETURN(PrepareDrawCall(PrimitiveType, StartVertex, vertex_count));
  cmd_list_->DrawInstanced(vertex_count, 1, StartVertex, 0);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::DrawPrimitiveUP(
    D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
    CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride) {
  if (!bound_vertex_shader_) {
    LOG_ERROR() << "Cannot use DrawPrimitiveUP without a vertex shader.\n";
    return D3DERR_INVALIDCALL;
  }

  // Rewrite triangle fans as triangle lists.
  std::vector<uint8_t> rewritten_fan;
  if (PrimitiveType == D3DPT_TRIANGLEFAN) {
    rewritten_fan.reserve(3 * PrimitiveCount * VertexStreamZeroStride);

    auto insert_vertex = [&](uint32_t index) {
      const uint8_t *pStart =
          static_cast<const uint8_t *>(pVertexStreamZeroData) +
          index * VertexStreamZeroStride;
      std::copy(pStart, pStart + VertexStreamZeroStride,
                std::back_inserter(rewritten_fan));
    };

    for (uint32_t i = 0; i < PrimitiveCount; ++i) {
      insert_vertex(0);
      insert_vertex(i + 1);
      insert_vertex(i + 2);
    }
    pVertexStreamZeroData = rewritten_fan.data();
    PrimitiveType = D3DPT_TRIANGLELIST;
  }

  int vertex_count;
  switch (PrimitiveType) {
    case D3DPT_LINELIST:
      vertex_count = 2 * PrimitiveCount;
      break;
    case D3DPT_TRIANGLELIST:
      vertex_count = 3 * PrimitiveCount;
      break;
    case D3DPT_TRIANGLESTRIP:
      vertex_count = 2 + PrimitiveCount;
      break;
    default:
      FAIL("TODO: Count number of vertices for PrimitiveType of %d",
           PrimitiveType);
      break;
  }

  // Allocate some ring buffer memory.
  size_t num_bytes = vertex_count * VertexStreamZeroStride;
  DynamicRingBuffer::Allocation alloc =
      dynamic_ring_buffer()->Allocate(num_bytes);
  memcpy(dynamic_ring_buffer()->GetCpuPtrFor(alloc), pVertexStreamZeroData,
         num_bytes);
  D3D12_VERTEX_BUFFER_VIEW vbuffer_view{
      .BufferLocation = dynamic_ring_buffer()->GetGpuPtrFor(alloc),
      .SizeInBytes = safe_cast<UINT>(num_bytes),
      .StrideInBytes = VertexStreamZeroStride};

  ASSERT_HR(SetStreamSource(0, nullptr, 0));
  HR_OR_RETURN(PrepareDrawCall(PrimitiveType, 0, vertex_count));
  // Overwrite whatever vertex buffer the prepare set.
  cmd_list_->IASetVertexBuffers(0, 1, &vbuffer_view);
  cmd_list_->DrawInstanced(vertex_count, 1, 0, 0);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::DrawIndexedPrimitive(
    D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices,
    UINT startIndex, UINT primCount) {
  if (!bound_index_buffer_) return D3DERR_INVALIDCALL;

  int index_count;
  switch (PrimitiveType) {
    case D3DPT_TRIANGLELIST:
      index_count = 3 * primCount;
      break;
    case D3DPT_TRIANGLESTRIP:
      index_count = 2 + primCount;
      break;
    default:
      FAIL("TODO: Count number of vertices for PrimitiveType of %d",
           PrimitiveType);
      break;
  }

  HR_OR_RETURN(PrepareDrawCall(PrimitiveType, minIndex + bound_base_vertex_,
                               NumVertices));

  D3D12_INDEX_BUFFER_VIEW ib_view{
      .BufferLocation = bound_index_buffer_->GetGpuPtr(),
      .SizeInBytes = static_cast<UINT>(
          DXGIFormatSize(bound_index_buffer_->index_buffer_fmt()) *
          (startIndex + index_count)),
      .Format = bound_index_buffer_->index_buffer_fmt()};
  MarkResourceAsUsed(bound_index_buffer_);
  cmd_list_->IASetIndexBuffer(&ib_view);

  cmd_list_->DrawIndexedInstanced(index_count, 1, startIndex,
                                  bound_base_vertex_, 0);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE Device::Present(CONST RECT *pSourceRect,
                                          CONST RECT *pDestRect,
                                          HWND hDestWindowOverride,
                                          CONST RGNDATA *pDirtyRegion) {
  TRACE_ENTRY(hDestWindowOverride);
  ASSERT(hDestWindowOverride == nullptr || hDestWindowOverride == window_);
  SubmitAndWait(true);
  return S_OK;
}

// Only used during reset. Does not clean up fence state.
void Device::SubmitAndWait(bool should_present) {
  ASSERT(!(dirty_flags_ & DIRTY_FLAG_CMD_LIST_CLOSED));

  // Transition back buffer to present.
  if (should_present) {
    TransitionTexture(back_buffers_[current_back_buffer_].get(), 0,
                      D3D12_RESOURCE_STATE_PRESENT);
  }

  // Persist any dynamic buffers.
  for (auto buffer : buffers_to_persist_) {
    buffer->PersistDynamicChanges();
  }
  buffers_to_persist_.clear();

  // Close the command list, then execute it.
  ASSERT_HR(cmd_list_->Close());
  dirty_flags_ |= DIRTY_FLAG_CMD_LIST_CLOSED;
  ID3D12CommandList *cmd_list = cmd_list_.Get();
  cmd_queue_->ExecuteCommandLists(1, &cmd_list);
  // Present!
  if (should_present) {
    ASSERT_HR(swap_chain_->Present(1, 0));
  }

  // Grab a new fence value, set it at the end of the command queue execution.
  fence_values_.at(current_back_buffer_) = next_fence_++;
  ASSERT_HR(cmd_queue_->Signal(cmd_list_done_fence_.get(),
                               fence_values_[current_back_buffer_]));

  // Update our back buffer index.
  current_back_buffer_ = swap_chain_->GetCurrentBackBufferIndex();

  // Wait for it.
  WaitForFrame(fence_values_[current_back_buffer_]);

  // Reset the command list for the next frame.
  ASSERT_HR(cmd_allocators_[current_back_buffer_]->Reset());
  ASSERT_HR(
      cmd_list_->Reset(cmd_allocators_[current_back_buffer_].get(), nullptr));
  dirty_flags_ ^= DIRTY_FLAG_CMD_LIST_CLOSED;
  dirty_flags_ |= DIRTY_FLAG_ALL_RESOURCES;
}

void Device::WaitForFrame(uint64_t frame_number) {
  ASSERT(frame_number <= next_fence_);

  if (cmd_list_done_fence_->GetCompletedValue() < frame_number) {
    // Is this a frame that we're currently building?
    if (frame_number + 1 == next_fence_ &&
        !(dirty_flags_ & DIRTY_FLAG_CMD_LIST_CLOSED)) {
      // SubmitAndWait will call us again to wait for the frame, but at that
      // point fence_values_[current_back_buffer_] will have incremented.
      SubmitAndWait(false);
    } else {
      LOG(TRACE) << "Waiting for fence " << frame_number << ".\n";
      ASSERT_HR(cmd_list_done_fence_->SetEventOnCompletion(
          frame_number, cmd_list_done_event_handle_));
      WaitForSingleObjectEx(cmd_list_done_event_handle_, 60 * 1000, FALSE);
    }
  }

  // Free any frame resources.
  FreeFrameResources(frame_number);
}

void Device::FreeFrameResources(uint64_t frame_number) {
  for (size_t i = 0; i < frame_resources_to_free_.size(); ++i) {
    if (fence_values_[i] <= frame_number) {
      frame_resources_to_free_[i].clear();
    }
  }

  dynamic_ring_buffer_->HasCompletedFrame(frame_number);
  dynamic_ring_buffer_->SetCurrentFrame(CurrentFrame());
}

uint64_t Device::CurrentFrame() const { return next_fence_; }

}  // namespace Dx8to12