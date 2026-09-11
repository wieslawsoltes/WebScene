#pragma once
#include "image_metadata.h"

namespace webscene::graphics {
struct adapter_luid {
    uint32_t low{}; int32_t high{}; bool valid{};
    bool operator==(const adapter_luid&) const = default;
};
enum class dxgi_api { d3d11,d3d12 };
enum class dxgi_sync { none,shared_fence,keyed_mutex };
enum class dxgi_bridge_status {
    supported,unknown_adapter,cross_adapter,unsupported_format,unsupported_alpha,
    needs_resolve,unsupported_synchronization,invalid_dimensions,unsupported_api
};
struct dxgi_endpoint {
    adapter_luid adapter;
    dxgi_api api{};
    uint32_t color_formats{}; // Bit (image_format - 1).
    uint32_t alpha_modes{};   // Bit (image_alpha - 1).
    bool shared_fence{},keyed_mutex{};
};
struct dxgi_bridge_choice { dxgi_bridge_status status; dxgi_sync synchronization{dxgi_sync::none}; };
// Capability policy only. Endpoint capabilities must come from native device
// queries; this function does not claim an import or a synchronization pass.
inline dxgi_bridge_choice choose_dxgi_bridge(const dxgi_endpoint& producer,const dxgi_endpoint& consumer,
    const image_metadata& image,uint32_t samples=1) noexcept {
    if ((producer.api!=dxgi_api::d3d11 && producer.api!=dxgi_api::d3d12)
        || (consumer.api!=dxgi_api::d3d11 && consumer.api!=dxgi_api::d3d12))
        return {dxgi_bridge_status::unsupported_api};
    if (!producer.adapter.valid || !consumer.adapter.valid) return {dxgi_bridge_status::unknown_adapter};
    if (!(producer.adapter==consumer.adapter)) return {dxgi_bridge_status::cross_adapter};
    if (!image.width || !image.height) return {dxgi_bridge_status::invalid_dimensions};
    if (samples!=1) return {dxgi_bridge_status::needs_resolve};
    const auto format=static_cast<uint32_t>(image.format);
    if (format<1 || format>5 || !(producer.color_formats & consumer.color_formats & (1U<<(format-1))))
        return {dxgi_bridge_status::unsupported_format};
    const auto alpha=static_cast<uint32_t>(image.alpha);
    if (alpha<1 || alpha>3 || !(producer.alpha_modes & consumer.alpha_modes & (1U<<(alpha-1))))
        return {dxgi_bridge_status::unsupported_alpha};
    if (producer.shared_fence && consumer.shared_fence)
        return {dxgi_bridge_status::supported,dxgi_sync::shared_fence};
    // Keyed-mutex support for D3D12 combinations requires a separately proven
    // bridge. Do not silently substitute it for shared-fence synchronization.
    if (producer.api==dxgi_api::d3d11 && consumer.api==dxgi_api::d3d11
        && producer.keyed_mutex && consumer.keyed_mutex)
        return {dxgi_bridge_status::supported,dxgi_sync::keyed_mutex};
    return {dxgi_bridge_status::unsupported_synchronization};
}
} // namespace webscene::graphics
