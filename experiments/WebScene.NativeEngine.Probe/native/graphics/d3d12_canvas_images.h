#pragma once
#include "d3d12_shared_color.h"
#include "owned_image_pool.h"
#include <thread>
#if defined(_WIN32)
namespace webscene::graphics {
// Engine-thread allocator. Every outstanding lease anchors the native storage,
// including after the canvas owner is disposed. GPU completion remains explicit.
class d3d12_canvas_images {
    struct storage final : image_provider_lifetime {
        image_provider_kind kind() const noexcept override { return image_provider_kind::d3d12; }
        struct slot { std::unique_ptr<d3d12_shared_color> color; image_metadata metadata{}; };
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        std::array<slot,4> slots;
        std::mutex mutex;
        uint64_t bytes{};
        explicit storage(ID3D12Device* value):device(value) {}
    };
    std::shared_ptr<storage> storage_;
    owned_image_pool pool_;
    uint64_t limit_;
    const std::thread::id thread_=std::this_thread::get_id();
    void check_thread() const {
        if (thread_!=std::this_thread::get_id()) throw std::logic_error("D3D12 image allocation requires owner thread");
    }
public:
    struct frame {
        owned_image_pool::producer producer;
        const d3d12_shared_color* color; // Borrowed through producer, never independently retained.
        image_metadata metadata;
    };
    d3d12_canvas_images(ID3D12Device* device,uint64_t budget,size_t tickets=128,
        std::shared_ptr<completion_wake> wake={})
        :storage_(std::make_shared<storage>(device)),pool_(storage_,tickets,std::move(wake),4),limit_(budget) {
        if (!device || !budget) throw std::invalid_argument("D3D12 images require device and budget");
    }
    std::optional<frame> acquire(image_metadata metadata,HRESULT& status) {
        check_thread(); status=S_OK;
        auto writer=pool_.acquire();
        if (!writer) { status=DXGI_ERROR_WAS_STILL_DRAWING; return {}; }
        // Reservations outlive the lock on exceptional unwind, so completion
        // wake callbacks cannot re-enter while the storage mutex is held.
        std::array<std::optional<owned_image_pool::producer>,3> idle;
        std::lock_guard lock(storage_->mutex);
        auto& slot=storage_->slots[writer->slot()];
        const bool reuse=slot.color && slot.metadata.width==metadata.width
            && slot.metadata.height==metadata.height && slot.metadata.format==metadata.format;
        metadata.allocation=reuse ? slot.metadata.allocation : new_owner_token();
        writer->set_metadata(metadata);
        if (!reuse) {
            if (slot.color) storage_->bytes-=slot.color->allocation_bytes();
            slot.color.reset();
            status=d3d12_shared_color::create(storage_->device.Get(),metadata,limit_-storage_->bytes,slot.color);
            for (auto& reservation:idle) {
                if (status!=E_OUTOFMEMORY) break;
                auto candidate=pool_.acquire();
                if (!candidate) break; // Busy images cannot be evicted.
                reservation.emplace(std::move(*candidate));
                auto& cached=storage_->slots[reservation->slot()];
                if (cached.color) storage_->bytes-=cached.color->allocation_bytes();
                cached.color.reset();
                status=d3d12_shared_color::create(storage_->device.Get(),metadata,limit_-storage_->bytes,slot.color);
            }
            // Keep reservations until all retries finish, or acquire() could
            // select the same empty slot repeatedly instead of the next cache.
            for (auto& reservation:idle) if (reservation) reservation->cancel(false);
            if (FAILED(status)) { writer->cancel(false); return {}; }
            storage_->bytes+=slot.color->allocation_bytes();
        }
        slot.metadata=metadata;
        return frame{std::move(*writer),slot.color.get(),metadata};
    }
    // Read-only native bridge lookup. The caller must keep consumer alive through
    // its GPU completion; returning this object neither begins access nor waits.
    static const d3d12_shared_color& resolve(const owned_image_pool::consumer& consumer,
        adapter_luid adapter) {
        const auto metadata=consumer.describe();
        const auto anchor=consumer.provider();
        if (!anchor || anchor->kind()!=image_provider_kind::d3d12 || !adapter.valid)
            throw std::invalid_argument("foreign D3D12 image provider");
        const auto provider=std::static_pointer_cast<storage>(anchor);
        std::lock_guard lock(provider->mutex);
        for (const auto& slot:provider->slots) {
            if (slot.color && slot.metadata.allocation==metadata.allocation
                && slot.metadata.allocation_generation==metadata.allocation_generation
                && slot.metadata.content_serial==metadata.content_serial) {
                if (!(slot.color->adapter()==adapter)) throw std::invalid_argument("cross-adapter D3D12 image");
                return *slot.color;
            }
        }
        throw std::invalid_argument("D3D12 image allocation unavailable");
    }
    uint64_t allocation_bytes() const { check_thread(); return storage_->bytes; }
    size_t busy_images() const { return pool_.busy_images(); }
    image_lease_pool::occupancy inspect_occupancy() const { return pool_.inspect_occupancy(); }
    void close() { pool_.close(); }
};
} // namespace webscene::graphics
#endif
