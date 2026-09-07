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
    explicit iosurface_canvas_images(uint64_t budget)
        :pool_(storage_),limit_(budget) {
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
    static const iosurface_color& resolve(const owned_image_pool::consumer& consumer) {
        const auto metadata=consumer.describe();
        const auto provider=std::dynamic_pointer_cast<storage>(consumer.provider());
        if (!provider) throw std::invalid_argument("Foreign IOSurface lease provider");
        std::lock_guard lock(provider->mutex);
        for (const auto& slot:provider->slots)
            if (slot.color && slot.metadata.allocation==metadata.allocation &&
                slot.metadata.allocation_generation==metadata.allocation_generation &&
                slot.metadata.content_serial==metadata.content_serial) return *slot.color;
        throw std::invalid_argument("IOSurface generation unavailable");
    }
    size_t busy_images() const { return pool_.busy_images(); }
};
} // namespace webscene::graphics
#endif
