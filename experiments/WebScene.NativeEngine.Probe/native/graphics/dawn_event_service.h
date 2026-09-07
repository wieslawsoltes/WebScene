#pragma once
#include "completion_mailbox.h"
#include <webgpu/webgpu_cpp.h>

namespace webscene::graphics {
// Construct lazily on the engine thread. ProcessEvents and completion delivery
// are independent of presentation/RAF. Backend callbacks publish native records;
// only pump() invokes the engine's promise-delivery function.
class dawn_event_service {
    const std::thread::id engine_thread_ = std::this_thread::get_id();
    std::shared_ptr<completion_mailbox> completions_;
    wgpu::Instance instance_;
    bool closed_{};
    void check_thread() const {
        if (std::this_thread::get_id() != engine_thread_)
            throw std::logic_error("Dawn event service requires engine thread");
    }
public:
    dawn_event_service(size_t completion_capacity, std::shared_ptr<completion_wake> wake)
        : completions_(std::make_shared<completion_mailbox>(completion_capacity, std::move(wake))),
          instance_(wgpu::CreateInstance()) {
        if (!instance_) throw std::runtime_error("Dawn instance creation failed");
    }
    ~dawn_event_service() {
        if (std::this_thread::get_id() != engine_thread_) std::terminate();
        // Late native callbacks keep the mailbox alive but cannot deliver JS.
        completions_->close();
    }
    dawn_event_service(const dawn_event_service&) = delete;
    dawn_event_service& operator=(const dawn_event_service&) = delete;
    const wgpu::Instance& instance() const {
        check_thread();
        if (closed_) throw std::logic_error("Dawn event service is closed");
        return instance_;
    }
    std::shared_ptr<completion_mailbox> completions() const { check_thread(); return completions_; }
    template<class Deliver> size_t pump(Deliver deliver, size_t budget = 64) {
        check_thread();
        if (!closed_) instance_.ProcessEvents();
        size_t delivered = 0;
        while (delivered < budget && completions_->drain_one(deliver)) ++delivered;
        return delivered;
    }
    // Call before discarding the JS context, then drain cancellation records on
    // its engine thread. Closing admission does not imply GPU submission finish.
    void close() { check_thread(); closed_ = true; completions_->close(); }
};
} // namespace webscene::graphics
