#pragma once
#include "v8_webgpu_canvas_configuration.h"
#include "v8_webgpu_texture_descriptor.h"
namespace webscene::graphics {
// Content-side checks specific to canvas formats/usage. Required-format feature
// checks must precede these in configure; native texture validation follows.
inline void validate_webgpu_canvas_format_usage(const webgpu_canvas_configuration& configuration) {
    switch(configuration.format) {
        case wgpu::TextureFormat::RGBA8Unorm:
        case wgpu::TextureFormat::BGRA8Unorm:
        case wgpu::TextureFormat::RGBA16Float:break;
        default:throw std::invalid_argument("Unsupported WebGPU canvas format");
    }
    if(configuration.usage&0x20u)throw std::invalid_argument("Canvas textures cannot use TRANSIENT_ATTACHMENT");
}
// Snapshot the canvas bitmap size, not its CSS layout dimensions. In particular,
// zero dimensions remain zero for subsequent native validation, and a custom
// usage does not acquire RENDER_ATTACHMENT or presenter-only access implicitly.
inline webgpu_texture_descriptor webgpu_canvas_texture_descriptor(const webgpu_canvas_configuration& configuration,uint32_t width,uint32_t height) {
    webgpu_texture_descriptor result;
    result.size={width,height,1};result.format=configuration.format;result.usage=configuration.usage;
    result.view_formats=configuration.view_formats;
    return result;
}
} // namespace webscene::graphics
