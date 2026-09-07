#pragma once
#include <webgpu/webgpu_cpp.h>
#include <memory>

namespace webscene::graphics {
// Cross-platform Dawn shared-memory ownership/access boundary. Platform code
// supplies the import descriptor and native allocation owner. The caller must
// retain this object through GPU completion; EndAccess is not a completion wait.
class dawn_shared_image final {
    std::shared_ptr<void> native_owner_;
    wgpu::Device device_;
    wgpu::SharedTextureMemory memory_;
    wgpu::Texture texture_;
    bool active_=false, failed_=false, expired_=false;
    dawn_shared_image(wgpu::Device device,std::shared_ptr<void> owner,wgpu::SharedTextureMemory memory,
                      wgpu::Texture texture)
        :native_owner_(std::move(owner)),device_(std::move(device)),memory_(std::move(memory)),texture_(std::move(texture)) {}
public:
    dawn_shared_image(const dawn_shared_image&)=delete;
    dawn_shared_image& operator=(const dawn_shared_image&)=delete;
    static std::shared_ptr<dawn_shared_image> import(
        const wgpu::Device& device,const wgpu::SharedTextureMemoryDescriptor& import,
        const wgpu::TextureDescriptor& description,std::shared_ptr<void> owner) {
        if (!device || !owner || description.dimension!=wgpu::TextureDimension::e2D ||
            !description.size.width || !description.size.height ||
            description.size.depthOrArrayLayers!=1 || description.sampleCount!=1 ||
            description.mipLevelCount!=1) return {};
        auto memory=device.ImportSharedTextureMemory(&import);
        wgpu::SharedTextureMemoryProperties properties{};
        if (!memory || memory.GetProperties(&properties)!=wgpu::Status::Success ||
            properties.format!=description.format ||
            properties.size.width!=description.size.width ||
            properties.size.height!=description.size.height ||
            properties.size.depthOrArrayLayers!=1 ||
            (properties.usage & description.usage)!=description.usage) return {};
        auto texture=memory.CreateTexture(&description);
        if (!texture) return {};
        return std::shared_ptr<dawn_shared_image>(
            new dawn_shared_image(device,std::move(owner),std::move(memory),std::move(texture)));
    }
    bool matches(const wgpu::Device& device,const void* allocation)const noexcept {
        return device.Get()==device_.Get()&&allocation==native_owner_.get();
    }
    const wgpu::Texture& texture() const noexcept { return texture_; }
    bool begin(const wgpu::SharedTextureMemoryBeginAccessDescriptor& access) {
        if (active_ || failed_ || expired_) return false;
        if (memory_.BeginAccess(texture_,&access)!=wgpu::Status::Success) {
            failed_=true;
            return false;
        }
        active_=true;
        return true;
    }
    // Expire this frame's WebGPU texture object after EndAccess, before its
    // native allocation can be recycled. Submitted work retains its resources;
    // a JavaScript reference to the old texture cannot write a later frame.
    bool expire_texture() {
        if(active_ || failed_)return false;
        if(!expired_){texture_.Destroy();expired_=true;}
        return true;
    }
    bool end(wgpu::SharedTextureMemoryEndAccessState& handoff) {
        if (!active_ || failed_) return false;
        if (memory_.EndAccess(texture_,&handoff)!=wgpu::Status::Success) {
            failed_=true; // Access ownership is uncertain; never offer reuse.
            return false;
        }
        active_=false;
        return true;
    }
};
} // namespace webscene::graphics
