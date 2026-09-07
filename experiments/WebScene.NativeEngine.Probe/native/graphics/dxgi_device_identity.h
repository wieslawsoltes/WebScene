#pragma once
#include "dxgi_bridge_contract.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace webscene::graphics {
// LUIDs identify adapters within the current system boot, not persistent hardware
// identities. Query the actual devices; vendor/device IDs are not sufficient.
inline HRESULT query_adapter_luid(ID3D11Device* device,adapter_luid& result) noexcept {
    result={};
    if (!device) return E_INVALIDARG;
    HRESULT status=device->GetDeviceRemovedReason();
    if (FAILED(status)) return status;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    status=device->QueryInterface(IID_PPV_ARGS(&dxgi));
    if (FAILED(status)) return status;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    status=dxgi->GetAdapter(&adapter);
    if (FAILED(status)) return status;
    DXGI_ADAPTER_DESC description{};
    status=adapter->GetDesc(&description);
    if (FAILED(status)) return status;
    result={description.AdapterLuid.LowPart,description.AdapterLuid.HighPart,true};
    return S_OK;
}
inline HRESULT query_adapter_luid(ID3D12Device* device,adapter_luid& result) noexcept {
    result={};
    if (!device) return E_INVALIDARG;
    const auto status=device->GetDeviceRemovedReason();
    if (FAILED(status)) return status;
    const auto luid=device->GetAdapterLuid();
    result={luid.LowPart,luid.HighPart,true};
    return S_OK;
}
// Do not retain stale endpoint identity on a failed device query. Other endpoint
// fields still require separate format/usage and synchronization qualification.
inline HRESULT identify_dxgi_endpoint(ID3D11Device* device,dxgi_endpoint& endpoint) noexcept {
    endpoint={}; endpoint.api=dxgi_api::d3d11;
    return query_adapter_luid(device,endpoint.adapter);
}
inline HRESULT identify_dxgi_endpoint(ID3D12Device* device,dxgi_endpoint& endpoint) noexcept {
    endpoint={}; endpoint.api=dxgi_api::d3d12;
    return query_adapter_luid(device,endpoint.adapter);
}
} // namespace webscene::graphics
#endif
