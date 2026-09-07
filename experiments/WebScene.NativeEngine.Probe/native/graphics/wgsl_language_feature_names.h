#pragma once
#include <webgpu/webgpu_cpp.h>
#include <array>
#include <string_view>
#include <vector>
#include <stdexcept>
namespace webscene::graphics {
// Explicit allowlist from https://www.w3.org/TR/WGSL/#language-extensions-sec
// reviewed 2026-09-07 against the pinned Dawn SDK. Never enumerate Chromium
// testing/experimental extensions into the browser-facing capability snapshot.
struct wgsl_language_feature_name { std::string_view name; wgpu::WGSLLanguageFeatureName native; };
inline constexpr std::array<wgsl_language_feature_name,13> wgsl_language_feature_names{{
    {"readonly_and_readwrite_storage_textures",wgpu::WGSLLanguageFeatureName::ReadonlyAndReadwriteStorageTextures},
    {"packed_4x8_integer_dot_product",wgpu::WGSLLanguageFeatureName::Packed4x8IntegerDotProduct},
    {"unrestricted_pointer_parameters",wgpu::WGSLLanguageFeatureName::UnrestrictedPointerParameters},
    {"pointer_composite_access",wgpu::WGSLLanguageFeatureName::PointerCompositeAccess},
    {"uniform_buffer_standard_layout",wgpu::WGSLLanguageFeatureName::UniformBufferStandardLayout},
    {"subgroup_id",wgpu::WGSLLanguageFeatureName::SubgroupId},
    {"subgroup_uniformity",wgpu::WGSLLanguageFeatureName::SubgroupUniformity},
    {"texture_and_sampler_let",wgpu::WGSLLanguageFeatureName::TextureAndSamplerLet},
    {"texture_formats_tier1",wgpu::WGSLLanguageFeatureName::TextureFormatsTier1},
    {"linear_indexing",wgpu::WGSLLanguageFeatureName::LinearIndexing},
    {"immediate_address_space",wgpu::WGSLLanguageFeatureName::ImmediateAddressSpace},
    {"fragment_depth",wgpu::WGSLLanguageFeatureName::FragmentDepth},
    {"buffer_view",wgpu::WGSLLanguageFeatureName::BufferView},
}};
inline std::vector<std::string_view> supported_wgsl_language_feature_names(const wgpu::Instance& instance) {
    if (!instance) throw std::invalid_argument("WGSL features require a native instance");
    std::vector<std::string_view> result;
    for (const auto& feature:wgsl_language_feature_names)
        if (instance.HasWGSLLanguageFeature(feature.native)) result.push_back(feature.name);
    return result;
}
} // namespace webscene::graphics
