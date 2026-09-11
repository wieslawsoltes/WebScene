#pragma once
#include "completion_mailbox.h"
#include "work_queue.h"
#include <array>

namespace webscene::graphics {
class graphics_service;
// Internal native dispatch ABI. Arguments are values/generation handles only;
// the upload span owns all transient input. Dispatchers are static native code,
// never V8 callbacks, and must report validation failures without throwing.
struct graphics_command {
    using arguments=std::array<uint64_t,8>;
    void (*execute)(graphics_service&,std::span<const std::byte>,const arguments&) noexcept{};
    arguments values{};
};
// A producer endpoint may outlive the engine. It has no engine pointer and stops
// admission on close; finalizers/driver threads can safely retain this endpoint.
class command_channel {
    work_queue<graphics_command> queue_;
    std::shared_ptr<completion_wake> wake_;
public:
    command_channel(size_t capacity,size_t upload_limit,std::shared_ptr<completion_wake> wake)
        : queue_(capacity,upload_limit),wake_(std::move(wake)) {}
    enqueue_result enqueue(graphics_command command,std::span<const std::byte> upload={}) {
        if (!command.execute) throw std::invalid_argument("native graphics dispatcher is required");
        const auto result=queue_.try_push(command,upload);
        if (result==enqueue_result::accepted && wake_) wake_->signal();
        return result;
    }
    queue_metrics metrics() const { return queue_.metrics(); }
private:
    friend class graphics_service;
    template<class Execute> bool consume_one(Execute execute) { return queue_.consume_one(execute); }
    void close() { queue_.close(); }
};
} // namespace webscene::graphics
