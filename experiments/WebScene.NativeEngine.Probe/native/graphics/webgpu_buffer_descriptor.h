#pragma once
#include <webgpu/webgpu_cpp.h>
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace webscene::graphics {
struct webgpu_buffer_descriptor {
    std::string label;
    uint64_t size{};
    uint32_t usage{};
    bool mapped_at_creation{};
};
// GPUBufferUsage from the pinned browser IDL. Dawn additionally supports private
// flags (currently TexelBuffer at 0x400); never pass browser bits by raw cast.
inline std::optional<wgpu::BufferUsage> webgpu_buffer_usage(uint32_t bits) {
    constexpr std::array<wgpu::BufferUsage,10> flags{
        wgpu::BufferUsage::MapRead,wgpu::BufferUsage::MapWrite,
        wgpu::BufferUsage::CopySrc,wgpu::BufferUsage::CopyDst,
        wgpu::BufferUsage::Index,wgpu::BufferUsage::Vertex,
        wgpu::BufferUsage::Uniform,wgpu::BufferUsage::Storage,
        wgpu::BufferUsage::Indirect,wgpu::BufferUsage::QueryResolve};
    if (bits & ~0x3ffu) return std::nullopt;
    auto result=wgpu::BufferUsage::None;
    for (size_t i=0;i<flags.size();++i) if (bits & (1u<<i)) result|=flags[i];
    return result;
}
// Borrowed descriptor: label storage must survive the synchronous Dawn call.
// Unknown bits require a WebGPU validation error, not a WebIDL TypeError. The
// caller must handle nullopt through its validation/error-object path.
inline std::optional<wgpu::BufferDescriptor> make_dawn_buffer_descriptor(const webgpu_buffer_descriptor& source) {
    const auto usage=webgpu_buffer_usage(source.usage);
    if (!usage) return std::nullopt;
    wgpu::BufferDescriptor result{};
    result.label=wgpu::StringView(source.label.data(),source.label.size());
    result.size=source.size;
    result.usage=*usage;
    result.mappedAtCreation=source.mapped_at_creation;
    return result;
}
// Reject temporaries: they would leave a dangling label in the borrowed result.
std::optional<wgpu::BufferDescriptor> make_dawn_buffer_descriptor(webgpu_buffer_descriptor&&)=delete;
std::optional<wgpu::BufferDescriptor> make_dawn_buffer_descriptor(const webgpu_buffer_descriptor&&)=delete;
} // namespace webscene::graphics
