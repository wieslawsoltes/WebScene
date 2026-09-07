#pragma once
#include "completion_mailbox.h"
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
    void check_thread() const {
        if (std::this_thread::get_id()!=thread_)
            throw std::logic_error("Dawn device requires its engine thread");
    }
public:
    dawn_device(uint64_t engine,std::shared_ptr<completion_mailbox> mailbox,
                wgpu::Adapter adapter,wgpu::Device device,std::shared_ptr<device_loss_signal> loss={})
        : owner_{engine,new_owner_token(),0},mailbox_(std::move(mailbox)),
          adapter_(std::move(adapter)),device_(std::move(device)),loss_(std::move(loss)) {
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
        process_loss();
        closed_=true;
        // Logical cancellation is independent of physical GPU completion.
        // Dawn retains submitted native resources until its backend is safe;
        // higher-level submission tables still require their completion fences.
        if (!lost_) mailbox_->cancel_owner(owner_);
        device_.Destroy();
    }
};
} // namespace webscene::graphics
