#pragma once
#include "dawn_iosurface_canvas_texture.h"
#include "iosurface_canvas_images.h"
#include "producer_completion_gate.h"
#include <functional>
#if defined(__APPLE__)
namespace webscene::graphics {
// Nonblocking producer handoff. Completed consumers use take_ready(); capable
// GPU consumers may take a validated image with all producer dependencies.
class dawn_iosurface_submission final : public std::enable_shared_from_this<dawn_iosurface_submission> {
public:
    enum class status { pending, ready, failed, consumed, discarded };
private:
    mutable std::mutex mutex_;
    status status_=status::pending;
    std::optional<owned_image_pool::retained> image_;
    wgpu::Future future_{}, validation_future_{};
    producer_completion_gate completion_;
    // Own EndAccess fences and timeline values beyond the submitting stack.
    // Snapshot ownership also keeps these dependencies alive for a GPU consumer.
    wgpu::SharedTextureMemoryEndAccessState handoff_;
    bool present_=true, handoff_valid_=false;
    void finish(bool queue,bool valid,const std::shared_ptr<completion_wake>& wake) {
        bool notify=false;
        {
            std::lock_guard lock(mutex_);
            if (completion_.finish(queue ? producer_completion_gate::phase::queue
                    : producer_completion_gate::phase::validation,valid)) {
                const auto succeeded=completion_.state()==producer_completion_gate::result::success;
                status_=succeeded ? (present_ ? status::ready : status::discarded) : status::failed;
                if (!succeeded) image_.reset();
                notify=true;
            }
            notify=notify || (!queue && present_ && handoff_valid_ && completion_.validated_for_gpu_wait());
        }
        if (notify && wake) wake->signal();
    }
public:
    status state() const { std::lock_guard lock(mutex_); return status_; }
    std::optional<owned_image_pool::retained> take_ready() {
        std::lock_guard lock(mutex_);
        if (status_!=status::ready) return {};
        status_=status::consumed;
        return std::move(image_);
    }
    // A captured CPU scene owns an exact output independently of the provider's
    // destructive ready queue. Metadata is available while pending. Early image
    // transfer requires validation and a consumer that encodes GPU dependencies.
    class snapshot final {
        std::shared_ptr<dawn_iosurface_submission> submission_;
        std::optional<owned_image_pool::retained> image_;
        friend class dawn_iosurface_submission;
        snapshot(std::shared_ptr<dawn_iosurface_submission> submission,
            owned_image_pool::retained image)
            : submission_(std::move(submission)),image_(std::move(image)) {}
    public:
        snapshot(const snapshot&)=delete;
        snapshot& operator=(const snapshot&)=delete;
        image_metadata describe() const {
            if(!image_)throw std::logic_error("Captured image already transferred");
            return image_->describe();
        }
        status state() const { return submission_->state(); }
        // Immutable after publish_submitted/submit returns. Does not certify image
        // readiness; an asynchronous consumer must encode all dependencies.
        const wgpu::SharedTextureMemoryEndAccessState& producer_handoff() const noexcept {
            return submission_->handoff_;
        }
        bool can_enqueue_gpu_wait() const {
            std::lock_guard lock(submission_->mutex_);
            const auto& handoff=submission_->handoff_;
            return submission_->present_ && submission_->handoff_valid_
                && submission_->completion_.validated_for_gpu_wait()
                && submission_->status_!=status::failed && submission_->status_!=status::discarded
                && handoff.initialized && handoff.fenceCount>0
                && handoff.fenceCount==handoff.signaledValueCount;
        }
        std::optional<owned_image_pool::retained> take_for_gpu_wait() {
            if(!can_enqueue_gpu_wait() || !image_) return {};
            auto result=std::move(image_);image_.reset();return result;
        }
        std::optional<owned_image_pool::retained> take_ready() {
            const auto current=state();
            if(current!=status::ready&&current!=status::consumed)return {};
            if(!image_)return {};
            auto result=std::move(image_);
            image_.reset();
            return result;
        }
    };
    std::unique_ptr<snapshot> capture_snapshot() {
        std::lock_guard lock(mutex_);
        if(!image_ || status_==status::failed || status_==status::discarded
            || status_==status::consumed)return {};
        auto retained=image_->retain();
        if(!retained)return {}; // Retention pressure does not expose a partial scene.
        return std::unique_ptr<snapshot>(new snapshot(shared_from_this(),std::move(*retained)));
    }
    // Exposed for diagnostic waits only. Ordinary callers poll state or use wake.
    wgpu::Future completion_future() const noexcept { return future_; }
    wgpu::Future validation_future() const noexcept { return validation_future_; }
    // Handoff for application work already submitted on this device's queue.
    // No command is resubmitted. Even publication backpressure must retain the
    // frame until queue completion because its allocation may already be in use.
    // present=false retires resize/unconfigure work without exposing an image;
    // discarded contents need not be initialized, but completion is still required.
    static std::shared_ptr<dawn_iosurface_submission> publish_submitted(
        iosurface_canvas_images::frame&& frame,const wgpu::Device& device,
        std::shared_ptr<dawn_shared_image> shared,std::shared_ptr<void> device_lifetime={},
        std::shared_ptr<completion_wake> wake={},bool present=true) {
        if(!device||!frame.color||!shared||!shared->matches(device,frame.color->borrowed_handle()))
            throw std::invalid_argument("Submitted image must match its device and native allocation");
        auto result=std::make_shared<dawn_iosurface_submission>();
        auto pending=std::make_shared<iosurface_canvas_images::frame>(std::move(frame));
        pending->producer.begin();
        result->present_=present;
        if(present) {
            auto image=pending->producer.publish();
            if(!image)result->completion_.reject();
            if(image)result->image_.emplace(std::move(*image));
        }
        // Scope only host handoff operations, never application JS recording or
        // its error-scope stack. Undefined image contents cannot be presented.
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::SharedTextureMemoryEndAccessState end;
        const bool ended=shared->end(end);
        const bool expired=ended&&shared->expire_texture();
        const bool valid=expired&&(!present||end.initialized);
        result->handoff_valid_=valid;
        result->handoff_=std::move(end);
        result->future_=device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
            [result,pending,shared,device,device_lifetime,wake,valid,present](wgpu::QueueWorkDoneStatus status,wgpu::StringView) {
                pending->producer.complete();
                if(!present)pending->producer.cancel();
                result->finish(true,valid&&status==wgpu::QueueWorkDoneStatus::Success,wake);
            });
        result->validation_future_=device.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
            [result,device,device_lifetime,wake](wgpu::PopErrorScopeStatus status,wgpu::ErrorType error,wgpu::StringView) {
                result->finish(false,status==wgpu::PopErrorScopeStatus::Success&&error==wgpu::ErrorType::NoError,wake);
            });
        return result;
    }
    // Invoke on the device/producer owner thread. The recorder may only encode;
    // it must not submit, alter error scopes, or let the borrowed texture escape.
    // Validation and queue completion must both succeed before publication.
    using recorder=std::function<wgpu::CommandBuffer(const wgpu::Texture&)>;
    static std::shared_ptr<dawn_iosurface_submission> submit(
        iosurface_canvas_images::frame&& frame,const wgpu::Device& device,
        const recorder& record,std::shared_ptr<void> device_lifetime={},
        std::shared_ptr<completion_wake> wake={}) {
        if (!device || !record || !frame.color)
            throw std::invalid_argument("Dawn IOSurface submission requires device, frame and recorder");
        // Balance the validation scope even when import/recording rejects work.
        struct validation_scope {
            wgpu::Device device;
            bool popped=false;
            explicit validation_scope(wgpu::Device value):device(std::move(value)) {
                device.PushErrorScope(wgpu::ErrorFilter::Validation);
            }
            ~validation_scope() {
                if (!popped) device.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
                    [](wgpu::PopErrorScopeStatus,wgpu::ErrorType,wgpu::StringView) {});
            }
        } scope(device);
        auto pending=std::make_shared<iosurface_canvas_images::frame>(std::move(frame));
        wgpu::TextureDescriptor texture{};
        texture.dimension=wgpu::TextureDimension::e2D;
        texture.size={pending->metadata.width,pending->metadata.height,1};
        texture.format=wgpu::TextureFormat::BGRA8Unorm;
        texture.usage=wgpu::TextureUsage::RenderAttachment;
        auto shared=import_dawn_iosurface_canvas_texture(*pending,device,texture);
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
        const bool expired=ended && shared->expire_texture();
        result->handoff_valid_=ended && expired && end.initialized;
        result->handoff_=std::move(end);
        result->future_=queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
            [result,pending,shared,device,device_lifetime,wake,ended,expired]
            (wgpu::QueueWorkDoneStatus completed,wgpu::StringView) {
                pending->producer.complete();
                result->finish(true,ended && expired && completed==wgpu::QueueWorkDoneStatus::Success,wake);
            });
        scope.popped=true;
        result->validation_future_=device.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
            [result,device,device_lifetime,wake](wgpu::PopErrorScopeStatus completed,
                wgpu::ErrorType error,wgpu::StringView) {
                result->finish(false,completed==wgpu::PopErrorScopeStatus::Success &&
                    error==wgpu::ErrorType::NoError,wake);
            });
        return result;
    }
};
} // namespace webscene::graphics
#endif
