#include "direct3d8.h"

#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_3.h>

#include "aixlog.hpp"
#include "device.h"

namespace Dx8to12 {
Direct3D8::Direct3D8() {
  LOG(TRACE) << "Creating Direct3D8.\n";
  UINT Flags = 0;
#ifdef DX8TO12_ENABLE_VALIDATION
  Flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
  ASSERT_HR(
      CreateDXGIFactory2(Flags, IID_PPV_ARGS(dxgi_factory_.GetForInit())));

  IDXGIAdapter *adapter;
  std::vector<IDXGIOutput *> outputs;
  HRESULT hr;
  while ((hr = dxgi_factory_->EnumAdapters(static_cast<UINT>(adapters_.size()),
                                           &adapter)) == S_OK) {
    adapters_.push_back(adapter);
    outputs.clear();
    IDXGIOutput *output;
    while ((hr = adapter->EnumOutputs(static_cast<UINT>(outputs.size()),
                                      &output)) == S_OK) {
      outputs.push_back(output);
    }
    adapter_outputs_.emplace_back(std::move(outputs));
  }
}

Direct3D8::~Direct3D8() {
  for (IDXGIAdapter *adapter : adapters_) adapter->Release();
  for (auto &outputs : adapter_outputs_) {
    for (IDXGIOutput *output : outputs) output->Release();
  }
}

HRESULT STDMETHODCALLTYPE Direct3D8::QueryInterface(REFIID riid,
                                                    void **ppvObj) {
  if (ppvObj == nullptr)
    return E_POINTER;
  else if (riid == IID_IDirect3D8 || riid == __uuidof(IUnknown)) {
    AddRef();
    *ppvObj = static_cast<IDirect3D8 *>(this);
    return S_OK;
  } else {
    FAIL("Invalid Direct3D8::QueryInterface.");
    // return E_NOINTERFACE;
  }
}

UINT STDMETHODCALLTYPE Direct3D8::GetAdapterCount() {
  return static_cast<UINT>(adapters_.size());
}

__declspec(nothrow) HRESULT STDMETHODCALLTYPE Direct3D8::GetAdapterIdentifier(
    UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER8 *pIdentifier) {
  LOG(TRACE) << "GetAdapterIdentifier(" << Adapter << "," << Flags << ")\n";
  if (Adapter >= adapters_.size()) return D3DERR_INVALIDCALL;
  IDXGIAdapter *adapter = adapters_[Adapter];

  DXGI_ADAPTER_DESC desc = {};
  HR_OR_RETURN(adapter->GetDesc(&desc));
  *pIdentifier = {};
  snprintf(pIdentifier->Driver, sizeof(pIdentifier->Driver), "D3d8to12 Driver");
  size_t num_chars_converted;
  wcstombs_s(&num_chars_converted, pIdentifier->Description, desc.Description,
             sizeof(pIdentifier->Description) - 1);
  pIdentifier->DriverVersion = LARGE_INTEGER{.QuadPart = desc.Revision};  // ?
  pIdentifier->VendorId = desc.VendorId;
  pIdentifier->DeviceId = desc.DeviceId;
  pIdentifier->SubSysId = desc.SubSysId;
  pIdentifier->Revision = desc.Revision;
  pIdentifier->DeviceIdentifier.Data1 = desc.AdapterLuid.LowPart;
  pIdentifier->DeviceIdentifier.Data2 =
      static_cast<unsigned short>(desc.AdapterLuid.HighPart);
  pIdentifier->DeviceIdentifier.Data3 =
      static_cast<unsigned short>(desc.AdapterLuid.HighPart >> 16);
  pIdentifier->WHQLLevel = 1;  // WHQL validated, but no date information.

  return S_OK;
}

UINT STDMETHODCALLTYPE Direct3D8::GetAdapterModeCount(UINT Adapter) {
  if (Adapter >= adapter_outputs_.size() || adapter_outputs_[Adapter].empty())
    return 0;
  // TODO: Support more than one output.
  IDXGIOutput *output = adapter_outputs_[Adapter][0];
  constexpr DXGI_FORMAT formats_to_check[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
  UINT total_count = 0;
  for (DXGI_FORMAT format : formats_to_check) {
    UINT count = 0;
    ASSERT_HR(output->GetDisplayModeList(format, 0, &count, nullptr));
    total_count += count;
  }
  return total_count;
}

__declspec(nothrow) HRESULT
    __stdcall Direct3D8::EnumAdapterModes(UINT Adapter, UINT Mode,
                                          D3DDISPLAYMODE *pMode) {
  LOG(TRACE) << "EnumAdapterModes(" << Adapter << "," << Mode << ");\n";
  if (Adapter >= adapter_outputs_.size() || adapter_outputs_[Adapter].empty())
    return 0;
  // TODO: Support more than one output.
  IDXGIOutput *output = adapter_outputs_[Adapter][0];
  constexpr DXGI_FORMAT formats_to_check[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
  UINT count = 0;
  ASSERT_HR(
      output->GetDisplayModeList(formats_to_check[0], 0, &count, nullptr));
  std::vector<DXGI_MODE_DESC> modes(count);  // TODO: Cache this.
  ASSERT_HR(
      output->GetDisplayModeList(formats_to_check[0], 0, &count, modes.data()));
  if (Mode >= modes.size()) return D3DERR_INVALIDCALL;
  const DXGI_MODE_DESC &mode = modes[Mode];
  pMode->Width = mode.Width;
  pMode->Height = mode.Height;
  pMode->RefreshRate =
      mode.RefreshRate.Numerator / mode.RefreshRate.Denominator;
  pMode->Format = DXGIToD3DFormat(mode.Format);
  return S_OK;
}

HRESULT
STDMETHODCALLTYPE Direct3D8::GetAdapterDisplayMode(UINT Adapter,
                                                   D3DDISPLAYMODE *pMode) {
  if (Adapter >= adapter_outputs_.size() || adapter_outputs_[Adapter].empty())
    return D3DERR_INVALIDCALL;
  // TODO: Support more than one output.
  IDXGIOutput *output = adapter_outputs_[Adapter][0];
  DXGI_OUTPUT_DESC outputDesc;
  output->GetDesc(&outputDesc);
  HMONITOR hMonitor = outputDesc.Monitor;
  MONITORINFOEX monitorInfo;
  monitorInfo.cbSize = sizeof(MONITORINFOEX);
  GetMonitorInfo(hMonitor, &monitorInfo);
  DEVMODE devMode;
  devMode.dmSize = sizeof(DEVMODE);
  devMode.dmDriverExtra = 0;
  EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode);

  DXGI_MODE_DESC current = {};
  current.Width = devMode.dmPelsWidth;
  current.Height = devMode.dmPelsHeight;
  if (devMode.dmDisplayFrequency > 1) {
    current.RefreshRate.Numerator = devMode.dmDisplayFrequency;
    current.RefreshRate.Denominator = 1;
  }
  current.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

  DXGI_MODE_DESC closestMode;
  ASSERT_HR(output->FindClosestMatchingMode(&current, &closestMode, NULL));
  pMode->Width = closestMode.Width;
  pMode->Height = closestMode.Height;
  pMode->RefreshRate = static_cast<UINT>(closestMode.RefreshRate.Numerator /
                                         closestMode.RefreshRate.Denominator);
  pMode->Format = DXGIToD3DFormat(closestMode.Format);
  return S_OK;
}

HRESULT
STDMETHODCALLTYPE Direct3D8::CheckDeviceType(UINT Adapter, D3DDEVTYPE CheckType,
                                             D3DFORMAT DisplayFormat,
                                             D3DFORMAT BackBufferFormat,
                                             BOOL Windowed) {
  if (Adapter >= adapters_.size())
    return D3DERR_INVALIDCALL;
  else if (CheckType != D3DDEVTYPE_HAL ||
           (DisplayFormat != D3DFMT_R8G8B8 && DisplayFormat != D3DFMT_A8R8G8B8))
    return D3DERR_NOTAVAILABLE;
  IDXGIAdapter *adapter = adapters_[Adapter];
  ComPtr<ID3D12Device> device;
  HR_OR_RETURN(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(device.GetForInit())));
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support{
      .Format = DXGIFromD3DFormat(BackBufferFormat)};
  if (support.Format == DXGI_FORMAT_UNKNOWN) return D3DERR_NOTAVAILABLE;
  HR_OR_RETURN(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                           &support, sizeof(support)));
  if (!HasFlag(support.Support1, D3D12_FORMAT_SUPPORT1_DISPLAY))
    return D3DERR_NOTAVAILABLE;
  return D3D_OK;
}

HRESULT
STDMETHODCALLTYPE Direct3D8::CheckDeviceFormat(
    UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage,
    D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) {
  LOG(TRACE) << "CheckDeviceFormat(" << Adapter << "," << DeviceType << ","
             << AdapterFormat << "," << Usage << "," << RType << ","
             << CheckFormat << ")\n";
  if (Adapter >= adapters_.size())
    return D3DERR_INVALIDCALL;
  else if (DeviceType != D3DDEVTYPE_HAL)
    return D3DERR_NOTAVAILABLE;
  IDXGIAdapter *adapter = adapters_[Adapter];
  ComPtr<ID3D12Device> device;
  HR_OR_RETURN(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(device.GetForInit())));
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support{.Format =
                                                DXGIFromD3DFormat(CheckFormat)};
  if (support.Format == DXGI_FORMAT_UNKNOWN) return D3DERR_NOTAVAILABLE;
  HR_OR_RETURN(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                           &support, sizeof(support)));
  bool is_valid = true;
  if (HasFlag(Usage, D3DUSAGE_RENDERTARGET)) {
    is_valid &= HasFlag(support.Support1, D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
    Usage &= ~D3DUSAGE_RENDERTARGET;
  }
  if (RType == D3DRTYPE_SURFACE) {
    is_valid &= HasFlag(support.Support1, D3D12_FORMAT_SUPPORT1_TEXTURE2D);
    if (HasFlag(Usage, D3DUSAGE_DEPTHSTENCIL)) {
      is_valid &=
          HasFlag(support.Support1, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
      Usage &= ~D3DUSAGE_DEPTHSTENCIL;
    }
    if (Usage != 0) FAIL("More usage: 0x%X", Usage);
    ASSERT(Usage == 0);
  } else if (RType == D3DRTYPE_TEXTURE) {
    is_valid &= HasFlag(support.Support1, D3D12_FORMAT_SUPPORT1_TEXTURE2D);
    ASSERT(Usage == 0);
  } else {
    FAIL("Unexpected RType %d", RType);
  }
  return is_valid ? S_OK : D3DERR_NOTAVAILABLE;
}

HRESULT
STDMETHODCALLTYPE Direct3D8::GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType,
                                           D3DCAPS8 *pCaps) {
  *pCaps = Device::GetDefaultCaps(Adapter);
  return S_OK;
}

HMONITOR
STDMETHODCALLTYPE Direct3D8::GetAdapterMonitor(UINT Adapter) {
  if (Adapter >= adapter_outputs_.size() || adapter_outputs_[Adapter].empty())
    return nullptr;
  // TODO: Support more than one output.
  IDXGIOutput *output = adapter_outputs_[Adapter][0];
  DXGI_OUTPUT_DESC outputDesc;
  output->GetDesc(&outputDesc);
  return outputDesc.Monitor;
}

HRESULT STDMETHODCALLTYPE Direct3D8::CreateDevice(
    UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS *pPresentationParameters,
    IDirect3DDevice8 **ppReturnedDeviceInterface) {
  if (Adapter >= adapter_outputs_.size() || adapter_outputs_[Adapter].empty())
    return D3DERR_INVALIDCALL;
  ASSERT(DeviceType == D3DDEVTYPE_HAL);
  ASSERT(BehaviorFlags & D3DCREATE_HARDWARE_VERTEXPROCESSING);
  ASSERT(!(BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING));
  ASSERT(!(BehaviorFlags & D3DCREATE_MULTITHREADED));
  ASSERT(!HasFlag(BehaviorFlags, D3DCREATE_DISABLE_DRIVER_MANAGEMENT));
  *ppReturnedDeviceInterface = nullptr;
  Device *device = new Device(this);
  if (!device->Create(hFocusWindow, dxgi_factory_, ComWrap(adapters_[Adapter]),
                      Adapter, *pPresentationParameters)) {
    delete device;
    return D3DERR_INVALIDDEVICE;
  }
  *ppReturnedDeviceInterface = device;
  return S_OK;
}

}  // namespace Dx8to12

extern "C" {
IDirect3D8 *WINAPI Direct3DCreate8(UINT SDKVersion) {
  return new Dx8to12::Direct3D8();
}
}

#define ACTUALLY_DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
  EXTERN_C const GUID DECLSPEC_SELECTANY name = {                             \
      l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}

ACTUALLY_DEFINE_GUID(IID_IDirect3D8, 0x1dd9e8da, 0x1c77, 0x4d40, 0xb0, 0xcf,
                     0x98, 0xfe, 0xfd, 0xff, 0x95, 0x12);
ACTUALLY_DEFINE_GUID(IID_IDirect3DDevice8, 0x7385e5df, 0x8fe8, 0x41d5, 0x86,
                     0xb6, 0xd7, 0xb4, 0x85, 0x47, 0xb6, 0xcf);

#undef ACTUALLY_DEFINE_GUID