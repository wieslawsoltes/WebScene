#pragma once
#include "dawn_shared_image.h"
#include "iosurface_canvas_images.h"
#include <functional>
#if defined(__APPLE__)
namespace webscene::graphics {
// Nonblocking producer handoff. Only take_ready() can expose the scene image;
// EndAccess alone never makes an image ready for the presenter.
class dawn_iosurface_submission final {
public:
    enum class status { pending, ready, failed, consumed };
private:
    mutable std::mutex mutex_;
    status status_=status::pending;
    std::optional<owned_image_pool::retained> image_;
    wgpu::Future future_{};
public:
    status state() const { std::lock_guard lock(mutex_); return status_; }
    std::optional<owned_image_pool::retained> take_ready() {
        std::lock_guard lock(mutex_);
        if (status_!=status::ready) return {};
        status_=status::consumed;
        return std::move(image_);
    }
    // Exposed for diagnostic waits only. Ordinary callers poll state or use wake.
    wgpu::Future completion_future() const noexcept { return future_; }
    // Invoke on the device/producer owner thread. The recorder may only encode;
    // it must not submit or let the borrowed texture escape. Callers retain their
    // device error-scope contract: queue completion does not certify validation.
    using recorder=std::function<wgpu::CommandBuffer(const wgpu::Texture&)>;
    static std::shared_ptr<dawn_iosurface_submission> submit(
        iosurface_canvas_images::frame&& frame,const wgpu::Device& device,
        const recorder& record,std::shared_ptr<void> device_lifetime={},
        std::shared_ptr<completion_wake> wake={}) {
        if (!device || !record || !frame.color)
            throw std::invalid_argument("Dawn IOSurface submission requires device, frame and recorder");
        auto pending=std::make_shared<iosurface_canvas_images::frame>(std::move(frame));
        auto surface=pending->color->borrowed_handle();
        std::shared_ptr<void> owner(const_cast<void*>(CFRetain(surface)),[](void* p) { CFRelease(p); });
        wgpu::SharedTextureMemoryIOSurfaceDescriptor io{}; io.ioSurface=surface;
        wgpu::SharedTextureMemoryDescriptor import{}; import.nextInChain=&io;
        wgpu::TextureDescriptor texture{};
        texture.dimension=wgpu::TextureDimension::e2D;
        texture.size={pending->metadata.width,pending->metadata.height,1};
        texture.format=wgpu::TextureFormat::BGRA8Unorm;
        texture.usage=wgpu::TextureUsage::RenderAttachment;
        auto shared=dawn_shared_image::import(device,import,texture,std::move(owner));
        if (!shared) return {};
        wgpu::SharedTextureMemoryBeginAccessDescriptor access{};
        access.initialized=false; // Recorder must initialize every presented pixel.
        if (!shared->begin(access)) return {};
        wgpu::CommandBuffer command;
        try { command=record(shared->texture()); }
        catch (...) { wgpu::SharedTextureMemoryEndAccessState end; shared->end(end); throw; }
        if (!command) { wgpu::SharedTextureMemoryEndAccessState end; shared->end(end); return {}; }
        auto result=std::make_shared<dawn_iosurface_submission>();
        pending->producer.begin();
        auto image=pending->producer.publish();
        if (!image) {
            wgpu::SharedTextureMemoryEndAccessState end; shared->end(end);
            pending->producer.complete();
            return {};
        }
        result->image_.emplace(std::move(*image));
        auto queue=device.GetQueue();
        queue.Submit(1,&command);
        wgpu::SharedTextureMemoryEndAccessState end;
        const bool ended=shared->end(end);
        result->future_=queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
            [result,pending,shared,device,device_lifetime,wake,ended]
            (wgpu::QueueWorkDoneStatus completed,wgpu::StringView) {
                pending->producer.complete();
                {
                    std::lock_guard lock(result->mutex_);
                    result->status_=ended && completed==wgpu::QueueWorkDoneStatus::Success
                        ? status::ready : status::failed;
                    if (result->status_==status::failed) result->image_.reset();
                }
                if (wake) wake->signal();
            });
        return result;
    }
};
} // namespace webscene::graphics
#endif
