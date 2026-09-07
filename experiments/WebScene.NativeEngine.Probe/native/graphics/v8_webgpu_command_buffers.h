#pragma once
#include "v8_webgpu_labeled_resources.h"
namespace webscene::graphics {
struct v8_webgpu_command_buffers_traits {
    using native_type=wgpu::CommandBuffer;
    static constexpr const char* name="GPUCommandBuffer";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute) {
        device.with_command_buffer(handle,std::move(execute));
    }
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle) noexcept {
        return graphics_service::deferred_command_buffer_release(device,handle);
    }
};
using v8_webgpu_command_buffers=v8_webgpu_labeled_resources<v8_webgpu_command_buffers_traits>;
} // namespace webscene::graphics
