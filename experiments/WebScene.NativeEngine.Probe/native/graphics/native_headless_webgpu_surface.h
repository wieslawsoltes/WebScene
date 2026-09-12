#pragma once
#include "native_webgpu_device.h"
#include "native_image_capture.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace webscene::graphics {
struct headless_webgpu_options {
    wgpu::BackendType backend = wgpu::BackendType::Vulkan;
    bool allow_software = false;
    bool force_software = false;
    std::shared_ptr<completion_wake> wake;
    static headless_webgpu_options environment() {
        auto enabled = [](const char* name) { const auto* value = std::getenv(name); return value && std::string_view(value) == "1"; };
        headless_webgpu_options result;
        result.allow_software = enabled("WEBSCENE_HEADLESS_ALLOW_SOFTWARE");
        result.force_software = enabled("WEBSCENE_HEADLESS_FORCE_SOFTWARE");
        return result;
    }
};
struct headless_adapter_info {
    std::string vendor, architecture, device, description;
    wgpu::BackendType backend{};
    bool software{};
};
// Offscreen native WebGPU, not a Linux external-memory desktop presenter.
// Uses the shared immutable lease contract; completion progresses while idle.
class native_headless_webgpu_surface final {
    struct storage final : native_image_capture_provider {
        struct slot { wgpu::Texture texture; image_metadata metadata; uint64_t bytes{}; };
        std::shared_ptr<native_webgpu_device> gpu;
        const std::thread::id owner = std::this_thread::get_id();
        std::array<slot, 3> slots;
        std::mutex mutex;
        std::atomic<uint64_t> captures{}, captured_bytes{};
        uint64_t allocated_bytes{};
        explicit storage(std::shared_ptr<native_webgpu_device> device) : gpu(std::move(device)) {}
        void check_thread() const {
            if (owner != std::this_thread::get_id()) throw std::logic_error("Headless WebGPU is owner-thread-only");
        }
        captured_native_image capture(std::shared_ptr<owned_image_pool::consumer> consumer,
                                      image_capture_options options) override {
            check_thread();
            const auto metadata = consumer->describe();
            wgpu::Texture texture;
            {
                std::lock_guard lock(mutex);
                for (const auto& item : slots)
                    if (item.metadata.allocation == metadata.allocation &&
                        item.metadata.allocation_generation == metadata.allocation_generation &&
                        item.metadata.content_serial == metadata.content_serial) texture = item.texture;
            }
            if (!texture || gpu->failed->load()) throw std::runtime_error("Stale or lost native capture image");
            const uint64_t row = (uint64_t(metadata.width) * 4 + 255) & ~uint64_t(255);
            const uint64_t packed = uint64_t(metadata.width) * metadata.height * 4;
            const uint64_t bytes = row * metadata.height;
            if (row > UINT32_MAX || bytes > options.byte_budget || packed > options.byte_budget - bytes)
                throw std::length_error("Diagnostic GPU capture exceeds byte budget");
            wgpu::BufferDescriptor bd{};
            bd.size = bytes; bd.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
            auto buffer = gpu->device.CreateBuffer(&bd);
            if (!buffer) throw std::runtime_error("Capture buffer allocation failed");
            auto encoder = gpu->device.CreateCommandEncoder();
            wgpu::TexelCopyTextureInfo source{}; source.texture = texture;
            wgpu::TexelCopyBufferInfo destination{}; destination.buffer = buffer;
            destination.layout.bytesPerRow = uint32_t(row); destination.layout.rowsPerImage = metadata.height;
            wgpu::Extent3D extent{metadata.width, metadata.height, 1};
            encoder.CopyTextureToBuffer(&source, &destination, &extent);
            auto commands = encoder.Finish();
            gpu->device.GetQueue().Submit(1, &commands);
            // Even a timeout must retain the consumer until actual copy completion.
            gpu->device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
                [consumer = std::move(consumer), buffer](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
            ++captures; captured_bytes += packed;
            auto mapped = std::make_shared<std::atomic<bool>>(false);
            auto future = buffer.MapAsync(wgpu::MapMode::Read, 0, bytes, wgpu::CallbackMode::WaitAnyOnly,
                [mapped](wgpu::MapAsyncStatus status, wgpu::StringView) { mapped->store(status == wgpu::MapAsyncStatus::Success); });
            if (gpu->instance.WaitAny(future, options.timeout_ns) != wgpu::WaitStatus::Success ||
                !mapped->load() || gpu->failed->load()) {
                buffer.Destroy();
                throw std::runtime_error("Diagnostic GPU capture timed out or failed");
            }
            captured_native_image result{metadata, metadata.width * 4, {}};
            try {
                result.pixels.resize(packed);
                const auto* input = static_cast<const uint8_t*>(buffer.GetConstMappedRange());
                if (!input) throw std::runtime_error("Capture mapping returned no data");
                for (uint32_t y = 0; y < metadata.height; ++y)
                    std::memcpy(result.pixels.data() + uint64_t(y) * result.row_bytes,
                                input + uint64_t(y) * row, result.row_bytes);
            } catch (...) { buffer.Unmap(); throw; }
            buffer.Unmap();
            return result;
        }
    };
    struct submission {
        std::optional<owned_image_pool::producer> producer;
        std::shared_ptr<const webscene_gpu_image_lease_v3> image;
        image_metadata metadata;
        std::atomic<webscene_gpu_image_snapshot::status> state{webscene_gpu_image_snapshot::status::pending};
    };
    class snapshot final : public webscene_gpu_image_snapshot {
        std::shared_ptr<submission> value_;
    public:
        explicit snapshot(std::shared_ptr<submission> value) : value_(std::move(value)) {}
        image_metadata describe() const override { return value_->metadata; }
        status state() const override { return value_->state.load(std::memory_order_acquire); }
        std::shared_ptr<const webscene_gpu_image_lease_v3> resolve() override {
            return state() == status::ready ? value_->image : nullptr;
        }
    };
    std::shared_ptr<native_webgpu_device> gpu_;
    std::shared_ptr<storage> storage_;
    owned_image_pool pool_;
    std::shared_ptr<completion_wake> wake_;
    std::shared_ptr<submission> active_;
    std::vector<std::weak_ptr<submission>> pending_;
    headless_adapter_info adapter_;
    uint64_t canvas_, generation_ = 1, serial_{}, budget_;
    uint32_t width_, height_, maximum_dimension_{};
    bool closed_{};
    void check_size(uint32_t width, uint32_t height) const {
        if (!width || !height || width > maximum_dimension_ || height > maximum_dimension_ ||
            uint64_t(width) * height > budget_ / 4)
            throw std::invalid_argument("Headless surface dimensions exceed device or byte limits");
    }
    std::shared_ptr<webscene_gpu_image_snapshot> retire(bool publish) {
        if (!active_) return {};
        auto item = std::exchange(active_, {});
        std::shared_ptr<snapshot> output;
        std::exception_ptr failure;
        try {
            if (publish) {
                if (auto lease = item->producer->publish())
                    item->image = std::make_shared<webscene_gpu_image_lease_v3>(std::move(*lease));
                output = std::make_shared<snapshot>(item);
            }
        } catch (...) { failure = std::current_exception(); }
        gpu_->device.GetQueue().OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
            [item, gpu = gpu_, wake = wake_](wgpu::QueueWorkDoneStatus status, wgpu::StringView) {
                item->producer->complete(); item->producer.reset();
                const bool success = status == wgpu::QueueWorkDoneStatus::Success;
                if (!success) gpu->failed->store(true);
                item->state.store(success && item->image ? webscene_gpu_image_snapshot::status::ready
                                                        : webscene_gpu_image_snapshot::status::failed,
                                  std::memory_order_release);
                if (wake) wake->signal();
            });
        std::erase_if(pending_, [](const auto& value) { return value.expired(); });
        pending_.push_back(item);
        if (failure) std::rethrow_exception(failure);
        return output;
    }
public:
    native_headless_webgpu_surface(uint64_t canvas, uint32_t width, uint32_t height,
        uint64_t budget = 64ULL * 1024 * 1024,
        headless_webgpu_options options = headless_webgpu_options::environment())
        : gpu_(std::make_shared<native_webgpu_device>(native_webgpu_device::create(options.backend, {}, options.force_software))),
          storage_(std::make_shared<storage>(gpu_)), pool_(storage_, 128, options.wake), wake_(std::move(options.wake)),
          canvas_(canvas), budget_(budget), width_(width), height_(height) {
        if (!canvas_) throw std::invalid_argument("Native GPU surface requires a canvas identity");
        wgpu::AdapterInfo info{};
        if (gpu_->adapter.GetInfo(&info) != wgpu::Status::Success)
            throw std::runtime_error("Cannot identify headless WebGPU adapter");
        auto text = [](wgpu::StringView value) { return !value.data ? std::string{} : value.length == WGPU_STRLEN ? std::string(value.data) : std::string(value.data, value.length); };
        adapter_ = {text(info.vendor), text(info.architecture), text(info.device), text(info.description),
                    info.backendType, info.adapterType == wgpu::AdapterType::CPU};
        if (info.backendType != options.backend || info.adapterType == wgpu::AdapterType::Unknown)
            throw std::runtime_error("Unqualified headless adapter classification");
        if ((adapter_.software && !options.allow_software) || (options.force_software && !adapter_.software))
            throw std::runtime_error("Software WebGPU requires explicit opt-in and honest adapter classification");
        wgpu::Limits limits{};
        if (gpu_->device.GetLimits(&limits) != wgpu::Status::Success)
            throw std::runtime_error("Cannot query headless device limits");
        maximum_dimension_ = limits.maxTextureDimension2D;
        check_size(width, height);
    }
    ~native_headless_webgpu_surface() { close(); }
    native_headless_webgpu_surface(const native_headless_webgpu_surface&) = delete;
    native_headless_webgpu_surface& operator=(const native_headless_webgpu_surface&) = delete;
    const wgpu::Device& device() const { return gpu_->device; }
    const headless_adapter_info& adapter_info() const { return adapter_; }
    bool failed() const { return gpu_->failed->load(); }
    uint64_t diagnostic_captures() const { return storage_->captures.load(); }
    uint64_t diagnostic_capture_bytes() const { return storage_->captured_bytes.load(); }
    uint64_t allocated_bytes() const { return storage_->allocated_bytes; }
    auto occupancy() const { return pool_.inspect_occupancy(); }
    wgpu::Texture current_texture() {
        storage_->check_thread();
        if (closed_ || failed()) throw std::runtime_error("Headless surface is closed or lost");
        if (active_) return storage_->slots[active_->producer->slot()].texture;
        auto writer = pool_.acquire();
        if (!writer) return {}; // Explicit bounded backpressure, never a hidden wait.
        auto item = std::make_shared<submission>();
        const auto index = writer->slot();
        std::lock_guard lock(storage_->mutex);
        auto& slot = storage_->slots[index];
        const uint64_t bytes = uint64_t(width_) * height_ * 4;
        if (!slot.texture || slot.metadata.width != width_ || slot.metadata.height != height_) {
            if (storage_->allocated_bytes - slot.bytes + bytes > budget_) {
                // Only reservations proven idle may be evicted during resize.
                std::vector<owned_image_pool::producer> idle;
                while (auto other = pool_.acquire()) {
                    auto& old = storage_->slots[other->slot()];
                    storage_->allocated_bytes -= old.bytes; old = {};
                    idle.push_back(std::move(*other));
                }
                for (auto& reservation : idle) reservation.cancel(false);
                if (storage_->allocated_bytes - slot.bytes + bytes > budget_) return {};
            }
            wgpu::TextureDescriptor descriptor{};
            descriptor.size = {width_, height_, 1}; descriptor.format = wgpu::TextureFormat::BGRA8Unorm;
            descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc |
                               wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::TextureBinding;
            auto texture = gpu_->device.CreateTexture(&descriptor);
            if (!texture || failed()) throw std::runtime_error("Headless texture allocation failed");
            storage_->allocated_bytes = storage_->allocated_bytes - slot.bytes + bytes;
            slot.texture = std::move(texture); slot.bytes = bytes; slot.metadata.allocation = new_owner_token();
        }
        auto& metadata = slot.metadata;
        metadata.canvas = canvas_; metadata.allocation_generation = generation_; metadata.content_serial = ++serial_;
        metadata.width = width_; metadata.height = height_; metadata.format = image_format::bgra8_unorm;
        metadata.producer_timeline = canvas_; metadata.producer_value = serial_;
        writer->set_metadata(metadata); item->metadata = metadata;
        item->producer.emplace(std::move(*writer));
        item->producer->begin(); active_ = std::move(item);
        return slot.texture;
    }
    void resize(uint32_t width, uint32_t height) {
        storage_->check_thread();
        if (closed_) throw std::runtime_error("Cannot resize a closed surface");
        check_size(width, height);
        if (width == width_ && height == height_) return;
        retire(false); width_ = width; height_ = height; ++generation_;
    }
    std::shared_ptr<webscene_gpu_image_snapshot> present() { storage_->check_thread(); return retire(true); }
    void process_events() { storage_->check_thread(); gpu_->instance.ProcessEvents(); }
    void close() {
        if (closed_) return;
        storage_->check_thread(); retire(false); closed_ = true; pool_.close();
    }
};
} // namespace webscene::graphics
