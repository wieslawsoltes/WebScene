#pragma once
#include "v8_webgpu_labeled_resources.h"
namespace webscene::graphics {
struct v8_webgpu_samplers_traits {
    using native_type=wgpu::Sampler;
    static constexpr const char* name="GPUSampler";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute) {
        device.with_sampler(handle,std::move(execute));
    }
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle) noexcept {
        return graphics_service::deferred_sampler_release(device,handle);
    }
};
using v8_webgpu_samplers=v8_webgpu_labeled_resources<v8_webgpu_samplers_traits>;
} // namespace webscene::graphics
