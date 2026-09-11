#pragma once
#include "webgpu_canvas_texture_descriptor.h"
#include <functional>
namespace webscene::graphics {
// Host callbacks run on the engine thread, invoke no JavaScript, and must retain
// imported allocations through GPU completion. They never implement CPU copies.
struct webgpu_canvas_host {
    std::function<void(const webgpu_canvas_configuration&)> validate;
    std::function<wgpu::Texture(const webgpu_canvas_configuration&,const webgpu_texture_descriptor&)> acquire;
    std::function<void(const wgpu::Texture&,bool present)> retire;
    std::function<void()> invalidate;
};
} // namespace webscene::graphics
