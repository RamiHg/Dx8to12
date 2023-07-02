#pragma once

#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "d3d8.h"
#include "device_limits.h"
#include "pool_heap.h"
#include "render_state.h"
#include "shader_parser.h"
#include "util.h"
#include "utils/dx_utils.h"
#include "vertex_shader.h"

interface IDXGIFactory2;
interface IDXGIAdapter;
interface IDXGISwapChain3;
interface IDXGIOutput;
interface ID3D12Device;
interface ID3D12CommandQueue;
interface ID3D12CommandList;
interface ID3D12CommandAllocator;
interface ID3D12RootSignature;

namespace D3D12MA {
class Allocator;
}

namespace Dx8to12 {
class Buffer;
class DynamicRingBuffer;
class GpuTexture;

class Device : public IDirect3DDevice8, RefCounted {
 public:
  Device(IDirect3D8 *direct3d8);
  virtual ~Device();

  static D3DCAPS8 GetDefaultCaps(UINT adapter_index);
  bool Create(HWND window, ComPtr<IDXGIFactory2> factory,
              ComPtr<IDXGIAdapter> adapter, int adapter_index,
              const D3DPRESENT_PARAMETERS &presentParams);

  ID3D12Device *device() const { return d3d12_device_.get(); }
  ID3D12GraphicsCommandList *cmd_list() const { return cmd_list_.get(); }
  DescriptorPoolHeap &srv_heap() { return srv_heap_; }
  DescriptorPoolHeap *rtv_heap() { return &rtv_heap_; }
  DescriptorPoolHeap *dsv_heap() { return &dsv_heap_; }
  DynamicRingBuffer *dynamic_ring_buffer() {
    return dynamic_ring_buffer_.get();
  }
  // TODO: Actually put this in GPU mem.
  DynamicRingBuffer *dynamic_gpu_ring_buffer() {
    return dynamic_ring_buffer_.get();
  }

  uint64_t CurrentFrame() const;
  void CopyBuffer(ID3D12Resource *dest, int64_t dest_offset,
                  ID3D12Resource *src, int64_t src_offset, int64_t num_bytes);
  void CopyBufferToTexture(GpuTexture *dest, uint32_t dest_subresource,
                           ID3D12Resource *src,
                           D3D12_PLACED_SUBRESOURCE_FOOTPRINT src_footprint);
  void TransitionTexture(GpuTexture *texture, uint32_t subresource,
                         D3D12_RESOURCE_STATES state_after);

  // Marks a dynamic buffer that needs to be persisted at the end of the frame.
  void MarkBufferForPersist(Buffer *buffer);

  template <typename T>
  void MarkResourceAsUsed(InternalPtr<T> resource) {
    frame_resources_to_free_.at(current_back_buffer_)
        .push_back(InternalPtr<RefCounted>(resource.Get()));
  }

#ifdef DX8TO12_USE_ALLOCATOR
  D3D12MA::Allocator *allocator() { return allocator_.get(); }
  ComPtr<D3D12MA::Allocator> allocator_;
#endif

  // IDirect3DDevice8 implementation.
 public:
#undef PURE
#define PURE VIRT_NOT_IMPLEMENTED

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObj) override;
  ULONG STDMETHODCALLTYPE AddRef(THIS) override { return RefCounted::AddRef(); }
  ULONG STDMETHODCALLTYPE Release(THIS) override {
    return RefCounted::Release();
  }

  /*** IDirect3DDevice8 methods ***/
  virtual HRESULT STDMETHODCALLTYPE TestCooperativeLevel(THIS) override;
  virtual UINT STDMETHODCALLTYPE GetAvailableTextureMem(THIS) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  ResourceManagerDiscardBytes(DWORD Bytes) override {
    TRACE_ENTRY(Bytes);
    return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D8 **ppD3D8) override {
    *ppD3D8 = direct3d8_.get();
    direct3d8_->AddRef();
    return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS8 *pCaps) override;
  virtual HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE *pMode) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) PURE;
  virtual HRESULT STDMETHODCALLTYPE SetCursorProperties(
      UINT XHotSpot, UINT YHotSpot, IDirect3DSurface8 *pCursorBitmap) PURE;
  virtual void STDMETHODCALLTYPE SetCursorPosition(int X, int Y,
                                                   DWORD Flags) PURE;
  virtual BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pPresentationParameters,
                            IDirect3DSwapChain8 **pSwapChain) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) override;
  virtual HRESULT STDMETHODCALLTYPE
  Present(CONST RECT *pSourceRect, CONST RECT *pDestRect,
          HWND hDestWindowOverride, CONST RGNDATA *pDirtyRegion) override;
  virtual HRESULT STDMETHODCALLTYPE
  GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE Type,
                IDirect3DSurface8 **ppBackBuffer) override;
  virtual HRESULT STDMETHODCALLTYPE
  GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) PURE;
  virtual void STDMETHODCALLTYPE SetGammaRamp(DWORD Flags,
                                              CONST D3DGAMMARAMP *pRamp) PURE;
  virtual void STDMETHODCALLTYPE GetGammaRamp(D3DGAMMARAMP *pRamp) PURE;
  virtual HRESULT STDMETHODCALLTYPE CreateTexture(
      UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
      D3DPOOL Pool, IDirect3DTexture8 **ppTexture) override;
  virtual HRESULT STDMETHODCALLTYPE
  CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels,
                      DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                      IDirect3DVolumeTexture8 **ppVolumeTexture) PURE;
  virtual HRESULT STDMETHODCALLTYPE CreateCubeTexture(
      UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
      IDirect3DCubeTexture8 **ppCubeTexture) override;
  COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE
  CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                     IDirect3DVertexBuffer8 **ppVertexBuffer) override;
  virtual HRESULT STDMETHODCALLTYPE
  CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                    IDirect3DIndexBuffer8 **ppIndexBuffer) override;
  virtual HRESULT STDMETHODCALLTYPE
  CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format,
                     D3DMULTISAMPLE_TYPE MultiSample, BOOL Lockable,
                     IDirect3DSurface8 **ppSurface) PURE;
  virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(
      UINT Width, UINT Height, D3DFORMAT Format,
      D3DMULTISAMPLE_TYPE MultiSample, IDirect3DSurface8 **ppSurface) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format,
                     IDirect3DSurface8 **ppSurface) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  CopyRects(IDirect3DSurface8 *pSourceSurface, CONST RECT *pSourceRectsArray,
            UINT cRects, IDirect3DSurface8 *pDestinationSurface,
            CONST POINT *pDestPointsArray) override;
  virtual HRESULT STDMETHODCALLTYPE
  UpdateTexture(IDirect3DBaseTexture8 *pSourceTexture,
                IDirect3DBaseTexture8 *pDestinationTexture) override;
  virtual HRESULT STDMETHODCALLTYPE
  GetFrontBuffer(IDirect3DSurface8 *pDestSurface) PURE;
  HRESULT STDMETHODCALLTYPE
  SetRenderTarget(IDirect3DSurface8 *pRenderTarget,
                  IDirect3DSurface8 *pNewZStencil) override;
  virtual HRESULT STDMETHODCALLTYPE
  GetRenderTarget(IDirect3DSurface8 **ppRenderTarget) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  GetDepthStencilSurface(IDirect3DSurface8 **ppZStencilSurface) override;
  virtual HRESULT STDMETHODCALLTYPE BeginScene(THIS) override;
  virtual HRESULT STDMETHODCALLTYPE EndScene(THIS) override;
  virtual HRESULT STDMETHODCALLTYPE Clear(DWORD Count, CONST D3DRECT *pRects,
                                          DWORD Flags, D3DCOLOR Color, float Z,
                                          DWORD Stencil) override;
  virtual HRESULT STDMETHODCALLTYPE
  SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix) override;
  virtual HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State,
                                                 D3DMATRIX *pMatrix) override;
  virtual HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE,
                                                      CONST D3DMATRIX *) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  SetViewport(CONST D3DVIEWPORT8 *pViewport) override;
  virtual HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8 *pViewport) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  SetMaterial(CONST D3DMATERIAL8 *pMaterial) override;
  virtual HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL8 *pMaterial) PURE;
  virtual HRESULT STDMETHODCALLTYPE SetLight(DWORD Index,
                                             CONST D3DLIGHT8 *) override;
  virtual HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT8 *) PURE;
  virtual HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index,
                                                BOOL Enable) override;
  virtual HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index,
                                                   BOOL *pEnable) PURE;
  virtual HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index,
                                                 CONST float *pPlane) PURE;
  virtual HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index,
                                                 float *pPlane) PURE;
  virtual HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State,
                                                   DWORD Value) override;
  virtual HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State,
                                                   DWORD *pValue) PURE;
  virtual HRESULT STDMETHODCALLTYPE BeginStateBlock(THIS) PURE;
  virtual HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD *pToken) PURE;
  virtual HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD Token) PURE;
  virtual HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD Token) PURE;
  virtual HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD Token) PURE;
  virtual HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type,
                                                     DWORD *pToken) override {
    return S_OK;
  }
  virtual HRESULT STDMETHODCALLTYPE
  SetClipStatus(CONST D3DCLIPSTATUS8 *pClipStatus) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  GetClipStatus(D3DCLIPSTATUS8 *pClipStatus) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  GetTexture(DWORD Stage, IDirect3DBaseTexture8 **ppTexture) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  SetTexture(DWORD Stage, IDirect3DBaseTexture8 *pTexture) override;
  virtual HRESULT STDMETHODCALLTYPE GetTextureStageState(
      DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) override;
  virtual HRESULT STDMETHODCALLTYPE SetTextureStageState(
      DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override;
  virtual HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD *pNumPasses) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  GetInfo(DWORD DevInfoID, void *pDevInfoStruct,
          DWORD DevInfoStructSize) VIRT_NOT_IMPLEMENTED;
  virtual HRESULT STDMETHODCALLTYPE
  SetPaletteEntries(UINT PaletteNumber, CONST PALETTEENTRY *pEntries) override {
    return D3DERR_NOTAVAILABLE;
  }
  virtual HRESULT STDMETHODCALLTYPE
  GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY *pEntries) override {
    return D3DERR_NOTAVAILABLE;
  }
  virtual HRESULT STDMETHODCALLTYPE
  SetCurrentTexturePalette(UINT PaletteNumber) override {
    return D3DERR_NOTAVAILABLE;
  }
  virtual HRESULT STDMETHODCALLTYPE
  GetCurrentTexturePalette(UINT *PaletteNumber) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex,
                UINT PrimitiveCount) override;
  virtual HRESULT STDMETHODCALLTYPE
  DrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT minIndex, UINT NumVertices,
                       UINT startIndex, UINT primCount) override;
  virtual HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(
      D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
      CONST void *pVertexStreamZeroData, UINT VertexStreamZeroStride) override;
  virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(
      D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
      UINT NumVertexIndices, UINT PrimitiveCount, CONST void *pIndexData,
      D3DFORMAT IndexDataFormat, CONST void *pVertexStreamZeroData,
      UINT VertexStreamZeroStride) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount,
                  IDirect3DVertexBuffer8 *pDestBuffer, DWORD Flags) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  CreateVertexShader(CONST DWORD *pDeclaration, CONST DWORD *pFunction,
                     DWORD *pHandle, DWORD Usage) override;
  virtual HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD Handle) override;
  virtual HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD *pHandle) PURE;
  HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD Handle) override;
  HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(
      DWORD Register, CONST void *pConstantData, DWORD ConstantCount) override;
  virtual HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(
      DWORD Register, void *pConstantData, DWORD ConstantCount) PURE;
  virtual HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(
      DWORD Handle, void *pData, DWORD *pSizeOfData) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  GetVertexShaderFunction(DWORD Handle, void *pData, DWORD *pSizeOfData) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 *pStreamData,
                  UINT Stride) override;
  virtual HRESULT STDMETHODCALLTYPE
  GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 **ppStreamData,
                  UINT *pStride) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  SetIndices(IDirect3DIndexBuffer8 *pIndexData, UINT BaseVertexIndex) override;
  virtual HRESULT STDMETHODCALLTYPE
  GetIndices(IDirect3DIndexBuffer8 **ppIndexData, UINT *pBaseVertexIndex) PURE;
  HRESULT STDMETHODCALLTYPE CreatePixelShader(CONST DWORD *pFunction,
                                              DWORD *pHandle) override;
  HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD Handle) override;
  virtual HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD *pHandle) PURE;
  HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD Handle) override;
  virtual HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(
      DWORD Register, CONST void *pConstantData, DWORD ConstantCount) PURE;
  virtual HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(
      DWORD Register, void *pConstantData, DWORD ConstantCount) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  GetPixelShaderFunction(DWORD Handle, void *pData, DWORD *pSizeOfData) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  DrawRectPatch(UINT Handle, CONST float *pNumSegs,
                CONST D3DRECTPATCH_INFO *pRectPatchInfo) PURE;
  virtual HRESULT STDMETHODCALLTYPE
  DrawTriPatch(UINT Handle, CONST float *pNumSegs,
               CONST D3DTRIPATCH_INFO *pTriPatchInfo) PURE;
  virtual HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) PURE;

  // Is called only on device reset. Does not advance current frame. Does not
  // adjust persist any dynamic buffers.
  void SubmitAndWait(bool should_present);
  void WaitForFrame(uint64_t frame_number);

#undef PURE
#define PURE = 0

 private:
  static constexpr DWORD kFirstShaderHandle =
      0x10000;  // Assume worst-case 0xFFFF FVF flag usage.

  static constexpr bool debug_lockstep_ = true;

  // Called both during normal construction and after Reset.
  HRESULT Init(const D3DPRESENT_PARAMETERS &presentParams);
  void InitRootSignatures();

  ComPtr<ID3D12PipelineState> CreatePSO(D3DPRIMITIVETYPE d3d8_prim_type);
  HRESULT PrepareDrawCall(D3DPRIMITIVETYPE PrimitiveType, int start_vertex,
                          int num_vertices);

  // Empties buffers_to_persist_, releases any frame resources, advances current
  // frame.
  void FreeFrameResources(uint64_t frame_number);

  D3DMATRIX GetTransform(D3DTRANSFORMSTATETYPE state);

  int ref_count_;

  ComPtr<IDirect3D8> direct3d8_;  // Have to hold on for GetDirect3D.
  HWND window_ = nullptr;
  ComPtr<IDXGIFactory2> dxgi_factory_;
  ComPtr<ID3D12Device> d3d12_device_;
  ComPtr<IDXGISwapChain3> swap_chain_;
  ComPtr<IDXGIAdapter> adapter_;
  ComPtr<IDXGIOutput> adapter_output_;
  int adapter_index_;

  ComPtr<ID3D12CommandQueue> cmd_queue_;
  std::array<ComPtr<ID3D12CommandAllocator>, kNumBackBuffers> cmd_allocators_;
  ComPtr<ID3D12GraphicsCommandList>
      cmd_list_;  // Main list used for everything.

  ComPtr<ID3D12Fence> cmd_list_done_fence_;
  HANDLE cmd_list_done_event_handle_ = nullptr;

  int current_back_buffer_ = 0;
  std::array<uint64_t, kNumBackBuffers> fence_values_ = {};
  uint64_t next_fence_ = 1;

  ComPtr<ID3D12Debug5> debug_interface_;
  ComPtr<ID3D12InfoQueue1> info_queue_;
  DWORD info_queue_cookie_;

  D3DCAPS8 caps_;

  std::vector<ComPtr<GpuTexture>> back_buffers_;

  ComPtr<GpuTexture> depth_stencil_tex_;

  // Shader resources/handles.
  // TODO: Don't count shader references, instead make PSO own shaders and count
  // PSO references instead.
  std::unordered_map<DWORD, InternalPtr<VertexShader>> vertex_shaders_;
  std::unordered_map<DWORD, InternalPtr<PixelShader>> pixel_shaders_;
  DWORD next_shader_handle_ = kFirstShaderHandle;

  // Render state.

  // Explicitly-set render targets.
  InternalPtr<GpuTexture> bound_render_target_;
  InternalPtr<GpuTexture> bound_depth_target_;

  // Viewport.
  D3D12_VIEWPORT viewport_ = {.MaxDepth = 1.f};
  // Material.
  D3DMATERIAL8 material_ = {};
  // Light definitions.
  std::unordered_map<DWORD, D3DLIGHT8> lights_;
  // Which lights are enabled.
  std::unordered_set<DWORD> enabled_lights_;  // TODO: Use bitfield.
  // Transforms.
  // TODO: Don't store in unordered map.. Don't be lazy..
  std::unordered_map<D3DTRANSFORMSTATETYPE, D3DMATRIX> transforms_;
  // Bound textures.
  std::array<InternalPtr<GpuTexture>, kMaxTexStages> bound_textures_;
  // Bound vertex streams.
  std::array<InternalPtr<Buffer>, kMaxVertexStreams>
      bound_vertex_streams_;  // Have to store as vertex buffer because of
                              // ambiguous cast.
  InternalPtr<Buffer> bound_index_buffer_;
  uint32_t bound_base_vertex_ = 0;

  // Bound vertex shader.
  DWORD bound_vertex_shader_ = 0;
  DWORD bound_pixel_shader_ = 0;

  // Bound vertex/pixel shader constants.
  std::vector<DirectX::SimpleMath::Vector4> bound_vs_cregs_;

  RenderState render_state_;
  std::array<TextureStageState, kMaxTexStages> texture_stage_states_;

  // Internal rendering resources.

  std::unordered_map<PSOState, ComPtr<ID3D12PipelineState>> pso_cache_;
  std::unordered_map<PixelShaderState, ComPtr<ID3DBlob>> ps_cache_;
  std::unordered_map<SamplerDesc, D3D12_GPU_DESCRIPTOR_HANDLE> sampler_cache_;

  enum DirtyFlags : uint32_t {
    DIRTY_FLAG_CMD_LIST_CLOSED = 0x00000001,
    DIRTY_FLAG_OM = DIRTY_FLAG_CMD_LIST_CLOSED << 1,
    DIRTY_FLAG_TRANSFORMS = DIRTY_FLAG_OM << 1,
    DIRTY_FLAG_VS_CBUFFER = DIRTY_FLAG_TRANSFORMS << 1,
    DIRTY_FLAG_PS_CBUFFER = DIRTY_FLAG_VS_CBUFFER << 1,
    DIRTY_FLAG_PS_TEXTURES = DIRTY_FLAG_PS_CBUFFER << 1,
    DIRTY_FLAG_PS_SAMPLERS = DIRTY_FLAG_PS_TEXTURES << 1,
    DIRTY_FLAG_LIGHTS = DIRTY_FLAG_PS_SAMPLERS << 1,

    DIRTY_FLAG_ALL = DIRTY_FLAG_LIGHTS | (DIRTY_FLAG_LIGHTS - 1),
    DIRTY_FLAG_ALL_RESOURCES = DIRTY_FLAG_ALL & ~DIRTY_FLAG_CMD_LIST_CLOSED,
  };

  DirtyFlags dirty_flags_ = DIRTY_FLAG_ALL;

  std::unique_ptr<DynamicRingBuffer> dynamic_ring_buffer_;

  ComPtr<Buffer> vs_cbuffer_;
  ComPtr<Buffer> lights_cbuffer_;
  ComPtr<Buffer> ps_cbuffer_;

  // Constant buffers used to store constants for the programmable vertex and
  // pixel shaders.
  ComPtr<Buffer> vs_creg_cbuffer_;
  ComPtr<Buffer> ps_creg_cbuffer_;

  ComPtr<ID3D12RootSignature> main_root_sig_;
  unsigned int textures_start_bindslot_ = UINT32_MAX;

  DescriptorPoolHeap rtv_heap_;
  DescriptorPoolHeap srv_heap_;
  DescriptorPoolHeap sampler_heap_;
  DescriptorPoolHeap dsv_heap_;

  std::array<std::vector<InternalPtr<RefCounted>>, kNumBackBuffers>
      frame_resources_to_free_;
  std::unordered_set<ComPtr<Buffer>> buffers_to_persist_;

  // TODO: Make macro for this. Or just make dirty_flags_ an int.
  friend DirtyFlags &operator|=(DirtyFlags &, DirtyFlags);
  friend DirtyFlags &operator^=(DirtyFlags &, DirtyFlags);
};

static constexpr D3D12_HEAP_PROPERTIES kSystemMemHeapProps = {
    .Type = D3D12_HEAP_TYPE_CUSTOM,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_L0};

ComPtr<ID3DBlob> CreatePixelShaderFromState(const PixelShaderState &s);

}  // namespace Dx8to12
