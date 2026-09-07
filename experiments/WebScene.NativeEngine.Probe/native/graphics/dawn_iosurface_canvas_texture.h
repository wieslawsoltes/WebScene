#pragma once
#include "dawn_shared_image.h"
#include "iosurface_canvas_images.h"
#if defined(__APPLE__)
namespace webscene::graphics {
// Import the exact pool allocation using the application's canvas descriptor.
// The caller keeps the frame reservation and hands both objects to submission
// retirement after application work. No new pixel storage, pixel copy or queue
// submission occurs; Dawn creates a texture object over the shared storage.
inline std::shared_ptr<dawn_shared_image> import_dawn_iosurface_canvas_texture(
    const iosurface_canvas_images::frame& frame,const wgpu::Device& device,
    const wgpu::TextureDescriptor& description) {
    if(!frame.color||frame.metadata.format!=image_format::bgra8_unorm||
        description.format!=wgpu::TextureFormat::BGRA8Unorm||
        description.size.width!=frame.metadata.width||description.size.height!=frame.metadata.height)
        throw std::invalid_argument("Canvas texture descriptor must match its IOSurface allocation");
    auto surface=frame.color->borrowed_handle();
    std::shared_ptr<void> owner(const_cast<void*>(CFRetain(surface)),[](void* value){CFRelease(value);});
    wgpu::SharedTextureMemoryIOSurfaceDescriptor io{};io.ioSurface=surface;
    wgpu::SharedTextureMemoryDescriptor import{};import.nextInChain=&io;
    return dawn_shared_image::import(device,import,description,std::move(owner));
}
} // namespace webscene::graphics
#endif
