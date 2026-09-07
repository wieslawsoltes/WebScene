#pragma once
#include "v8_webgpu_labeled_resources.h"
namespace webscene::graphics {
struct v8_webgpu_render_pipelines_traits {
    using native_type=wgpu::RenderPipeline;
    static constexpr const char* name="GPURenderPipeline";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute) {
        device.with_render_pipeline(handle,std::move(execute));
    }
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle) noexcept {
        return graphics_service::deferred_render_pipeline_release(device,handle);
    }
};
using v8_webgpu_render_pipelines=v8_webgpu_labeled_resources<v8_webgpu_render_pipelines_traits>;
} // namespace webscene::graphics
