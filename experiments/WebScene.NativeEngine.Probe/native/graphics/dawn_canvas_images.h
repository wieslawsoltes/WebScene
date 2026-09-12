#pragma once
#include "owned_image_pool.h"
#include <webgpu/webgpu_cpp.h>

namespace webscene::graphics {
// Engine-thread allocator. Pool tickets retain native references independently
// of the canvas owner. This does not grant a lease permission to destroy Device.
class dawn_canvas_images {
    struct storage final : image_provider_lifetime {
        struct slot { wgpu::Texture texture; image_metadata metadata{}; uint64_t bytes{}; };
        mutable std::mutex mutex;
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
    enum class submission_status { pending,success,failed };
    struct submitted_frame {
        owned_image_pool::retained image;
        std::shared_ptr<std::atomic<submission_status>> status;
    };
    // Submit only command buffers recorded for this frame/device. Device error
    // scopes still report command validation separately from queue completion.
    std::optional<submitted_frame> submit(frame&& input,const wgpu::CommandBuffer& commands,
        std::shared_ptr<completion_wake> completion={}) {
        check_thread();
        if (!commands || !input.producer.belongs_to(storage_.get()))
            throw std::invalid_argument("foreign canvas producer or missing commands");
        auto status=std::make_shared<std::atomic<submission_status>>(submission_status::pending);
        auto pending=std::make_shared<frame>(std::move(input));
        pending->producer.begin();
        auto image=pending->producer.publish();
        if (!image) {
            // No GPU work has been submitted. Return admission backpressure
            // without leaving a producer outstanding or waking our own retry.
            pending->producer.complete(); pending->producer.cancel(false);
            return {};
        }
        auto queue=storage_->device.GetQueue();
        queue.Submit(1,&commands);
        queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
            [pending,status,completion](wgpu::QueueWorkDoneStatus result,wgpu::StringView) {
                pending->producer.complete();
                status->store(result==wgpu::QueueWorkDoneStatus::Success
                    ? submission_status::success : submission_status::failed,std::memory_order_release);
                if (completion) completion->signal();
            });
        return submitted_frame{std::move(*image),std::move(status)};
    }
    // Native applications submit their own command buffers before presenting.
    // Even if retaining a snapshot hits the ticket limit, keep the producer
    // reserved until ALL work already submitted to its queue has completed.
    std::optional<submitted_frame> retire_submitted(frame&& input,
        std::shared_ptr<completion_wake> completion={}, wgpu::Future* submitted_future=nullptr) {
        check_thread();
        if (!input.producer.belongs_to(storage_.get()))
            throw std::invalid_argument("foreign canvas producer");
        auto status=std::make_shared<std::atomic<submission_status>>(submission_status::pending);
        auto pending=std::make_shared<frame>(std::move(input));
        pending->producer.begin();
        auto image=pending->producer.publish();
        auto future=storage_->device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
            [pending,status,completion](wgpu::QueueWorkDoneStatus result,wgpu::StringView) {
                pending->producer.complete();
                status->store(result==wgpu::QueueWorkDoneStatus::Success
                    ? submission_status::success : submission_status::failed,std::memory_order_release);
                if (completion) completion->signal();
            });
        if (submitted_future) *submitted_future=future;
        if (!image) return {}; // callback still owns the submitted producer
        return submitted_frame{std::move(*image),std::move(status)};
    }
    dawn_canvas_images(wgpu::Device device,uint64_t byte_limit,size_t tickets=128,std::shared_ptr<completion_wake> wake={})
        :storage_(std::make_shared<storage>(std::move(device))),pool_(storage_,tickets,std::move(wake)),byte_limit_(byte_limit) {
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
        // Reserve idle slots until eviction finishes. Normal rollback is quiet;
        // exceptional unwinding releases storage's mutex before wake callbacks.
        std::array<std::optional<owned_image_pool::producer>,2> idle;
        std::lock_guard lock(storage_->mutex);
        auto& slot=storage_->slots[writer->slot()];
        const bool reuse=slot.texture && slot.metadata.width==metadata.width
            && slot.metadata.height==metadata.height && slot.metadata.format==metadata.format;
        metadata.allocation=reuse ? slot.metadata.allocation : new_owner_token();
        // Validate all portable fields before changing native storage.
        writer->set_metadata(metadata);
        for (auto& reservation:idle) {
            if (bytes<=byte_limit_-(storage_->resident_bytes-slot.bytes)) break;
            auto candidate=pool_.acquire();
            if (!candidate) break; // Retained/submitted images cannot be evicted.
            reservation.emplace(std::move(*candidate));
            auto& cached=storage_->slots[reservation->slot()];
            cached.texture=nullptr;
            storage_->resident_bytes-=cached.bytes; cached.bytes=0;
        }
        for (auto& reservation:idle) if (reservation) reservation->cancel(false);
        if (bytes>byte_limit_-(storage_->resident_bytes-slot.bytes)) {
            writer->cancel(false); return {};
        }
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
    // Native presenter only. Keep the consumer alive through its GPU fence;
    // resolving an object is not synchronization with its producer timeline.
    // The caller may use this texture for reading only, never Destroy or writes.
    static wgpu::Texture resolve(const owned_image_pool::consumer& consumer,const wgpu::Device& device) {
        const auto metadata=consumer.describe();
        const auto provider=std::dynamic_pointer_cast<storage>(consumer.provider());
        if (!provider || !device || provider->device.Get()!=device.Get())
            throw std::invalid_argument("foreign Dawn image provider or device");
        std::lock_guard lock(provider->mutex);
        for (const auto& slot:provider->slots) {
            if (slot.texture && slot.metadata.allocation==metadata.allocation
                && slot.metadata.allocation_generation==metadata.allocation_generation
                && slot.metadata.content_serial==metadata.content_serial)
                return slot.texture;
        }
        throw std::invalid_argument("Dawn image allocation unavailable");
    }
    uint64_t resident_bytes() const { check_thread(); return storage_->resident_bytes; }
    uint64_t created_images() const { check_thread(); return storage_->created; }
    size_t busy_images() const { return pool_.busy_images(); }
    void close() { pool_.close(); }
};
} // namespace webscene::graphics
