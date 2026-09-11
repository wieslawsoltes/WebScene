#pragma once
#if defined(_WIN32)
#include "dxgi_device_identity.h"
#include <dxgi1_4.h>
#include <stdexcept>

namespace webscene::graphics {
// Select a real DXGI adapter, then use its LUID for both Dawn discovery and
// native allocation. The presenter independently checks its actual device.
// No process-global device survives the owning canvas/document.
inline Microsoft::WRL::ComPtr<IDXGIAdapter1> windows_gpu_adapter() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))
        || FAILED(factory->EnumAdapters1(0,&adapter)))
        throw std::runtime_error("DXGI hardware adapter unavailable");
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
        throw std::runtime_error("DXGI software adapter is not a presentation backend");
    return adapter;
}
inline LUID windows_gpu_adapter_luid() {
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(windows_gpu_adapter()->GetDesc1(&description)))
        throw std::runtime_error("DXGI adapter identity unavailable");
    return description.AdapterLuid;
}
inline Microsoft::WRL::ComPtr<ID3D12Device> windows_canvas_device() {
    auto adapter=windows_gpu_adapter();
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))
        throw std::runtime_error("D3D12 canvas device unavailable");
    return device;
}
}
#endif
