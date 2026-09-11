#pragma once
#include "dawn_dxgi_image.h"
#include "d3d12_canvas_images.h"
#if defined(_WIN32)
namespace webscene::graphics {
inline std::shared_ptr<dawn_dxgi_image> import_dawn_dxgi_canvas_texture(
    const d3d12_canvas_images::frame& frame,const wgpu::Device& device,
    const wgpu::TextureDescriptor& descriptor,ID3D12Device* allocator) {
    dxgi_endpoint endpoint;
    if (FAILED(identify_dxgi_endpoint(allocator,endpoint)))return {};
    // Alpha is a presentation interpretation of these four-channel formats.
    endpoint.alpha_modes=3;
    endpoint.shared_fence=device.HasFeature(wgpu::FeatureName::SharedFenceDXGISharedHandle);
    std::unique_ptr<dawn_dxgi_image> image;
    if(dawn_dxgi_image::import(device,frame.color->borrowed_handle(),endpoint,endpoint,
        frame.metadata,descriptor.usage,image)!=dxgi_import_status::success)return {};
    return std::shared_ptr<dawn_dxgi_image>(std::move(image));
}
}
#endif
