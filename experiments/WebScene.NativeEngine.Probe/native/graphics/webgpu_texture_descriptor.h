#pragma once
#include <webgpu/webgpu_cpp.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace webscene::graphics {
struct webgpu_texture_descriptor {
    std::string label;
    wgpu::TextureDimension dimension=wgpu::TextureDimension::e2D;
    wgpu::TextureFormat format=wgpu::TextureFormat::Undefined;
    uint32_t mip_levels=1,samples=1,usage=0;
    wgpu::Extent3D size{0,1,1};
    bool valid_extent_shape=true;
    wgpu::TextureViewDimension binding_dimension=wgpu::TextureViewDimension::Undefined;
    std::vector<wgpu::TextureFormat> view_formats;
    template<class Execute> void with_native(Execute execute) const & {
        if(!valid_extent_shape)throw std::invalid_argument("Invalid texture extent shape");
        wgpu::TextureDescriptor result{};result.label=wgpu::StringView(label.data(),label.size());
        result.dimension=dimension;result.format=format;result.mipLevelCount=mip_levels;result.sampleCount=samples;result.size=size;
        // Exclude host-only usage bits. Invalid browser masks remain invalid
        // native descriptors so Dawn reports validation instead of enabling them.
        result.usage=(usage&~0x3fu)?wgpu::TextureUsage::None:static_cast<wgpu::TextureUsage>(usage);
        result.viewFormatCount=view_formats.size();result.viewFormats=view_formats.data();
        wgpu::TextureBindingViewDimension binding{};binding.textureBindingViewDimension=binding_dimension;
        if(binding_dimension!=wgpu::TextureViewDimension::Undefined)result.nextInChain=&binding;
        execute(result);
    }
};
} // namespace webscene::graphics
