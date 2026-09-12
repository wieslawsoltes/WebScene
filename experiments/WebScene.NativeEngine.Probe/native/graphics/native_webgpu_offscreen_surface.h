#pragma once
#include "native_webgpu_device.h"
#include "dawn_canvas_images.h"
#include "image_lease_abi.h"
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <cstring>
#include <limits>
#include <optional>
#include <thread>

namespace webscene::graphics {
// Dedicated native completion progress. Wait on submitted-work futures instead
// of driving a render timer or waiting on the application/owner thread. Dawn's
// spontaneous callback mode alone does not guarantee progress when all frames
// are paused. This service never touches document state or presents a frame.
class offscreen_completion_service {
    struct state {
        std::shared_ptr<native_webgpu_device> gpu;
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<wgpu::Future> pending;
        bool closing=false;
        explicit state(std::shared_ptr<native_webgpu_device> value):gpu(std::move(value)) {}
    };
    std::shared_ptr<state> state_;
    std::thread worker_;
    static void work(const std::shared_ptr<state>& state) noexcept {
        for(;;) {
            wgpu::Future future;
            {
                std::unique_lock lock(state->mutex);
                state->wake.wait(lock,[&]{return state->closing || !state->pending.empty();});
                if(state->pending.empty()) return;
                future=state->pending.front();state->pending.pop_front();
            }
            // The only blocking GPU wait is on this completion worker (or an
            // explicitly requested capture). Timeout is a device failure,
            // never a fabricated successful frame or owner-thread GPU wait.
            if(state->gpu->instance.WaitAny(future,30'000'000'000ULL)!=wgpu::WaitStatus::Success) {
                state->gpu->failed->store(true);
                state->gpu->device.Destroy();
                state->gpu->instance.WaitAny(future,0);
            }
        }
    }
public:
    explicit offscreen_completion_service(std::shared_ptr<native_webgpu_device> gpu)
        :state_(std::make_shared<state>(std::move(gpu))),worker_([state=state_]{work(state);}) {}
    offscreen_completion_service(const offscreen_completion_service&)=delete;
    offscreen_completion_service& operator=(const offscreen_completion_service&)=delete;
    ~offscreen_completion_service() {
        {std::lock_guard lock(state_->mutex);state_->closing=true;}
        state_->wake.notify_one();
        // The worker owns its independent state until the final completion.
        // A lease may have its final release inside a native callback.
        if(worker_.get_id()==std::this_thread::get_id()) worker_.detach();
        else worker_.join();
    }
    void enqueue(wgpu::Future future) const {
        std::lock_guard lock(state_->mutex);
        if(state_->closing || state_->pending.size()>=128)
            throw std::runtime_error("Offscreen GPU completion admission backpressure");
        state_->pending.push_back(future);state_->wake.notify_one();
    }
};

// No OS window or external-memory handle is required. Images remain on the
// application's Dawn device. This is NOT a Vulkan-to-EGL/Wayland presenter.
struct offscreen_gpu_dependencies final : webscene_gpu_producer_dependencies {
    std::shared_ptr<native_webgpu_device> gpu;
    offscreen_completion_service completion;
    mutable std::atomic<uint64_t> captures{};
    explicit offscreen_gpu_dependencies(std::shared_ptr<native_webgpu_device> value)
        : gpu(std::move(value)),completion(gpu) {}
    size_t count() const noexcept override { return 0; }
    bool metal_event(size_t,void*& event,uint64_t& value) const override {
        event=nullptr;value=0;return false;
    }
};
class offscreen_gpu_snapshot final : public webscene_gpu_image_snapshot {
    dawn_canvas_images::submitted_frame submitted_;
    std::shared_ptr<offscreen_gpu_dependencies> dependencies_;
    image_metadata metadata_;
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolved_;
public:
    offscreen_gpu_snapshot(dawn_canvas_images::submitted_frame frame,
                          std::shared_ptr<offscreen_gpu_dependencies> dependencies)
        : submitted_(std::move(frame)), dependencies_(std::move(dependencies)),
          metadata_(submitted_.image.describe()) {}
    image_metadata describe() const override { return metadata_; }
    status state() const override {
        switch(submitted_.status->load(std::memory_order_acquire)) {
            case dawn_canvas_images::submission_status::pending: return status::pending;
            case dawn_canvas_images::submission_status::success: return status::ready;
            default: return status::failed;
        }
    }
    std::shared_ptr<const webscene_gpu_image_lease_v3> resolve() override {
        if(state()!=status::ready) return {};
        if(!resolved_) resolved_=std::make_shared<webscene_gpu_image_lease_v3>(
            std::move(submitted_.image),dependencies_);
        return resolved_;
    }
    // The default resolve_with_gpu_waits deliberately remains readiness-gated:
    // no external semaphore handoff is advertised by this offscreen backend.
};
struct offscreen_capture {
    image_metadata metadata;
    // Tightly packed RGBA8. Alpha convention follows metadata.alpha.
    std::vector<uint8_t> pixels;
};

// Explicit diagnostic/export readback, NEVER called by present/process_events.
// The image consumer survives until the queue has completed the copy, including
// timeout/error paths. Only the capture request may synchronously wait.
inline offscreen_capture capture_offscreen_image(const webscene_gpu_image_lease_v3& image,
    uint64_t byte_limit=256ULL*1024*1024, uint64_t timeout_ns=30'000'000'000ULL) {
    auto dependencies=std::dynamic_pointer_cast<const offscreen_gpu_dependencies>(image.dependencies);
    if(!dependencies || image.requires_producer_wait)
        throw std::invalid_argument("Capture requires a completed offscreen Dawn image");
    const auto metadata=image.value.describe();
    if(metadata.format!=image_format::bgra8_unorm && metadata.format!=image_format::rgba8_unorm)
        throw std::invalid_argument("Capture currently requires BGRA8/RGBA8 unorm");
    const uint64_t row=uint64_t(metadata.width)*4;
    const uint64_t stride=(row+255)&~uint64_t(255);
    if(!metadata.width || !metadata.height || stride>std::numeric_limits<uint32_t>::max() || stride>byte_limit/metadata.height)
        throw std::invalid_argument("Capture exceeds the byte budget");
    struct consumer_owner {
        std::optional<owned_image_pool::consumer> value;
        explicit consumer_owner(owned_image_pool::consumer c):value(std::move(c)) {}
        ~consumer_owner() { if(value) value->complete(); }
    };
    auto consumer=image.value.begin_consumer();
    if(!consumer) throw std::runtime_error("Capture consumer admission backpressure");
    auto owner=std::make_shared<consumer_owner>(std::move(*consumer));
    const auto& gpu=*dependencies->gpu;
    auto texture=dawn_canvas_images::resolve(*owner->value,gpu.device);
    wgpu::BufferDescriptor bd{};
    bd.size=stride*metadata.height;
    bd.usage=wgpu::BufferUsage::CopyDst|wgpu::BufferUsage::MapRead;
    auto buffer=gpu.device.CreateBuffer(&bd);
    if(!buffer) throw std::runtime_error("Capture buffer creation failed");
    auto encoder=gpu.device.CreateCommandEncoder();
    wgpu::TexelCopyTextureInfo src{};src.texture=texture;
    wgpu::TexelCopyBufferInfo dst{};dst.buffer=buffer;
    dst.layout.bytesPerRow=uint32_t(stride);dst.layout.rowsPerImage=metadata.height;
    wgpu::Extent3D extent{metadata.width,metadata.height,1};
    encoder.CopyTextureToBuffer(&src,&dst,&extent);
    auto commands=encoder.Finish();
    auto queue=gpu.device.GetQueue();
    queue.Submit(1,&commands);
    // The captured owner is intentionally released by this completion, not by
    // the lifetime of the blocking caller or by a cancelled buffer mapping.
    auto completed=queue.OnSubmittedWorkDone(wgpu::CallbackMode::AllowSpontaneous,
        [owner](wgpu::QueueWorkDoneStatus,wgpu::StringView) {});
    dependencies->completion.enqueue(completed);
    struct mapped_state { bool ok=false; std::vector<uint8_t> pixels; };
    auto state=std::make_shared<mapped_state>();
    auto future=buffer.MapAsync(wgpu::MapMode::Read,0,bd.size,wgpu::CallbackMode::WaitAnyOnly,
        [state,buffer,metadata,row,stride](wgpu::MapAsyncStatus status,wgpu::StringView) {
            if(status!=wgpu::MapAsyncStatus::Success) return;
            try {
                const auto* data=static_cast<const uint8_t*>(buffer.GetConstMappedRange());
                if(data) {
                    state->pixels.resize(size_t(row)*metadata.height);
                    for(uint32_t y=0;y<metadata.height;++y)
                        std::memcpy(state->pixels.data()+size_t(y)*row,data+size_t(y)*stride,size_t(row));
                    if(metadata.format==image_format::bgra8_unorm)
                        for(size_t i=0;i<state->pixels.size();i+=4) std::swap(state->pixels[i],state->pixels[i+2]);
                    state->ok=true;
                }
            } catch(...) { state->ok=false; }
            buffer.Unmap();
        });
    const auto wait=gpu.instance.WaitAny(future,timeout_ns);
    if(wait!=wgpu::WaitStatus::Success) {
        buffer.Unmap();
        // Service cancellation when available; the queue-completion callback
        // independently protects the image even when this wait times out.
        gpu.instance.WaitAny(future,0);
        throw std::runtime_error("Offscreen capture mapping timed out or was cancelled");
    }
    if(!state->ok || gpu.failed->load()) throw std::runtime_error("Offscreen capture failed");
    dependencies->captures.fetch_add(1);
    return {metadata,std::move(state->pixels)};
}

class native_webgpu_offscreen_surface final {
    const std::thread::id thread_=std::this_thread::get_id();
    std::shared_ptr<native_webgpu_device> gpu_;
    std::shared_ptr<offscreen_gpu_dependencies> dependencies_;
    dawn_canvas_images images_;
    uint64_t canvas_,generation_=1,serial_=0;
    uint32_t width_,height_;
    std::optional<dawn_canvas_images::frame> current_;
    static bool software_requested() {
        const auto* value=std::getenv("WEBSCENE_HEADLESS_FORCE_SOFTWARE_ADAPTER");
        return value && std::string_view(value)=="1";
    }
    static wgpu::BackendType backend() {
#if defined(__APPLE__)
        return wgpu::BackendType::Metal;
#elif defined(_WIN32)
        return wgpu::BackendType::D3D12;
#else
        return wgpu::BackendType::Vulkan;
#endif
    }
    static std::shared_ptr<native_webgpu_device> create_gpu() {
        // Dawn's forceFallbackAdapter currently recognizes only SwiftShader,
        // not other software Vulkan ICDs such as Mesa Lavapipe. The caller
        // selects its ICD; verify the returned adapter rather than mislabeling
        // a hardware/default adapter or silently rejecting a software driver.
        auto gpu=std::make_shared<native_webgpu_device>(
            native_webgpu_device::create(backend(),{wgpu::FeatureName::ImplicitDeviceSynchronization}));
        wgpu::AdapterInfo info{};
        if(gpu->adapter.GetInfo(&info)!=wgpu::Status::Success)
            throw std::runtime_error("Cannot inspect offscreen GPU adapter");
        const bool hardware=info.adapterType==wgpu::AdapterType::DiscreteGPU ||
                            info.adapterType==wgpu::AdapterType::IntegratedGPU;
        if(software_requested() && info.adapterType!=wgpu::AdapterType::CPU)
            throw std::runtime_error("Software GPU requested: select a CPU Vulkan ICD with VK_ICD_FILENAMES");
        if(!hardware && !software_requested())
            throw std::runtime_error("Software GPU requires WEBSCENE_HEADLESS_FORCE_SOFTWARE_ADAPTER=1");
        return gpu;
    }
    void check_thread() const {
        if(std::this_thread::get_id()!=thread_) throw std::logic_error("Offscreen surface requires its owner thread");
    }
    std::optional<dawn_canvas_images::submitted_frame> retire() {
        if(!current_) return {};
        wgpu::Future future;
        auto value=images_.retire_submitted(std::move(*current_),{},&future);
        current_.reset();dependencies_->completion.enqueue(future);return value;
    }
public:
    native_webgpu_offscreen_surface(uint64_t canvas,uint32_t width,uint32_t height,
                                   uint64_t byte_budget=64ULL*1024*1024)
        :gpu_(create_gpu()),dependencies_(std::make_shared<offscreen_gpu_dependencies>(gpu_)),
         images_(gpu_->device,byte_budget),canvas_(canvas),width_(width),height_(height) {
        if(!canvas) throw std::invalid_argument("Offscreen surface requires a nonzero canvas identity");
    }
    native_webgpu_offscreen_surface(const native_webgpu_offscreen_surface&)=delete;
    native_webgpu_offscreen_surface& operator=(const native_webgpu_offscreen_surface&)=delete;
    ~native_webgpu_offscreen_surface() { check_thread();retire(); }
    const wgpu::Device& device() const { return gpu_->device; }
    const wgpu::Adapter& adapter() const { return gpu_->adapter; }
    wgpu::Texture current_texture() {
        check_thread();
        if(failed()) throw std::runtime_error("Offscreen GPU device lost");
        if(!current_) {
            image_metadata m;m.canvas=canvas_;m.allocation_generation=generation_;
            m.content_serial=++serial_;m.producer_timeline=canvas_;m.producer_value=serial_;
            m.width=width_;m.height=height_;m.format=image_format::bgra8_unorm;
            auto acquired=images_.acquire(m);
            if(acquired) current_.emplace(std::move(*acquired));
        }
        return current_?current_->texture:wgpu::Texture{};
    }
    void resize(uint32_t width,uint32_t height) {
        check_thread();if(width==width_ && height==height_) return;
        retire();width_=width;height_=height;++generation_;
    }
    std::shared_ptr<webscene_gpu_image_snapshot> present() {
        check_thread();auto submitted=retire();
        return submitted?std::make_shared<offscreen_gpu_snapshot>(std::move(*submitted),dependencies_):nullptr;
    }
    void process_events() { check_thread();gpu_->instance.ProcessEvents(); }
    bool failed() const { return gpu_->failed->load(); }
    uint64_t resident_bytes() const { return images_.resident_bytes(); }
    uint64_t created_images() const { return images_.created_images(); }
    size_t busy_images() const { return images_.busy_images(); }
    uint64_t capture_count() const { return dependencies_->captures.load(); }
};
} // namespace webscene::graphics
