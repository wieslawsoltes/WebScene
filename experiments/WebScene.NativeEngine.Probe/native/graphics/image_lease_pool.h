#pragma once
#include "resource_table.h"
#include "image_metadata.h"
#include <array>
#include <mutex>
#include <optional>

namespace webscene::graphics {
struct image_write_token { uint64_t pool{},generation{}; uint32_t slot{}; };
struct image_lease_token { uint64_t pool{},generation{}; uint32_t index{}; };
// Three image slots, independent of scene publication counts. This class owns
// lifetime metadata only; the backend retains allocations until slots are idle.
class image_lease_pool {
    enum class phase { idle,writing,published };
    enum class lease_kind { free,retained,consumer };
    struct image_slot {
        phase state{};
        uint64_t generation{};
        size_t retained{},consumers{};
        bool producer_done{};
        bool metadata_set{};
        image_metadata metadata{};
    };
    struct lease_slot {
        lease_kind kind{};
        uint64_t generation{};
        uint32_t image{};
    };
    const uint64_t identity_=new_owner_token();
    mutable std::mutex mutex_;
    std::array<image_slot,3> images_{};
    std::vector<lease_slot> leases_;
    bool closed_{};
    image_slot& image(image_write_token token) {
        if (token.pool!=identity_ || token.slot>=images_.size()) throw std::invalid_argument("foreign image writer");
        auto& item=images_[token.slot];
        if (item.state==phase::idle || item.generation!=token.generation) throw std::invalid_argument("stale image writer");
        return item;
    }
    lease_slot& lease(image_lease_token token,lease_kind kind) {
        if (token.pool!=identity_ || token.index>=leases_.size()) throw std::invalid_argument("foreign image lease");
        auto& item=leases_[token.index];
        if (item.kind!=kind || item.generation!=token.generation) throw std::invalid_argument("stale or wrong-kind image lease");
        return item;
    }
    std::optional<image_lease_token> allocate(uint32_t image,lease_kind kind) {
        for (uint32_t i=0;i<leases_.size();++i) {
            auto& item=leases_[i];
            if (item.kind==lease_kind::free && item.generation!=UINT64_MAX) {
                ++item.generation; item.kind=kind; item.image=image;
                return image_lease_token{identity_,item.generation,i};
            }
        }
        return {};
    }
    void recycle(image_slot& item) {
        if (item.state==phase::published && item.producer_done && !item.retained && !item.consumers)
            item.state=phase::idle;
    }
public:
    explicit image_lease_pool(size_t lease_capacity=128) {
        if (!lease_capacity || lease_capacity>UINT32_MAX) throw std::invalid_argument("invalid image lease capacity");
        leases_.resize(lease_capacity);
    }
    std::optional<image_write_token> acquire_write() {
        std::lock_guard lock(mutex_);
        if (closed_) return {};
        for (uint32_t i=0;i<images_.size();++i) {
            auto& item=images_[i];
            if (item.state==phase::idle && item.generation!=UINT64_MAX) {
                ++item.generation; item.state=phase::writing; item.producer_done=false; item.metadata_set=false;
                return image_write_token{identity_,item.generation,i};
            }
        }
        return {};
    }
    void set_metadata(image_write_token writer,const image_metadata& metadata) {
        std::lock_guard lock(mutex_);
        auto& item=image(writer);
        if (item.state!=phase::writing) throw std::invalid_argument("published image metadata is immutable");
        if (!metadata.canvas || !metadata.allocation || !metadata.allocation_generation
            || !metadata.width || !metadata.height || !metadata.producer_timeline
            || static_cast<uint32_t>(metadata.format)<1 || static_cast<uint32_t>(metadata.format)>5
            || static_cast<uint32_t>(metadata.alpha)<1 || static_cast<uint32_t>(metadata.alpha)>3
            || static_cast<uint32_t>(metadata.color_space)<1 || static_cast<uint32_t>(metadata.color_space)>2
            || static_cast<uint32_t>(metadata.orientation)<1 || static_cast<uint32_t>(metadata.orientation)>2)
            throw std::invalid_argument("invalid portable image metadata");
        item.metadata=metadata; item.metadata_set=true;
    }
    image_metadata describe(image_lease_token token) const {
        std::lock_guard lock(mutex_);
        if (token.pool!=identity_ || token.index>=leases_.size()) throw std::invalid_argument("foreign image lease");
        const auto& owned=leases_[token.index];
        if (owned.kind==lease_kind::free || owned.generation!=token.generation)
            throw std::invalid_argument("stale image lease");
        return images_[owned.image].metadata;
    }
    // On capacity exhaustion the writer stays reserved; the caller retries.
    std::optional<image_lease_token> publish(image_write_token writer) {
        std::lock_guard lock(mutex_);
        auto& item=image(writer);
        if (item.state!=phase::writing) throw std::invalid_argument("image already published");
        if (!item.metadata_set) throw std::invalid_argument("image publication requires metadata");
        auto token=allocate(writer.slot,lease_kind::retained);
        if (!token) return {};
        item.state=phase::published; item.retained=1;
        return token;
    }
    // Only cancel a reservation before any backend work uses its allocation.
    void cancel_write(image_write_token writer) {
        std::lock_guard lock(mutex_);
        auto& item=image(writer);
        if (item.state!=phase::writing) throw std::invalid_argument("cannot cancel published image");
        item.state=phase::idle;
    }
    std::optional<image_lease_token> retain(image_lease_token source) {
        std::lock_guard lock(mutex_);
        const auto index=lease(source,lease_kind::retained).image;
        auto result=allocate(index,lease_kind::retained);
        if (result) ++images_[index].retained;
        return result;
    }
    // Registration may precede producer completion. The presenter must enqueue
    // the producer's GPU wait before sampling; no CPU wait is required here.
    std::optional<image_lease_token> begin_consumer(image_lease_token source) {
        std::lock_guard lock(mutex_);
        const auto index=lease(source,lease_kind::retained).image;
        auto result=allocate(index,lease_kind::consumer);
        if (result) ++images_[index].consumers;
        return result;
    }
    void release(image_lease_token token) {
        std::lock_guard lock(mutex_);
        auto& owned=lease(token,lease_kind::retained);
        auto& item=images_[owned.image];
        owned.kind=lease_kind::free; --item.retained; recycle(item);
    }
    void finish_consumer(image_lease_token token) {
        std::lock_guard lock(mutex_);
        auto& owned=lease(token,lease_kind::consumer);
        auto& item=images_[owned.image];
        owned.kind=lease_kind::free; --item.consumers; recycle(item);
    }
    void finish_producer(image_write_token writer) {
        std::lock_guard lock(mutex_);
        auto& item=image(writer);
        if (item.producer_done) throw std::invalid_argument("invalid producer completion");
        item.producer_done=true; recycle(item);
    }
    // Stop new frames. Existing retained scenes can still be redrawn/released.
    void close() { std::lock_guard lock(mutex_); closed_=true; }
    size_t busy_images() const {
        std::lock_guard lock(mutex_);
        size_t count=0;
        for (const auto& item:images_) count+=item.state!=phase::idle;
        return count;
    }
};
} // namespace webscene::graphics
