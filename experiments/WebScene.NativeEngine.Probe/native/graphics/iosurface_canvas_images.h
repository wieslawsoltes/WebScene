#pragma once
#include "iosurface_color.h"
#include "owned_image_pool.h"
#include <thread>
#if defined(__APPLE__)
namespace webscene::graphics {
// IOSurface storage behind the shared versioned lease contract. Producer and
// consumer GPU completion are supplied by the Dawn/ANGLE/presenter integrations.
class iosurface_canvas_images {
    struct storage final : image_provider_lifetime {
        image_provider_kind kind() const noexcept override { return image_provider_kind::iosurface; }
        struct slot { std::shared_ptr<iosurface_color> color; image_metadata metadata{}; };
        std::array<slot,3> slots;
        std::mutex mutex;
        uint64_t bytes{};
    };
    std::shared_ptr<storage> storage_=std::make_shared<storage>();
    owned_image_pool pool_;
    uint64_t limit_;
    const std::thread::id thread_=std::this_thread::get_id();
public:
    struct frame {
        owned_image_pool::producer producer;
        const iosurface_color* color; // Borrowed through the producer lease.
        image_metadata metadata;
    };
    explicit iosurface_canvas_images(uint64_t budget,std::shared_ptr<completion_wake> wake={})
        :pool_(storage_,128,std::move(wake)),limit_(budget) {
        if (!budget) throw std::invalid_argument("IOSurface pool requires a budget");
    }
    std::optional<frame> acquire(image_metadata metadata) {
        if (thread_!=std::this_thread::get_id())
            throw std::logic_error("IOSurface allocation requires owner thread");
        if (metadata.format!=image_format::bgra8_unorm)
            throw std::invalid_argument("IOSurface pool requires negotiated BGRA8");
        auto writer=pool_.acquire();
        if (!writer) return {};
        std::lock_guard lock(storage_->mutex);
        auto& slot=storage_->slots[writer->slot()];
        const bool reuse=slot.color && slot.metadata.width==metadata.width &&
            slot.metadata.height==metadata.height;
        metadata.allocation=reuse ? slot.metadata.allocation : new_owner_token();
        writer->set_metadata(metadata);
        if (!reuse) {
            if (slot.color) storage_->bytes-=slot.color->allocation_bytes();
            slot.color.reset();
            slot.color=iosurface_color::create_bgra8(metadata.width,metadata.height,limit_-storage_->bytes);
            if (!slot.color) { writer->cancel(false); return {}; }
            storage_->bytes+=slot.color->allocation_bytes();
        }
        slot.metadata=metadata;
        return frame{std::move(*writer),slot.color.get(),metadata};
    }
    // Adopt an already decoded immutable surface. Three slots bound outstanding
    // frames; a busy slot remains owned until every retained scene/GPU consumer
    // finishes. No readback, upload or pixel copy occurs here.
    std::optional<owned_image_pool::retained> adopt(image_metadata metadata,
        std::shared_ptr<iosurface_color> color) {
        if (thread_!=std::this_thread::get_id()) throw std::logic_error("Image publication requires owner thread");
        if (!color || metadata.format!=image_format::bgra8_unorm ||
            IOSurfaceGetWidth(color->borrowed_handle())!=metadata.width ||
            IOSurfaceGetHeight(color->borrowed_handle())!=metadata.height)
            throw std::invalid_argument("Decoded image metadata mismatch");
        if(color->allocation_bytes()>limit_)throw std::length_error("Decoded surface exceeds image budget");
        auto writer=pool_.acquire(); if (!writer) return {};
        {
            std::lock_guard lock(storage_->mutex);
            auto& slot=storage_->slots[writer->slot()];
            auto previous=slot.color ? slot.color->allocation_bytes() : 0;
            if (color->allocation_bytes()>limit_-(storage_->bytes-previous)) {writer->cancel(false);return {};}
            storage_->bytes=storage_->bytes-previous+color->allocation_bytes();
            metadata.allocation=new_owner_token(); writer->set_metadata(metadata);
            slot.color=std::move(color); slot.metadata=metadata;
        }
        writer->begin(); writer->complete(); return writer->publish();
    }
    static const iosurface_color& resolve(const owned_image_pool::consumer& consumer) {
        const auto metadata=consumer.describe();
        const auto anchor=consumer.provider();
        if (anchor->kind()!=image_provider_kind::iosurface)
            throw std::invalid_argument("Foreign IOSurface lease provider");
        const auto provider=std::static_pointer_cast<storage>(anchor);
        std::lock_guard lock(provider->mutex);
        for (const auto& slot:provider->slots)
            if (slot.color && slot.metadata.allocation==metadata.allocation &&
                slot.metadata.allocation_generation==metadata.allocation_generation &&
                slot.metadata.content_serial==metadata.content_serial) return *slot.color;
        throw std::invalid_argument("IOSurface generation unavailable");
    }
    image_lease_pool::occupancy inspect_occupancy() const { return pool_.inspect_occupancy(); }
    size_t busy_images() const { return pool_.busy_images(); }
};
} // namespace webscene::graphics
#endif
