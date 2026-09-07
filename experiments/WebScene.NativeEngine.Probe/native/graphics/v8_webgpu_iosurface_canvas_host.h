#pragma once
#include "v8_webgpu_canvas_context.h"
#include "dawn_iosurface_canvas_host.h"
#if defined(__APPLE__)
namespace webscene::graphics {
// The document supplies canvas/generation/content and producer timeline identities. This
// adapter supplies bitmap and presentation metadata from the configuration.
inline webgpu_canvas_host make_iosurface_webgpu_canvas_host(
    std::shared_ptr<dawn_iosurface_canvas_host> provider,
    std::function<image_metadata()> next_metadata,std::shared_ptr<void> device_lifetime={}) {
    if(!provider||!next_metadata)throw std::invalid_argument("Canvas provider and frame identity source required");
    webgpu_canvas_host result;
    result.validate=[](const webgpu_canvas_configuration& config) {
        if(config.format!=wgpu::TextureFormat::BGRA8Unorm||config.color_space!="srgb"||config.tone_mapping!="standard")
            throw std::invalid_argument("IOSurface presenter currently requires BGRA8 sRGB standard tone mapping");
        if(!config.device.HasFeature(wgpu::FeatureName::SharedTextureMemoryIOSurface)||
            !config.device.HasFeature(wgpu::FeatureName::SharedFenceMTLSharedEvent))
            throw std::invalid_argument("Canvas device lacks native IOSurface sharing capabilities");
    };
    result.acquire=[provider,next_metadata=std::move(next_metadata),device_lifetime=std::move(device_lifetime)](
        const webgpu_canvas_configuration& config,const webgpu_texture_descriptor& descriptor) {
        auto metadata=next_metadata();metadata.width=descriptor.size.width;metadata.height=descriptor.size.height;
        metadata.format=image_format::bgra8_unorm;metadata.color_space=image_color_space::srgb;
        metadata.alpha=config.alpha_mode=="opaque"?image_alpha::opaque:image_alpha::premultiplied;
        metadata.orientation=image_orientation::top_left;
        wgpu::Texture texture;
        descriptor.with_native([&](const auto& native){texture=provider->acquire(metadata,config.device,native,device_lifetime);});
        return texture;
    };
    result.retire=[provider](const wgpu::Texture& texture,bool present){provider->retire(texture,present);};
    return result;
}
} // namespace webscene::graphics
#endif
