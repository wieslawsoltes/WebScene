#pragma once
#include "v8_webgpu_labeled_resources.h"
namespace webscene::graphics {
struct v8_webgpu_pipeline_layouts_traits {
    using native_type=wgpu::PipelineLayout;
    static constexpr const char* name="GPUPipelineLayout";
    template<class Execute> static void with(dawn_device& device,resource_handle<native_type> handle,Execute execute) {
        device.with_pipeline_layout(handle,std::move(execute));
    }
    static graphics_command release(resource_handle<dawn_device> device,resource_handle<native_type> handle) noexcept {
        return graphics_service::deferred_pipeline_layout_release(device,handle);
    }
};
using v8_webgpu_pipeline_layouts=v8_webgpu_labeled_resources<v8_webgpu_pipeline_layouts_traits>;
} // namespace webscene::graphics
