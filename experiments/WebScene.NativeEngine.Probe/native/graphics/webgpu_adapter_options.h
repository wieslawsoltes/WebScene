#pragma once
#include <webgpu/webgpu_cpp.h>
#include <optional>
#include <string>

namespace webscene::graphics {
// GPURequestAdapterOptions from @webref/idl 3.82.1. Browser dictionary fields
// remain separate from Dawn's backend/private adapter selection extensions.
struct webgpu_adapter_options {
    std::u16string feature_level=u"core";
    std::optional<wgpu::PowerPreference> power_preference;
    bool force_fallback_adapter=false;
    bool xr_compatible=false;
};
// An absent descriptor means discovery must resolve null, not submit a
// different request. Backend choice belongs to the host, never the JS dictionary.
inline std::optional<wgpu::RequestAdapterOptions> make_dawn_adapter_options(
    const webgpu_adapter_options& requested,wgpu::BackendType host_backend=wgpu::BackendType::Undefined) {
    if (requested.xr_compatible) return {}; // No WebXR device integration yet.
    wgpu::RequestAdapterOptions native{};
    if (requested.feature_level==u"core") native.featureLevel=wgpu::FeatureLevel::Core;
    else if (requested.feature_level==u"compatibility") native.featureLevel=wgpu::FeatureLevel::Compatibility;
    else return {};
    native.powerPreference=requested.power_preference.value_or(wgpu::PowerPreference::Undefined);
    native.forceFallbackAdapter=requested.force_fallback_adapter;
    native.backendType=host_backend;
    return native;
}
} // namespace webscene::graphics
