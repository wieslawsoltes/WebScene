#pragma once
#include "v8_webgpu_labeled_resources.h"
namespace webscene::graphics {
struct v8_webgpu_bind_groups_traits {
    using native_type=wgpu::BindGroup;
    static constexpr const char* name="GPUBindGroup";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute) {
        device.with_bind_group(handle,std::move(execute));
    }
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle) noexcept {
        return graphics_service::deferred_bind_group_release(device,handle);
    }
};
using v8_webgpu_bind_groups=v8_webgpu_labeled_resources<v8_webgpu_bind_groups_traits>;
} // namespace webscene::graphics
