#pragma once
#include "completion_mailbox.h"
#include "resource_table.h"
#include <webgpu/webgpu_cpp.h>

namespace webscene::graphics {
// One logical WebGPU device. Adapter/device references remain native and belong
// to the engine thread; callback captures retain only the completion mailbox.
struct device_loss_signal {
    std::atomic<bool> lost{};
    std::shared_ptr<completion_wake> wake;
    explicit device_loss_signal(std::shared_ptr<completion_wake> value) : wake(std::move(value)) {}
    static void configure(wgpu::DeviceDescriptor& descriptor,std::shared_ptr<device_loss_signal> signal) {
        if (!signal) throw std::invalid_argument("device loss signal is required");
        descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
            [signal](const wgpu::Device&,wgpu::DeviceLostReason reason,wgpu::StringView) {
                if (reason!=wgpu::DeviceLostReason::Destroyed && reason!=wgpu::DeviceLostReason::CallbackCancelled) {
                    signal->lost.store(true,std::memory_order_release);
                    if (signal->wake) signal->wake->signal();
                }
            });
    }
};
class dawn_device {
    const std::thread::id thread_=std::this_thread::get_id();
    const resource_owner owner_;
    std::shared_ptr<completion_mailbox> mailbox_;
    wgpu::Adapter adapter_;
    wgpu::Device device_;
    bool closed_{};
    bool lost_{};
    std::shared_ptr<device_loss_signal> loss_;
    resource_table<wgpu::Buffer> buffers_;
    size_t active_buffer_scopes_{};
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_)
            throw std::logic_error("Dawn device requires its engine thread");
    }
public:
    dawn_device(uint64_t engine,std::shared_ptr<completion_mailbox> mailbox,
                wgpu::Adapter adapter,wgpu::Device device,std::shared_ptr<device_loss_signal> loss={},size_t buffer_capacity=1024)
        : owner_{engine,new_owner_token(),0},mailbox_(std::move(mailbox)),
          adapter_(std::move(adapter)),device_(std::move(device)),loss_(std::move(loss)),buffers_(buffer_capacity,owner_) {
        if (!engine || !mailbox_ || !adapter_ || !device_)
            throw std::invalid_argument("Dawn device requires native ownership");
    }
    dawn_device(const dawn_device&)=delete;
    dawn_device& operator=(const dawn_device&)=delete;
    ~dawn_device() { if (std::this_thread::get_id()!=thread_) std::terminate(); close(); }
    resource_owner owner() const { check_thread(); return owner_; }
    const wgpu::Device& native() const {
        check_thread();
        if (closed_ || lost_ || (loss_ && loss_->lost.load(std::memory_order_acquire)))
            throw std::logic_error("Dawn device is closed or lost");
        return device_;
    }
    // Internal native descriptor entry point. Browser descriptor validation and
    // error-object handling must precede this call in the JavaScript binding.
    resource_handle<wgpu::Buffer> create_buffer(const wgpu::BufferDescriptor& descriptor) {
        const auto& device=native();
        if (!buffers_.can_insert()) throw std::length_error("graphics buffer limit reached");
        auto buffer=device.CreateBuffer(&descriptor);
        if (!buffer) throw std::runtime_error("Dawn did not return a buffer");
        return buffers_.insert(owner_,std::make_unique<wgpu::Buffer>(std::move(buffer)));
    }
    template<class Execute> void with_buffer(resource_handle<wgpu::Buffer> handle,Execute execute) {
        check_thread();
        const auto& buffer=buffers_.get(handle,owner_);
        struct guard {
            size_t& count;
            explicit guard(size_t& value) : count(value) { ++count; }
            ~guard() { --count; }
        } scope(active_buffer_scopes_);
        execute(buffer);
    }
    // WebGPU destroy invalidates the native allocation, but the wrapper remains
    // valid for metadata and repeated destroy calls until it is itself released.
    void destroy_buffer(resource_handle<wgpu::Buffer> handle) {
        check_thread();
        if (active_buffer_scopes_) throw std::logic_error("Cannot destroy buffers during execution");
        buffers_.get(handle,owner_).Destroy();
    }
    // Wrapper collection releases only this reference. Dawn/queued operations
    // retain their own native references; collection must never call Destroy.
    void release_buffer(resource_handle<wgpu::Buffer> handle) {
        check_thread();
        if (active_buffer_scopes_) throw std::logic_error("Cannot release buffers during execution");
        buffers_.destroy(handle,owner_);
    }
    size_t live_buffers() const { check_thread(); return buffers_.resident_count(); }
    bool loss_pending() const {
        check_thread();
        return !closed_ && !lost_ && loss_ && loss_->lost.load(std::memory_order_acquire);
    }
    void process_loss() {
        check_thread();
        if (!loss_pending()) return;
        lost_=true;
        mailbox_->cancel_owner(owner_,completion_status::device_lost);
    }
    void close() {
        check_thread();
        if (closed_) return;
        if (active_buffer_scopes_) throw std::logic_error("Cannot close device during buffer execution");
        process_loss();
        closed_=true;
        // Logical cancellation is independent of physical GPU completion.
        // Dawn retains submitted native resources until its backend is safe;
        // higher-level submission tables still require their completion fences.
        if (!lost_) mailbox_->cancel_owner(owner_);
        device_.Destroy();
        buffers_.destroy_owner(owner_);
    }
};
} // namespace webscene::graphics
