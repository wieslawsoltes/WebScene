#pragma once
#include "v8_webgpu_pipeline_resources.h"
namespace webscene::graphics {
struct v8_webgpu_compute_pipelines_traits {
    using native_type=wgpu::ComputePipeline;
    static constexpr const char* name="GPUComputePipeline";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute) {
        device.with_compute_pipeline(handle,std::move(execute));
    }
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle) noexcept {
        return graphics_service::deferred_compute_pipeline_release(device,handle);
    }
};
using v8_webgpu_compute_pipelines=v8_webgpu_pipeline_resources<v8_webgpu_compute_pipelines_traits>;
} // namespace webscene::graphics
