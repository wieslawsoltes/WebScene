#pragma once
#include "v8_webgpu_labeled_resources.h"
namespace webscene::graphics {
struct v8_webgpu_texture_views_traits {
    using native_type=wgpu::TextureView;
    static constexpr const char* name="GPUTextureView";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute) {
        device.with_texture_view(handle,std::move(execute));
    }
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle) noexcept {
        return graphics_service::deferred_texture_view_release(device,handle);
    }
};
using v8_webgpu_texture_views=v8_webgpu_labeled_resources<v8_webgpu_texture_views_traits>;
} // namespace webscene::graphics
