#pragma once
#include "completion_mailbox.h"
#include <chrono>
#include <condition_variable>

namespace webscene::graphics {
// Contains no engine pointer. A backend may retain this signal after the engine
// is destroyed; the last reference simply releases an unused condition variable.
class engine_wake final : public completion_wake {
    std::mutex mutex_;
    std::condition_variable changed_;
    bool pending_{};
public:
    void signal() noexcept override {
        {
            std::lock_guard lock(mutex_);
            pending_ = true;
        }
        changed_.notify_one();
    }
    template<class Predicate>
    bool wait_for(std::chrono::milliseconds duration, Predicate ready) {
        std::unique_lock lock(mutex_);
        const bool signalled = changed_.wait_for(lock, duration, [&] { return pending_ || ready(); });
        pending_ = false;
        return signalled;
    }
};
} // namespace webscene::graphics
