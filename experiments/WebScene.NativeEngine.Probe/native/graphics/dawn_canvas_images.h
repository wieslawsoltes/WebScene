#pragma once
#include "owned_image_pool.h"
#include <webgpu/webgpu_cpp.h>

namespace webscene::graphics {
// Engine-thread allocator. Pool tickets retain native references independently
// of the canvas owner. This does not grant a lease permission to destroy Device.
class dawn_canvas_images {
    struct storage final : image_provider_lifetime {
        struct slot { wgpu::Texture texture; image_metadata metadata{}; uint64_t bytes{}; };
        wgpu::Device device;
        std::array<slot,3> slots;
        uint64_t resident_bytes{},created{};
        explicit storage(wgpu::Device value):device(std::move(value)) {}
    };
    const std::thread::id thread_=std::this_thread::get_id();
    std::shared_ptr<storage> storage_;
    owned_image_pool pool_;
    uint64_t byte_limit_;
    void check_thread() const {
        if (thread_!=std::this_thread::get_id()) throw std::logic_error("image allocation requires engine thread");
    }
public:
    struct frame {
        owned_image_pool::producer producer;
        wgpu::Texture texture;
        image_metadata metadata;
    };
    dawn_canvas_images(wgpu::Device device,uint64_t byte_limit,size_t tickets=128)
        :storage_(std::make_shared<storage>(std::move(device))),pool_(storage_,tickets),byte_limit_(byte_limit) {
        if (!storage_->device || !byte_limit) throw std::invalid_argument("Dawn image storage requires device and budget");
    }
    std::optional<frame> acquire(image_metadata metadata) {
        check_thread();
        wgpu::TextureFormat format;
        uint32_t pixel_bytes=4;
        switch (metadata.format) {
            case image_format::rgba8_unorm: format=wgpu::TextureFormat::RGBA8Unorm; break;
            case image_format::bgra8_unorm: format=wgpu::TextureFormat::BGRA8Unorm; break;
            case image_format::rgba16_float: format=wgpu::TextureFormat::RGBA16Float; pixel_bytes=8; break;
            case image_format::rgba8_srgb: format=wgpu::TextureFormat::RGBA8UnormSrgb; break;
            case image_format::bgra8_srgb: format=wgpu::TextureFormat::BGRA8UnormSrgb; break;
            default: throw std::invalid_argument("unsupported canvas format");
        }
        wgpu::Limits limits{};
        if (storage_->device.GetLimits(&limits)!=wgpu::Status::Success
            || !metadata.width || !metadata.height || metadata.width>limits.maxTextureDimension2D
            || metadata.height>limits.maxTextureDimension2D)
            throw std::invalid_argument("invalid canvas texture dimensions");
        const uint64_t pixels=uint64_t(metadata.width)*metadata.height;
        if (pixels>byte_limit_/pixel_bytes) return {};
        const auto bytes=pixels*pixel_bytes;
        auto writer=pool_.acquire();
        if (!writer) return {};
        auto& slot=storage_->slots[writer->slot()];
        if (bytes>byte_limit_-(storage_->resident_bytes-slot.bytes)) return {};
        const bool reuse=slot.texture && slot.metadata.width==metadata.width
            && slot.metadata.height==metadata.height && slot.metadata.format==metadata.format;
        metadata.allocation=reuse ? slot.metadata.allocation : new_owner_token();
        // Validate all portable fields before changing native storage.
        writer->set_metadata(metadata);
        if (!reuse) {
            // Only an idle slot can be acquired. Drop its old allocation before
            // replacement so this allocator does not temporarily exceed budget.
            slot.texture=nullptr; storage_->resident_bytes-=slot.bytes; slot.bytes=0;
            wgpu::TextureDescriptor descriptor{};
            descriptor.size={metadata.width,metadata.height,1};
            descriptor.format=format;
            descriptor.usage=wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding
                | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
            slot.texture=storage_->device.CreateTexture(&descriptor);
            if (!slot.texture) throw std::runtime_error("Dawn canvas texture creation failed");
            slot.bytes=bytes; storage_->resident_bytes+=bytes; ++storage_->created;
        }
        slot.metadata=metadata;
        return frame{std::move(*writer),slot.texture,metadata};
    }
    uint64_t resident_bytes() const { check_thread(); return storage_->resident_bytes; }
    uint64_t created_images() const { check_thread(); return storage_->created; }
    size_t busy_images() const { return pool_.busy_images(); }
    void close() { pool_.close(); }
};
} // namespace webscene::graphics
