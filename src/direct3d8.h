#pragma once

#include <vector>

#include "d3d8.h"
#include "util.h"

interface IDXGIAdapter;
interface IDXGIFactory2;
interface IDXGIOutput;
interface ID3D12Device;

namespace Dx8to12 {
class Direct3D8 : public IDirect3D8, RefCounted {
 public:
  Direct3D8();
  virtual ~Direct3D8();

  virtual __declspec(nothrow) HRESULT STDMETHODCALLTYPE
      QueryInterface(REFIID riid, void **ppvObj) override;

  virtual __declspec(nothrow) ULONG STDMETHODCALLTYPE AddRef() override {
    return RefCounted::AddRef();
  }

  virtual __declspec(nothrow) ULONG STDMETHODCALLTYPE Release() override {
    return RefCounted::Release();
  }

  virtual __declspec(nothrow) HRESULT STDMETHODCALLTYPE
      RegisterSoftwareDevice(void *pInitializeFunction) override {
    NOT_IMPLEMENTED();
  }

  virtual __declspec(nothrow) UINT STDMETHODCALLTYPE GetAdapterCount() override;

  virtual __declspec(nothrow) HRESULT __stdcall GetAdapterIdentifier(
      UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER8 *pIdentifier) override;

  virtual __declspec(nothrow) UINT
      __stdcall GetAdapterModeCount(UINT Adapter) override;

  virtual __declspec(nothrow) HRESULT
      __stdcall EnumAdapterModes(UINT Adapter, UINT Mode,
                                 D3DDISPLAYMODE *pMode) override;

  virtual __declspec(nothrow) HRESULT
      __stdcall GetAdapterDisplayMode(UINT Adapter,
                                      D3DDISPLAYMODE *pMode) override;

  virtual __declspec(nothrow) HRESULT
      __stdcall CheckDeviceType(UINT Adapter, D3DDEVTYPE CheckType,
                                D3DFORMAT DisplayFormat,
                                D3DFORMAT BackBufferFormat,
                                BOOL Windowed) override;

  virtual __declspec(nothrow) HRESULT
      __stdcall CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType,
                                  D3DFORMAT AdapterFormat, DWORD Usage,
                                  D3DRESOURCETYPE RType,
                                  D3DFORMAT CheckFormat) override;

  virtual __declspec(nothrow) HRESULT __stdcall CheckDeviceMultiSampleType(
      UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat,
      BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType) override {
    NOT_IMPLEMENTED();
  }

  virtual __declspec(nothrow) HRESULT
      __stdcall CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType,
                                       D3DFORMAT AdapterFormat,
                                       D3DFORMAT RenderTargetFormat,
                                       D3DFORMAT DepthStencilFormat) override {
    return S_OK;
  }

  virtual __declspec(nothrow) HRESULT
      __stdcall GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType,
                              D3DCAPS8 *pCaps) override;

  virtual __declspec(nothrow) HMONITOR
      __stdcall GetAdapterMonitor(UINT Adapter) override;

  virtual __declspec(nothrow) HRESULT __stdcall CreateDevice(
      UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
      DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters,
      IDirect3DDevice8 **ppReturnedDeviceInterface) override;

 private:
  IDXGIOutput *GetDefaultOutputFor(UINT Adapter) {
    // TODO: Support more than one output.
    return adapter_outputs_.at(Adapter).at(0);
  }

  ComPtr<IDXGIFactory2> dxgi_factory_;

  std::vector<IDXGIAdapter *> adapters_;
  std::vector<std::vector<IDXGIOutput *>> adapter_outputs_;
};
}  // namespace Dx8to12