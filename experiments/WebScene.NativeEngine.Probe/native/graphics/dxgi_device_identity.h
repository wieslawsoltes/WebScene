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
inline DXGI_FORMAT dxgi_color_format(image_format format) noexcept {
    switch (format) {
        case image_format::rgba8_unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case image_format::bgra8_unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case image_format::rgba16_float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case image_format::rgba8_srgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case image_format::bgra8_srgb: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    }
    return DXGI_FORMAT_UNKNOWN;
}
// Candidate color formats only: the allocation/import path must still validate
// sharing flags, view compatibility, alpha convention and synchronization.
inline HRESULT query_dxgi_color_formats(ID3D11Device* device,uint32_t& result) noexcept {
    result=0;
    if (!device) return E_INVALIDARG;
    uint32_t candidate=0;
    constexpr UINT required=D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET
        | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
    for (uint32_t format=1;format<=5;++format) {
        UINT support=0;
        const auto status=device->CheckFormatSupport(dxgi_color_format(static_cast<image_format>(format)),&support);
        if (FAILED(status)) return status;
        if ((support & required)==required) candidate|=1U<<(format-1);
    }
    const auto status=device->GetDeviceRemovedReason();
    if (FAILED(status)) return status;
    result=candidate; return S_OK;
}
inline HRESULT query_dxgi_color_formats(ID3D12Device* device,uint32_t& result) noexcept {
    result=0;
    if (!device) return E_INVALIDARG;
    uint32_t candidate=0;
    constexpr UINT required=D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET
        | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
    for (uint32_t format=1;format<=5;++format) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format=dxgi_color_format(static_cast<image_format>(format));
        const auto status=device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));
        if (FAILED(status)) return status;
        if ((static_cast<UINT>(support.Support1) & required)==required) candidate|=1U<<(format-1);
    }
    const auto status=device->GetDeviceRemovedReason();
    if (FAILED(status)) return status;
    result=candidate; return S_OK;
}
// Do not retain stale endpoint identity on a failed device query. Other endpoint
// fields still require allocation/import, alpha and synchronization qualification.
inline HRESULT identify_dxgi_endpoint(ID3D11Device* device,dxgi_endpoint& endpoint) noexcept {
    endpoint={}; endpoint.api=dxgi_api::d3d11;
    auto status=query_adapter_luid(device,endpoint.adapter);
    if (FAILED(status)) return status;
    status=query_dxgi_color_formats(device,endpoint.color_formats);
    if (FAILED(status)) endpoint.adapter={};
    return status;
}
inline HRESULT identify_dxgi_endpoint(ID3D12Device* device,dxgi_endpoint& endpoint) noexcept {
    endpoint={}; endpoint.api=dxgi_api::d3d12;
    auto status=query_adapter_luid(device,endpoint.adapter);
    if (FAILED(status)) return status;
    status=query_dxgi_color_formats(device,endpoint.color_formats);
    if (FAILED(status)) endpoint.adapter={};
    return status;
}
} // namespace webscene::graphics
#endif
