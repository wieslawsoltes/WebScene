#pragma once
#include <webgpu/webgpu_cpp.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace webscene::graphics {
struct webgpu_canvas_configuration {
    wgpu::Device device;
    wgpu::TextureFormat format=wgpu::TextureFormat::Undefined;
    uint32_t usage=0x10;
    std::vector<wgpu::TextureFormat> view_formats;
    std::string alpha_mode="opaque",color_space="srgb",tone_mapping="standard";
};
} // namespace webscene::graphics
