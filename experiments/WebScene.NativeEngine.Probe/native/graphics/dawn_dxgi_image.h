#pragma once
#include "dxgi_bridge_contract.h"
#include "nt_handle.h"
#include <memory>
#include <webgpu/webgpu_cpp.h>

namespace webscene::graphics {
enum class dxgi_import_status {
    success,unsupported_platform,invalid_argument,unsupported_pairing,
    missing_device_feature,handle_failure,import_failure,property_mismatch,out_of_memory
};
// Import ownership only. No texture is exposed for GPU use until the explicit
// BeginAccess/EndAccess integration is implemented.
class dawn_dxgi_image {
    struct handle_anchor { virtual ~handle_anchor()=default; };
#if defined(_WIN32)
    struct windows_handle final : handle_anchor {
        owned_nt_handle value;
        explicit windows_handle(owned_nt_handle owned):value(std::move(owned)) {}
    };
#endif
    std::shared_ptr<handle_anchor> handle_; // Last destroyed, after Dawn objects.
    wgpu::SharedTextureMemory memory_;
    wgpu::Texture texture_;
    wgpu::SharedTextureMemoryProperties properties_{};
    static wgpu::TextureFormat native_format(image_format format) {
        switch (format) {
            case image_format::rgba8_unorm: return wgpu::TextureFormat::RGBA8Unorm;
            case image_format::bgra8_unorm: return wgpu::TextureFormat::BGRA8Unorm;
            case image_format::rgba16_float: return wgpu::TextureFormat::RGBA16Float;
            case image_format::rgba8_srgb: return wgpu::TextureFormat::RGBA8UnormSrgb;
            case image_format::bgra8_srgb: return wgpu::TextureFormat::BGRA8UnormSrgb;
        }
        return wgpu::TextureFormat::Undefined;
    }
    dxgi_import_status initialize(const wgpu::Device& device,void* handle,const image_metadata& image,
        dxgi_sync synchronization,wgpu::TextureUsage usage) {
        wgpu::SharedTextureMemoryDXGISharedHandleDescriptor dxgi{};
        dxgi.handle=handle; dxgi.useKeyedMutex=synchronization==dxgi_sync::keyed_mutex;
        wgpu::SharedTextureMemoryDescriptor descriptor{}; descriptor.nextInChain=&dxgi;
        memory_=device.ImportSharedTextureMemory(&descriptor);
        if (!memory_ || memory_.GetProperties(&properties_)!=wgpu::Status::Success || memory_.IsDeviceLost())
            return dxgi_import_status::import_failure;
        if (properties_.size.width!=image.width || properties_.size.height!=image.height
            || properties_.size.depthOrArrayLayers!=1 || properties_.format!=native_format(image.format)
            || (properties_.usage & usage)!=usage)
            return dxgi_import_status::property_mismatch;
        wgpu::TextureDescriptor texture{};
        texture.size=properties_.size; texture.format=properties_.format; texture.usage=usage;
        texture_=memory_.CreateTexture(&texture);
        return texture_ ? dxgi_import_status::success : dxgi_import_status::import_failure;
    }
public:
    dawn_dxgi_image()=default;
    dawn_dxgi_image(const dawn_dxgi_image&)=delete;
    dawn_dxgi_image& operator=(const dawn_dxgi_image&)=delete;
    const wgpu::SharedTextureMemoryProperties& properties() const noexcept { return properties_; }
    static dxgi_import_status import(const wgpu::Device& device,void* borrowed_nt_handle,
        const dxgi_endpoint& producer,const dxgi_endpoint& consumer,const image_metadata& image,
        wgpu::TextureUsage usage,std::unique_ptr<dawn_dxgi_image>& result) {
        result.reset();
#if !defined(_WIN32)
        return dxgi_import_status::unsupported_platform;
#else
        if (!device || !win32_nt_handle_ops::valid(borrowed_nt_handle) || usage==wgpu::TextureUsage::None)
            return dxgi_import_status::invalid_argument;
        const auto choice=choose_dxgi_bridge(producer,consumer,image);
        if (choice.status!=dxgi_bridge_status::supported) return dxgi_import_status::unsupported_pairing;
        if (!device.HasFeature(wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle)
            || (choice.synchronization==dxgi_sync::shared_fence
                && !device.HasFeature(wgpu::FeatureName::SharedFenceDXGISharedHandle)))
            return dxgi_import_status::missing_device_feature;
        try {
            auto owned=std::make_unique<dawn_dxgi_image>();
            auto handle=std::make_shared<windows_handle>(owned_nt_handle::duplicate(borrowed_nt_handle));
            owned->handle_=handle;
            const auto status=owned->initialize(device,handle->value.get(),image,choice.synchronization,usage);
            if (status!=dxgi_import_status::success) return status;
            result=std::move(owned); return dxgi_import_status::success;
        } catch (const std::bad_alloc&) { return dxgi_import_status::out_of_memory; }
          catch (const std::system_error&) { return dxgi_import_status::handle_failure; }
#endif
    }
};
} // namespace webscene::graphics
